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
        m_ubuf.reset();
        m_vbuf.reset();
        m_textureSize = {};
        m_textureDirty = !m_pendingImage.isNull();
        m_lutDirty = true;
        m_lut3dDirty = true;
        m_selMaskDirty = true;
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
        // std140: mat4 mvp (64) + PreviewState's 13 floats (52) at offset 64.
        // Rounded up to a 16-byte multiple → 128 bytes.
        m_ubuf.reset(r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 128));
        m_ubuf->create();
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

void CanvasWidget::ensurePipeline()
{
    if (m_pipeline || !m_srb)
        return;

    QRhi *r = rhi();
    m_pipeline.reset(r->newGraphicsPipeline());
    m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/texture.vert.qsb"))},
        {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/texture.frag.qsb"))},
    });

    QRhiVertexInputLayout layout;
    layout.setBindings({{4 * sizeof(float)}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)},
    });
    m_pipeline->setVertexInputLayout(layout);
    m_pipeline->setShaderResourceBindings(m_srb.get());
    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_pipeline->setSampleCount(renderTarget()->sampleCount());
    m_pipeline->create();
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

    if (m_srbDirty && m_texture && m_lutTexture && m_lut3dTexture && m_selMaskTexture) {
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

    ensurePipeline();

    const QSize target = renderTarget()->pixelSize();
    const bool drawable = m_pipeline && m_srb && m_texture;
    if (drawable) {
        const QMatrix4x4 mvp = computeMvp(target);
        u->updateDynamicBuffer(m_ubuf.get(), 0, 64, mvp.constData());
        // PreviewState's floats are contiguous and match the shader block order.
        static_assert(sizeof(PreviewState) == 14 * sizeof(float),
                      "PreviewState must be 14 tightly-packed floats");
        u->updateDynamicBuffer(m_ubuf.get(), 64, sizeof(PreviewState), &m_preview.exposure);
    }

    const QColor clearColor(17, 17, 19); // matches the app's dark canvas
    cb->beginPass(renderTarget(), clearColor, {1.0f, 0}, u);

    if (drawable) {
        cb->setGraphicsPipeline(m_pipeline.get());
        cb->setViewport({0, 0, float(target.width()), float(target.height())});
        cb->setShaderResources(m_srb.get());
        const QRhiCommandBuffer::VertexInput vbufBinding(m_vbuf.get(), 0);
        cb->setVertexInput(0, 1, &vbufBinding);
        cb->draw(4);
    }

    cb->endPass();
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
    update();
}
