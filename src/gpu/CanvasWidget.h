#pragma once

#include "core/CropState.h"
#include "core/Vignette.h"
#include "core/LayerPreview.h"
#include "core/Lut.h"
#include "core/Lut3D.h"
#include "core/PreviewState.h"
#include "core/SelectiveMask.h"

#include <QRhiWidget>
#include <rhi/qrhi.h>

#include <QImage>
#include <QMatrix4x4>
#include <QPointF>
#include <QSize>
#include <QtCore/qfloat16.h>

#include <cstdint>
#include <memory>
#include <vector>

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

    // Swaps in a new image. The upload happens lazily on the next render. By
    // default the view resets to fit (a newly opened image); pass keepView=true
    // for a same-size in-place update (e.g. a heal result) to preserve zoom/pan.
    void setImage(const QImage &image, bool keepView = false);

    // Sets the preview parameters accumulated from the edit graph; applied live
    // in the fragment shader.
    void setPreviewState(const PreviewState &state);

    // Sets the per-channel tone-curve LUTs applied after the tone ops.
    void setCurveLuts(const ChannelLuts &luts);

    // Sets the 3D LUT "look" applied last (resampled into a fixed-size cube).
    void setLut3D(const Lut3D &look);

    // Sets the selective colour-affinity mask texture (used when mask mode is
    // colour). An empty mask resets to fully-selected.
    void setSelectiveMask(const MaskBuffer &mask);

    // Sets the layers ABOVE the Base (which is driven by the setters above).
    // Each is composited as an additional pass over the running result.
    void setExtraLayers(const std::vector<LayerPreview> &layers);

    // Resets zoom/pan so the image is fit-to-window and centred.
    void resetView();

    // Centres and sets the fit-relative zoom (1.0 = fit-to-window; <1 leaves a
    // margin). The crop tool uses this to pull the frame in from the screen edges
    // so its handles are reachable.
    void setFitZoom(float zoom);

    // Crop/orientation view. Mode: None = full source (identity); Applied =
    // oriented + cropped (browse); Editing = oriented full frame (crop tool open,
    // with the crop gizmo on top). The transform is applied in the present pass;
    // the offscreen always renders the full composite.
    // CropMaskEdit: like CropEditing (full oriented frame, crop rect ignored) but
    // used by the mask/gizmo tools. The image displays oriented so the user's
    // rotation/flip is preserved, while the coordinate helpers below transparently
    // convert to/from the un-oriented source frame the masks actually live in.
    enum CropViewMode { CropNone, CropApplied, CropEditing, CropMaskEdit };
    void setCropState(const CropState &crop, CropViewMode mode);

    // Creative (post-crop) vignette params for the present pass. Mirrors the
    // export-side core/Vignette.cpp; positioned over the displayed cropped frame.
    void setVignette(const VignetteParams &v);

    // Clipping warnings ("blinkies"): when on, the present pass paints blown
    // highlights red and crushed shadows blue over the final displayed colour.
    void setClipping(bool on);
    // Effective displayed image size (device-independent units relative to the
    // source texture), accounting for orientation + crop in the current mode.
    QSizeF effectiveImageSize() const;

    // image-normalised [0,1] → widget-logical position (inverse of the pick
    // mapping). For on-canvas gizmos. Returns the displayed scale in
    // logical px/normalised-unit via *scaleOut (x,y) if requested.
    QPointF widgetForNormalized(QPointF norm, QSizeF *dispLogicalOut = nullptr) const;
    QPointF normalizedForWidget(QPointF widgetLogical) const; // inverse, clamped [0,1]

    // Enters colour-pick mode: the next left-click emits colorPointPicked with
    // the image-normalised position instead of panning.
    void setColorPickMode(bool on);

    // Brush mode: left-drag paints (emits brush signals) instead of panning.
    void setBrushMode(bool on);

    // Brush size (1-100 slider units) and hardness (0-1) for the on-canvas brush
    // ring cursor. Must match MainWindow's stamp radius math.
    void setBrushCursor(float size, float hardness);

    // While true (a size/hardness modifier key is held), the wheel adjusts the
    // brush via brushAdjustRequested instead of zooming.
    void setBrushAdjusting(bool on) { m_brushAdjusting = on; }

