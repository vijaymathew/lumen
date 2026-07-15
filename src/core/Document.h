#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QPixmap>
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

    // Non-copyable: a Document owns heavy, unique per-image state (the edit
    // graph, decoded pixels). Move-only would come later if tabs need it.
    Document(const Document &) = delete;
    Document &operator=(const Document &) = delete;

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
    QImage sourceQImage;         // for colour sampling + preview mask
    QImage originalQImage;       // decoded source, no edits (Before/After); lazy
    bool compareOriginal = false; // Before/After: show the un-edited original
    QByteArray sourceBytes;      // original encoded source, for embedding in .lumen
    QString sourceName;          // original source file name
    QString sourcePath;          // for a sensible default export name

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
