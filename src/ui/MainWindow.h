#pragma once

#include <QByteArray>
#include <QFutureWatcher>
#include <QImage>
#include <QMainWindow>
#include <QPointF>

#include <atomic>

#include "core/CurvesNode.h"
#include "core/EditGraph.h"
#include "core/HealNode.h"
#include "core/LensCorrectionNode.h"
#include "core/LutNode.h"
#include "core/MaskSpec.h"
#include "core/DenoiseNode.h"
#include "core/Histogram.h"
#include "core/MonoNode.h"
#include "core/RawLoader.h"
#include "core/SelectiveMask.h"
#include "core/SharpenNode.h"
#include "core/TuneNode.h"
#include "input/InputController.h"

#include <vector>

class CanvasWidget;
class CommandPalette;
class CurvesPanel;
class DenoisePanel;
class HealPanel;
class HistogramWidget;
class LayersPanel;
class LensPanel;
class MaskGizmo;
class LooksPanel;
class MonoPanel;
class SharpenPanel;
class TonePanel;
class QLabel;
class QTimer;

// MainWindow is the immersive shell: a fullscreen canvas with a "/"-triggered
// command palette floating over it and a dismissible hint bar. It owns the
// InputController and routes commands to actions. Milestone 1 wires the
// navigation/shell commands; editing tools arrive in later milestones.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Loads an image at startup (e.g. a path passed on the command line).
    bool openPath(const QString &path);

protected:
    void resizeEvent(QResizeEvent *e) override;
    // Central key handling: catches keys via propagation no matter which child
    // widget has focus, so the active tool can always be closed.
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;

