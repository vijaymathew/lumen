#pragma once

#include <atomic>

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QPixmap>
#include <QPointF>
#include <QString>

#include "core/EditGraph.h"
#include "core/Image.h"
#include "core/RawLoader.h"
#include "core/StructureNode.h"
#include "core/Vignette.h"

class TuneNode;
class CurvesNode;
class LutNode;
class MonoNode;
class ColorMixerNode;
class ColorGradeNode;
class HealNode;
class LensCorrectionNode;
class DenoiseNode;
class DefringeNode;
class SharpenNode;
class StructureNode;
class GrainNode;

// Document owns all state for one open image: its edit graph and cached node
// pointers, source pixels + bytes, project/save + autosave bookkeeping, RAW
// decode options, the brush/mask paint session, preset state, and per-image
// view/peek state. MainWindow owns the shell (canvas, panels, overlays, the
// InputController) and binds exactly one active Document into it at a time.
//
// Extracting this state is the groundwork for keeping several images open at
// once in tabs (see the tabs plan). Today MainWindow holds exactly one Document.
// State migrates here in stages so the move can proceed cluster by cluster
// without ever breaking the build; this class starts empty on purpose.
class Document {
public:
    Document();
    ~Document();

    // Stable identity for the document's whole lifetime, unique across all
    // documents in the session. Async jobs are tagged with this so a result can
    // be routed back to the document that started it even after the user has
    // switched tabs, and dropped if that document has since been closed. Never 0
    // (0 means "no document").
    const quint64 id;

    // Per-document "latest wins" generation counters for the async preview
    // pipelines. A new request bumps the counter; in-flight workers read it and
    // bail when superseded. Per-document (not global) so a bake on one tab never
    // cancels a bake on another. Atomic: bumped on the UI thread, read on the
    // worker. INVARIANT: a Document must not be destroyed while it still has
    // in-flight work — its watchers are drained first (see MainWindow teardown /
    // tab close), so a worker's read of these is always valid.
    std::atomic<quint64> healGen{0};
    std::atomic<quint64> histGen{0};
    std::atomic<quint64> decodeGen{0};
    std::atomic<quint64> lensGen{0};

    // Non-copyable: a Document owns heavy, unique per-image state (the edit
    // graph, decoded pixels). Move-only would come later if tabs need it.
    Document(const Document &) = delete;
    Document &operator=(const Document &) = delete;

    // Builds the Base layer's fixed node chain (lens -> heal -> denoise ->
    // defringe -> sharpen -> structure -> tune -> colorMixer -> curves ->
    // colorGrade -> lut -> mono -> grain), caches the raw node pointers, and
    // snapshots the pristine graph into defaultGraphState. Call once, right after
    // construction, before installing any source. Every document (each tab) needs
    // its own built graph; restoreState/loadProjectState then restore the Base
    // nodes in place, keeping these pointers valid.
    void buildBaseGraph();

    // --- Population (pure state; no shell/UI) -----------------------------
    // Populate this document for a freshly opened image: reset the graph to its
    // pristine state, install the source, seed the lens node from EXIF (RAW) and
    // the caller's RAW lens defaults, and install the camera colour profile
    // (seeding the WB slider to as-shot for a RAW). Callers reflect the result
    // into the shell via MainWindow::bindDocument().
    void initFromImage(const Image &source, const QByteArray &bytes,
                       const QString &imagePath, bool isRaw,
                       const raw::LensMetadata &meta,
                       const raw::RawLensDefaults &lensDefaults);

    // Populate this document from a loaded .lumen project: adopt its decode
    // options, install the embedded source, restore the saved edit graph (which
    // carries the lens params), and refresh the camera profile from the decode
    // while keeping the restored WB temperature.
    void initFromProject(const Image &source, const QByteArray &bytes,
                         const QString &sourceFileName,
                         const QString &projectFilePath,
                         const QJsonObject &graphState,
                         const raw::RawDecodeOptions &opts, bool isRaw,
                         const raw::LensMetadata &meta);

    // Installs the RAW camera colour profile on every layer's TuneNode so white
    // balance is camera-accurate. `seedKelvin` moves the slider to the as-shot
    // temperature (opening a fresh RAW) vs keeping the restored value (projects).
    void applyCameraProfile(const raw::ColorProfile &profile, bool seedKelvin);

    // --- Edit graph -------------------------------------------------------
    // The non-destructive edit graph. The GPU preview reads the tune node's
    // exposure live; Export walks the graph at full resolution via libvips.
    EditGraph graph;
    // Snapshot of the graph in its pristine state (Base layer, neutral nodes,
    // no selective layers), captured once after construction. Restored when a
    // new image is opened so the previous image's edits don't carry over.
    QJsonObject defaultGraphState;

