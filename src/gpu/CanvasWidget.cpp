#include "gpu/CanvasWidget.h"

#include "gpu/ZoomMath.h"

#include <QFile>
#include <QMouseEvent>
#include <QWheelEvent>
#include <rhi/qshader.h>

#include <algorithm>
#include <cmath>

namespace {

// A unit quad in [0,1] with matching texture coordinates, as a triangle strip.
// position.xy, texcoord.uv interleaved.
constexpr float kQuad[] = {
    // x     y     u     v
    0.0f, 0.0f, 0.0f, 0.0f, // top-left
    1.0f, 0.0f, 1.0f, 0.0f, // top-right
    0.0f, 1.0f, 0.0f, 1.0f, // bottom-left
    1.0f, 1.0f, 1.0f, 1.0f, // bottom-right
};

QShader loadShader(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        return QShader::fromSerialized(f.readAll());
    return {};
}

// Fixed cube edge for the GPU look texture. Any loaded LUT is resampled into it,
// so the 3D texture is created once and only re-uploaded on change.
constexpr int kLut3DDim = 32;

std::vector<uint8_t> resampleCube(const Lut3D &lut)
{
    std::vector<uint8_t> cube(static_cast<size_t>(kLut3DDim) * kLut3DDim * kLut3DDim * 4);
    double out[3];
    for (int b = 0; b < kLut3DDim; ++b) {
        for (int g = 0; g < kLut3DDim; ++g) {
            for (int r = 0; r < kLut3DDim; ++r) {
                lut.sample(r / double(kLut3DDim - 1), g / double(kLut3DDim - 1),
                           b / double(kLut3DDim - 1), out);
                const size_t idx = ((static_cast<size_t>(b) * kLut3DDim + g) * kLut3DDim + r) * 4;
                for (int c = 0; c < 3; ++c)
                    cube[idx + c] = static_cast<uint8_t>(
                        std::clamp(std::lround(out[c] * 255.0), 0L, 255L));
                cube[idx + 3] = 255;
            }
        }
    }
    return cube;
}

// Coverage mask → RGBA bytes (R = coverage; shader samples .r). 1x1 white if
// empty (full coverage).
std::vector<uint8_t> maskRgba(const MaskBuffer &m, int &w, int &h)
{
    if (m.isEmpty()) {
        w = h = 1;
        return {255, 255, 255, 255};
    }
    w = m.width;
    h = m.height;
    std::vector<uint8_t> d(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < m.data.size(); ++i) {
        d[i * 4 + 0] = static_cast<uint8_t>(std::clamp(std::lround(m.data[i] * 255.0f), 0L, 255L));
        d[i * 4 + 1] = 0;
        d[i * 4 + 2] = 0;
        d[i * 4 + 3] = 255;
    }
    return d;
}

} // namespace

CanvasWidget::CanvasWidget(QWidget *parent)
    : QRhiWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    m_lut3dData = resampleCube(Lut3D{}); // identity
}

void CanvasWidget::setImage(const QImage &image, bool keepView)
{
    m_pendingImage = image;
    m_textureDirty = true;
    if (!keepView)
        resetView();
    update();
}

void CanvasWidget::resetView()
{
    m_zoom = 1.0f;
    m_pan = {0.0, 0.0};
    emit viewChanged();
    update();
}

void CanvasWidget::setPreviewState(const PreviewState &state)
{
    if (m_preview == state)
        return;
    m_preview = state;
    update();
}

void CanvasWidget::setCurveLuts(const ChannelLuts &luts)
{
    if (m_luts == luts)
        return;
    m_luts = luts;
    m_lutDirty = true;
    update();
}

void CanvasWidget::setLut3D(const Lut3D &look)
{
    std::vector<uint8_t> cube = resampleCube(look);
    if (cube == m_lut3dData)
        return;
    m_lut3dData = std::move(cube);
    m_lut3dDirty = true;
    update();
}