private:
    void buildCommands();
    void runCommand(const QString &id);
    void openImageDialog();
    void saveProject();   // write the current work to a .lumen file
    void openProject();   // pick a .lumen file via dialog, then load it
    bool loadProjectFile(const QString &path); // load a .lumen (source + layers)
    void toggleFullScreen();
    void showHint(const QString &text);
    void layoutOverlays();

    void openCommandPalette();
    void openLayersTool();    // toggles the Layers panel
    void showLayersPanel();   // ensures it is visible (used by the selective cmd)
    void hideLayersPanel();
    void refreshLayersPanel();
    void addAdjustmentLayer();
    void deleteActiveLayer();
    void selectLayer(int index);
    // Mask editing for the active layer (Layers-panel controls + on-canvas gizmo).
    void setActiveLayerMaskType(int maskType);
    void onLayerMaskEdited(const MaskSpec &spec, bool commit);
    void syncMaskGizmo();      // reflect the active layer's mask into the gizmo
    void updateMaskEditing();  // enable/disable the canvas brush for a Brush mask
    void endMaskBrushSession(); // commit in-progress mask-brush strokes
    // The active layer's tone/curves/look/mono nodes (tools edit the active layer).
    TuneNode *activeTune() const;
    CurvesNode *activeCurves() const;
    LutNode *activeLut() const;
    MonoNode *activeMono() const;
    void openToneTool();
    void closeToneTool();
    void openCurvesTool();
    void closeCurvesTool();
    void openLooksTool();
    void closeLooksTool();
    void loadLookFile();
    void openMonoTool();
    void closeMonoTool();
    void openLensTool();  // toggles the Lens & Perspective panel
    void closeLensTool();
    void openSharpenTool();  // toggles the Sharpen panel
    void closeSharpenTool();
    void openDenoiseTool();  // toggles the Denoise panel
    void closeDenoiseTool();
    void toggleHistogram();  // show/hide the histogram overlay
    void updateHistogram();  // recompute from the current result (when visible)
    // Recomputes the cached lens-corrected working source (and its display
    // QImage) from the original; cheap no-op when no correction is active. Called
    // when the lens parameters or the source image change — NOT per heal dab.
    void refreshWorkingSource();
    void recomputeSelectiveMask(); // uploads the active layer's mask as the overlay
    void onColorPicked(const QPointF &imageNormalized);
    // A selective adjustment is a masked layer (mask = Luminosity/Colour/Brush +
    // the layer's TuneNode), edited via the Layers panel. ensureSelectiveLayer
    // adds/selects a layer to drive and returns its index.
    int ensureSelectiveLayer();
    void syncBrushMaskToLayer(); // copy the working brush mask into the active layer
    void openHealTool();
    void closeHealTool();
    // base texture = source healed by the heal node. keepView preserves zoom/pan
    // (true for in-place heal updates; false only when loading a new image).
    void refreshBaseImage(bool keepView = true);
    void initBrushMask();
    void beginBrushStroke();
    void brushAt(const QPointF &imageNormalized);
    void endBrushStroke();
    bool brushSessionUndo(); // returns true if it handled the undo
    void adjustBrush(int steps); // s/h + wheel
    void syncBrushPanel();       // reflect m_brushSize/Hardness into the open panel
    void closeActiveTool();
    void updatePreview(); // push tone state + curve LUT + look to the canvas
    void exportImage();

    void doUndo();
    void doRedo();
    void afterHistoryChange(); // refresh preview + any open tool after undo/redo

    InputController m_input;
    CanvasWidget *m_canvas = nullptr;
    QWidget *m_scrim = nullptr;     // dims the image behind the command palette
    QWidget *m_brushRing = nullptr; // on-canvas brush size/hardness cursor
    QWidget *m_healBusy = nullptr;  // animated "Healing…" badge during async heal
    CommandPalette *m_palette = nullptr;
    TonePanel *m_tonePanel = nullptr;
    CurvesPanel *m_curvesPanel = nullptr;
    LooksPanel *m_looksPanel = nullptr;
    MonoPanel *m_monoPanel = nullptr;
    LensPanel *m_lensPanel = nullptr;
    SharpenPanel *m_sharpenPanel = nullptr;
    DenoisePanel *m_denoisePanel = nullptr;
    HealPanel *m_healPanel = nullptr;
    HistogramWidget *m_histogram = nullptr;
    QTimer *m_histTimer = nullptr; // debounces histogram recompute
    QTimer *m_bakeTimer = nullptr; // debounces sharpen base re-bake
    LayersPanel *m_layersPanel = nullptr;
    MaskGizmo *m_maskGizmo = nullptr; // on-canvas gradient/radial mask editor
    QLabel *m_hint = nullptr;

    // The non-destructive edit graph. The GPU preview reads the tune node's
    // exposure live; Export walks the graph at full resolution via libvips.
    EditGraph m_graph;
    TuneNode *m_tune = nullptr;          // owned by m_graph
    CurvesNode *m_curves = nullptr;      // owned by m_graph
    LutNode *m_lutNode = nullptr;        // owned by m_graph
    MonoNode *m_mono = nullptr;          // owned by m_graph
    HealNode *m_heal = nullptr;          // owned by m_graph (second in the chain)
    LensCorrectionNode *m_lens = nullptr; // owned by m_graph (first in the chain)
    DenoiseNode *m_denoise = nullptr;     // owned by m_graph (after heal, before sharpen)
    SharpenNode *m_sharpen = nullptr;     // owned by m_graph (after denoise, before tune)
    Image m_workingSource;               // cached lens-corrected source (preview base input)
    QString m_sourcePath;                // for a sensible default export name
    QString m_exportExt = QStringLiteral("jpg"); // remembered export format
    int m_exportQuality = 90;                    // remembered export quality
    QImage m_sourceQImage;               // for colour sampling + preview mask
    QByteArray m_sourceBytes;            // original encoded source, for embedding in .lumen
    QString m_sourceName;                // original source file name
    QString m_projectPath;               // current .lumen path (empty until saved/opened)
    int m_maskView = 0;                  // selective mask overlay (preview-only)

    // Shared brush-paint session (used by the selective brush and the heal
    // brush, one at a time).
    enum class BrushTarget { None, Selective, Heal };
    BrushTarget m_brushTarget = BrushTarget::None;
    MaskBuffer m_brushMask;
    std::vector<float> m_strokeBaseMask;         // mask at the current stroke's start
    std::vector<std::vector<float>> m_brushUndo; // per-stroke snapshots
    int m_brushSize = 30;
    int m_brushHardness = 50;
    bool m_brushAdd = true;
    QPointF m_lastBrushPoint;
    bool m_brushHasLast = false;
    bool m_healPainting = false;      // a heal stroke is in progress (red overlay)
    bool m_adjustHardness = false;    // s/h + wheel target: false=size, true=hardness

    // The heal (inpaint) preview runs off the UI thread so Detailed mode never
    // freezes the app; only the latest request's result is applied.
    QFutureWatcher<QImage> m_healWatcher;
    std::atomic<quint64> m_healGen{0};

    // The histogram consumes the full-res composite, so it too is computed off
    // the UI thread; the latest request wins.
    QFutureWatcher<HistogramData> m_histWatcher;
    std::atomic<quint64> m_histGen{0};
};