    // Cached raw pointers to the Base-layer nodes (owned by `graph`). The bake
    // order runs lens -> heal -> denoise -> defringe -> sharpen -> structure
    // (baked in libvips), then the pointwise ops the shader replicates:
    // tune -> colorMixer -> curves -> colorGrade -> lut -> mono -> grain.
    TuneNode *tune = nullptr;
    CurvesNode *curves = nullptr;
    LutNode *lut = nullptr;
    MonoNode *mono = nullptr;
    ColorMixerNode *colorMixer = nullptr;
    ColorGradeNode *colorGrade = nullptr;
    HealNode *heal = nullptr;
    LensCorrectionNode *lens = nullptr;
    DenoiseNode *denoise = nullptr;
    DefringeNode *defringe = nullptr;
    SharpenNode *sharpen = nullptr;
    StructureNode *structure = nullptr;
    GrainNode *grain = nullptr;

    // --- Source pixels & bytes -------------------------------------------
    Image workingSource;         // cached lens-corrected source (preview base input)
    // sourceGeneration workingSource was last built from; lets refreshWorkingSource
    // skip the (full-res, per-pixel) lens re-warp when neither the pixels nor the
    // lens node's own params/enabled state (tracked by its dirty flag) have moved
    // since — see MainWindow::refreshWorkingSource.
    quint64 workingSourceGeneration = 0;
    QImage sourceQImage;         // for colour sampling + preview mask
    QImage originalQImage;       // decoded source, no edits (Before/After); lazy
    bool compareOriginal = false; // Before/After: show the un-edited original
    QByteArray sourceBytes;      // original encoded source, for embedding in .lumen
    QString sourceName;          // original source file name
    QString sourcePath;          // for a sensible default export name
    raw::LensMetadata meta;      // EXIF camera/lens + capture settings ("Image info")

    // --- Project & export ------------------------------------------------
    QString projectPath;         // current .lumen path (empty until saved/opened)
    QString exportExt = QStringLiteral("jpg"); // remembered export format
    int exportQuality = 90;                    // remembered export quality
    int exportLongEdge = 0;                    // remembered long-edge cap (0 = full)
    Image::ColorSpace exportColorSpace =       // remembered output colour space
        Image::ColorSpace::SRGB;

    // --- Per-image view flags (preview-only) -----------------------------
    int maskView = 0;            // selective mask overlay
    bool showClipping = false;   // on-canvas clipping warnings ("blinkies")
    bool overlaysHidden = false; // user hid the on-canvas gizmo geometry

    // Canvas zoom/pan for this tab, snapshotted on switch-away and restored on
    // switch-back (mirrors CanvasWidget::ViewState; kept as primitives so core
    // doesn't depend on the GPU layer). viewValid stays false until the tab has
    // been shown once, so a never-shown tab fits-to-window instead of restoring
    // a meaningless default.
    float viewZoom = 1.0f;
    QPointF viewPan{0.0, 0.0};
    bool viewValid = false;

    // --- RAW decode options ----------------------------------------------
    // Decode-time (baked, stored per-project in the .lumen). Seeded from the
    // global preference on construction; each opened RAW carries its own copy.
    raw::RawDecodeOptions rawOptions;

    // --- Autosave & crash-recovery bookkeeping ---------------------------
    // While projectPath is empty, autosave writes recoveryPath in ~/.lumen/
    // projects; once saved/opened it targets the user file. The two snapshots
    // drive dirty detection without per-edit hooks.
    QString recoveryPath;        // this session's recovery file (lazy; empty = none)
    QByteArray openDoc;          // doc as opened/loaded (pristine baseline)
    QByteArray lastAutosaveDoc;  // doc as last persisted (skip redundant writes)
    bool autosaveInFlight = false; // single-flight guard
    QByteArray pendingAutosaveDoc; // doc snapshot the in-flight write carries

    // --- Peek ("show up to here") ----------------------------------------
    // A transient, non-committed view backed by a graph snapshot.
    bool peeking = false;
    QJsonObject peekSnapshot;
    int viewCeiling = -1;        // index of the peeked-to adjustment, or -1 for full

    // --- Preset state ----------------------------------------------------
    int presetAmount = 100;      // Presets browser Amount slider [0,100]
    QString activePresetId;      // id of the preset on the current preset layer, if any
    // Vignette/structure aren't layer nodes, so Amount can't blend them via layer
    // opacity. We snapshot the baseline (pre-preset) and the preset's own values
    // and interpolate by Amount — at 0% the original is restored, at 100% the
    // preset is fully in.
    VignetteParams presetBaselineVignette;
    VignetteParams presetVignette;
    StructureNode::Values presetBaselineStructure;
    StructureNode::Values presetStructure;
    // Thumbnail cache: preset thumbnails depend only on source + Base edits + crop
    // (the preset layer is suppressed while rendering), so they're cached keyed by
    // that signature and re-rendered only when it changes. sourceGeneration bumps
    // on a new source; user-preset edits clear the cache explicitly.
    QByteArray thumbCacheSig;
    QHash<QString, QPixmap> thumbCache;
    quint64 sourceGeneration = 0;
};