void CanvasWidget::initialize(QRhiCommandBuffer *cb)
{
    QRhi *r = rhi();

    // RHI changed (first init, or device loss/recreation): drop everything.
    if (m_rhi != r) {
        m_pipeline.reset();
        m_srb.reset();
        m_texture.reset();
        m_sampler.reset();
        m_lutTexture.reset();
        m_lutSampler.reset();
        m_lut3dTexture.reset();
        m_lut3dSampler.reset();
        m_selMaskTexture.reset();
        m_selMaskSampler.reset();
        m_layerMaskTexture.reset();
        m_presentPipeline.reset();
        m_presentSrb.reset();
        m_presentSrbB.reset();
        m_presentUbuf.reset();
        m_offscreenRt.reset();
        m_offscreenRtB.reset();
        m_offscreenRpd.reset();
        m_offscreenTex.reset();
        m_offscreenTexB.reset();
        m_extraLayers.clear();
        m_ubuf.reset();
        m_vbuf.reset();
        m_textureSize = {};
        m_textureDirty = !m_pendingImage.isNull();
        m_lutDirty = true;
        m_lut3dDirty = true;
        m_selMaskDirty = true;
        m_layerMaskDirty = true;
        m_srbDirty = true;
        m_rhi = r;
    }

    if (!m_vbuf) {
        m_vbuf.reset(r->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                  sizeof(kQuad)));
        m_vbuf->create();
        QRhiResourceUpdateBatch *b = r->nextResourceUpdateBatch();
        b->uploadStaticBuffer(m_vbuf.get(), kQuad);
        cb->resourceUpdate(b);
    }

    if (!m_ubuf) {
        // std140: mat4 mvp (64) + PreviewState's floats at offset 64.
        // 26 floats (104 bytes) → 168, rounded up to a 16-byte multiple → 176.
        m_ubuf.reset(r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 176));
        m_ubuf->create();
    }

    if (!m_presentUbuf) {
        m_presentUbuf.reset(r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
        m_presentUbuf->create();
    }

    if (!m_sampler) {
        m_sampler.reset(r->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                      QRhiSampler::None, QRhiSampler::ClampToEdge,
                                      QRhiSampler::ClampToEdge));
        m_sampler->create();
    }

    if (!m_lutSampler) {
        m_lutSampler.reset(r->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                         QRhiSampler::None, QRhiSampler::ClampToEdge,
                                         QRhiSampler::ClampToEdge));
        m_lutSampler->create();
    }

    if (!m_lutTexture) {
        // 256x1 RGBA tone-curve LUT: R/G/B channels hold the per-channel curves.
        m_lutTexture.reset(r->newTexture(QRhiTexture::RGBA8, QSize(256, 1)));
        m_lutTexture->create();
        m_lutDirty = true;
    }

    if (!m_lut3dSampler) {
        // Linear in all three axes → hardware trilinear interpolation.
        m_lut3dSampler.reset(r->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                           QRhiSampler::None, QRhiSampler::ClampToEdge,
                                           QRhiSampler::ClampToEdge,
                                           QRhiSampler::ClampToEdge));
        m_lut3dSampler->create();
    }

    if (!m_lut3dTexture) {
        m_lut3dTexture.reset(r->newTexture(QRhiTexture::RGBA8, kLut3DDim, kLut3DDim,
                                           kLut3DDim, 1, QRhiTexture::ThreeDimensional));
        m_lut3dTexture->create();
        m_lut3dDirty = true;
    }

    if (!m_selMaskSampler) {
        m_selMaskSampler.reset(r->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                             QRhiSampler::None, QRhiSampler::ClampToEdge,
                                             QRhiSampler::ClampToEdge));
        m_selMaskSampler->create();
    }

    if (!m_selMaskTexture) {
        // RGBA8 (not R8) so arbitrary-width rows stay 4-byte aligned; the shader
        // samples .r.
        m_selMaskTexture.reset(r->newTexture(QRhiTexture::RGBA8, QSize(m_selMaskW, m_selMaskH)));
        m_selMaskTexture->create();
        m_selMaskDirty = true;
        m_srbDirty = true;
    }

    if (!m_layerMaskTexture) {
        // The Base layer's mask is white (full coverage); extra layers carry
        // their own. 1x1 white, sampled as .r.
        m_layerMaskTexture.reset(r->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        m_layerMaskTexture->create();
        m_layerMaskDirty = true;
        m_srbDirty = true;
    }
}

void CanvasWidget::buildSrb()
{
    QRhi *r = rhi();
    m_srb.reset(r->newShaderResourceBindings());
    m_srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_ubuf.get()),
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage, m_texture.get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(
            2, QRhiShaderResourceBinding::FragmentStage, m_lutTexture.get(), m_lutSampler.get()),
        QRhiShaderResourceBinding::sampledTexture(
            3, QRhiShaderResourceBinding::FragmentStage, m_lut3dTexture.get(), m_lut3dSampler.get()),
        QRhiShaderResourceBinding::sampledTexture(
            4, QRhiShaderResourceBinding::FragmentStage, m_selMaskTexture.get(), m_selMaskSampler.get()),
        QRhiShaderResourceBinding::sampledTexture(
            5, QRhiShaderResourceBinding::FragmentStage, m_layerMaskTexture.get(), m_sampler.get()),
    });
    m_srb->create();
}

