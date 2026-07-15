#include "core/Document.h"

#include <QFileInfo>

#include "core/ColorGradeNode.h"
#include "core/ColorMixerNode.h"
#include "core/CurvesNode.h"
#include "core/DefringeNode.h"
#include "core/DenoiseNode.h"
#include "core/GrainNode.h"
#include "core/HealNode.h"
#include "core/LensCorrectionNode.h"
#include "core/LutNode.h"
#include "core/MonoNode.h"
#include "core/SharpenNode.h"
#include "core/StructureNode.h"
#include "core/TuneNode.h"

namespace {
// Hands out unique, monotonically increasing document ids. Starts at 1 so 0 can
// mean "no document". Atomic: documents may be created from any thread in
// principle, and it's cheap insurance.
std::atomic<quint64> g_nextDocId{1};
} // namespace

Document::Document() : id(g_nextDocId.fetch_add(1, std::memory_order_relaxed)) {}
Document::~Document() = default;

void Document::buildBaseGraph()
{
    // The graph owns the nodes; we keep raw pointers to drive them. The "baked"
    // group runs first in libvips and is rendered into the preview base texture:
    // lens (warps geometry) -> heal (edits pixels) -> denoise -> defringe ->
    // sharpen -> structure (neighbourhood ops; denoise before sharpen so noise
    // isn't amplified, structure after sharpen so local contrast builds on the
    // detail). Then the pointwise/LUT ops the shader replicates: tune ->
    // colormixer -> curves -> colorgrade -> lut -> mono. Selective adjustments are
    // masked layers above the Base.
    lens =
        static_cast<LensCorrectionNode *>(graph.addNode(std::make_unique<LensCorrectionNode>()));
    heal = static_cast<HealNode *>(graph.addNode(std::make_unique<HealNode>()));
    denoise = static_cast<DenoiseNode *>(graph.addNode(std::make_unique<DenoiseNode>()));
    defringe = static_cast<DefringeNode *>(graph.addNode(std::make_unique<DefringeNode>()));
    sharpen = static_cast<SharpenNode *>(graph.addNode(std::make_unique<SharpenNode>()));
    structure =
        static_cast<StructureNode *>(graph.addNode(std::make_unique<StructureNode>()));
    tune = static_cast<TuneNode *>(graph.addNode(std::make_unique<TuneNode>()));
    colorMixer =
        static_cast<ColorMixerNode *>(graph.addNode(std::make_unique<ColorMixerNode>()));
    curves = static_cast<CurvesNode *>(graph.addNode(std::make_unique<CurvesNode>()));
    colorGrade =
        static_cast<ColorGradeNode *>(graph.addNode(std::make_unique<ColorGradeNode>()));
    lut = static_cast<LutNode *>(graph.addNode(std::make_unique<LutNode>()));
    mono = static_cast<MonoNode *>(graph.addNode(std::make_unique<MonoNode>()));
    // Film grain is the final finishing step (after mono), applied over the whole
    // image. A pointwise-in-shader op, so it bakes nothing.
    grain = static_cast<GrainNode *>(graph.addNode(std::make_unique<GrainNode>()));

    // Remember the pristine graph (neutral Base nodes, no selective layers) so
    // opening an image can reset to it.
    defaultGraphState = graph.saveState();
}

void Document::applyCameraProfile(const raw::ColorProfile &profile, bool seedKelvin)
{
    if (!profile.valid)
        return;
    for (int i = 0; i < graph.layerCount(); ++i) {
        if (auto *t = static_cast<TuneNode *>(
                graph.layer(i).nodeOfType(QStringLiteral("tune"))))
            t->setCameraProfile(profile.camToRgb, profile.xyzToCam, profile.asShotMul,
                                seedKelvin);
    }
}

void Document::initFromImage(const Image &source, const QByteArray &bytes,
                             const QString &imagePath, bool isRaw,
                             const raw::LensMetadata &meta,
                             const raw::RawLensDefaults &lensDefaults)
{
    // Keep the original encoded bytes so we can embed them verbatim in a .lumen.
    sourceBytes = bytes;
    sourceName = QFileInfo(imagePath).fileName();
    projectPath.clear(); // opening a raw image starts a new (unsaved) project

    // A freshly opened image starts from a clean slate: reset the graph to its
    // pristine state so the previous image's adjustments (and any selective
    // layers) don't carry over.
    maskView = 0;
    graph.restoreState(defaultGraphState);

    graph.setSource(source); // full-res original; the lens node corrects on top
    ++sourceGeneration;      // new pixels → preset thumbnails must re-render

    // Seed the lens node: a RAW carries EXIF identity for automatic correction;
    // anything else starts neutral (manual perspective still available).
    LensCorrectionNode::Params lp; // defaults: corrections on, perspective neutral
    if (isRaw) {
        lp.cameraMaker = meta.cameraMaker;
        lp.cameraModel = meta.cameraModel;
        lp.lensModel = meta.lensModel;
        lp.focalLength = meta.focalLength;
        lp.aperture = meta.aperture;
        lp.focusDistance = meta.focusDistance;
        // Seed the automatic lens corrections from the user's RAW defaults.
        lp.distortion = lensDefaults.distortion;
        lp.tca = lensDefaults.tca;
        lp.vignetting = lensDefaults.vignetting;
    }
    lens->setParams(lp);
    // Camera-accurate white balance: install the colour profile and seed the
    // slider at the as-shot temperature (non-RAW keeps the sRGB defaults).
    if (isRaw)
        applyCameraProfile(meta.color, /*seedKelvin=*/true);

    sourcePath = imagePath;
    recoveryPath.clear(); // new unsaved document → no recovery file yet
}

void Document::initFromProject(const Image &source, const QByteArray &bytes,
                               const QString &sourceFileName,
                               const QString &projectFilePath,
                               const QJsonObject &graphState,
                               const raw::RawDecodeOptions &opts, bool isRaw,
                               const raw::ColorProfile &color)
{
    rawOptions = opts; // adopt the project's decode options

    maskView = 0;
    sourceBytes = bytes;
    sourceName = sourceFileName;
    graph.setSource(source);
    ++sourceGeneration;              // new pixels → preset thumbnails must re-render
    graph.loadProjectState(graphState); // restores the lens node's params too
    // Refresh the camera profile from the actual decode, keeping the restored
    // WB temperature (don't reseed to as-shot).
    if (isRaw)
        applyCameraProfile(color, /*seedKelvin=*/false);

    projectPath = projectFilePath;
    sourcePath = projectFilePath; // export defaults to "<project>-edited.<ext>"
    recoveryPath.clear();         // autosave now targets the user file
}
