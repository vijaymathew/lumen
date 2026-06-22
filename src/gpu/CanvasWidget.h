#pragma once

#include "core/PreviewState.h"

#include <QRhiWidget>
#include <rhi/qrhi.h>

#include <QImage>
#include <QMatrix4x4>
#include <QPointF>
#include <QSize>

#include <memory>

// CanvasWidget displays the working image as a single textured quad rendered
// through Qt RHI, so the same code path runs on Metal (macOS), Vulkan, and
// OpenGL. It supports fit-to-window display plus interactive zoom (wheel) and
// pan (left-drag). No edit pipeline yet — this is the "display the image" half
// of milestone 1.
//
// Zoom is currently centred on the viewport (not the cursor); see TODO in the
// wheel handler.
class CanvasWidget : public QRhiWidget {
    Q_OBJECT

public:
    explicit CanvasWidget(QWidget *parent = nullptr);

    // Swaps in a new image. The upload happens lazily on the next render.
    void setImage(const QImage &image);

    // Sets the preview parameters accumulated from the edit graph; applied live
    // in the fragment shader.
    void setPreviewState(const PreviewState &state);

    // Resets zoom/pan so the image is fit-to-window and centred.
    void resetView();

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;

    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    void ensurePipeline();
    QMatrix4x4 computeMvp(const QSize &targetPixels);
    // Multiplies zoom by `factor`, keeping the image point under the cursor fixed.
    void zoomAt(float factor, const QPointF &cursorDevicePx);

    // RHI we last built resources against; used to detect device changes.
    QRhi *m_rhi = nullptr;

    std::unique_ptr<QRhiBuffer> m_vbuf;
    std::unique_ptr<QRhiBuffer> m_ubuf;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;

    // Pending image waiting to be uploaded to m_texture.
    QImage m_pendingImage;
    bool m_textureDirty = false;
    QSize m_textureSize;

    // View state, in device pixels.
    float m_zoom = 1.0f;
    QPointF m_pan{0.0, 0.0};
    QPointF m_lastMousePos;

    // Preview adjustments (from the edit graph) fed to the shader.
    PreviewState m_preview;
};