void CanvasWidget::setSelectiveMask(const MaskBuffer &mask)
{
    std::vector<uint8_t> data;
    int w = 1, h = 1;
    if (mask.isEmpty()) {
        data = {255, 0, 0, 255}; // fully selected
    } else {
        w = mask.width;
        h = mask.height;
        data.resize(mask.data.size() * 4);
        for (size_t i = 0; i < mask.data.size(); ++i) {
            const uint8_t v = static_cast<uint8_t>(
                std::clamp(std::lround(mask.data[i] * 255.0f), 0L, 255L));
            data[i * 4 + 0] = v; // shader samples .r
            data[i * 4 + 1] = 0;
            data[i * 4 + 2] = 0;
            data[i * 4 + 3] = 255;
        }
    }
    if (data == m_selMaskData && w == m_selMaskW && h == m_selMaskH)
        return;
    m_selMaskData = std::move(data);
    m_selMaskW = w;
    m_selMaskH = h;
    m_selMaskDirty = true;
    update();
}

void CanvasWidget::setExtraLayers(const std::vector<LayerPreview> &layers)
{
    // Rebuild all (indices/inputs shift when the count changes; cheap for a few).
    m_extraLayers.resize(layers.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        m_extraLayers[i].data = layers[i];
        m_extraLayers[i].dirty = true;
    }
    update();
}

void CanvasWidget::buildExtraLayer(GpuLayer &gl, int index, QRhiResourceUpdateBatch *batch)
{
    QRhi *r = rhi();
    if (!gl.ubuf) {
        gl.ubuf.reset(r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 176));
        gl.ubuf->create();
    }
    if (!gl.curveTex) {
        gl.curveTex.reset(r->newTexture(QRhiTexture::RGBA8, QSize(256, 1)));
        gl.curveTex->create();
    }
    if (!gl.lut3dTex) {
        gl.lut3dTex.reset(r->newTexture(QRhiTexture::RGBA8, kLut3DDim, kLut3DDim, kLut3DDim,
                                        1, QRhiTexture::ThreeDimensional));
        gl.lut3dTex->create();
    }
    int sw, sh;
    const std::vector<uint8_t> selBytes = maskRgba(gl.data.selMask, sw, sh);
    if (!gl.selMaskTex || gl.selMaskTex->pixelSize() != QSize(sw, sh)) {
        gl.selMaskTex.reset(r->newTexture(QRhiTexture::RGBA8, QSize(sw, sh)));
        gl.selMaskTex->create();
    }
    int lw, lh;
    const std::vector<uint8_t> layBytes = maskRgba(gl.data.layerMask, lw, lh);
    if (!gl.layerMaskTex || gl.layerMaskTex->pixelSize() != QSize(lw, lh)) {
        gl.layerMaskTex.reset(r->newTexture(QRhiTexture::RGBA8, QSize(lw, lh)));
        gl.layerMaskTex->create();
    }

    // Uniforms: fixed offscreen-fill transform + this layer's state/opacity.
    PreviewState st = gl.data.state;
    st.layerOpacity = gl.data.opacity;
    const QMatrix4x4 fill = fillMvp(m_textureSize);
    batch->updateDynamicBuffer(gl.ubuf.get(), 0, 64, fill.constData());
    batch->updateDynamicBuffer(gl.ubuf.get(), 64, sizeof(PreviewState), &st.exposure);

    QByteArray cbytes(256 * 4, char(0));
    for (int i = 0; i < 256; ++i) {
        cbytes[i * 4 + 0] = static_cast<char>(gl.data.curves[0][i]);
        cbytes[i * 4 + 1] = static_cast<char>(gl.data.curves[1][i]);
        cbytes[i * 4 + 2] = static_cast<char>(gl.data.curves[2][i]);
        cbytes[i * 4 + 3] = char(255);
    }
    batch->uploadTexture(gl.curveTex.get(), QRhiTextureUploadDescription(
                                                QRhiTextureUploadEntry(0, 0,
                                                    QRhiTextureSubresourceUploadDescription(cbytes))));

    const std::vector<uint8_t> cube = resampleCube(gl.data.look);
    const int sliceBytes = kLut3DDim * kLut3DDim * 4;
    std::vector<QByteArray> slices;
    std::vector<QRhiTextureUploadEntry> entries;
    for (int z = 0; z < kLut3DDim; ++z) {
        slices.emplace_back(reinterpret_cast<const char *>(cube.data() + size_t(z) * sliceBytes),
                            sliceBytes);
        entries.emplace_back(z, 0, QRhiTextureSubresourceUploadDescription(slices.back()));
    }
    QRhiTextureUploadDescription d3;
    d3.setEntries(entries.begin(), entries.end());
    batch->uploadTexture(gl.lut3dTex.get(), d3);

    const auto upload2d = [&](QRhiTexture *t, const std::vector<uint8_t> &bytes) {
        QRhiTextureSubresourceUploadDescription sub(
            QByteArray(reinterpret_cast<const char *>(bytes.data()),
                       static_cast<qsizetype>(bytes.size())));
        batch->uploadTexture(t, QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, sub)));
    };
    upload2d(gl.selMaskTex.get(), selBytes);
    upload2d(gl.layerMaskTex.get(), layBytes);

    // Input ping-pongs: even-indexed layers read A, odd read B.
    QRhiTexture *input = (index % 2 == 0) ? m_offscreenTex.get() : m_offscreenTexB.get();
    gl.srb.reset(r->newShaderResourceBindings());
    gl.srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            gl.ubuf.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  input, m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                  gl.curveTex.get(), m_lutSampler.get()),
        QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage,
                                                  gl.lut3dTex.get(), m_lut3dSampler.get()),
        QRhiShaderResourceBinding::sampledTexture(4, QRhiShaderResourceBinding::FragmentStage,
                                                  gl.selMaskTex.get(), m_selMaskSampler.get()),
        QRhiShaderResourceBinding::sampledTexture(5, QRhiShaderResourceBinding::FragmentStage,
                                                  gl.layerMaskTex.get(), m_sampler.get()),
    });
    gl.srb->create();
}