signals:
    // Right-click on the canvas requests the command palette — a pointer-only
    // entry point that mirrors the "/" key (docs/DESIGN.md §4.6). Inert while a
    // brush or colour-pick is active.
    void paletteRequested();
    void colorPointPicked(QPointF imageNormalized);
    void brushStrokeBegan();
    void brushPoint(QPointF imageNormalized);
    void brushStrokeEnded();
    // Brush ring geometry in widget-logical coords; visible=false hides it.
    void brushCursorMoved(QPointF widgetPos, qreal outerRadius, qreal innerRadius,
                          bool visible);
    // Wheel notches (+/-) while a brush-adjust key is held.
    void brushAdjustRequested(int steps);
    // Emitted when the zoom/pan transform changes, so on-canvas overlays (the
    // mask gizmo) can repaint against the new mapping.
    void viewChanged();

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;

    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    // GPU resources for one extra (above-Base) layer.
    struct GpuLayer {
        LayerPreview data;
        bool dirty = true;
        std::unique_ptr<QRhiBuffer> ubuf;
        std::unique_ptr<QRhiTexture> curveTex;
        std::unique_ptr<QRhiTexture> lut3dTex;
        std::unique_ptr<QRhiTexture> selMaskTex;
        std::unique_ptr<QRhiTexture> layerMaskTex;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
    };

    void ensurePipeline();
    void ensureOffscreen(); // offscreen targets sized to the image
    void buildSrb();
    // Builds/uploads an extra layer's GPU resources; `index` selects its input
    // (ping-pong A/B). `batch` carries the uploads.
    void buildExtraLayer(GpuLayer &gl, int index, QRhiResourceUpdateBatch *batch);
    void emitBrushCursor(QPointF widgetPos);
    QPointF imageNormalizedAt(const QPointF &widgetPos);
    QMatrix4x4 computeMvp(const QSize &targetPixels);
    QMatrix4x4 fillMvp(const QSize &targetPixels); // maps the quad to fill target
    // Present-pass texcoord transform: output unit-quad → source [0,1], encoding
    // the current crop/orientation. Identity in CropNone.
    QMatrix4x4 cropTexXform() const;
    // In CropMaskEdit the display is oriented but masks live in the un-oriented
    // source frame; these convert a normalised point between the two (no-op in
    // every other mode). oriented→source uses cropTexXform; source→oriented its
    // inverse. Both map the unit square onto itself, so results stay in [0,1].
    QPointF sourceNormFromOriented(QPointF orientedNorm) const;
    QPointF orientedNormFromSource(QPointF sourceNorm) const;
    // Multiplies zoom by `factor`, keeping the image point under the cursor fixed.
    void zoomAt(float factor, const QPointF &cursorDevicePx);

    // RHI we last built resources against; used to detect device changes.
    QRhi *m_rhi = nullptr;

    std::unique_ptr<QRhiBuffer> m_vbuf;
    std::unique_ptr<QRhiBuffer> m_ubuf;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiTexture> m_texture;
    std::unique_ptr<QRhiSampler> m_lutSampler;
    std::unique_ptr<QRhiTexture> m_lutTexture; // 256x1 tone-curve LUT
    std::unique_ptr<QRhiSampler> m_lut3dSampler;
    std::unique_ptr<QRhiTexture> m_lut3dTexture; // 32^3 look LUT
    std::unique_ptr<QRhiSampler> m_selMaskSampler;
    std::unique_ptr<QRhiTexture> m_selMaskTexture; // selective mask (R8)
    std::unique_ptr<QRhiTexture> m_layerMaskTexture; // base layer mask (white)
    bool m_layerMaskDirty = true;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline; // adjustment pass (offscreen)
    bool m_srbDirty = true;

    // Two offscreen targets (A/B) the adjustment passes ping-pong between; the
    // present pass draws the final one to the screen with zoom/pan.
    std::unique_ptr<QRhiTexture> m_offscreenTex;   // A
    std::unique_ptr<QRhiTextureRenderTarget> m_offscreenRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_offscreenRpd;
    std::unique_ptr<QRhiTexture> m_offscreenTexB;  // B
    std::unique_ptr<QRhiTextureRenderTarget> m_offscreenRtB;
    std::unique_ptr<QRhiBuffer> m_presentUbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_presentSrb;  // samples A
    std::unique_ptr<QRhiShaderResourceBindings> m_presentSrbB; // samples B
    std::unique_ptr<QRhiGraphicsPipeline> m_presentPipeline;
    std::vector<GpuLayer> m_extraLayers;

    // Pending image waiting to be uploaded to m_texture.
    QImage m_pendingImage;
    bool m_textureDirty = false;
    QSize m_textureSize;

    // Crop/orientation view state (applied in the present pass).
    CropState m_crop;
    CropViewMode m_cropView = CropNone;
    VignetteParams m_vignette; // creative vignette (present pass)
    bool m_showClipping = false; // clipping warnings overlay (present pass)

    // View state, in device pixels.
    float m_zoom = 1.0f;
    QPointF m_pan{0.0, 0.0};
    QPointF m_lastMousePos;
    bool m_pickMode = false;
    bool m_brushMode = false;
    bool m_brushing = false;
    float m_brushCursorSize = 30.0f;     // 1-100 slider units
    float m_brushCursorHardness = 0.5f;  // 0-1
    QPointF m_lastCursorPos;
    bool m_hasCursorPos = false;
    bool m_brushAdjusting = false;       // s/h modifier held → wheel adjusts brush

    // Preview adjustments (from the edit graph) fed to the shader.
    PreviewState m_preview;
    ChannelLuts m_luts = identityChannelLuts(); // per-channel tone curves
    bool m_lutDirty = true;
    // 32^3 RGBA half-float cube (RGBA16F); identity by default. Half-float keeps
    // the loaded LUT's precision (and HDR/out-of-range outputs) on the GPU while
    // retaining hardware trilinear filtering.
    std::vector<qfloat16> m_lut3dData;
    bool m_lut3dDirty = true;
    std::vector<uint8_t> m_selMaskData{255, 0, 0, 255}; // RGBA, R=mask; 1x1 selected
    int m_selMaskW = 1;
    int m_selMaskH = 1;
    bool m_selMaskDirty = true;
};
