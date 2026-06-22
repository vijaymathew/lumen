#include "gpu/CanvasWidget.h"

#include <QFile>
#include <QMouseEvent>
#include <QWheelEvent>
#include <rhi/qshader.h>

#include <algorithm>

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

} // namespace

CanvasWidget::CanvasWidget(QWidget *parent)
    : QRhiWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
}

void CanvasWidget::setImage(const QImage &image)
{
    m_pendingImage = image;
    m_textureDirty = true;
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
    if (m_preview.exposure == state.exposure)
        return;
    m_preview = state;
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
        m_ubuf.reset();
        m_vbuf.reset();
        m_textureSize = {};
        m_textureDirty = !m_pendingImage.isNull();
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
        // std140 layout: mat4 mvp (64 bytes) + float exposure at offset 64.
        // Rounded up to a 16-byte multiple → 80 bytes.
        m_ubuf.reset(r->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80));
        m_ubuf->create();
    }

    if (!m_sampler) {
        m_sampler.reset(r->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                      QRhiSampler::None, QRhiSampler::ClampToEdge,
                                      QRhiSampler::ClampToEdge));
        m_sampler->create();
    }
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
    const float w = targetPixels.width();
    const float h = targetPixels.height();
    const float iw = m_textureSize.width();
    const float ih = m_textureSize.height();
    if (iw <= 0 || ih <= 0 || w <= 0 || h <= 0)
        return {};

    const float fit = std::min(w / iw, h / ih);
    const float scale = fit * m_zoom;
    const float dw = iw * scale;
    const float dh = ih * scale;
    const float dx = (w - dw) * 0.5f + static_cast<float>(m_pan.x());
    const float dy = (h - dh) * 0.5f + static_cast<float>(m_pan.y());

    // Pixel-space orthographic projection, top-left origin.
    QMatrix4x4 proj;
    proj.ortho(0.0f, w, h, 0.0f, -1.0f, 1.0f);

    QMatrix4x4 model;
    model.translate(dx, dy);
    model.scale(dw, dh);

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

            // The srb references the texture, so rebuild it (layout is stable,
            // so the pipeline stays valid).
            m_srb.reset(r->newShaderResourceBindings());
            m_srb->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0,
                    QRhiShaderResourceBinding::VertexStage
                        | QRhiShaderResourceBinding::FragmentStage,
                    m_ubuf.get()),
                QRhiShaderResourceBinding::sampledTexture(
                    1, QRhiShaderResourceBinding::FragmentStage,
                    m_texture.get(), m_sampler.get()),
            });
            m_srb->create();
        }

        u->uploadTexture(m_texture.get(), keepAlive);
        m_textureSize = s;
        m_pendingImage = QImage();
        m_textureDirty = false;
    }

    ensurePipeline();

    const QSize target = renderTarget()->pixelSize();
    const bool drawable = m_pipeline && m_srb && m_texture;
    if (drawable) {
        const QMatrix4x4 mvp = computeMvp(target);
        u->updateDynamicBuffer(m_ubuf.get(), 0, 64, mvp.constData());
        u->updateDynamicBuffer(m_ubuf.get(), 64, sizeof(float), &m_preview.exposure);
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

void CanvasWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        m_lastMousePos = e->position();
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *e)
{
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
    // TODO(milestone-2): zoom around the cursor position rather than the centre.
    const float factor = (e->angleDelta().y() > 0) ? 1.1f : (1.0f / 1.1f);
    m_zoom = std::clamp(m_zoom * factor, 0.05f, 40.0f);
    update();
}