void CanvasWidget::ensurePipeline()
{
    if (m_pipeline && m_presentPipeline)
        return; // both built
    if (!m_srb || !m_offscreenRpd || !m_presentSrb)
        return; // offscreen target / bindings not ready yet

    QRhi *r = rhi();
    QRhiVertexInputLayout layout;
    layout.setBindings({{4 * sizeof(float)}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
    });

    // Adjustment pass: the full chain, rendered into the offscreen target (no
    // MSAA, identity-fill transform).
    m_pipeline.reset(r->newGraphicsPipeline());
    m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/texture.vert.qsb"))},
        {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/texture.frag.qsb"))},
    });
    m_pipeline->setVertexInputLayout(layout);
    m_pipeline->setShaderResourceBindings(m_srb.get());
    m_pipeline->setRenderPassDescriptor(m_offscreenRpd.get());
    m_pipeline->setSampleCount(1);
    m_pipeline->create();

    // Present pass: draws the offscreen result to the screen with zoom/pan.
    m_presentPipeline.reset(r->newGraphicsPipeline());
    m_presentPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_presentPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/present.vert.qsb"))},
        {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/present.frag.qsb"))},
    });
    m_presentPipeline->setVertexInputLayout(layout);
    m_presentPipeline->setShaderResourceBindings(m_presentSrb.get());
    m_presentPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_presentPipeline->setSampleCount(renderTarget()->sampleCount());
    m_presentPipeline->create();
}

