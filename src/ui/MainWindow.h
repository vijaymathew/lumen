#pragma once

#include <QByteArray>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QMainWindow>
#include <QPixmap>
#include <QPointF>

#include <atomic>

#include "core/ColorGradeNode.h"
#include "core/CurvesNode.h"
#include "core/EditGraph.h"
#include "core/HealNode.h"
#include "core/LensCorrectionNode.h"
#include "core/LutNode.h"
#include "core/MaskSpec.h"
#include "core/DefringeNode.h"
#include "core/DenoiseNode.h"
#include "core/GrainNode.h"
#include "core/Histogram.h"
#include "core/MonoNode.h"
#include "core/RawLoader.h"
#include "core/SelectiveMask.h"
#include "core/SharpenNode.h"
#include "core/StructureNode.h"
#include "core/TuneNode.h"
#include "input/InputController.h"

#include <functional>
#include <vector>

class CanvasWidget;
class CommandPalette;
class CurvesPanel;
class DenoisePanel;
class DefringePanel;
class RawSettingsPanel;
class HealPanel;
class HistogramWidget;
class LayersPanel;
class AdjustmentsPanel;
class ColorGradePanel;
class LensPanel;
class MaskGizmo;
class ZoneGizmo;
class CropGizmo;
class CropPanel;
class LooksPanel;
class PresetsPanel;
namespace preset { struct Builtin; }
class MonoPanel;
class ColorMixerNode;
class ColorMixerPanel;
class GrainPanel;
class VignettePanel;
class SharpenPanel;
class StructurePanel;
class TonePanel;
class QLabel;
class QPushButton;
class QTimer;

// MainWindow is the immersive shell: a fullscreen canvas with a "/"-triggered
// command palette floating over it and a dismissible hint bar. It owns the
// InputController and routes commands to actions. Milestone 1 wires the
// navigation/shell commands; editing tools arrive in later milestones.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Loads an image at startup (e.g. a path passed on the command line).
    bool openPath(const QString &path);

    // Startup crash recovery: if ~/.lumen/projects holds an autosaved document
    // from a session that didn't shut down cleanly, offer to restore the newest.
    // Returns true if work was restored. Skipped when an image is opened from the
    // command line (explicit intent wins). See main().
    bool offerCrashRecovery();