void CanvasWidget::ensureOffscreen()
{
    if (m_textureSize.isEmpty())
        return;
    if (m_offscreenTex && m_offscreenTex->pixelSize() == m_textureSize)
        return;

    QRhi *r = rhi();
    m_offscreenTex.reset(r->newTexture(QRhiTexture::RGBA8, m_textureSize, 1,
                                       QRhiTexture::RenderTarget));
    m_offscreenTex->create();
    m_offscreenTexB.reset(r->newTexture(QRhiTexture::RGBA8, m_textureSize, 1,
                                        QRhiTexture::RenderTarget));
    m_offscreenTexB->create();

    QRhiTextureRenderTargetDescription rtDesc(QRhiColorAttachment(m_offscreenTex.get()));
    m_offscreenRt.reset(r->newTextureRenderTarget(rtDesc));
    m_offscreenRpd.reset(m_offscreenRt->newCompatibleRenderPassDescriptor());
    m_offscreenRt->setRenderPassDescriptor(m_offscreenRpd.get());
    m_offscreenRt->create();

    QRhiTextureRenderTargetDescription rtDescB(QRhiColorAttachment(m_offscreenTexB.get()));
    m_offscreenRtB.reset(r->newTextureRenderTarget(rtDescB));
    m_offscreenRtB->setRenderPassDescriptor(m_offscreenRpd.get()); // compatible
    m_offscreenRtB->create();

    // Present samples whichever offscreen holds the final result.
    const auto makePresentSrb = [&](QRhiTexture *tex) {
        auto srb = r->newShaderResourceBindings();
        srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, m_presentUbuf.get()),
            QRhiShaderResourceBinding::sampledTexture(
                1, QRhiShaderResourceBinding::FragmentStage, tex, m_sampler.get()),
        });
        srb->create();
        return srb;
    };
    m_presentSrb.reset(makePresentSrb(m_offscreenTex.get()));
    m_presentSrbB.reset(makePresentSrb(m_offscreenTexB.get()));

    m_pipeline.reset();        // adjustment pipeline depends on the offscreen rpd
    m_presentPipeline.reset(); // and the present pipeline on the present srb
    for (auto &gl : m_extraLayers)
        gl.dirty = true; // their srbs reference the offscreen textures
}

QMatrix4x4 CanvasWidget::fillMvp(const QSize &target)
{
    QMatrix4x4 proj;
    proj.ortho(0.0f, target.width(), target.height(), 0.0f, -1.0f, 1.0f);
    QMatrix4x4 model;
    model.scale(target.width(), target.height());
    QMatrix4x4 m = rhi()->clipSpaceCorrMatrix() * proj * model;
    // Render flipped on bottom-up (OpenGL) framebuffers so the offscreen texture
    // is stored upright — matching uploaded textures (source/masks). Then every
    // pass samples consistently and the present pass needs no flip.
    if (rhi()->isYUpInFramebuffer()) {
        QMatrix4x4 flip;
        flip.scale(1.0f, -1.0f, 1.0f);
        m = flip * m;
    }
    return m;
}

QMatrix4x4 CanvasWidget::computeMvp(const QSize &targetPixels)
{
    const QSizeF widget(targetPixels.width(), targetPixels.height());
    const QSizeF image(m_textureSize.width(), m_textureSize.height());
    if (image.isEmpty() || widget.isEmpty())
        return {};

    const QSizeF disp = zoommath::displayedSize(widget, image, m_zoom);
    const QPointF tl = zoommath::imageTopLeft(widget, image, m_zoom, m_pan);

    // Pixel-space orthographic projection, top-left origin.
    QMatrix4x4 proj;
    proj.ortho(0.0f, widget.width(), widget.height(), 0.0f, -1.0f, 1.0f);

    QMatrix4x4 model;
    model.translate(tl.x(), tl.y());
    model.scale(disp.width(), disp.height());

    // clipSpaceCorrMatrix() adapts GL-style NDC to whatever backend is active.
    return rhi()->clipSpaceCorrMatrix() * proj * model;
}

void CanvasWidget::render(QRhiCommandBuffer *cb)
{
    QRhi *r = rhi();
    QRhiResourceUpdateBatch *u = r->nextResourceUpdateBatch();

    // Keep the converted image alive until after the pass is submitted below.
    QImage keepAlive;
    if (m_textureDirty && !m_pendingImage.isNull()) {
        keepAlive = m_pendingImage.convertToFormat(QImage::Format_RGBA8888);
        const QSize s = keepAlive.size();

        if (!m_texture || m_texture->pixelSize() != s) {
            m_texture.reset(r->newTexture(QRhiTexture::RGBA8, s));
            m_texture->create();
            m_srbDirty = true; // srb references the image texture
        }

        u->uploadTexture(m_texture.get(), keepAlive);
        m_textureSize = s;
        m_pendingImage = QImage();
        m_textureDirty = false;
    }

    // Selective mask texture (R8). Recreate when its size changes.
    if (m_selMaskDirty && m_selMaskTexture) {
        const QSize s(m_selMaskW, m_selMaskH);
        if (m_selMaskTexture->pixelSize() != s) {
            m_selMaskTexture.reset(r->newTexture(QRhiTexture::RGBA8, s));
            m_selMaskTexture->create();
            m_srbDirty = true;
        }
        QRhiTextureSubresourceUploadDescription sub(
            QByteArray(reinterpret_cast<const char *>(m_selMaskData.data()),
                       static_cast<qsizetype>(m_selMaskData.size())));
        u->uploadTexture(m_selMaskTexture.get(),
                         QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, sub)));
        m_selMaskDirty = false;
    }

    if (m_layerMaskDirty && m_layerMaskTexture) {
        const uint8_t white[4] = {255, 255, 255, 255};
        QRhiTextureSubresourceUploadDescription sub(
            QByteArray(reinterpret_cast<const char *>(white), 4));
        u->uploadTexture(m_layerMaskTexture.get(),
                         QRhiTextureUploadDescription(QRhiTextureUploadEntry(0, 0, sub)));
        m_layerMaskDirty = false;
    }

    if (m_srbDirty && m_texture && m_lutTexture && m_lut3dTexture && m_selMaskTexture
        && m_layerMaskTexture) {
        buildSrb();
        m_srbDirty = false;
    }

    if (m_lutDirty && m_lutTexture) {
        // Interleave the three LUTs into RGBA texels (alpha unused).
        QByteArray bytes(256 * 4, char(0));
        for (int i = 0; i < 256; ++i) {
            bytes[i * 4 + 0] = static_cast<char>(m_luts[0][i]);
            bytes[i * 4 + 1] = static_cast<char>(m_luts[1][i]);
            bytes[i * 4 + 2] = static_cast<char>(m_luts[2][i]);
            bytes[i * 4 + 3] = char(255);
        }
        QRhiTextureSubresourceUploadDescription sub(bytes);
        QRhiTextureUploadEntry entry(0, 0, sub);
        u->uploadTexture(m_lutTexture.get(), QRhiTextureUploadDescription(entry));
        m_lutDirty = false;
    }

    if (m_lut3dDirty && m_lut3dTexture) {
        // One upload entry per depth slice (layer = z) of the 3D texture.
        const int sliceBytes = kLut3DDim * kLut3DDim * 4;
        std::vector<QByteArray> slices;
        std::vector<QRhiTextureUploadEntry> entries;
        slices.reserve(kLut3DDim);
        entries.reserve(kLut3DDim);
        for (int z = 0; z < kLut3DDim; ++z) {
            slices.emplace_back(reinterpret_cast<const char *>(
                                    m_lut3dData.data() + static_cast<size_t>(z) * sliceBytes),
                                sliceBytes);
            entries.emplace_back(z, 0, QRhiTextureSubresourceUploadDescription(slices.back()));
        }
        QRhiTextureUploadDescription desc;
        desc.setEntries(entries.begin(), entries.end());
        u->uploadTexture(m_lut3dTexture.get(), desc);
        m_lut3dDirty = false;
    }

    ensureOffscreen();
    ensurePipeline();

    const QSize target = renderTarget()->pixelSize();
    const QColor clearColor(17, 17, 19); // matches the app's dark canvas
    const QRhiCommandBuffer::VertexInput vbufBinding(m_vbuf.get(), 0);
    const bool drawable = m_pipeline && m_presentPipeline && m_srb && m_presentSrb
                       && m_texture && m_offscreenRt && m_offscreenRtB;

    if (drawable) {
        // Adjustment uniforms: a fixed fill transform for the offscreen target.
        const QMatrix4x4 fill = fillMvp(m_textureSize);
        u->updateDynamicBuffer(m_ubuf.get(), 0, 64, fill.constData());
        static_assert(sizeof(PreviewState) == 26 * sizeof(float),
                      "PreviewState must be 26 tightly-packed floats");
        u->updateDynamicBuffer(m_ubuf.get(), 64, sizeof(PreviewState), &m_preview.exposure);
        // Present transform: zoom/pan onto the screen.
        const QMatrix4x4 mvp = computeMvp(target);
        u->updateDynamicBuffer(m_presentUbuf.get(), 0, 64, mvp.constData());

        const QRhiViewport imgViewport(0, 0, float(m_textureSize.width()),
                                       float(m_textureSize.height()));

        // Base pass: the Base layer's chain into offscreen A.
        cb->beginPass(m_offscreenRt.get(), QColor(0, 0, 0, 0), {1.0f, 0}, u);
        cb->setGraphicsPipeline(m_pipeline.get());
        cb->setViewport(imgViewport);
        cb->setShaderResources(m_srb.get());
        cb->setVertexInput(0, 1, &vbufBinding);
        cb->draw(4);
        cb->endPass();

        // One pass per extra layer, ping-ponging A↔B (even index → B, odd → A).
        for (int i = 0; i < static_cast<int>(m_extraLayers.size()); ++i) {
            GpuLayer &gl = m_extraLayers[i];
            QRhiResourceUpdateBatch *batch = nullptr;
            if (gl.dirty || !gl.srb) {
                batch = r->nextResourceUpdateBatch();
                buildExtraLayer(gl, i, batch);
                gl.dirty = false;
            }
            QRhiTextureRenderTarget *outRt =
                (i % 2 == 0) ? m_offscreenRtB.get() : m_offscreenRt.get();
            cb->beginPass(outRt, QColor(0, 0, 0, 0), {1.0f, 0}, batch);
            cb->setGraphicsPipeline(m_pipeline.get());
            cb->setViewport(imgViewport);
            cb->setShaderResources(gl.srb.get());
            cb->setVertexInput(0, 1, &vbufBinding);
            cb->draw(4);
            cb->endPass();
        }

        // Present the final offscreen (A if an even number of extra layers, else B).
        QRhiShaderResourceBindings *finalSrb =
            (m_extraLayers.size() % 2 == 0) ? m_presentSrb.get() : m_presentSrbB.get();
        cb->beginPass(renderTarget(), clearColor, {1.0f, 0});
        cb->setGraphicsPipeline(m_presentPipeline.get());
        cb->setViewport({0, 0, float(target.width()), float(target.height())});
        cb->setShaderResources(finalSrb);
        cb->setVertexInput(0, 1, &vbufBinding);
        cb->draw(4);
        cb->endPass();
    } else {
        cb->beginPass(renderTarget(), clearColor, {1.0f, 0}, u);
        cb->endPass();
    }
}