protected:
    void resizeEvent(QResizeEvent *e) override;
    // Guards unsaved work: prompts (or silently flushes a saved document) before
    // the window closes.
    void closeEvent(QCloseEvent *e) override;
    // Central key handling: catches keys via propagation no matter which child
    // widget has focus, so the active tool can always be closed.
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    // Drag handling for the persistent overlays (histogram via its surface, the
    // view-toggle cluster via its grip). Once dragged, layoutOverlays() stops
    // auto-pinning that overlay and only clamps it back into view.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildCommands();
    void runCommand(const QString &id);
    void openImageDialog();
    void saveProject();   // save to the current file; prompts only on first save
    void saveProjectAs(); // always prompt for a new .lumen destination
    void openProject();   // pick a .lumen file via dialog, then load it
    void loadProjectFile(const QString &path); // load a .lumen async (source + layers)

    // Save helpers. promptSaveProjectPath() runs the Save dialog; writeProjectAsync()
    // snapshots the document and writes it off the UI thread; saveProjectSync() is
    // the blocking save the quit/discard flow needs (it must finish before the
    // document can be discarded); applySaveSuccess() updates state after a write.
    QString promptSaveProjectPath();
    void writeProjectAsync(const QString &path);
    bool saveProjectSync();
    void applySaveSuccess(const QString &path);

    // Reusable edit presets / copy-paste settings. copy/paste move the current
    // look through an in-memory clipboard; save/apply persist it as a .lumenpreset
    // file. All four share applyPreset(), which overwrites the creative edit and
    // refreshes the UI (see Preset.h for what a preset does and does not carry).
    void copySettings();
    void pasteSettings();
    void savePreset();
    void applyPresetFile();
    void applyPreset(const QJsonObject &presetObj, const QString &doneHint);

    // --- Autosave & crash recovery -----------------------------------------
    // The current document serialised the way a project is saved: the edit graph
    // plus the per-project RAW decode options. Used for both writing and for
    // dirty detection (compare against the open/last-saved baselines).
    QJsonObject buildDocGraph() const;
    QByteArray currentDocBytes() const;
    // The source bytes to embed (original encoded file, or a PNG of the source as
    // a fallback); *name receives the matching file name.
    QByteArray sourceForSave(QString *name) const;
    void startAutosave();        // (re)start the autosave timer for a document
    void performAutosave();      // timer slot: write if the document changed
    bool flushAutosaveSync();    // synchronous write to the current target (on close)
    void deleteRecoveryFile();   // remove this session's recovery file, if any
    // Returns false only if the user cancels; otherwise the current document may
    // be safely discarded (saved, flushed, or the user chose to discard).
    bool maybeSaveBeforeDiscard();
    // Loads a recovery file as unsaved work (keeps autosaving to it, prompts on
    // close). Unlike loadProjectFile, it does not adopt the path as the user file.
    bool restoreRecovery(const QString &path);
    // Resets the dirty baselines to the current document (called after open/save).
    void resetAutosaveBaseline();
    void toggleFullScreen();
    // Overlays a small "✕" close button on a floating panel's top-right corner
    // and routes it to `onClose` — a pointer counterpart to the Esc/Enter close.
    void addPanelCloseButton(QWidget *panel, std::function<void()> onClose);
    void showHint(const QString &text);
    // The persistent which-key legend for the current input mode. Shown in the
    // hint bar whenever the mode changes; transient showHint() messages override
    // it until the next mode change.
    QString modeHintText() const;
    void updateModeHint();
    void layoutOverlays();

    void openCommandPalette();
    void openLayersTool();    // toggles the Layers panel
    void showLayersPanel();   // ensures it is visible (used by the selective cmd)
    void hideLayersPanel();
    void refreshLayersPanel();
    // Re-syncs any visible adjustment panel (tone/curves/looks/mono) with the
    // active layer's current node values. Read-only: it never adds nodes.
    void reseedOpenPanels();
    void addAdjustmentLayer();
    void deleteActiveLayer();
    void selectLayer(int index);
    // Mask editing for the active layer (Layers-panel controls + on-canvas gizmo).
    void setActiveLayerMaskType(int maskType);
    void onLayerMaskEdited(const MaskSpec &spec, bool commit);
    void syncMaskGizmo();      // reflect the active layer's mask into the gizmo
    void syncZoneGizmo();      // reflect the active layer's zone shapes into the gizmo
    // Show/hide the on-canvas overlay geometry (zone shapes + mask gizmo); a
    // transient view preference. Hidden is view-only (the mask still renders).
    void setOverlayGeometryVisible(bool visible);
    void updateMaskEditing();  // enable/disable the canvas brush for a Brush mask
    void endMaskBrushSession(); // commit in-progress mask-brush strokes
    // Installs the RAW camera colour profile on every layer's TuneNode so white
    // balance is camera-accurate. `seedKelvin` moves the slider to the as-shot
    // temperature (opening a fresh RAW) vs keeping the restored value (projects).
    void applyCameraProfile(const raw::ColorProfile &profile, bool seedKelvin);

    // The active layer's tone/curves/look/mono nodes (tools edit the active layer).
    TuneNode *activeTune() const;
    CurvesNode *activeCurves() const;
    LutNode *activeLut() const;
    MonoNode *activeMono() const;
    ColorGradeNode *activeColorGrade() const;
    ColorMixerNode *activeColorMixer() const;
    void openToneTool();
    void closeToneTool();
    void openCurvesTool();
    void closeCurvesTool();
    void openLooksTool();
    void closeLooksTool();
    void loadLookFile();
    void openPresetsTool();  // shows the Presets browser (built-in looks)
    void closePresetsTool();
    // Renders a thumbnail of the current photo with each built-in preset applied.
    void refreshPresetThumbnails();
    // Drops any cached preset thumbnails, forcing a re-render on the next refresh.
    void invalidatePresetThumbCache();
    // Applies a preset as a full-coverage adjustment layer at opacity = amount%,
    // replacing any prior preset layer. The Amount slider blends the whole look.
    void applyPresetLook(const preset::Builtin &b, int amountPct);
    void setPresetAmount(int amountPct); // live-updates the active preset layer's opacity
    Layer &addPresetLayer(const QString &name); // full-coverage adjustment layer
    int presetLayerIndex() const;               // the active preset layer, or -1
    // Blends the graph vignette from the pre-preset baseline toward the active
    // preset's vignette by amount% (the vignette can't ride the layer opacity, so
    // Amount scales it here instead — keeping it part of the "whole look" blend).
    void applyPresetVignette(int amountPct);
    // Same idea for structure (applied to the Base structure node). Returns whether
    // the Base node changed; re-baking is expensive, so callers coalesce the re-bake
    // (the bake timer) rather than kicking it per Amount tick.
    bool applyPresetStructure(int amountPct);
    void openMonoTool();
    void closeMonoTool();
    void openColorGradeTool();
    void closeColorGradeTool();
    void openColorMixerTool();
    void closeColorMixerTool();
    void openLensTool();  // toggles the Lens & Perspective panel
    void closeLensTool();
    void openSharpenTool();  // toggles the Sharpen panel
    void closeSharpenTool();
    void openStructureTool();  // toggles the Structure (local contrast) panel
    void closeStructureTool();
    void openDenoiseTool();  // toggles the Denoise panel
    void closeDenoiseTool();
    void openDefringeTool(); // toggles the Defringe panel
    void closeDefringeTool();
    void openRawTool();      // toggles the RAW Defaults panel
    void closeRawTool();
    // Re-decodes the currently open RAW with m_rawOptions (from its kept source
    // bytes) and rebuilds the pipeline, preserving edits and view. No-op for a
    // non-RAW source.
    void redecodeCurrent();
    void openGrainTool();    // toggles the Film Grain panel
    void closeGrainTool();
    void openVignetteTool(); // toggles the Vignette panel
    void closeVignetteTool();
    void openCropTool();     // toggles the Crop & Rotate panel
    void closeCropTool();
    // Pushes the graph's crop to the canvas with the right view mode for the
    // current context (Editing while the crop tool is open; full frame while a
    // gizmo/pick tool needs it; else the cropped browse view).
    void updateCropView();
    // Sets the window title. Empty document → "Lumen <version>" (launch/idle);
    // otherwise "Lumen — <document>". One place so the format never drifts.
    void updateTitle(const QString &document = QString());
    double sourceAspect() const; // original (un-oriented) source width/height
    // Largest centred crop rect clear of the straighten tilt's transparent
    // corners, for `c`'s orientation and the active aspect (m_cropAspect).
    // Returns the full frame when `c` is not straightened.
    QRectF straightenSafeCropRect(const CropState &c) const;
    void toggleHistogram();  // show/hide the histogram overlay
    void updateHistogram();  // recompute from the current result (when visible)
    void toggleClipping();   // show/hide on-canvas clipping warnings ("blinkies")
    // Reflects the current histogram / clipping / history state into the
    // bottom-right view-toggle cluster (keeps the buttons in sync with the
    // keyboard + palette paths that can flip the same state).
    void syncViewToggles();
    // Recomputes the cached lens-corrected working source (and its display
    // QImage) from the original; cheap no-op when no correction is active. Called
    // when the lens parameters or the source image change — NOT per heal dab.
    void refreshWorkingSource();
    // Rebuilds every preview stage from the current graph state (working source →
    // base bake → selective mask → crop view → preview uniforms). The canonical
    // "reflect the whole graph" path, used after a load and after Adjustments-panel
    // toggles/deletes/peeks.
    void rebuildPreviewFromGraph();
    // Before/After: toggle showing the un-edited original vs the edited image.
    void setCompareOriginal(bool on);
    const QImage &originalImage(); // lazily decoded source for Before/After

    // --- Adjustments panel -------------------------------------------------
    // One applied edit, exposed to the Adjustments panel. `order` is its position
    // in the global pipeline (used by the "show up to here" peek).
    struct Adjustment {
        QString name;
        int order = 0;
        std::function<bool()> isEnabled;
        std::function<void(bool)> setEnabled;
        std::function<void()> reset; // delete: remove this edit's effect
    };
    void openAdjustmentsTool();  // toggles the Adjustments panel
    void closeAdjustmentsTool();
    void rebuildAdjustments();   // re-scan active edits and repaint the panel
    bool nodeIsActive(const EditNode *node) const; // params differ from neutral
    void onAdjustmentToggle(int index, bool on);
    void onAdjustmentDelete(int index);
    void peekUpTo(int index);    // show the image up to the index-th adjustment
    void exitPeek();             // leave the peek view, restore the real graph
    void recomputeSelectiveMask(); // uploads the active layer's mask as the overlay
    // The baked passes (heal/denoise/defringe/sharpen) all re-run together, so the
    // busy badge labels by which op the user actually triggered. A handler sets
    // m_bakeOp before kicking the bake; refreshBaseImage consumes it for the
    // label and falls back to precedence when it's Auto (e.g. a load/lens refresh).
    enum class BakeOp { Auto, Heal, Denoise, Defringe, Sharpen, Structure };
    BakeOp m_bakeOp = BakeOp::Auto;
    // Canvas colour-pick has two purposes: choosing a colour-mask target, or the
    // white-balance eyedropper. `m_pickPurpose` routes the picked point.
    enum class PickPurpose { MaskColour, WhiteBalance };
    PickPurpose m_pickPurpose = PickPurpose::MaskColour;
    void onColorPicked(const QPointF &imageNormalized);
    // A selective adjustment is a masked layer (mask = Luminosity/Colour/Brush +
    // the layer's TuneNode), edited via the Layers panel. ensureSelectiveLayer
    // adds/selects a layer to drive and returns its index.
    int ensureSelectiveLayer();
    // Appends a new adjustment layer with the full node set and a default
    // full-range Luminosity mask (so the panel's mask editor — Shadows/Midtones/
    // Highlights presets included — is available immediately). Shared by the
    // Layers-panel Add button and the Selective command.
    Layer &addMaskedAdjustmentLayer(const QString &name);
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
    QWidget *m_healBusy = nullptr;  // animated badge during async base re-bake (heal/denoise/sharpen)
    CommandPalette *m_palette = nullptr;
    TonePanel *m_tonePanel = nullptr;
    CurvesPanel *m_curvesPanel = nullptr;
    LooksPanel *m_looksPanel = nullptr;
    PresetsPanel *m_presetsPanel = nullptr;
    int m_presetAmount = 100;   // Presets browser Amount slider [0,100]
    QString m_activePresetId;   // id of the preset on the current preset layer, if any
    // Vignette isn't a layer node, so Amount can't blend it via layer opacity.
    // We snapshot the graph vignette that existed before a preset was applied
    // (the "baseline") and the active preset's own vignette, then interpolate
    // between them by Amount — so at 0% the original vignette is restored and at
    // 100% the preset's is fully in.
    VignetteParams m_presetBaselineVignette;
    VignetteParams m_presetVignette;
    // Structure (local contrast) is a baked Base op — it can't ride the preset
    // layer's opacity either, so it gets the same baseline/preset snapshot + Amount
    // blend as the vignette (applied to the Base structure node, re-baked).
    StructureNode::Values m_presetBaselineStructure;
    StructureNode::Values m_presetStructure;
    // Thumbnail cache: rendering every preset over the current photo is O(library)
    // and runs on open/apply. Thumbnails only depend on the source + Base edits +
    // crop (the preset layer is suppressed during the render), so we cache the
    // pixmaps keyed by that signature and re-render only when it changes. Bumped on
    // a new source; user-preset edits clear the cache explicitly.
    QByteArray m_thumbCacheSig;
    QHash<QString, QPixmap> m_thumbCache;
    quint64 m_sourceGeneration = 0;
    MonoPanel *m_monoPanel = nullptr;
    ColorMixerPanel *m_colorMixerPanel = nullptr;
    ColorGradePanel *m_colorGradePanel = nullptr;
    LensPanel *m_lensPanel = nullptr;
    SharpenPanel *m_sharpenPanel = nullptr;
    StructurePanel *m_structurePanel = nullptr;
    GrainPanel *m_grainPanel = nullptr;
    VignettePanel *m_vignettePanel = nullptr;
    CropPanel *m_cropPanel = nullptr;
    DenoisePanel *m_denoisePanel = nullptr;
    DefringePanel *m_defringePanel = nullptr;
    RawSettingsPanel *m_rawPanel = nullptr;
    HealPanel *m_healPanel = nullptr;
    HistogramWidget *m_histogram = nullptr;
    QTimer *m_histTimer = nullptr; // debounces histogram recompute
    QTimer *m_bakeTimer = nullptr; // debounces sharpen base re-bake
    LayersPanel *m_layersPanel = nullptr;
    MaskGizmo *m_maskGizmo = nullptr; // on-canvas gradient/radial mask editor
    ZoneGizmo *m_zoneGizmo = nullptr; // on-canvas exclusive-zone shape editor
    CropGizmo *m_cropGizmo = nullptr; // on-canvas crop rectangle editor
    double m_cropAspect = 0.0;        // active crop aspect (w/h; 0 = free), for straighten inset
    QLabel *m_hint = nullptr;
    // Bottom-right cluster of glanceable view toggles (mirror the G/J/A keys).
    QWidget *m_viewToggles = nullptr;
    QLabel *m_clusterGrip = nullptr; // drag handle for the cluster
    QPushButton *m_histToggleBtn = nullptr;
    QPushButton *m_clipToggleBtn = nullptr;
    QPushButton *m_historyToggleBtn = nullptr;
    // Overlay-drag state. The *Moved flags latch once the user repositions an
    // overlay so layoutOverlays() leaves it where they put it (clamped to view).
    QWidget *m_draggingOverlay = nullptr;
    QPoint m_overlayDragStartGlobal;
    QPoint m_overlayStartPos;
    bool m_histMoved = false;
    bool m_clusterMoved = false;
    // Per-panel "✕" close buttons, repositioned to the top-right on panel resize.
    QHash<QWidget *, QPushButton *> m_panelClose;

    // The non-destructive edit graph. The GPU preview reads the tune node's
    // exposure live; Export walks the graph at full resolution via libvips.
    EditGraph m_graph;
    // Snapshot of the graph in its pristine state (Base layer, neutral nodes,
    // no selective layers), captured once after construction. Restored when a
    // new image is opened so the previous image's edits don't carry over.
    QJsonObject m_defaultGraphState;
    TuneNode *m_tune = nullptr;          // owned by m_graph
    CurvesNode *m_curves = nullptr;      // owned by m_graph
    LutNode *m_lutNode = nullptr;        // owned by m_graph
    MonoNode *m_mono = nullptr;          // owned by m_graph
    ColorMixerNode *m_colorMixer = nullptr; // owned by m_graph (after tune, before curves)
    ColorGradeNode *m_colorGrade = nullptr; // owned by m_graph
    HealNode *m_heal = nullptr;          // owned by m_graph (second in the chain)
    LensCorrectionNode *m_lens = nullptr; // owned by m_graph (first in the chain)
    DenoiseNode *m_denoise = nullptr;     // owned by m_graph (after heal, before defringe)
    DefringeNode *m_defringe = nullptr;   // owned by m_graph (after denoise, before sharpen)
    SharpenNode *m_sharpen = nullptr;     // owned by m_graph (after defringe, before structure)
    StructureNode *m_structure = nullptr; // owned by m_graph (local contrast, after sharpen)
    GrainNode *m_grain = nullptr;         // owned by m_graph (final Base node, after mono)
    Image m_workingSource;               // cached lens-corrected source (preview base input)
    QString m_sourcePath;                // for a sensible default export name
    QString m_exportExt = QStringLiteral("jpg"); // remembered export format
    int m_exportQuality = 90;                    // remembered export quality
    int m_exportLongEdge = 0;                    // remembered long-edge cap (0 = full)
    Image::ColorSpace m_exportColorSpace =       // remembered output colour space
        Image::ColorSpace::SRGB;
    QImage m_sourceQImage;               // for colour sampling + preview mask
    QImage m_originalQImage;             // decoded source, no edits (Before/After); lazy
    bool m_compareOriginal = false;      // Before/After: show the un-edited original
    QByteArray m_sourceBytes;            // original encoded source, for embedding in .lumen
    QString m_sourceName;                // original source file name
    QString m_projectPath;               // current .lumen path (empty until saved/opened)
    QJsonObject m_copiedSettings;        // in-memory clipboard for Copy/Paste Settings
    int m_maskView = 0;                  // selective mask overlay (preview-only)
    bool m_showClipping = false;         // on-canvas clipping warnings (preview-only)
    bool m_overlaysHidden = false;       // user hid the on-canvas gizmo geometry

    // Autosave & crash recovery. While m_projectPath is empty, autosave writes
    // m_recoveryPath in ~/.lumen/projects; once saved/opened it targets the user
    // file. The two doc snapshots drive dirty detection without per-edit hooks.
    // Adjustments panel state. m_adjustments is rebuilt each refresh and is
    // parallel to the panel's rows (signals carry the row index). Peek is a
    // transient, non-committed "show up to here" view backed by a graph snapshot.
    AdjustmentsPanel *m_adjustmentsPanel = nullptr;
    std::vector<Adjustment> m_adjustments;
    bool m_peeking = false;
    QJsonObject m_peekSnapshot;
    int m_viewCeiling = -1; // index of the peeked-to adjustment, or -1 for full

    QTimer *m_autosaveTimer = nullptr;
    QString m_recoveryPath;              // this session's recovery file (lazy; empty = none)
    QByteArray m_openDoc;                // doc as opened/loaded (pristine baseline)
    QByteArray m_lastAutosaveDoc;        // doc as last persisted (skip redundant writes)
    QFutureWatcher<bool> m_autosaveWatcher; // off-thread write completion
    bool m_autosaveInFlight = false;     // single-flight guard
    QByteArray m_pendingAutosaveDoc;     // doc snapshot the in-flight write carries

    // Automatic-RAW configuration. m_rawOptions are decode-time (baked, stored
    // per-project in the .lumen); m_rawLensDefaults seed the lens node on open.
    // Both default to the global preference (loadRawDefaults) at construction.
    raw::RawDecodeOptions m_rawOptions;
    raw::RawLensDefaults m_rawLensDefaults;
    QTimer *m_redecodeTimer = nullptr;   // debounces re-decode on RAW option drags

    // Shared brush-paint session (used by the selective brush and the heal
    // brush, one at a time).
    enum class BrushTarget { None, Selective, Heal };
    BrushTarget m_brushTarget = BrushTarget::None;
    MaskBuffer m_brushMask;
    MaskBuffer m_strokeMask;                     // current stroke's footprint (heal overlay)
    std::vector<std::vector<float>> m_brushUndo; // per-stroke snapshots
    int m_brushSize = 30;
    int m_brushHardness = 50;
    bool m_brushAdd = true;
    QPointF m_lastBrushPoint;
    bool m_brushHasLast = false;
    bool m_healPainting = false;      // a heal stroke is in progress (red overlay)
    bool m_selectivePainting = false; // a selective-mask stroke is in progress (forces the overlay)
    bool m_adjustHardness = false;    // s/h + wheel target: false=size, true=hardness

    // The heal (inpaint) preview runs off the UI thread so Detailed mode never
    // freezes the app; only the latest request's result is applied.
    QFutureWatcher<QImage> m_healWatcher;
    std::atomic<quint64> m_healGen{0};

    // The histogram consumes the full-res composite, so it too is computed off
    // the UI thread; the latest request wins.
    QFutureWatcher<HistogramData> m_histWatcher;
    std::atomic<quint64> m_histGen{0};

    // RAW re-decode (a full demosaic) also runs off the UI thread so the app
    // stays responsive and the busy badge can animate; the latest request wins.
    struct DecodeResult {
        Image image;
        raw::LensMetadata meta;
        QString error;
    };
    QFutureWatcher<DecodeResult> m_decodeWatcher;
    std::atomic<quint64> m_decodeGen{0};

    // Export runs the full-res graph walk + libvips encode off the UI thread (a
    // heal mask makes result() eager, and encoding a large image is slow either
    // way). Re-entry is blocked while a write is in flight.
    struct ExportResult {
        bool ok = false;
        QString error;
        QString path;
    };
    QFutureWatcher<ExportResult> m_exportWatcher;
    // True from the moment an export is armed until its worker is launched on the
    // next event-loop tick — closes the re-entry window the deferred launch opens.
    bool m_exportPending = false;

    // Save (serialise the document + embed the source bytes, then write) runs off
    // the UI thread so the "Saving…" badge animates; re-entry is blocked while a
    // write is in flight.
    struct SaveResult {
        bool ok = false;
        QString error;
        QString path;
    };
    QFutureWatcher<SaveResult> m_saveWatcher;

    // Opening a file decodes off the UI thread behind the "Opening…" badge (a RAW
    // demosaic is slow); a finish handler installs the result on the UI thread.
    struct OpenImageResult {
        Image source;
        raw::LensMetadata meta;
        QString error;
        QString path;
        QByteArray bytes; // original encoded file, kept to embed in a .lumen
        bool isRaw = false;
    };
    QFutureWatcher<OpenImageResult> m_openImageWatcher;

    struct OpenProjectResult {
        bool loaded = false; // project::load() succeeded
        QString error;
        QString path;
        QJsonObject graph;
        QByteArray sourceBytes;
        QString sourceName;
        bool isRaw = false;
        raw::RawDecodeOptions rawOptions;
        Image source; // decoded embedded image
        raw::LensMetadata meta;
    };
    QFutureWatcher<OpenProjectResult> m_openProjectWatcher;

    // Pure decode steps (no UI state) run on the worker thread; the sync crash-
    // recovery path reuses decodeProjectFile too.
    static OpenImageResult decodeImageFile(const QString &path,
                                           const raw::RawDecodeOptions &opts);
    static OpenProjectResult decodeProjectFile(const QString &path);
    void finishOpenImage(const OpenImageResult &r); // install after async decode
    bool applyProjectResult(const OpenProjectResult &r); // shared project install
    bool openBusy() const; // an open/decode is already in flight
};