void CanvasWidget::setColorPickMode(bool on)
{
    m_pickMode = on;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
}

void CanvasWidget::setBrushMode(bool on)
{
    m_brushMode = on;
    m_brushing = false;
    if (!on)
        m_brushAdjusting = false;
    // Track hover moves so the ring follows the cursor without a button held.
    setMouseTracking(on);
    // Hide the system pointer in brush mode — the ring overlay is the cursor.
    setCursor(on ? Qt::BlankCursor : Qt::ArrowCursor);
    if (!on)
        emit brushCursorMoved({}, 0, 0, false);
    else if (m_hasCursorPos)
        emitBrushCursor(m_lastCursorPos);
}

void CanvasWidget::setBrushCursor(float size, float hardness)
{
    m_brushCursorSize = size;
    m_brushCursorHardness = hardness;
    if (m_brushMode && m_hasCursorPos)
        emitBrushCursor(m_lastCursorPos);
}

void CanvasWidget::emitBrushCursor(QPointF widgetPos)
{
    m_lastCursorPos = widgetPos;
    m_hasCursorPos = true;
    if (!m_brushMode || m_textureSize.isEmpty()) {
        emit brushCursorMoved(widgetPos, 0, 0, false);
        return;
    }
    const qreal dpr = devicePixelRatioF();
    const QSizeF widget(width() * dpr, height() * dpr);
    const QSizeF image(m_textureSize.width(), m_textureSize.height());
    const QSizeF disp = zoommath::displayedSize(widget, image, m_zoom);
    // Match MainWindow::brushAt: radius = (size/100)*0.3 of the image's smaller
    // displayed dimension. Convert device px back to logical for the overlay.
    const double f = (m_brushCursorSize / 100.0) * 0.3;
    const double outerLogical = f * std::min(disp.width(), disp.height()) / dpr;
    const double innerLogical = outerLogical * m_brushCursorHardness;
    emit brushCursorMoved(widgetPos, outerLogical, innerLogical, true);
}

void CanvasWidget::leaveEvent(QEvent *)
{
    m_hasCursorPos = false;
    emit brushCursorMoved({}, 0, 0, false);
}

QPointF CanvasWidget::widgetForNormalized(QPointF norm, QSizeF *dispLogicalOut) const
{
    const qreal dpr = devicePixelRatioF();
    const QSizeF widget(width() * dpr, height() * dpr);
    const QSizeF image(m_textureSize.width(), m_textureSize.height());
    if (image.isEmpty() || widget.isEmpty())
        return {};
    const QSizeF disp = zoommath::displayedSize(widget, image, m_zoom);
    const QPointF tl = zoommath::imageTopLeft(widget, image, m_zoom, m_pan);
    if (dispLogicalOut)
        *dispLogicalOut = QSizeF(disp.width() / dpr, disp.height() / dpr);
    const QPointF device(tl.x() + norm.x() * disp.width(),
                         tl.y() + norm.y() * disp.height());
    return device / dpr;
}

QPointF CanvasWidget::normalizedForWidget(QPointF widgetLogical) const
{
    const qreal dpr = devicePixelRatioF();
    const QSizeF widget(width() * dpr, height() * dpr);
    const QSizeF image(m_textureSize.width(), m_textureSize.height());
    if (image.isEmpty() || widget.isEmpty())
        return {};
    const QSizeF disp = zoommath::displayedSize(widget, image, m_zoom);
    const QPointF tl = zoommath::imageTopLeft(widget, image, m_zoom, m_pan);
    const QPointF device = widgetLogical * dpr;
    return QPointF(std::clamp((device.x() - tl.x()) / disp.width(), 0.0, 1.0),
                  std::clamp((device.y() - tl.y()) / disp.height(), 0.0, 1.0));
}

QPointF CanvasWidget::imageNormalizedAt(const QPointF &widgetPos)
{
    const qreal dpr = devicePixelRatioF();
    const QSizeF widget(width() * dpr, height() * dpr);
    const QSizeF image(m_textureSize.width(), m_textureSize.height());
    const QPointF px = zoommath::imagePixelAt(widget, image, m_zoom, m_pan, widgetPos * dpr);
    return QPointF(std::clamp(px.x() / image.width(), 0.0, 1.0),
                   std::clamp(px.y() / image.height(), 0.0, 1.0));
}

void CanvasWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;

    if (m_textureSize.isEmpty()) {
        m_lastMousePos = e->position();
        return;
    }

    if (m_pickMode) {
        const QPointF norm = imageNormalizedAt(e->position());
        setColorPickMode(false);
        emit colorPointPicked(norm);
        return;
    }

    if (m_brushMode) {
        m_brushing = true;
        emitBrushCursor(e->position());
        emit brushStrokeBegan();
        emit brushPoint(imageNormalizedAt(e->position()));
        return;
    }

    m_lastMousePos = e->position();
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_brushing) {
        m_brushing = false;
        emit brushStrokeEnded();
    }
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_brushMode)
        emitBrushCursor(e->position()); // keep the ring under the cursor
    if (m_brushing) {
        emit brushPoint(imageNormalizedAt(e->position()));
        return;
    }
    if (e->buttons() & Qt::LeftButton) {
        const QPointF delta = e->position() - m_lastMousePos;
        m_lastMousePos = e->position();
        // Convert logical-pixel delta to device pixels (pan is in device px).
        m_pan += delta * devicePixelRatioF();
        emit viewChanged();
        update();
    }
}

void CanvasWidget::wheelEvent(QWheelEvent *e)
{
    // While a brush-adjust key (s/h) is held, the wheel changes the brush
    // instead of zooming.
    if (m_brushMode && m_brushAdjusting && e->angleDelta().y() != 0) {
        emit brushAdjustRequested(e->angleDelta().y() > 0 ? 1 : -1);
        return;
    }

    const float factor = (e->angleDelta().y() > 0) ? 1.1f : (1.0f / 1.1f);
    // Zoom around the cursor: the image point under the pointer stays put.
    zoomAt(factor, e->position() * devicePixelRatioF());
    if (m_brushMode) // brush ring scales with zoom
        emitBrushCursor(e->position());
}

void CanvasWidget::zoomAt(float factor, const QPointF &cursorDevicePx)
{
    const QSizeF widget(width() * devicePixelRatioF(), height() * devicePixelRatioF());
    const QSizeF image(m_textureSize.width(), m_textureSize.height());
    if (image.isEmpty() || widget.isEmpty())
        return;

    const float newZoom = std::clamp(m_zoom * factor, 0.05f, 40.0f);
    if (newZoom == m_zoom)
        return;

    m_pan = zoommath::panForZoom(widget, image, m_zoom, newZoom, m_pan, cursorDevicePx);
    m_zoom = newZoom;
    emit viewChanged();
    update();
}
