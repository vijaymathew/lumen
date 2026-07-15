#include "ui/MainWindow.h"

#include "core/Autosave.h"
#include "core/Document.h"
#include "core/NodeFactory.h"
#include "core/HealNode.h"
#include "core/Image.h"
#include "core/LayerPreview.h"
#include "core/MaskSpec.h"
#include "core/BuiltinPresets.h"
#include "core/Preset.h"
#include "core/Project.h"
#include "core/RawLoader.h"
#include "core/SelectiveMask.h"
#include "gpu/CanvasWidget.h"
#include "input/CommandPalette.h"
#include "ui/CurvesPanel.h"
#include "ui/ImageOpenDialog.h"
#include "ui/ExportDialog.h"
#include "core/Histogram.h"
#include "ui/DenoisePanel.h"
#include "ui/HealPanel.h"
#include "ui/HistogramWidget.h"
#include "ui/LayersPanel.h"
#include "ui/AdjustmentsPanel.h"
#include "ui/LensPanel.h"
#include "ui/MaskGizmo.h"
#include "ui/ZoneGizmo.h"
#include "ui/CropGizmo.h"
#include "ui/CropPanel.h"
#include "ui/LooksPanel.h"
#include "ui/PresetsPanel.h"
#include "ui/ColorGradePanel.h"
#include "ui/MonoPanel.h"
#include "core/ColorMixerNode.h"
#include "ui/ColorMixerPanel.h"
#include "ui/GrainPanel.h"
#include "ui/VignettePanel.h"
#include "ui/DefringePanel.h"
#include "ui/RawSettingsPanel.h"
#include "ui/SharpenPanel.h"
#include "ui/StructurePanel.h"
#include "ui/TonePanel.h"

#include <memory>
#include <utility>

#include <cstdio>

#include <QBuffer>
#include <QCloseEvent>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>
#include <QLabel>
#include <QMessageBox>
#include <QTabBar>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent>

#include <algorithm>

// Supplied by the build (CMake PROJECT_VERSION). Fallback keeps non-CMake tooling
// compiling and reads as an obvious dev build rather than a real release.
#ifndef LUMEN_VERSION
#define LUMEN_VERSION "0.0.0-dev"
#endif

namespace {

// Name prefix that marks the Presets browser's dedicated full-coverage layer, so
// it can be found/replaced across applies. Shown in the Layers panel too.
const QString kPresetLayerPrefix = QStringLiteral("Preset: ");

// Interpolates a vignette between `base` and `target` by t∈[0,1], so a preset's
// vignette can fade in with the Amount slider. A disabled side contributes no
// darkening (amount 0) and borrows the other side's falloff shape, so fading from
// "no vignette" to a preset's vignette is a smooth deepening rather than a jump.
VignetteParams blendVignette(VignetteParams base, VignetteParams target, float t)
{
    if (!base.enabled) {
        base.amount = 0.0f;
        base.midpoint = target.midpoint;
        base.roundness = target.roundness;
        base.feather = target.feather;
    }
    if (!target.enabled) {
        target.amount = 0.0f;
        target.midpoint = base.midpoint;
        target.roundness = base.roundness;
        target.feather = base.feather;
    }
    const auto mix = [t](float a, float b) { return a + (b - a) * t; };
    VignetteParams r;
    r.amount = mix(base.amount, target.amount);
    r.midpoint = mix(base.midpoint, target.midpoint);
    r.roundness = mix(base.roundness, target.roundness);
    r.feather = mix(base.feather, target.feather);
    r.enabled = (base.enabled || target.enabled) && std::abs(r.amount) > 1e-3f;
    return r;
}

// The structure (local-contrast) values a preset carries, or defaults (disabled)
// if it has none. Structure is a baked Base op — it can't ride a preset layer's
// opacity, so the browser drives the Base node from these directly.
StructureNode::Values structureFromPreset(const QJsonObject &data)
{
    StructureNode s;
    for (const QJsonValue &v : data.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject e = v.toObject();
        if (e.value(QStringLiteral("type")).toString() == QLatin1String("structure")) {
            s.restoreState(e.value(QStringLiteral("state")).toObject());
            break;
        }
    }
    return s.values();
}

// Interpolates structure between `base` and `target` by t∈[0,1], mirroring the
// vignette blend so Amount fades the whole look (structure included) toward the
// original. A disabled side contributes no local contrast (amount 0).
StructureNode::Values blendStructure(StructureNode::Values base, StructureNode::Values target,
                                     float t)
{
    const float a = base.enabled ? base.amount : 0.0f;
    const float b = target.enabled ? target.amount : 0.0f;
    StructureNode::Values r;
    r.amount = a + (b - a) * t;
    r.radius = target.enabled ? target.radius : base.radius;
    r.enabled = (base.enabled || target.enabled) && std::abs(r.amount) > 1e-3f;
    return r;
}

// Snapshot a TuneNode's tonal state for seeding the Tone panel. Centralised so
// the (positional) ToneValues aggregate has one definition to keep in sync.
ToneValues toneValuesOf(const TuneNode *t)
{
    return {t->exposure(),   t->contrast(), t->highlights(), t->shadows(),
            t->whites(),      t->blacks(),   t->saturation(), t->vibrance(),
            t->kelvin(),      t->tint()};
}

// --- File-dialog directory memory -----------------------------------------
// The file dialogs remember where you last were, per kind (open image, open /
// save project, export, look LUT), persisted via QSettings. Returns the stored
// directory if it still exists, else `fallback`.
QString lastDir(const QString &key, const QString &fallback)
{
    const QString d =
        QSettings().value(QStringLiteral("dialogDirs/") + key).toString();
    return (!d.isEmpty() && QFileInfo(d).isDir()) ? d : fallback;
}

// Stores the directory containing `chosenPath` (a file the user just picked).
void rememberDir(const QString &key, const QString &chosenPath)
{
    if (chosenPath.isEmpty())
        return;
    QSettings().setValue(QStringLiteral("dialogDirs/") + key,
                         QFileInfo(chosenPath).absolutePath());
}

// --- RAW decode defaults (global preference) ------------------------------
// The automatic-RAW configuration the user sets in the RAW Defaults panel,
// persisted globally and used to seed every new RAW open. (Per-project options
// are stored separately in the .lumen manifest.)
void loadRawDefaults(raw::RawDecodeOptions &opts, raw::RawLensDefaults &lens)
{
    QSettings s;
    opts.autoBright = s.value(QStringLiteral("raw/autoBright"), opts.autoBright).toBool();
    opts.autoBrightThreshold =
        s.value(QStringLiteral("raw/autoBrightThreshold"), opts.autoBrightThreshold).toFloat();
    opts.highlight = s.value(QStringLiteral("raw/highlight"), opts.highlight).toInt();
    opts.wb = s.value(QStringLiteral("raw/wb"), opts.wb).toInt();
    opts.demosaic = s.value(QStringLiteral("raw/demosaic"), opts.demosaic).toInt();
    lens.distortion = s.value(QStringLiteral("raw/lensDistortion"), lens.distortion).toBool();
    lens.tca = s.value(QStringLiteral("raw/lensTca"), lens.tca).toBool();
    lens.vignetting = s.value(QStringLiteral("raw/lensVignetting"), lens.vignetting).toBool();
}

void saveRawDefaults(const raw::RawDecodeOptions &opts, const raw::RawLensDefaults &lens)
{
    QSettings s;
    s.setValue(QStringLiteral("raw/autoBright"), opts.autoBright);
    s.setValue(QStringLiteral("raw/autoBrightThreshold"), opts.autoBrightThreshold);
    s.setValue(QStringLiteral("raw/highlight"), opts.highlight);
    s.setValue(QStringLiteral("raw/wb"), opts.wb);
    s.setValue(QStringLiteral("raw/demosaic"), opts.demosaic);
    s.setValue(QStringLiteral("raw/lensDistortion"), lens.distortion);
    s.setValue(QStringLiteral("raw/lensTca"), lens.tca);
    s.setValue(QStringLiteral("raw/lensVignetting"), lens.vignetting);
}

// Dim opacity for overlays (0–255). A tuning value, not a fixed constant
// (DESIGN.md §4.6): too dark loses context, too light hurts contrast.
constexpr int kScrimAlpha = 140; // ~0.55

// A translucent layer that dims whatever is behind it. It paints a
// semi-transparent fill without clearing to an opaque background first, so the
// (RHI-composited) canvas shows through, dimmed. The alpha is configurable so
// the same widget serves the heavy palette dim and a lighter "busy" dim (which
// must keep enough of the image visible to judge the effect being applied).
class Scrim : public QWidget {
public:
    explicit Scrim(QWidget *parent, int alpha = kScrimAlpha)
        : QWidget(parent), m_alpha(alpha)
    {
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(10, 10, 11, m_alpha));
    }

private:
    int m_alpha;
};

// How long after the last slider tick we wait before kicking the expensive
// sharpen/denoise base re-bake — long enough to read as "stopped sliding".
constexpr int kHeavyBakeSettleMs = 800;

// A small badge with an animated spinner, shown while a background base re-bake
// (heal / denoise / sharpen) is running. The label names the running op. The
// image underneath is left undimmed so it stays judgeable. Mouse-transparent.
class BusyBadge : public QWidget {
public:
    explicit BusyBadge(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFixedSize(148, 32);
        m_spin.setInterval(70);
        connect(&m_spin, &QTimer::timeout, this, [this] {
            m_angle = (m_angle + 30) % 360;
            m_sweep = (m_sweep + 4) % 100; // indeterminate progress strip
            update();
        });
        // Only show once the op has run longer than this, so quick passes
        // don't flash the badge.
        m_delay.setSingleShot(true);
        m_delay.setInterval(150);
        connect(&m_delay, &QTimer::timeout, this, [this] {
            show();
            raise();
            m_spin.start();
        });
        hide();
    }

    void setLabel(const QString &label)
    {
        if (m_label == label)
            return;
        m_label = label;
        if (isVisible())
            update();
    }

    void start()
    {
        if (isVisible() || m_delay.isActive())
            return;
        m_delay.start();
    }
    void stop()
    {
        m_delay.stop(); // cancel a pending show if the op finished in time
        m_spin.stop();
        hide();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 22, 220));
        p.drawRoundedRect(rect(), 8, 8);

        // Spinner: a dozen fading ticks rotating clockwise.
        const QPointF c(18, height() / 2.0);
        p.translate(c);
        p.rotate(m_angle);
        for (int i = 0; i < 12; ++i) {
            p.setPen(QPen(QColor(255, 255, 255, 40 + i * 16), 2.0, Qt::SolidLine,
                          Qt::RoundCap));
            p.drawLine(QPointF(0, -4.5), QPointF(0, -8.0));
            p.rotate(30);
        }
        p.resetTransform();

        p.setPen(QColor(0xe8, 0xe8, 0xea));
        p.drawText(QRect(34, 0, width() - 40, height() - 3),
                   Qt::AlignLeft | Qt::AlignVCenter, m_label);

        // Indeterminate progress strip along the bottom: a short segment that
        // sweeps left-to-right, reinforcing "working" alongside the spinner.
        const qreal trackX = 8.0, trackW = width() - 16.0, trackY = height() - 3.0;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 30));
        p.drawRoundedRect(QRectF(trackX, trackY, trackW, 2.0), 1.0, 1.0);
        const qreal segW = trackW * 0.3;
        const qreal segX = trackX + (trackW - segW) * (m_sweep / 100.0);
        p.setBrush(QColor(0x7F, 0x77, 0xDD, 220));
        p.drawRoundedRect(QRectF(segX, trackY, segW, 2.0), 1.0, 1.0);
    }

private:
    QTimer m_spin;  // animation
    QTimer m_delay; // show-after delay
    int m_angle = 0;
    int m_sweep = 0; // 0..99 progress-strip position
    QString m_label = QStringLiteral("Healing…");
};

// A transparent overlay that draws the brush cursor: an outer ring (size) and a
// fainter inner ring (hardness core). Mouse-transparent so the canvas below
// still receives events.
class BrushRing : public QWidget {
public:
    explicit BrushRing(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        hide();
    }

    void setRing(QPointF center, qreal outer, qreal inner, bool visible)
    {
        m_center = center;
        m_outer = outer;
        m_inner = inner;
        if (!visible || outer <= 0.5) {
            if (isVisible())
                hide();
            return;
        }
        if (!isVisible())
            show();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_outer <= 0.5)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        // Dark halo then a light ring, so it reads on any image.
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, 160), 3.0));
        p.drawEllipse(m_center, m_outer, m_outer);
        p.setPen(QPen(QColor(255, 255, 255, 230), 1.3));
        p.drawEllipse(m_center, m_outer, m_outer);
        if (m_inner > 1.0 && m_inner < m_outer - 0.5) {
            p.setPen(QPen(QColor(255, 255, 255, 110), 1.0, Qt::DashLine));
            p.drawEllipse(m_center, m_inner, m_inner);
        }
        // Centre dot.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 200));
        p.drawEllipse(m_center, 1.3, 1.3);
    }

private:
    QPointF m_center;
    qreal m_outer = 0;
    qreal m_inner = 0;
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Start with a single empty document (tab 0). Never zero documents.
    m_docs.push_back(std::make_unique<Document>());
    m_activeTab = 0;

    setAcceptDrops(true); // drop image/project files to open them as tabs

    updateTitle();

    // Seed the automatic-RAW configuration from the global preference; the first
    // RAW opened uses these (and each .lumen carries its own per-project copy).
    loadRawDefaults(doc().rawOptions, m_rawLensDefaults);

    // Build the initial document's Base graph (each tab/document has its own).
    doc().buildBaseGraph();

    m_canvas = new CanvasWidget(this);
    setCentralWidget(m_canvas);

    // Tab strip for multiple open documents. A MainWindow child overlaid on the
    // top edge of the canvas (like the other overlays), shown only when more than
    // one document is open so a single image stays fully immersive. Positioned in
    // layoutOverlays().
    m_tabBar = new QTabBar(this);
    m_tabBar->setDrawBase(false);
    m_tabBar->setExpanding(false);
    m_tabBar->setTabsClosable(true);
    m_tabBar->setElideMode(Qt::ElideRight);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setFocusPolicy(Qt::NoFocus);
    m_tabBar->setAutoFillBackground(true);
    m_tabBar->addTab(documentLabel(doc())); // tab for the initial document
    m_tabBar->hide();
    connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
        if (!m_inTabOp && index >= 0 && index != m_activeTab)
            switchToTab(index);
    });
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &MainWindow::closeTab);

    // Brush cursor ring overlay (a MainWindow child over the canvas, like the
    // scrim, so it composites reliably above the RHI content).
    auto *brushRing = new BrushRing(this);
    m_brushRing = brushRing;
    connect(m_canvas, &CanvasWidget::brushCursorMoved, this,
            [this, brushRing](QPointF pos, qreal outer, qreal inner, bool visible) {
                const QPoint inWindow = m_canvas->mapTo(this, pos.toPoint());
                brushRing->setRing(inWindow, outer, inner, visible);
            });

    // Progress badge for slow background passes. The image is intentionally NOT
    // dimmed while it runs, so the user can keep judging the picture.
    auto *busyBadge = new BusyBadge(this);
    m_healBusy = busyBadge;

    // Created before the palette so the palette stacks above it.
    m_scrim = new Scrim(this);
    m_scrim->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_scrim->hide();

    // The hint bar tracks the active mode (Browse / tool open / palette), so its
    // legend always reflects the keys available right now.
    connect(&m_input, &InputController::modeChanged, this,
            [this](InputController::Mode) { updateModeHint(); });

    m_palette = new CommandPalette(this);
    connect(m_palette, &CommandPalette::commandTriggered, this, &MainWindow::runCommand);
    connect(m_palette, &CommandPalette::dismissed, this, [this] {
        m_scrim->hide();
        m_input.setMode(InputController::Mode::Browse);
        m_canvas->setFocus();
    });

    m_tonePanel = new TonePanel(this);
    connect(m_tonePanel, &TonePanel::valuesChanged, this, [this](const ToneValues &v) {
        if (auto *t = activeTune()) { // route to the active layer
            t->setExposure(v.exposure);
            t->setContrast(v.contrast);
            t->setHighlights(v.highlights);
            t->setShadows(v.shadows);
            t->setWhites(v.whites);
            t->setBlacks(v.blacks);
            t->setSaturation(v.saturation);
            t->setVibrance(v.vibrance);
            t->setKelvin(v.kelvin);
            t->setTint(v.tint);
        }
        updatePreview(); // preview is driven by walking the graph
    });
    connect(m_tonePanel, &TonePanel::closed, this, &MainWindow::closeToneTool);
    connect(m_tonePanel, &TonePanel::whiteBalanceResetRequested, this, [this] {
        if (auto *t = activeTune()) {
            t->setKelvin(t->asShotKelvin());
            t->setTint(0.0f);
            updatePreview();
            m_tonePanel->reveal(toneValuesOf(t));
        }
    });
    connect(m_tonePanel, &TonePanel::whiteBalancePickRequested, this, [this] {
        m_pickPurpose = PickPurpose::WhiteBalance;
        m_canvas->setColorPickMode(true);
        showHint(QStringLiteral("Click a neutral grey to set white balance"));
    });

    m_curvesPanel = new CurvesPanel(this);
    connect(m_curvesPanel, &CurvesPanel::curveChanged, this, [this](const ChannelCurves &c) {
        if (auto *cv = activeCurves())
            cv->setCurves(c);
        updatePreview();
    });

    m_looksPanel = new LooksPanel(this);
    connect(m_looksPanel, &LooksPanel::loadRequested, this, &MainWindow::loadLookFile);
    connect(m_looksPanel, &LooksPanel::clearRequested, this, [this] {
        if (auto *l = activeLut())
            l->clear();
        m_looksPanel->setLookName(QString());
        updatePreview();
    });
    connect(m_looksPanel, &LooksPanel::intensityChanged, this, [this](double v) {
        if (auto *l = activeLut())
            l->setIntensity(static_cast<float>(v));
        updatePreview();
    });

    m_presetsPanel = new PresetsPanel(this);
    connect(m_presetsPanel, &PresetsPanel::applyRequested, this, [this](const QString &id) {
        for (const preset::Builtin &b : preset::library()) {
            if (b.id != id)
                continue;
            applyPresetLook(b, m_presetsPanel->amount());
            break;
        }
    });
    connect(m_presetsPanel, &PresetsPanel::amountChanged, this,
            [this](int percent) { setPresetAmount(percent); });
    connect(m_presetsPanel, &PresetsPanel::deleteRequested, this, [this](const QString &id) {
        const QString path = preset::userPresetPath(id);
        if (path.isEmpty())
            return;
        const QString label = QFileInfo(path).completeBaseName();
        if (QMessageBox::question(this, QStringLiteral("Lumen"),
                                  QStringLiteral("Delete preset “%1”?").arg(label),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
            return;
        if (QFile::remove(path)) {
            showHint(QStringLiteral("Deleted preset"));
            invalidatePresetThumbCache(); // the id may be reused by a later save
            refreshPresetThumbnails();
        } else {
            showHint(QStringLiteral("Could not delete the preset file"));
        }
    });
    connect(m_presetsPanel, &PresetsPanel::renameRequested, this,
            [this](const QString &id, const QString &newName) {
                const QString path = preset::userPresetPath(id);
                if (path.isEmpty())
                    return;
                QJsonObject obj;
                QString error;
                if (!preset::load(path, &obj, &error)) {
                    showHint(QStringLiteral("Could not open the preset file"));
                    return;
                }
                obj[QStringLiteral("name")] = newName; // the file keeps its path/id
                if (!preset::save(path, obj, &error)) {
                    showHint(QStringLiteral("Could not save the preset: %1").arg(error));
                    return;
                }
                showHint(QStringLiteral("Renamed preset to “%1”").arg(newName));
                refreshPresetThumbnails();
            });

    m_monoPanel = new MonoPanel(this);
    connect(m_monoPanel, &MonoPanel::valuesChanged, this, [this](const MonoValues &v) {
        if (auto *mono = activeMono())
            mono->setValues(v);
        updatePreview();
    });
    connect(m_monoPanel, &MonoPanel::closed, this, &MainWindow::closeMonoTool);

    m_colorMixerPanel = new ColorMixerPanel(this);
    connect(m_colorMixerPanel, &ColorMixerPanel::valuesChanged, this,
            [this](const ColorMixerValues &v) {
                if (auto *cm = activeColorMixer())
                    cm->setValues(v);
                updatePreview();
            });
    connect(m_colorMixerPanel, &ColorMixerPanel::closed, this,
            &MainWindow::closeColorMixerTool);

    m_colorGradePanel = new ColorGradePanel(this);
    connect(m_colorGradePanel, &ColorGradePanel::valuesChanged, this,
            [this](const ColorGradeValues &v) {
                if (auto *g = activeColorGrade())
                    g->setValues(v);
                updatePreview();
            });
    connect(m_colorGradePanel, &ColorGradePanel::closed, this,
            &MainWindow::closeColorGradeTool);

    m_lensPanel = new LensPanel(this);
    connect(m_lensPanel, &LensPanel::paramsChanged, this,
            [this](const LensCorrectionNode::Params &p) {
                if (!doc().lens)
                    return;
                doc().lens->setParams(p);
                refreshWorkingSource();   // re-render the corrected base
                refreshBaseImage();       // re-apply heal onto it, keep the view
                updatePreview();
            });
    connect(m_lensPanel, &LensPanel::closed, this, &MainWindow::closeLensTool);

    m_sharpenPanel = new SharpenPanel(this);
    connect(m_sharpenPanel, &SharpenPanel::valuesChanged, this,
            [this](const SharpenNode::Values &v) {
                if (!doc().sharpen)
                    return;
                doc().sharpen->setValues(v);
                // Sharpen bakes into the base; coalesce drags so we don't kick a
                // full-res pass per tick.
                m_bakeOp = BakeOp::Sharpen;
                m_bakeTimer->start();
            });
    connect(m_sharpenPanel, &SharpenPanel::closed, this, &MainWindow::closeSharpenTool);

    m_structurePanel = new StructurePanel(this);
    connect(m_structurePanel, &StructurePanel::valuesChanged, this,
            [this](const StructureNode::Values &v) {
                if (!doc().structure)
                    return;
                doc().structure->setValues(v);
                // Structure bakes into the base; coalesce drags so we don't kick a
                // full-res pass per tick.
                m_bakeOp = BakeOp::Structure;
                m_bakeTimer->start();
            });
    connect(m_structurePanel, &StructurePanel::closed, this, &MainWindow::closeStructureTool);

    m_grainPanel = new GrainPanel(this);
    connect(m_grainPanel, &GrainPanel::valuesChanged, this,
            [this](const GrainNode::Values &v) {
                if (!doc().grain)
                    return;
                doc().grain->setValues(v);
                updatePreview(); // grain is a live GPU step (no base re-bake)
            });
    connect(m_grainPanel, &GrainPanel::closed, this, &MainWindow::closeGrainTool);

    m_vignettePanel = new VignettePanel(this);
    connect(m_vignettePanel, &VignettePanel::valuesChanged, this,
            [this](const VignetteParams &v) {
                doc().graph.setVignette(v);
                m_canvas->setVignette(v);
                updatePreview(); // vignette is a live present-pass op (no base re-bake)
            });
    connect(m_vignettePanel, &VignettePanel::closed, this, &MainWindow::closeVignetteTool);

    m_cropPanel = new CropPanel(this);
    connect(m_cropPanel, &CropPanel::aspectChanged, this, [this](double aspect) {
        m_cropAspect = aspect;
        if (m_cropGizmo)
            m_cropGizmo->setAspect(aspect);
        // A straightened crop must re-inset to the new aspect to stay off the
        // tilt's transparent corners.
        CropState c = doc().graph.crop();
        if (std::abs(c.straighten) > 1e-6) {
            c.rect = straightenSafeCropRect(c);
            doc().graph.setCrop(c);
            if (m_cropGizmo)
                m_cropGizmo->setRect(c.rect);
            updateCropView();
            updatePreview();
        }
    });
    connect(m_cropPanel, &CropPanel::rotateRequested, this, [this](int deltaCW) {
        CropState c = doc().graph.crop();
        c.rotation = ((c.rotation + deltaCW) % 360 + 360) % 360;
        // A re-orient resets the rectangle (re-inset if a straighten is active).
        c.rect = straightenSafeCropRect(c);
        doc().graph.setCrop(c);
        if (m_cropGizmo)
            m_cropGizmo->setRect(c.rect);
        updateCropView();
        updatePreview();
    });
    connect(m_cropPanel, &CropPanel::flipRequested, this, [this](bool horizontal) {
        CropState c = doc().graph.crop();
        if (horizontal)
            c.flipH = !c.flipH;
        else
            c.flipV = !c.flipV;
        doc().graph.setCrop(c);
        updateCropView();
        updatePreview();
    });
    connect(m_cropPanel, &CropPanel::straightenChanged, this, [this](double deg) {
        CropState c = doc().graph.crop();
        c.straighten = deg;
        // Keep the crop clear of the tilt's transparent corners. (The whole crop
        // session is a single undo step, committed in closeCropTool.)
        c.rect = straightenSafeCropRect(c);
        doc().graph.setCrop(c);
        if (m_cropGizmo) {
            m_cropGizmo->setStraighten(deg);
            m_cropGizmo->setRect(c.rect);
        }
        updateCropView();
        updatePreview();
    });
    connect(m_cropPanel, &CropPanel::resetRequested, this, [this] {
        doc().graph.setCrop(CropState{});
        if (m_cropGizmo) {
            m_cropGizmo->setStraighten(0.0);
            m_cropGizmo->setRect(QRectF(0.0, 0.0, 1.0, 1.0));
        }
        if (m_cropPanel->isVisible())
            m_cropPanel->reveal(doc().graph.crop(), sourceAspect());
        updateCropView();
        updatePreview();
    });
    connect(m_cropPanel, &CropPanel::closed, this, &MainWindow::closeCropTool);

    m_denoisePanel = new DenoisePanel(this);
    connect(m_denoisePanel, &DenoisePanel::valuesChanged, this,
            [this](const DenoiseNode::Values &v) {
                if (!doc().denoise)
                    return;
                doc().denoise->setValues(v);
                // Denoise bakes into the base; coalesce drags (it's a full-res
                // LAB pass) just like sharpen.
                m_bakeOp = BakeOp::Denoise;
                m_bakeTimer->start();
            });
    connect(m_denoisePanel, &DenoisePanel::closed, this, &MainWindow::closeDenoiseTool);

    m_defringePanel = new DefringePanel(this);
    connect(m_defringePanel, &DefringePanel::valuesChanged, this,
            [this](const DefringeNode::Values &v) {
                if (!doc().defringe)
                    return;
                doc().defringe->setValues(v);
                // Defringe bakes into the base (a full-res LAB pass); coalesce
                // drags like denoise/sharpen.
                m_bakeOp = BakeOp::Defringe;
                m_bakeTimer->start();
            });
    connect(m_defringePanel, &DefringePanel::closed, this, &MainWindow::closeDefringeTool);

    m_rawPanel = new RawSettingsPanel(this);
    connect(m_rawPanel, &RawSettingsPanel::valuesChanged, this,
            [this](const raw::RawDecodeOptions &opts, const raw::RawLensDefaults &lens) {
                const bool decodeChanged = !(opts == doc().rawOptions);
                const bool lensChanged = !(lens == m_rawLensDefaults);
                doc().rawOptions = opts;
                m_rawLensDefaults = lens;
                saveRawDefaults(doc().rawOptions, m_rawLensDefaults); // new global default
                // Apply the lens-correction toggles to the open photo, preserving
                // its perspective and other lens params.
                if (lensChanged && doc().lens) {
                    LensCorrectionNode::Params p = doc().lens->params();
                    p.distortion = lens.distortion;
                    p.tca = lens.tca;
                    p.vignetting = lens.vignetting;
                    doc().lens->setParams(p);
                }
                if (decodeChanged) {
                    m_redecodeTimer->start(); // debounced re-decode (also refreshes lens)
                } else if (lensChanged) {
                    refreshWorkingSource();
                    refreshBaseImage(true);
                    updatePreview();
                }
            });
    connect(m_rawPanel, &RawSettingsPanel::closed, this, &MainWindow::closeRawTool);

    // Debounce timers: the histogram recompute and the sharpen base re-bake both
    // settle shortly after the user stops dragging.
    m_bakeTimer = new QTimer(this);
    m_bakeTimer->setSingleShot(true);
    // Settle delay: the sharpen/denoise base re-bake is a full-res pass, so we
    // only kick it once the user has clearly stopped sliding (not per tick).
    m_bakeTimer->setInterval(kHeavyBakeSettleMs);
    connect(m_bakeTimer, &QTimer::timeout, this, [this] {
        refreshBaseImage();
        updatePreview();
    });

    // Re-decode (RAW option change) is a full demosaic, so coalesce slider drags.
    m_redecodeTimer = new QTimer(this);
    m_redecodeTimer->setSingleShot(true);
    m_redecodeTimer->setInterval(kHeavyBakeSettleMs);
    connect(m_redecodeTimer, &QTimer::timeout, this, [this] { redecodeCurrent(); });

    m_histogram = new HistogramWidget(this);
    m_histTimer = new QTimer(this);
    m_histTimer->setSingleShot(true);
    // Recompute the histogram a beat after edits settle, not on every drag tick —
    // long enough that brief pauses mid-drag don't kick off a full recompute.
    m_histTimer->setInterval(300);
    connect(m_histTimer, &QTimer::timeout, this, &MainWindow::updateHistogram);
    connect(&m_histWatcher, &QFutureWatcher<HistogramData>::finished, this, [this] {
        if (!m_histWatcher.future().isValid())
            return;
        const HistogramData h = m_histWatcher.result();
        if (!docIsActive(m_histJobDoc))
            return; // computed for a tab that's no longer showing; drop it
        if (h.valid && m_histogram->isVisible())
            m_histogram->setData(h);
    });

    // Mask colour-pick + brush painting are driven from the Layers panel now;
    // the canvas wiring stays here.
    connect(m_canvas, &CanvasWidget::colorPointPicked, this, &MainWindow::onColorPicked);
    connect(m_canvas, &CanvasWidget::brushStrokeBegan, this, &MainWindow::beginBrushStroke);
    connect(m_canvas, &CanvasWidget::brushPoint, this, &MainWindow::brushAt);
    connect(m_canvas, &CanvasWidget::brushStrokeEnded, this, &MainWindow::endBrushStroke);
    connect(m_canvas, &CanvasWidget::brushAdjustRequested, this, &MainWindow::adjustBrush);

    // Right-click anywhere on the canvas opens the palette — the same effect as
    // the "/" key, so pointer-only users are never locked out (DESIGN.md §4.6).
    connect(m_canvas, &CanvasWidget::paletteRequested, this, [this] {
        switch (m_input.mode()) {
        case InputController::Mode::Browse:
            openCommandPalette();
            break;
        case InputController::Mode::ToolActive:
            closeActiveTool(); // mirror the "/"-from-a-tool swap
            openCommandPalette();
            break;
        default:
            break;
        }
    });

    // Background heal preview finished: apply the result (the watcher only
    // delivers the latest request's future).
    connect(&m_healWatcher, &QFutureWatcher<QImage>::finished, this, [this] {
        // The badge, canvas and histogram all belong to the active tab, so only
        // touch them when this bake's document is still the one on screen.
        const bool active = docIsActive(m_healJobDoc);
        if (active)
            static_cast<BusyBadge *>(m_healBusy)->stop();
        if (!m_healWatcher.future().isValid())
            return;
        const QImage healed = m_healWatcher.result();
        if (!active)
            return; // baked for a background tab; drop (re-baked on return)
        if (!healed.isNull())
            m_canvas->setImage(healed, /*keepView=*/true); // preserve zoom/pan
        // The histogram defers itself while a bake runs; recompute now (debounced).
        if (m_histogram->isVisible())
            m_histTimer->start();
    });

    // Background export finished: stop the badge and report the outcome.
    connect(&m_exportWatcher, &QFutureWatcher<ExportResult>::finished, this, [this] {
        // The badge is the active tab's; the outcome (a written file) is reported
        // regardless of which tab is showing now.
        if (docIsActive(m_exportJobDoc))
            static_cast<BusyBadge *>(m_healBusy)->stop();
        if (!m_exportWatcher.future().isValid())
            return;
        const ExportResult r = m_exportWatcher.result();
        if (!r.ok) {
            QMessageBox::warning(this, QStringLiteral("Lumen"),
                                 QStringLiteral("Export failed: %1").arg(r.error));
            return;
        }
        rememberDir(QStringLiteral("export"), r.path);
        showHint(QStringLiteral("Exported to %1").arg(QFileInfo(r.path).fileName()));
    });

    // Background project save finished: report the outcome and adopt the file.
    connect(&m_saveWatcher, &QFutureWatcher<SaveResult>::finished, this, [this] {
        if (docIsActive(m_saveJobDoc))
            static_cast<BusyBadge *>(m_healBusy)->stop();
        if (!m_saveWatcher.future().isValid())
            return;
        const SaveResult r = m_saveWatcher.result();
        if (!r.ok) {
            QMessageBox::warning(this, QStringLiteral("Lumen"),
                                 QStringLiteral("Could not save project: %1").arg(r.error));
            return;
        }
        // Reconcile the saved document's in-memory state (path, autosave baseline),
        // whichever tab is active now; drop if that document was closed.
        if (Document *d = docById(m_saveJobDoc))
            applySaveSuccess(*d, r.path);
    });

    // Background image open finished: install the decoded source + rebuild.
    connect(&m_openImageWatcher, &QFutureWatcher<OpenImageResult>::finished, this, [this] {
        static_cast<BusyBadge *>(m_healBusy)->stop();
        if (!m_openImageWatcher.future().isValid())
            return;
        if (docById(m_openJobDoc))
            finishOpenImage(m_openImageWatcher.result());
        // else: the tab this open was for was closed mid-decode; drop it.
        dequeueNextOpen(); // open the next queued file (if any) as its own tab
    });

    // Background project open finished: install the decoded document.
    connect(&m_openProjectWatcher, &QFutureWatcher<OpenProjectResult>::finished, this, [this] {
        static_cast<BusyBadge *>(m_healBusy)->stop();
        if (!m_openProjectWatcher.future().isValid())
            return;
        // Skip if the requesting tab was closed mid-decode; otherwise install it.
        if (docById(m_openJobDoc)) {
            const OpenProjectResult r = m_openProjectWatcher.result();
            if (!r.loaded)
                QMessageBox::warning(this, QStringLiteral("Lumen"), r.error);
            else if (r.source.isNull())
                QMessageBox::warning(
                    this, QStringLiteral("Lumen"),
                    QStringLiteral("Could not decode the embedded image: %1").arg(r.error));
            else
                applyProjectResult(r);
        }
        dequeueNextOpen(); // open the next queued file (if any) as its own tab
    });

    // Background RAW re-decode finished: install the new source and rebuild the
    // pipeline (the watcher only delivers the latest request's future).
    connect(&m_decodeWatcher, &QFutureWatcher<DecodeResult>::finished, this, [this] {
        const bool active = docIsActive(m_decodeJobDoc);
        if (active)
            static_cast<BusyBadge *>(m_healBusy)->stop();
        if (!m_decodeWatcher.future().isValid())
            return;
        Document *d = docById(m_decodeJobDoc);
        if (!d)
            return; // the tab this re-decode was for was closed; drop it
        const DecodeResult r = m_decodeWatcher.result();
        if (r.image.isNull()) {
            if (active && !r.error.isEmpty())
                showHint(QStringLiteral("Could not re-decode RAW: %1").arg(r.error));
            return;
        }
        // Install the new pixels on the target document (safe whether or not it's
        // the active tab).
        d->graph.setSource(r.image);
        ++d->sourceGeneration; // new pixels → preset thumbnails must re-render
        // Keep the current WB temperature (don't reseed to as-shot).
        d->applyCameraProfile(r.meta.color, /*seedKelvin=*/false);
        // The preview stages read the active document; only rebuild them when the
        // re-decoded document is on screen. A background tab picks up the new
        // source when it's next bound (tab switch → bindDocument, Stage 4).
        if (active) {
            refreshWorkingSource();  // rebuild the corrected source + display QImage
            refreshBaseImage(true);  // keep zoom/pan (itself async if a bake is active)
            recomputeSelectiveMask();
            updatePreview();
        }
    });

    m_healPanel = new HealPanel(this);
    connect(m_healPanel, &HealPanel::settingsChanged, this,
            [this](int size, int hardness, bool add) {
                m_brushSize = size;
                m_brushHardness = hardness;
                m_brushAdd = add;
                m_canvas->setBrushCursor(size, hardness / 100.0f);
            });
    connect(m_healPanel, &HealPanel::clearRequested, this, [this] {
        if (m_brushMask.isEmpty())
            return;
        m_brushUndo.push_back(m_brushMask.data); // clear is undoable
        std::fill(m_brushMask.data.begin(), m_brushMask.data.end(), 0.0f);
        doc().heal->setHealMask(m_brushMask);
        refreshBaseImage();
        updatePreview();
    });
    connect(m_healPanel, &HealPanel::qualityChanged, this, [this](bool hq) {
        doc().heal->setHighQuality(hq);
        refreshBaseImage(); // re-heal the current mask at the new quality
        updatePreview();
    });

    m_adjustmentsPanel = new AdjustmentsPanel(this);
    connect(m_adjustmentsPanel, &AdjustmentsPanel::compareToggled, this,
            [this](bool on) { setCompareOriginal(on); });
    connect(m_adjustmentsPanel, &AdjustmentsPanel::toggleRequested, this,
            &MainWindow::onAdjustmentToggle);
    connect(m_adjustmentsPanel, &AdjustmentsPanel::deleteRequested, this,
            &MainWindow::onAdjustmentDelete);
    connect(m_adjustmentsPanel, &AdjustmentsPanel::viewUpToRequested, this,
            &MainWindow::peekUpTo);

    m_layersPanel = new LayersPanel(this);
    connect(m_layersPanel, &LayersPanel::addRequested, this, &MainWindow::addAdjustmentLayer);
    connect(m_layersPanel, &LayersPanel::deleteRequested, this, &MainWindow::deleteActiveLayer);
    connect(m_layersPanel, &LayersPanel::layerSelected, this, &MainWindow::selectLayer);
    connect(m_layersPanel, &LayersPanel::renameRequested, this, [this](int i, const QString &name) {
        if (i <= 0 || i >= doc().graph.layerCount())
            return; // the Base layer (0) is not renamable
        const QString t = name.trimmed();
        if (t.isEmpty() || t == doc().graph.layer(i).name()) {
            refreshLayersPanel(); // restore the row (rejects empty/unchanged)
            return;
        }
        doc().graph.layer(i).setName(t);
        refreshLayersPanel();
        doc().graph.commit();
    });
    connect(m_layersPanel, &LayersPanel::visibilityToggled, this, [this](int i, bool on) {
        if (i >= 0 && i < doc().graph.layerCount()) {
            doc().graph.layer(i).setEnabled(on);
            refreshLayersPanel();
            updatePreview();
            doc().graph.commit();
        }
    });
    connect(m_layersPanel, &LayersPanel::opacityChanged, this, [this](int percent) {
        doc().graph.activeLayer().setOpacity(percent / 100.0f);
        updatePreview();
    });
    connect(m_layersPanel, &LayersPanel::maskTypeChanged, this,
            &MainWindow::setActiveLayerMaskType);
    connect(m_layersPanel, &LayersPanel::maskFeatherChanged, this, [this](int percent) {
        MaskSpec spec = doc().graph.activeLayer().mask();
        spec.feather = percent / 100.0f;
        doc().graph.activeLayer().setMask(spec);
        recomputeSelectiveMask();
        updatePreview();
        doc().graph.commit();
    });
    connect(m_layersPanel, &LayersPanel::maskInvertChanged, this, [this](bool on) {
        MaskSpec spec = doc().graph.activeLayer().mask();
        spec.invert = on;
        doc().graph.activeLayer().setMask(spec);
        recomputeSelectiveMask();
        updatePreview();
        doc().graph.commit();
    });
    connect(m_layersPanel, &LayersPanel::maskRangeChanged, this, [this](int low, int high) {
        MaskSpec spec = doc().graph.activeLayer().mask();
        spec.low = low / 100.0f;
        spec.high = high / 100.0f;
        doc().graph.activeLayer().setMask(spec);
        recomputeSelectiveMask();
        updatePreview();
        doc().graph.commit();
    });
    connect(m_layersPanel, &LayersPanel::maskColorRangeChanged, this, [this](int percent) {
        MaskSpec spec = doc().graph.activeLayer().mask();
        spec.colorRange = percent / 100.0f;
        doc().graph.activeLayer().setMask(spec);
        recomputeSelectiveMask();
        updatePreview();
        doc().graph.commit();
    });
    connect(m_layersPanel, &LayersPanel::maskPickColorRequested, this, [this] {
        m_pickPurpose = PickPurpose::MaskColour;
        m_canvas->setColorPickMode(true);
    });
    connect(m_layersPanel, &LayersPanel::maskShowChanged, this, [this](int mode) {
        doc().maskView = mode;
        recomputeSelectiveMask();
        updatePreview();
    });
    connect(m_layersPanel, &LayersPanel::brushSettingsChanged, this,
            [this](int size, int hardness, bool add) {
                m_brushSize = size;
                m_brushHardness = hardness;
                m_brushAdd = add;
                m_canvas->setBrushCursor(size, hardness / 100.0f);
            });
    connect(m_layersPanel, &LayersPanel::brushClearRequested, this, [this] {
        if (m_brushMask.isEmpty())
            return;
        m_brushUndo.push_back(m_brushMask.data); // clear is undoable
        std::fill(m_brushMask.data.begin(), m_brushMask.data.end(), 0.0f);
        syncBrushMaskToLayer();
        m_canvas->setSelectiveMask(m_brushMask);
        updatePreview();
    });

    // On-canvas gizmo for editing a layer's gradient/radial mask geometry.
    m_maskGizmo = new MaskGizmo(m_canvas, this);
    connect(m_maskGizmo, &MaskGizmo::changed, this,
            [this](const MaskSpec &s) { onLayerMaskEdited(s, /*commit=*/false); });
    connect(m_maskGizmo, &MaskGizmo::editFinished, this,
            [this](const MaskSpec &s) { onLayerMaskEdited(s, /*commit=*/true); });
    connect(m_canvas, &CanvasWidget::viewChanged, m_maskGizmo, &MaskGizmo::refresh);

    // On-canvas editor for the active layer's exclusive-zone shapes. It overlays
    // the mask gizmo and forwards unconsumed clicks down to it (so a gradient/
    // radial mask stays editable beneath the zone shapes).
    m_zoneGizmo = new ZoneGizmo(m_canvas, this);
    m_zoneGizmo->setFallthrough(m_maskGizmo);
    const auto applyZones = [this](const std::vector<MaskZoneShape> &shapes, bool commit) {
        if (doc().graph.activeLayerIndex() == 0)
            return;
        MaskSpec spec = doc().graph.activeLayer().mask();
        spec.zones = shapes;
        doc().graph.activeLayer().setMask(spec);
        m_layersPanel->setZoneCount(static_cast<int>(shapes.size()));
        recomputeSelectiveMask();
        updatePreview();
        if (commit)
            doc().graph.commit();
    };
    connect(m_zoneGizmo, &ZoneGizmo::changed, this,
            [applyZones](const std::vector<MaskZoneShape> &s) { applyZones(s, false); });
    connect(m_zoneGizmo, &ZoneGizmo::editFinished, this,
            [applyZones](const std::vector<MaskZoneShape> &s) { applyZones(s, true); });
    // After a shape is drawn the gizmo returns to Select; reflect that on the panel.
    connect(m_zoneGizmo, &ZoneGizmo::toolReset, this,
            [this] { m_layersPanel->resetZoneTool(); });
    connect(m_canvas, &CanvasWidget::viewChanged, m_zoneGizmo, &ZoneGizmo::refresh);

    // On-canvas crop rectangle editor (only visible while the Crop tool is open).
    m_cropGizmo = new CropGizmo(m_canvas, this);
    connect(m_cropGizmo, &CropGizmo::changed, this, [this](const QRectF &r) {
        CropState c = doc().graph.crop();
        c.rect = r;
        doc().graph.setCrop(c);
        // Live: don't commit yet (editFinished commits); the present pass stays in
        // Editing mode (full frame) so the gizmo keeps mapping 1:1.
    });
    connect(m_cropGizmo, &CropGizmo::editFinished, this, [this](const QRectF &r) {
        CropState c = doc().graph.crop();
        c.rect = r;
        doc().graph.setCrop(c);
        doc().graph.commit();
    });
    connect(m_canvas, &CanvasWidget::viewChanged, m_cropGizmo, [this] { m_cropGizmo->update(); });

    connect(m_layersPanel, &LayersPanel::zoneToolChanged, this, [this](int tool) {
        // Engaging a tool implies you want to see the overlay: un-hide first.
        if (doc().overlaysHidden)
            setOverlayGeometryVisible(true);
        m_zoneGizmo->setTool(static_cast<ZoneGizmo::Tool>(tool));
        syncZoneGizmo(); // ensure it is shown while a tool is engaged
    });
    connect(m_layersPanel, &LayersPanel::overlayVisibilityChanged, this,
            [this](bool visible) { setOverlayGeometryVisible(visible); });
    connect(m_layersPanel, &LayersPanel::zoneModeChanged, this,
            [this](bool subtract) { m_zoneGizmo->setSubtract(subtract); });
    connect(m_layersPanel, &LayersPanel::zoneFeatherChanged, this, [this](int percent) {
        if (doc().graph.activeLayerIndex() == 0)
            return;
        MaskSpec spec = doc().graph.activeLayer().mask();
        spec.zoneFeather = percent / 100.0f;
        doc().graph.activeLayer().setMask(spec);
        recomputeSelectiveMask();
        updatePreview();
        doc().graph.commit();
    });
    connect(m_layersPanel, &LayersPanel::zoneClearRequested, this, [this] {
        if (doc().graph.activeLayerIndex() == 0)
            return;
        MaskSpec spec = doc().graph.activeLayer().mask();
        if (spec.zones.empty())
            return;
        spec.zones.clear();
        doc().graph.activeLayer().setMask(spec);
        m_layersPanel->setZoneCount(0);
        syncZoneGizmo();
        recomputeSelectiveMask();
        updatePreview();
        doc().graph.commit();
    });

    // Dismissible hint bar, bottom-centre over the canvas.
    m_hint = new QLabel(this);
    m_hint->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_hint->setStyleSheet(QStringLiteral(
        "background: rgba(20,20,22,0.85); border-radius: 8px;"
        "padding: 6px 16px; color: #c8c8cc; font-size: 12px;"));
    m_hint->setText(modeHintText()); // Browse legend at startup
    m_hint->adjustSize();

    // View-toggle cluster, bottom-right. Glanceable on/off buttons for the
    // histogram, clipping warnings, and history (Adjustments) panel — the same
    // state the G / J / A keys and the palette flip. Buttons reflect current
    // state via syncViewToggles(); clicking routes through the same toggles.
    m_viewToggles = new QWidget(this);
    m_viewToggles->setObjectName(QStringLiteral("viewToggles"));
    m_viewToggles->setAttribute(Qt::WA_StyledBackground, true);
    m_viewToggles->setStyleSheet(QStringLiteral(
        "#viewToggles { background: rgba(20,20,22,0.85); border-radius: 8px; }"
        "#viewToggles QPushButton { background: transparent; border: none;"
        " color: #c8c8cc; padding: 5px 11px; font-size: 12px; border-radius: 6px; }"
        "#viewToggles QPushButton:hover { background: #2a2a2e; }"
        "#viewToggles QPushButton:checked { background: #3a3a40; color: #f0f0f1; }"
        "#clusterGrip { color: #6a6a70; font-size: 13px; padding: 0 2px; }"));
    auto *vtLayout = new QHBoxLayout(m_viewToggles);
    vtLayout->setContentsMargins(6, 4, 6, 4);
    vtLayout->setSpacing(4);
    // Drag handle: grab here to move the cluster (the buttons consume their own
    // clicks, so they can't double as a drag surface).
    m_clusterGrip = new QLabel(QStringLiteral("⠿"), m_viewToggles);
    m_clusterGrip->setObjectName(QStringLiteral("clusterGrip"));
    m_clusterGrip->setCursor(Qt::SizeAllCursor);
    m_clusterGrip->installEventFilter(this);
    vtLayout->addWidget(m_clusterGrip);
    const auto addToggle = [&](const QString &label, std::function<void()> onClick) {
        auto *b = new QPushButton(label, m_viewToggles);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus); // never steal keyboard focus from the canvas
        connect(b, &QPushButton::clicked, this, [onClick] { onClick(); });
        vtLayout->addWidget(b);
        return b;
    };
    m_histToggleBtn = addToggle(QStringLiteral("Histogram"), [this] { toggleHistogram(); });
    m_clipToggleBtn = addToggle(QStringLiteral("Clipping"), [this] { toggleClipping(); });
    m_historyToggleBtn = addToggle(QStringLiteral("History"), [this] { openAdjustmentsTool(); });
    m_viewToggles->adjustSize();
    syncViewToggles();

    // The histogram is a plain painted widget, so its whole surface drags.
    m_histogram->setCursor(Qt::SizeAllCursor);
    m_histogram->installEventFilter(this);

    // Pointer close (✕) for every floating panel. Tool panels share closeActiveTool
    // (only one is open at a time); the persistent panels close themselves.
    const auto closeTool = [this] { closeActiveTool(); };
    QWidget *toolPanels[] = {m_tonePanel,    m_curvesPanel,  m_looksPanel,
                             m_presetsPanel,  m_monoPanel,    m_colorMixerPanel,
                             m_colorGradePanel, m_lensPanel,  m_sharpenPanel,
                             m_structurePanel, m_denoisePanel, m_defringePanel,
                             m_rawPanel,      m_grainPanel,   m_vignettePanel,
                             m_cropPanel,     m_healPanel};
    for (QWidget *p : toolPanels)
        addPanelCloseButton(p, closeTool);
    addPanelCloseButton(m_layersPanel, [this] { hideLayersPanel(); });
    // The History (Adjustments) panel has no ✕ — the bottom-bar "History" toggle
    // shows/hides it.

    buildCommands();

    // Shell shortcuts. Bare keys are avoided so they don't clash with typing in
    // the palette; "/" and Esc are handled in keyPressEvent instead.
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+O")), this, [this] { openImageDialog(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+S")), this, [this] { saveProject(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")), this, [this] { saveProjectAs(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")), this, [this] { openProject(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")), this, [this] { copySettings(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")), this, [this] { pasteSettings(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Q")), this, [this] { close(); });
    new QShortcut(QKeySequence(Qt::Key_F11), this, [this] { toggleFullScreen(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+0")), this, [this] { m_canvas->resetView(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+P")), this, [this] {
        if (m_presetsPanel->isVisible())
            closePresetsTool();
        else
            openPresetsTool();
    });
    new QShortcut(QKeySequence::Undo, this, [this] { doUndo(); });
    new QShortcut(QKeySequence::Redo, this, [this] { doRedo(); });

    // Autosave: a periodic write of the working document so a crash doesn't lose
    // it. The interval (seconds) is a hidden QSettings knob; the timer only does
    // real work once a document is open and has changed (performAutosave).
    const int intervalSec =
        std::max(1, QSettings().value(QStringLiteral("autosave/intervalSec"), 30).toInt());
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setInterval(intervalSec * 1000);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::performAutosave);
    connect(&m_autosaveWatcher, &QFutureWatcher<bool>::finished, this, [this] {
        Document *d = docById(m_autosaveJobDoc);
        if (!d)
            return; // the tab being autosaved was closed; nothing to reconcile
        d->autosaveInFlight = false;
        if (m_autosaveWatcher.result())
            d->lastAutosaveDoc = d->pendingAutosaveDoc; // committed; skip identical writes
        else if (docIsActive(m_autosaveJobDoc))
            showHint(QStringLiteral("Autosave failed — your work is not yet saved"));
        d->pendingAutosaveDoc.clear();
    });

    // Sweep stale crash-recovery files from past sessions on every launch.
    autosave::purgeOlderThan(autosave::projectsDir(), 7, QDateTime::currentDateTime());

    resize(1280, 800);
}

MainWindow::~MainWindow()
{
    // Make sure no background pipeline task (heal/denoise/defringe/sharpen bake
    // or the histogram compute) is still touching libvips when this window's
    // cached images are freed — and before main() calls vips_shutdown().
    // Signal in-flight tasks to bail at their next checkpoint. Bump every
    // document's counters — the single shared watchers may hold a job for any tab.
    for (const auto &d : m_docs) {
        ++d->healGen;
        ++d->histGen;
        ++d->decodeGen;
    }
    if (m_healWatcher.isRunning())
        m_healWatcher.waitForFinished();
    if (m_histWatcher.isRunning())
        m_histWatcher.waitForFinished();
    if (m_decodeWatcher.isRunning())
        m_decodeWatcher.waitForFinished();
    if (m_exportWatcher.isRunning())
        m_exportWatcher.waitForFinished();
    if (m_saveWatcher.isRunning())
        m_saveWatcher.waitForFinished();
    if (m_openImageWatcher.isRunning())
        m_openImageWatcher.waitForFinished();
    if (m_openProjectWatcher.isRunning())
        m_openProjectWatcher.waitForFinished();
    if (m_autosaveWatcher.isRunning())
        m_autosaveWatcher.waitForFinished();
}

bool MainWindow::openBusy() const
{
    return m_openImageWatcher.isRunning() || m_openProjectWatcher.isRunning()
        || m_decodeWatcher.isRunning();
}

void MainWindow::dequeueNextOpen()
{
    if (m_openQueue.isEmpty() || openBusy())
        return;
    const QString path = m_openQueue.takeFirst();
    openPath(path); // routes .lumen to the project loader; each lands in its own tab
}

Document *MainWindow::docById(quint64 id) const
{
    if (id == 0)
        return nullptr;
    for (const auto &d : m_docs)
        if (d->id == id)
            return d.get();
    return nullptr;
}

bool MainWindow::docIsActive(quint64 id) const
{
    return id != 0 && doc().id == id;
}

void MainWindow::buildCommands()
{
    // ids consumed by runCommand(). Declared grouped by category and in a stable,
    // workflow order: the palette emits a section header whenever the category
    // changes in the browse view, so keep each category's commands consecutive.
    const QString file = QStringLiteral("File");
    const QString toneColor = QStringLiteral("Tone & Color");
    const QString detail = QStringLiteral("Detail & Repair");
    const QString cropLens = QStringLiteral("Crop & Lens");
    const QString effects = QStringLiteral("Effects");
    const QString selective = QStringLiteral("Selective");
    const QString presets = QStringLiteral("Presets");
    const QString edit = QStringLiteral("Edit");
    const QString view = QStringLiteral("View");
    const QString app = QStringLiteral("App");
    m_palette->setCommands({
        {QStringLiteral("open"), QStringLiteral("Open image…"), file},
        {QStringLiteral("open-project"), QStringLiteral("Open project (.lumen)…"), file},
        {QStringLiteral("save-project"), QStringLiteral("Save project"), file},
        {QStringLiteral("save-project-as"), QStringLiteral("Save project as (.lumen)…"), file},
        {QStringLiteral("export"), QStringLiteral("Export image…"), file},
        {QStringLiteral("tone"), QStringLiteral("Tone (exposure, contrast, saturation)"), toneColor},
        {QStringLiteral("curves"), QStringLiteral("Curves"), toneColor},
        {QStringLiteral("colormixer"), QStringLiteral("Color mixer (HSL)"), toneColor},
        {QStringLiteral("colorgrade"), QStringLiteral("Color grading (wheels)"), toneColor},
        {QStringLiteral("monochrome"), QStringLiteral("Monochrome (B&W + toning)"), toneColor},
        {QStringLiteral("looks"), QStringLiteral("Looks (LUT)"), toneColor},
        {QStringLiteral("sharpen"), QStringLiteral("Sharpen"), detail},
        {QStringLiteral("structure"), QStringLiteral("Structure (local contrast / clarity)"), detail},
        {QStringLiteral("denoise"), QStringLiteral("Denoise"), detail},
        {QStringLiteral("defringe"), QStringLiteral("Defringe"), detail},
        {QStringLiteral("heal"), QStringLiteral("Healing brush"), detail},
        {QStringLiteral("raw"), QStringLiteral("RAW defaults (auto adjustments)"), detail},
        {QStringLiteral("crop"), QStringLiteral("Crop & rotate"), cropLens},
        {QStringLiteral("lens"), QStringLiteral("Lens & perspective"), cropLens},
        {QStringLiteral("grain"), QStringLiteral("Film grain"), effects},
        {QStringLiteral("vignette"), QStringLiteral("Vignette"), effects},
        {QStringLiteral("selective"), QStringLiteral("Selective adjustment"), selective},
        {QStringLiteral("layers"), QStringLiteral("Layers"), selective},
        {QStringLiteral("presets-browser"), QStringLiteral("Presets browser (looks)  ·  Ctrl+P"), presets},
        {QStringLiteral("copy-settings"), QStringLiteral("Copy edit settings"), presets},
        {QStringLiteral("paste-settings"), QStringLiteral("Paste edit settings"), presets},
        {QStringLiteral("save-preset"), QStringLiteral("Save preset…"), presets},
        {QStringLiteral("apply-preset"), QStringLiteral("Apply preset…"), presets},
        {QStringLiteral("undo"), QStringLiteral("Undo"), edit},
        {QStringLiteral("redo"), QStringLiteral("Redo"), edit},
        {QStringLiteral("histogram"), QStringLiteral("Histogram (toggle)"), view},
        {QStringLiteral("clipping"), QStringLiteral("Clipping warnings (toggle)"), view},
        {QStringLiteral("adjustments"), QStringLiteral("Adjustments (history)"), view},
        {QStringLiteral("next-tab"), QStringLiteral("Next tab  ·  Ctrl+Tab"), view},
        {QStringLiteral("prev-tab"), QStringLiteral("Previous tab  ·  Ctrl+Shift+Tab"), view},
        {QStringLiteral("duplicate-tab"), QStringLiteral("Duplicate to a new tab"), view},
        {QStringLiteral("close-tab"), QStringLiteral("Close tab  ·  Ctrl+W"), view},
        {QStringLiteral("reset-view"), QStringLiteral("Reset view"), view},
        {QStringLiteral("fullscreen"), QStringLiteral("Toggle fullscreen"), view},
        {QStringLiteral("quit"), QStringLiteral("Quit Lumen"), app},
    });
}

void MainWindow::runCommand(const QString &id)
{
    if (id == QLatin1String("open")) {
        openImageDialog();
    } else if (id == QLatin1String("open-project")) {
        openProject();
    } else if (id == QLatin1String("save-project")) {
        saveProject();
    } else if (id == QLatin1String("save-project-as")) {
        saveProjectAs();
    } else if (id == QLatin1String("export")) {
        exportImage();
    } else if (id == QLatin1String("tone")) {
        openToneTool();
    } else if (id == QLatin1String("curves")) {
        openCurvesTool();
    } else if (id == QLatin1String("looks")) {
        openLooksTool();
    } else if (id == QLatin1String("monochrome")) {
        openMonoTool();
    } else if (id == QLatin1String("colormixer")) {
        openColorMixerTool();
    } else if (id == QLatin1String("colorgrade")) {
        openColorGradeTool();
    } else if (id == QLatin1String("selective")) {
        // A selective adjustment is a masked layer: add/select one, reveal its
        // mask editor (Layers panel) and the Tone tool to adjust it.
        ensureSelectiveLayer();
        showLayersPanel();
        openToneTool();
    } else if (id == QLatin1String("lens")) {
        openLensTool();
    } else if (id == QLatin1String("sharpen")) {
        openSharpenTool();
    } else if (id == QLatin1String("structure")) {
        openStructureTool();
    } else if (id == QLatin1String("denoise")) {
        openDenoiseTool();
    } else if (id == QLatin1String("defringe")) {
        openDefringeTool();
    } else if (id == QLatin1String("raw")) {
        openRawTool();
    } else if (id == QLatin1String("grain")) {
        openGrainTool();
    } else if (id == QLatin1String("vignette")) {
        openVignetteTool();
    } else if (id == QLatin1String("crop")) {
        openCropTool();
    } else if (id == QLatin1String("histogram")) {
        toggleHistogram();
    } else if (id == QLatin1String("clipping")) {
        toggleClipping();
    } else if (id == QLatin1String("heal")) {
        openHealTool();
    } else if (id == QLatin1String("layers")) {
        openLayersTool();
    } else if (id == QLatin1String("presets-browser")) {
        openPresetsTool();
    } else if (id == QLatin1String("copy-settings")) {
        copySettings();
    } else if (id == QLatin1String("paste-settings")) {
        pasteSettings();
    } else if (id == QLatin1String("save-preset")) {
        savePreset();
    } else if (id == QLatin1String("apply-preset")) {
        applyPresetFile();
    } else if (id == QLatin1String("adjustments")) {
        openAdjustmentsTool();
    } else if (id == QLatin1String("undo")) {
        doUndo();
    } else if (id == QLatin1String("redo")) {
        doRedo();
    } else if (id == QLatin1String("next-tab")) {
        if (tabCount() > 1)
            switchToTab((m_activeTab + 1) % tabCount());
    } else if (id == QLatin1String("prev-tab")) {
        if (tabCount() > 1)
            switchToTab((m_activeTab - 1 + tabCount()) % tabCount());
    } else if (id == QLatin1String("duplicate-tab")) {
        duplicateActiveTab();
    } else if (id == QLatin1String("close-tab")) {
        closeTab(m_activeTab);
    } else if (id == QLatin1String("reset-view")) {
        m_canvas->resetView();
    } else if (id == QLatin1String("fullscreen")) {
        toggleFullScreen();
    } else if (id == QLatin1String("quit")) {
        close();
    } else {
        // Editing tools not yet implemented.
        showHint(QStringLiteral("'%1' isn't implemented yet").arg(id));
    }
}

MainWindow::OpenImageResult MainWindow::decodeImageFile(const QString &path,
                                                        const raw::RawDecodeOptions &opts)
{
    OpenImageResult r;
    r.path = path;
    r.isRaw = raw::isRawPath(path);
    // Read the original encoded bytes once (kept to embed verbatim in a .lumen),
    // then decode from them — one disk read, and no UI-thread work.
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        r.bytes = f.readAll();
    if (r.bytes.isEmpty()) {
        r.error = QStringLiteral("Could not read '%1'").arg(path);
        return r;
    }
    r.source =
        r.isRaw
            ? raw::decodeBytes(r.bytes.constData(), r.bytes.size(), &r.error, &r.meta, opts)
            : Image::fromBytes(r.bytes.constData(), r.bytes.size(), &r.error);
    return r;
}

bool MainWindow::openPath(const QString &path)
{
    // Opening no longer discards the current document — it opens in a new tab (or
    // reuses the empty placeholder), so there's nothing to guard here. The
    // unsaved-work prompt lives on tab close / quit.
    if (path.endsWith(QStringLiteral(".lumen"), Qt::CaseInsensitive)) {
        loadProjectFile(path); // a project, not a raw image (async)
        return true;
    }
    if (openBusy()) {
        m_openQueue.append(path); // open this next, as its own tab
        return true;
    }

    // Decode off the UI thread so the app stays responsive and the "Opening…"
    // badge animates (a RAW demosaic is slow); finishOpenImage installs it.
    auto *badge = static_cast<BusyBadge *>(m_healBusy);
    badge->setLabel(QStringLiteral("Opening…"));
    badge->start();
    layoutOverlays();
    m_openJobDoc = doc().id; // the document this decode will populate on completion
    const raw::RawDecodeOptions opts = doc().rawOptions;
    m_openImageWatcher.setFuture(
        QtConcurrent::run([path, opts] { return decodeImageFile(path, opts); }));
    return true;
}

QString MainWindow::documentLabel(const Document &d) const
{
    QString name;
    if (!d.projectPath.isEmpty())
        name = QFileInfo(d.projectPath).fileName();
    else if (!d.sourceName.isEmpty())
        name = d.sourceName;
    else
        name = QStringLiteral("Untitled");
    // Leading dot marks unsaved edits (refreshed whenever the tab bar syncs).
    return documentIsDirty(d) ? QStringLiteral("• %1").arg(name) : name;
}

bool MainWindow::documentIsEmpty(const Document &d) const
{
    return d.graph.source().isNull();
}

void MainWindow::snapshotActiveView()
{
    if (documentIsEmpty(doc()))
        return;
    const CanvasWidget::ViewState v = m_canvas->viewState();
    doc().viewZoom = v.zoom;
    doc().viewPan = v.pan;
    doc().viewValid = true;
}

int MainWindow::addDocumentTab()
{
    auto d = std::make_unique<Document>();
    d->buildBaseGraph();
    m_docs.push_back(std::move(d));
    const int index = static_cast<int>(m_docs.size()) - 1;
    QSignalBlocker block(m_tabBar);
    m_tabBar->addTab(documentLabel(*m_docs[index]));
    return index;
}

void MainWindow::beginOpenIntoTab()
{
    if (documentIsEmpty(doc()))
        return; // reuse the empty placeholder document (and its tab)
    snapshotActiveView();
    const int index = addDocumentTab();
    m_activeTab = index;
    QSignalBlocker block(m_tabBar);
    m_tabBar->setCurrentIndex(index);
}

void MainWindow::reflectActiveDocument()
{
    // Tab-switch reflect: rebuild every shell surface from the active document
    // WITHOUT resetting its undo history or autosave baseline (bindDocument does
    // that for a fresh open).
    m_brushTarget = BrushTarget::None;
    m_brushUndo.clear();
    m_brushMask = MaskBuffer();

    // Empty placeholder (e.g. after closing the last tab): show the blank canvas
    // rather than the previous document's stale image.
    if (documentIsEmpty(doc())) {
        m_canvas->clearImage();
        updateTitle();
        return;
    }

    refreshWorkingSource();
    refreshBaseImage(false); // fits to window; the saved view is restored below
    recomputeSelectiveMask();
    updateCropView();        // a switched-to document may carry a crop/orientation
    updatePreview();
    if (m_layersPanel->isVisible())
        refreshLayersPanel();
    reseedOpenPanels();
    if (m_presetsPanel && m_presetsPanel->isVisible())
        refreshPresetThumbnails(); // re-render the browser against the new document
    syncViewToggles();
    updateTitle(documentIsEmpty(doc()) ? QString() : documentLabel(doc()));

    // Restore this tab's zoom/pan (refreshBaseImage fit it to window).
    if (doc().viewValid)
        m_canvas->setViewState({doc().viewZoom, doc().viewPan});
}

void MainWindow::switchToTab(int index)
{
    if (index < 0 || index >= tabCount() || index == m_activeTab) {
        QSignalBlocker block(m_tabBar); // keep the bar in sync even on a no-op
        m_tabBar->setCurrentIndex(m_activeTab);
        return;
    }
    snapshotActiveView();
    closeActiveTool(); // a tool/gizmo is tied to the outgoing document

    m_activeTab = index;
    {
        QSignalBlocker block(m_tabBar);
        m_tabBar->setCurrentIndex(index);
    }
    reflectActiveDocument();
}

void MainWindow::closeTab(int index)
{
    if (index < 0 || index >= tabCount())
        return;
    // Guard unsaved work in this tab before discarding it (cancel aborts close).
    if (!maybeSaveBeforeDiscard(*m_docs[index]))
        return;
    Document *d = m_docs[index].get();
    const quint64 id = d->id;

    // A document must not be destroyed while it still has in-flight async work.
    // Signal its workers to bail, then drain any shared watcher whose current job
    // targets it (its finish handler will then drop the result — the doc is gone).
    ++d->healGen;
    ++d->histGen;
    ++d->decodeGen;
    const auto drainIfTargets = [&](auto &watcher, quint64 jobDoc) {
        if (jobDoc == id && watcher.isRunning())
            watcher.waitForFinished();
    };
    drainIfTargets(m_healWatcher, m_healJobDoc);
    drainIfTargets(m_histWatcher, m_histJobDoc);
    drainIfTargets(m_decodeWatcher, m_decodeJobDoc);
    drainIfTargets(m_exportWatcher, m_exportJobDoc);
    drainIfTargets(m_saveWatcher, m_saveJobDoc);
    drainIfTargets(m_openImageWatcher, m_openJobDoc);
    drainIfTargets(m_openProjectWatcher, m_openJobDoc);
    drainIfTargets(m_autosaveWatcher, m_autosaveJobDoc);

    const bool wasActive = (index == m_activeTab);
    if (wasActive)
        closeActiveTool();

    m_docs.erase(m_docs.begin() + index);
    {
        QSignalBlocker block(m_tabBar);
        m_tabBar->removeTab(index);
    }

    // Never leave zero documents: recreate a fresh empty placeholder.
    if (m_docs.empty()) {
        auto nd = std::make_unique<Document>();
        nd->buildBaseGraph();
        m_docs.push_back(std::move(nd));
        m_activeTab = 0;
        QSignalBlocker block(m_tabBar);
        m_tabBar->addTab(documentLabel(doc()));
        m_tabBar->setCurrentIndex(0);
        block.unblock();
        reflectActiveDocument();
        syncTabBar();
        return;
    }

    if (m_activeTab > index)
        --m_activeTab;
    else if (m_activeTab == index)
        m_activeTab = std::min(index, tabCount() - 1);
    {
        QSignalBlocker block(m_tabBar);
        m_tabBar->setCurrentIndex(m_activeTab);
    }
    if (wasActive)
        reflectActiveDocument();
    syncTabBar();
}

void MainWindow::duplicateActiveTab()
{
    if (documentIsEmpty(doc())) {
        showHint(QStringLiteral("Open an image before duplicating"));
        return;
    }
    if (openBusy()) {
        showHint(QStringLiteral("Busy — try again in a moment"));
        return;
    }
    // Snapshot the active document as an in-memory "project" (source bytes + the
    // current edit graph), then decode + install it as a new tab. Re-decoding is
    // what keeps RAW white balance correct: the camera colour profile is a
    // decode-time artefact, not part of the serialised graph.
    QString name;
    OpenProjectResult r;
    r.loaded = true;
    r.sourceBytes = sourceForSave(doc(), &name);
    if (r.sourceBytes.isEmpty()) {
        showHint(QStringLiteral("Nothing to duplicate"));
        return;
    }
    r.sourceName = name;
    r.isRaw = raw::isRawPath(name);
    r.rawOptions = doc().rawOptions;
    r.graph = buildDocGraph(doc());
    r.path.clear(); // a duplicate is new, independent, unsaved work

    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    r.source = r.isRaw ? raw::decodeBytes(r.sourceBytes.constData(), r.sourceBytes.size(),
                                          &err, &r.meta, r.rawOptions)
                       : Image::fromBytes(r.sourceBytes.constData(), r.sourceBytes.size(), &err);
    QGuiApplication::restoreOverrideCursor();
    if (r.source.isNull()) {
        showHint(QStringLiteral("Could not duplicate this image"));
        return;
    }
    // Opens in a new tab (beginOpenIntoTab) with the edits + camera profile; r.path
    // is empty so the copy stays unsaved and independent of the original's file.
    applyProjectResult(r);
    showHint(QStringLiteral("Duplicated to a new tab"));
}

void MainWindow::syncTabBar()
{
    {
        QSignalBlocker block(m_tabBar);
        for (int i = 0; i < tabCount(); ++i)
            m_tabBar->setTabText(i, documentLabel(*m_docs[i]));
        m_tabBar->setCurrentIndex(m_activeTab);
    }
    m_tabBar->setVisible(tabCount() > 1);
    layoutOverlays();
}

void MainWindow::bindDocument(const BindOptions &opts)
{
    // The active canvas edits one document at a time; a fresh bind ends any
    // in-progress paint scratch (it lives on the shell, not the Document).
    m_brushTarget = BrushTarget::None;
    m_brushUndo.clear();
    m_brushMask = MaskBuffer();

    refreshWorkingSource();  // build the corrected source + display QImage
    refreshBaseImage(false); // fit the view (re-applies any heal mask)
    recomputeSelectiveMask();
    if (opts.pushCropView)
        updateCropView();    // push a restored crop/orientation to the canvas
    updatePreview();         // apply any existing edits
    doc().graph.resetHistory(); // fresh undo timeline for this document
    doc().viewValid = false;    // a freshly opened image starts fit-to-window

    // Fresh document session: baseline the just-loaded state (so an unedited open
    // won't autosave) and arm the autosave timer.
    resetAutosaveBaseline(doc());
    startAutosave();
    if (m_layersPanel->isVisible())
        refreshLayersPanel();   // the load may have changed the selective layers
    reseedOpenPanels();         // re-sync open tools with the document's values
    updateTitle(opts.title);
    syncTabBar();               // reflect the new label + show the bar if >1 tab
    if (!opts.hint.isEmpty())
        showHint(opts.hint);
}

void MainWindow::finishOpenImage(const OpenImageResult &r)
{
    if (r.source.isNull()) {
        QMessageBox::warning(this, QStringLiteral("Lumen"),
                             r.error.isEmpty()
                                 ? QStringLiteral("Could not open the image")
                                 : r.error);
        return;
    }

    beginOpenIntoTab(); // reuse the empty placeholder or open a new tab
    doc().initFromImage(r.source, r.bytes, r.path, r.isRaw, r.meta, m_rawLensDefaults);
    BindOptions opts;
    opts.title = QFileInfo(r.path).fileName();
    bindDocument(opts);
}

void MainWindow::redecodeCurrent()
{
    // Only RAW sources can be re-decoded (others have no decode-time options).
    if (doc().sourceBytes.isEmpty() || !raw::isRawPath(doc().sourceName))
        return;

    // Run the demosaic off the UI thread so the app stays responsive and the busy
    // badge animates; the latest request wins (decodeGen guards stale results).
    Document *d = &doc();
    m_decodeJobDoc = d->id;
    const quint64 gen = ++d->decodeGen;
    auto *badge = static_cast<BusyBadge *>(m_healBusy);
    badge->setLabel(QStringLiteral("Decoding…"));
    badge->start();
    layoutOverlays(); // position the badge + dim

    const QByteArray bytes = doc().sourceBytes;
    const raw::RawDecodeOptions opts = doc().rawOptions;
    m_decodeWatcher.setFuture(QtConcurrent::run([d, gen, bytes, opts]() -> DecodeResult {
        if (gen != d->decodeGen)
            return {}; // superseded before we even started
        DecodeResult r;
        r.image = raw::decodeBytes(bytes.constData(), bytes.size(), &r.error, &r.meta, opts);
        return r;
    }));
}

QString MainWindow::promptSaveProjectPath(const Document &d)
{
    // Default name "<source>.lumen", in the last-used project folder (falling
    // back to next-to-the-source on first save).
    const QFileInfo src(d.projectPath.isEmpty() ? d.sourcePath : d.projectPath);
    const QString dir = lastDir(QStringLiteral("saveProject"), src.dir().path());
    const QString suggested =
        QDir(dir).filePath(src.completeBaseName() + QStringLiteral(".lumen"));
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save project"), suggested,
        QStringLiteral("Lumen project (*.lumen)"));
    if (path.isEmpty())
        return {};
    if (!path.endsWith(QStringLiteral(".lumen"), Qt::CaseInsensitive))
        path += QStringLiteral(".lumen");
    return path;
}

void MainWindow::applySaveSuccess(Document &d, const QString &path)
{
    d.projectPath = path;
    rememberDir(QStringLiteral("saveProject"), path);
    // The work now lives in the user's file; autosave targets it from here on and
    // the transient recovery file is no longer needed.
    deleteRecoveryFile(d);
    resetAutosaveBaseline(d);
    startAutosave();
    if (docIsActive(d.id))
        updateTitle(QFileInfo(path).fileName());
    showHint(QStringLiteral("Saved %1").arg(QFileInfo(path).fileName()));
    syncTabBar(); // clear the dirty marker + refresh the label
}

void MainWindow::saveProject()
{
    if (doc().graph.source().isNull()) {
        showHint(QStringLiteral("Open an image before saving a project"));
        return;
    }
    if (m_saveWatcher.isRunning()) {
        showHint(QStringLiteral("A save is already in progress"));
        return;
    }
    // Re-save to the existing file silently; only a first ("fresh") save needs the
    // dialog. Use "Save as…" to write to a new location.
    const QString path =
        doc().projectPath.isEmpty() ? promptSaveProjectPath(doc()) : doc().projectPath;
    if (path.isEmpty())
        return;
    writeProjectAsync(path);
}

void MainWindow::saveProjectAs()
{
    if (doc().graph.source().isNull()) {
        showHint(QStringLiteral("Open an image before saving a project"));
        return;
    }
    if (m_saveWatcher.isRunning()) {
        showHint(QStringLiteral("A save is already in progress"));
        return;
    }
    const QString path = promptSaveProjectPath(doc());
    if (path.isEmpty())
        return;
    writeProjectAsync(path);
}

void MainWindow::writeProjectAsync(const QString &path)
{
    // Snapshot the document on the UI thread (reads the graph), then write it off
    // the UI thread behind the "Saving…" badge — the write can be slow to external
    // or network drives.
    QString name;
    const QByteArray bytes = sourceForSave(doc(), &name);
    const QJsonObject graph = buildDocGraph(doc());

    m_saveJobDoc = doc().id; // the document whose save-state the finish handler updates
    auto *badge = static_cast<BusyBadge *>(m_healBusy);
    badge->setLabel(QStringLiteral("Saving…"));
    badge->start();
    layoutOverlays();
    m_saveWatcher.setFuture(QtConcurrent::run([path, graph, bytes, name]() -> SaveResult {
        SaveResult r;
        r.path = path;
        r.ok = autosave::writeProjectAtomic(path, graph, bytes, name, &r.error);
        return r;
    }));
}

bool MainWindow::saveProjectSync(Document &d)
{
    // The blocking save the quit/discard flow needs: it must complete before the
    // document can be discarded, so it can't go through the async path.
    if (d.graph.source().isNull())
        return true; // nothing to save
    const QString path = promptSaveProjectPath(d);
    if (path.isEmpty())
        return false; // user cancelled the dialog → don't discard their work

    QString name;
    const QByteArray bytes = sourceForSave(d, &name);
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool ok = autosave::writeProjectAtomic(path, buildDocGraph(d), bytes, name, &error);
    QGuiApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Lumen"),
                             QStringLiteral("Could not save project: %1").arg(error));
        return false;
    }
    applySaveSuccess(d, path);
    return true;
}

// --- Autosave & crash recovery ---------------------------------------------

QJsonObject MainWindow::buildDocGraph(const Document &d) const
{
    // The edit graph plus the per-project RAW decode options — the exact document
    // a project file persists. (The graph stays decode-agnostic; loadProjectFile
    // reads the rawOptions key back.)
    QJsonObject graph = d.graph.saveState();
    graph[QStringLiteral("rawOptions")] = d.rawOptions.toJson();
    return graph;
}

QByteArray MainWindow::currentDocBytes(const Document &d) const
{
    return QJsonDocument(buildDocGraph(d)).toJson(QJsonDocument::Compact);
}

QByteArray MainWindow::sourceForSave(const Document &d, QString *name) const
{
    // Embed the original encoded bytes; fall back to a PNG of the source if they
    // aren't available (e.g. a source we only hold as a decoded QImage).
    QByteArray bytes = d.sourceBytes;
    QString outName = d.sourceName;
    if (bytes.isEmpty() && !d.sourceQImage.isNull()) {
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        d.sourceQImage.save(&buf, "PNG");
        outName = QStringLiteral("source.png");
    }
    if (name)
        *name = outName;
    return bytes;
}

void MainWindow::resetAutosaveBaseline(Document &d)
{
    d.openDoc = d.lastAutosaveDoc = currentDocBytes(d);
}

void MainWindow::startAutosave()
{
    // One shared timer drives autosave for every open document (performAutosave
    // sweeps them). Run it whenever any document has an image loaded.
    for (const auto &d : m_docs) {
        if (!d->graph.source().isNull()) {
            m_autosaveTimer->start();
            return;
        }
    }
}

void MainWindow::deleteRecoveryFile(Document &d)
{
    if (d.recoveryPath.isEmpty())
        return;
    QFile::remove(d.recoveryPath);
    QFile::remove(d.recoveryPath + QStringLiteral(".tmp"));
    d.recoveryPath.clear();
}

bool MainWindow::documentIsDirty(const Document &d) const
{
    if (d.graph.source().isNull())
        return false; // empty placeholder — nothing to save
    if (!d.projectPath.isEmpty())
        return false; // saved project, kept current by autosave
    // Unsaved (or recovered): dirty once it differs from the open baseline.
    // openDoc is an empty sentinel for recovered work, so that always reads dirty.
    return currentDocBytes(d) != d.openDoc;
}

void MainWindow::performAutosave()
{
    // One shared writer, so at most one document is autosaved per tick; the next
    // changed document is picked up on the following tick. Prefer the active
    // document so the tab you're editing is saved first.
    if (m_autosaveWatcher.isRunning())
        return;
    if (autosaveDocument(doc()))
        return;
    for (const auto &d : m_docs)
        if (d.get() != &doc() && autosaveDocument(*d))
            return;
}

bool MainWindow::autosaveDocument(Document &d)
{
    // Never persist a transient "show up to here" view — its disabled flags aren't
    // part of the real document.
    if (d.graph.source().isNull() || d.autosaveInFlight || d.peeking)
        return false;
    const QByteArray docBytes = currentDocBytes(d);
    if (docBytes == d.lastAutosaveDoc)
        return false; // nothing changed since the last write
    if (d.projectPath.isEmpty() && docBytes == d.openDoc)
        return false; // opened but never edited — don't spawn a recovery file

    QString target = d.projectPath;
    if (target.isEmpty()) {
        if (d.recoveryPath.isEmpty())
            d.recoveryPath = autosave::newRecoveryPath(
                autosave::projectsDir(), d.sourceName, QDateTime::currentDateTime());
        target = d.recoveryPath;
    }
    if (target.isEmpty())
        return false; // no writable location (projectsDir failed)

    QString name;
    const QByteArray bytes = sourceForSave(d, &name);
    const QJsonObject graph = buildDocGraph(d);
    d.pendingAutosaveDoc = docBytes;
    d.autosaveInFlight = true;
    m_autosaveJobDoc = d.id; // the document whose baseline the finish handler updates
    // Write off the UI thread: embedding the full source can be tens of MB. The
    // snapshot args are value copies, so the worker touches nothing the UI mutates.
    m_autosaveWatcher.setFuture(QtConcurrent::run([target, graph, bytes, name] {
        return autosave::writeProjectAtomic(target, graph, bytes, name);
    }));
    return true;
}

bool MainWindow::flushAutosaveSync(Document &d)
{
    if (d.graph.source().isNull())
        return true;
    // Don't race a background write to the same target.
    if (m_autosaveWatcher.isRunning())
        m_autosaveWatcher.waitForFinished();
    d.autosaveInFlight = false;

    const QByteArray docBytes = currentDocBytes(d);
    if (docBytes == d.lastAutosaveDoc)
        return true; // already persisted
    QString target = d.projectPath.isEmpty() ? d.recoveryPath : d.projectPath;
    if (target.isEmpty()) // unsaved & no recovery file yet → nothing to flush to
        return true;
    QString name;
    const QByteArray bytes = sourceForSave(d, &name);
    QString error;
    if (!autosave::writeProjectAtomic(target, buildDocGraph(d), bytes, name, &error)) {
        QMessageBox::warning(this, QStringLiteral("Lumen"),
                             QStringLiteral("Could not save your work: %1").arg(error));
        return false;
    }
    d.lastAutosaveDoc = docBytes;
    return true;
}

bool MainWindow::maybeSaveBeforeDiscard(Document &d)
{
    if (docIsActive(d.id))
        exitPeek(); // restore the real document before any save / serialise / discard
    if (d.graph.source().isNull())
        return true;
    if (!d.projectPath.isEmpty()) {
        // A saved document is continuously autosaved to the user's file: flush the
        // latest edits and discard silently (no prompt). On a write failure, fall
        // through to the prompt so the user can choose another location.
        if (flushAutosaveSync(d))
            return true;
    } else if (currentDocBytes(d) == d.openDoc) {
        return true; // opened but never edited — nothing to lose
    }

    // Name the document so the prompt is unambiguous when several tabs are open.
    const QString what = documentLabel(d);
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, QStringLiteral("Lumen"),
        QStringLiteral("Do you want to save your work on “%1”?").arg(what),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel)
        return false;
    if (choice == QMessageBox::Discard) {
        deleteRecoveryFile(d);
        return true;
    }
    // Save: route through the dialog synchronously — the document must actually be
    // persisted before we allow the discard, so this can't use the async path.
    return saveProjectSync(d);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    // Guard unsaved work across every open tab. A cancel on any one aborts the
    // whole quit.
    for (const auto &d : m_docs) {
        if (!maybeSaveBeforeDiscard(*d)) {
            e->ignore();
            return;
        }
    }
    // Clean exit: remember the open files for next launch, then drop each session
    // recovery file so the next launch doesn't mistake it for a crash.
    if (m_autosaveTimer)
        m_autosaveTimer->stop();
    saveSession();
    for (const auto &d : m_docs)
        deleteRecoveryFile(*d);
    e->accept();
}

void MainWindow::saveSession()
{
    // Record a reopenable path per document: the .lumen for saved projects, else
    // the original image. Only real, existing files (a recovered document's
    // sourcePath is just a display name, not a path — filtered out here).
    QStringList paths;
    for (const auto &d : m_docs) {
        if (d->graph.source().isNull())
            continue;
        const QString p = d->projectPath.isEmpty() ? d->sourcePath : d->projectPath;
        if (!p.isEmpty() && QFileInfo::exists(p))
            paths << p;
    }
    QSettings().setValue(QStringLiteral("session/openPaths"), paths);
}

bool MainWindow::restoreSession()
{
    const QStringList paths =
        QSettings().value(QStringLiteral("session/openPaths")).toStringList();
    bool any = false;
    for (const QString &p : paths) {
        if (QFileInfo::exists(p)) {
            openPath(p); // first reuses the empty placeholder; the rest queue as tabs
            any = true;
        }
    }
    return any;
}

void MainWindow::openProject()
{
    const QString dir = lastDir(
        QStringLiteral("openProject"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open project"), dir,
        QStringLiteral("Lumen project (*.lumen)"));
    if (path.isEmpty())
        return;
    rememberDir(QStringLiteral("openProject"), path);
    loadProjectFile(path); // opens in a new tab (or reuses the empty placeholder)
}

MainWindow::OpenProjectResult MainWindow::decodeProjectFile(const QString &path)
{
    OpenProjectResult r;
    r.path = path;
    project::Project proj;
    if (!project::load(path, &proj, &r.error))
        return r; // loaded stays false
    r.loaded = true;
    r.graph = proj.graph;
    r.sourceBytes = proj.sourceBytes;
    r.sourceName = proj.sourceName;
    r.isRaw = raw::isRawPath(proj.sourceName);
    // The project's per-project decode options (absent → today's defaults).
    r.rawOptions = proj.graph.contains(QStringLiteral("rawOptions"))
                       ? raw::RawDecodeOptions::fromJson(
                             proj.graph.value(QStringLiteral("rawOptions")).toObject())
                       : raw::RawDecodeOptions{};
    r.source =
        r.isRaw ? raw::decodeBytes(r.sourceBytes.constData(), r.sourceBytes.size(),
                                   &r.error, &r.meta, r.rawOptions)
                : Image::fromBytes(r.sourceBytes.constData(), r.sourceBytes.size(), &r.error);
    return r;
}

bool MainWindow::applyProjectResult(const OpenProjectResult &r)
{
    if (!r.loaded || r.source.isNull())
        return false;

    beginOpenIntoTab(); // reuse the empty placeholder or open a new tab
    doc().initFromProject(r.source, r.sourceBytes, r.sourceName, r.path, r.graph,
                          r.rawOptions, r.isRaw, r.meta.color);
    BindOptions opts;
    opts.pushCropView = true; // a project carries a saved crop/orientation
    opts.title = QFileInfo(r.path).fileName();
    opts.hint = QStringLiteral("Opened %1").arg(QFileInfo(r.path).fileName());
    bindDocument(opts);
    return true;
}

void MainWindow::loadProjectFile(const QString &path)
{
    if (openBusy()) {
        showHint(QStringLiteral("Already opening a file"));
        return;
    }
    // Decode the embedded source off the UI thread behind the "Opening…" badge;
    // the finish handler installs the document.
    auto *badge = static_cast<BusyBadge *>(m_healBusy);
    badge->setLabel(QStringLiteral("Opening…"));
    badge->start();
    layoutOverlays();
    m_openJobDoc = doc().id; // the document this decode will populate on completion
    m_openProjectWatcher.setFuture(
        QtConcurrent::run([path] { return decodeProjectFile(path); }));
}

// --- Reusable presets / copy-paste settings --------------------------------

void MainWindow::copySettings()
{
    if (doc().graph.source().isNull()) {
        showHint(QStringLiteral("Open an image before copying settings"));
        return;
    }
    m_copiedSettings = preset::fromGraph(doc().graph);
    showHint(QStringLiteral("Copied edit settings"));
}

void MainWindow::pasteSettings()
{
    if (m_copiedSettings.isEmpty()) {
        showHint(QStringLiteral("Nothing to paste — copy settings from a photo first"));
        return;
    }
    applyPreset(m_copiedSettings, QStringLiteral("Pasted edit settings"));
}

void MainWindow::savePreset()
{
    if (doc().graph.source().isNull()) {
        showHint(QStringLiteral("Open an image before saving a preset"));
        return;
    }
    const QFileInfo src(doc().sourcePath);
    // Default into the user presets folder so a saved preset immediately shows up
    // in the Presets browser (the user can still navigate elsewhere in the dialog).
    const QString presetsDir = preset::userPresetsDir();
    const QString dir = lastDir(QStringLiteral("preset"),
                                presetsDir.isEmpty() ? src.dir().path() : presetsDir);
    const QString suggested =
        QDir(dir).filePath(src.completeBaseName() + QStringLiteral(".lumenpreset"));
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save preset"), suggested,
        QStringLiteral("Lumen preset (*.lumenpreset)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".lumenpreset"), Qt::CaseInsensitive))
        path += QStringLiteral(".lumenpreset");

    const QString name = QFileInfo(path).completeBaseName();
    QString error;
    if (!preset::save(path, preset::fromGraph(doc().graph, name), &error)) {
        QMessageBox::warning(this, QStringLiteral("Lumen"),
                             QStringLiteral("Could not save preset: %1").arg(error));
        return;
    }
    rememberDir(QStringLiteral("preset"), path);
    invalidatePresetThumbCache(); // may overwrite an existing id with new settings
    if (m_presetsPanel->isVisible())
        refreshPresetThumbnails(); // surface the new preset in the open browser
    showHint(QStringLiteral("Saved preset “%1”").arg(name));
}

void MainWindow::applyPresetFile()
{
    if (doc().graph.source().isNull()) {
        showHint(QStringLiteral("Open an image before applying a preset"));
        return;
    }
    const QString dir = lastDir(
        QStringLiteral("preset"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Apply preset"), dir,
        QStringLiteral("Lumen preset (*.lumenpreset)"));
    if (path.isEmpty())
        return;
    rememberDir(QStringLiteral("preset"), path);

    QJsonObject obj;
    QString error;
    if (!preset::load(path, &obj, &error)) {
        QMessageBox::warning(this, QStringLiteral("Lumen"), error);
        return;
    }
    applyPreset(obj, QStringLiteral("Applied “%1”").arg(QFileInfo(path).completeBaseName()));
}

void MainWindow::applyPreset(const QJsonObject &presetObj, const QString &doneHint)
{
    if (!preset::applyToGraph(presetObj, doc().graph)) {
        showHint(QStringLiteral("That isn't a valid preset"));
        return;
    }
    // One undoable step, then refresh the preview and reseed any open tools — the
    // same path undo/redo uses to reflect a graph change in the UI.
    doc().graph.commit();
    afterHistoryChange();
    showHint(doneHint);
}

bool MainWindow::restoreRecovery(const QString &path)
{
    // Recovery runs at startup and its result gates the launch flow, so decode
    // synchronously (with a wait cursor) rather than through the async open.
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const OpenProjectResult r = decodeProjectFile(path);
    const bool ok = applyProjectResult(r);
    QGuiApplication::restoreOverrideCursor();
    if (!ok) {
        if (!r.loaded)
            QMessageBox::warning(this, QStringLiteral("Lumen"), r.error);
        return false;
    }
    // Loaded as a document, but a recovery file is NOT the user's file: keep it as
    // unsaved work that continues autosaving to this same file and prompts on
    // close (until the user explicitly Saves to a chosen path).
    doc().projectPath.clear();
    doc().recoveryPath = path;
    doc().sourcePath = doc().sourceName; // export naming from the original, not the temp file
    doc().openDoc = QByteArray();    // sentinel: recovered work is treated as unsaved
    updateTitle(QStringLiteral("%1 (recovered)").arg(QFileInfo(doc().sourceName).fileName()));
    showHint(QStringLiteral("Restored unsaved work — save it to keep a copy"));
    return true;
}

bool MainWindow::offerCrashRecovery()
{
    const QStringList files = autosave::findRecoveryFiles(autosave::projectsDir());
    if (files.isEmpty())
        return false;

    // Friendlier prompt: name the single source, or count several.
    QString what;
    if (files.size() == 1) {
        QString sourceLabel;
        project::Project peek;
        if (project::load(files.first(), &peek))
            sourceLabel = peek.sourceName;
        what = sourceLabel.isEmpty() ? QStringLiteral("your unsaved work")
                                     : QStringLiteral("your unsaved work on “%1”").arg(sourceLabel);
    } else {
        what = QStringLiteral("your unsaved work (%1 documents)").arg(files.size());
    }

    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Lumen"));
    box.setIcon(QMessageBox::Question);
    box.setText(QStringLiteral("Lumen didn't shut down cleanly last time."));
    box.setInformativeText(QStringLiteral("Restore %1?").arg(what));
    QPushButton *restore = box.addButton(QStringLiteral("Restore"), QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("Discard"), QMessageBox::DestructiveRole);
    box.setDefaultButton(restore);
    box.exec();

    if (box.clickedButton() != restore) {
        for (const QString &f : files) { // discard every leftover recovery file
            QFile::remove(f);
            QFile::remove(f + QStringLiteral(".tmp"));
        }
        return false;
    }

    // Restore each recovery file as its own tab; each keeps autosaving to its own
    // file (restoreRecovery leaves it live), so nothing is removed here.
    int restored = 0;
    for (const QString &f : files)
        if (restoreRecovery(f))
            ++restored;
    return restored > 0;
}

void MainWindow::openImageDialog()
{
    const QString dir = lastDir(
        QStringLiteral("openImage"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    // Build the filter from a single extension list, including UPPERCASE
    // variants — native file dialogs glob case-sensitively and cameras usually
    // write uppercase RAW extensions (.CR2, .NEF…). An "All files" fallback lets
    // the user pick anything we didn't enumerate.
    QStringList exts{QStringLiteral("jpg"),  QStringLiteral("jpeg"),
                     QStringLiteral("png"),  QStringLiteral("tif"),
                     QStringLiteral("tiff"), QStringLiteral("webp")};
    exts += raw::extensions();
    QStringList patterns;
    for (const QString &e : std::as_const(exts))
        patterns << QStringLiteral("*.%1").arg(e) << QStringLiteral("*.%1").arg(e.toUpper());
    const QString filter = QStringLiteral("Images (%1);;All files (*)")
                               .arg(patterns.join(QLatin1Char(' ')));
    // Our own preview dialog (not the native one) so every format — RAW included
    // — shows a consistent thumbnail as the user browses.
    const QString path =
        ImageOpenDialog::getOpenFileName(this, QStringLiteral("Open image"), dir, filter);
    if (!path.isEmpty()) {
        rememberDir(QStringLiteral("openImage"), path);
        openPath(path);
    }
}

void MainWindow::exportImage()
{
    if (doc().graph.source().isNull()) {
        showHint(QStringLiteral("Open an image before exporting"));
        return;
    }
    if (m_exportWatcher.isRunning() || m_exportPending) {
        showHint(QStringLiteral("An export is already in progress"));
        return;
    }

    // 1. Choose format + quality + size + colour space.
    ExportDialog dlg(this);
    dlg.setSelection(doc().exportExt, doc().exportQuality, doc().exportLongEdge, doc().exportColorSpace);
    if (dlg.exec() != QDialog::Accepted)
        return;
    doc().exportExt = dlg.extension();
    const int quality = dlg.quality();
    if (quality >= 0)
        doc().exportQuality = quality;
    doc().exportLongEdge = dlg.longEdge();
    doc().exportColorSpace = dlg.colorSpace();
    const Image::ExportOptions exportOpts{quality, dlg.bits(), doc().exportLongEdge,
                                          doc().exportColorSpace};

    // 2. Choose the path, defaulting to "<name>-edited.<ext>" in the last-used
    //    export folder (falling back to next-to-the-source).
    const QFileInfo src(doc().sourcePath);
    const QString dir = lastDir(QStringLiteral("export"), src.dir().path());
    const QString suggested = QDir(dir).filePath(
        src.completeBaseName() + QStringLiteral("-edited.") + doc().exportExt);
    const QString filter =
        QStringLiteral("%1 (*.%2)").arg(doc().exportExt.toUpper(), doc().exportExt);
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export image"),
                                                suggested, filter);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".") + doc().exportExt;

    // 3. Show the "Exporting…" badge now, then defer the snapshot and the worker
    //    launch to the next event-loop tick. Returning first lets the Save dialog
    //    actually close and the badge paint before we do any work.
    //    m_exportPending guards the (single-tick) window before the worker starts.
    auto *badge = static_cast<BusyBadge *>(m_healBusy);
    badge->setLabel(QStringLiteral("Exporting…"));
    badge->start();
    layoutOverlays();
    m_exportPending = true;

    QTimer::singleShot(0, this, [this, path, exportOpts]() {
        // Do EVERYTHING heavy on the worker thread — both the full-resolution
        // composite and the encode. Every node's apply() is eager (it materialises
        // pixels via vips_image_write_to_memory), so doc().graph.result() at full res
        // is a multi-second block; running it on the UI thread froze the whole app
        // (the badge animation included). Instead snapshot the graph as JSON (cheap:
        // node params plus PNG-encoded masks) and take a ref to the source, then
        // rebuild an independent EditGraph on the worker and composite there. The
        // worker never touches doc().graph's mutable node cache, so the export stays
        // race-free even if the user keeps editing. The finished handler reports
        // success/failure.
        const QJsonObject graphState = doc().graph.saveState();
        const Image source = doc().graph.source();
        m_exportPending = false;
        m_exportJobDoc = doc().id; // the document being exported
        m_exportWatcher.setFuture(QtConcurrent::run(
            [path, exportOpts, graphState, source]() -> ExportResult {
                ExportResult r;
                r.path = path;
                EditGraph graph;
                graph.setSource(source);
                graph.rebuildFromState(graphState); // independent copy of the stack
                const Image result = graph.result();
                if (result.isNull()) {
                    r.error = QStringLiteral("nothing to export");
                    return r;
                }
                r.ok = result.saveToFile(path, exportOpts, &r.error);
                return r;
            }));
    });
}

void MainWindow::toggleFullScreen()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void MainWindow::updatePreview()
{
    // Before/After: neutralise every shader stage so the original base shows
    // through unedited (the base texture is set to the original in refreshBaseImage).
    if (doc().compareOriginal) {
        m_canvas->setPreviewState(PreviewState{});
        m_canvas->setCurveLuts(identityChannelLuts());
        m_canvas->setLut3D(Lut3D{});
        m_canvas->setVignette(VignetteParams{});
        return;
    }

    // While actively painting a Brush-mask stroke, force the red overlay on so
    // the strokes you paint to *define* the region are visible even before a tone
    // is dialled in. Once the stroke ends this falls back to the explicit "Show
    // mask" toggle, so the user can hide the highlight (and see the layer's
    // opacity/adjustment) by choosing "Show: Off".
    const int overlayView = (doc().maskView != 0) ? doc().maskView : (m_selectivePainting ? 1 : 0);

    PreviewState ps = doc().graph.previewState();
    ps.selMaskView = static_cast<float>(overlayView); // preview-only overlay
    if (overlayView != 0)
        ps.selMaskMode = 1.0f; // overlay samples the uploaded mask texture
    // Fade the explicit "Show mask" highlight by the active layer's opacity so
    // opacity has a visible effect while inspecting the mask. While painting we
    // keep it full strength so the strokes you're drawing stay visible.
    if (!m_selectivePainting && doc().maskView != 0 && doc().graph.activeLayerIndex() > 0)
        ps.selMaskOpacity = doc().graph.activeLayer().opacity();
    if (m_healPainting) {
        // Show the in-progress heal stroke as a red overlay (the mask texture),
        // without any selective adjustment.
        ps.selEnabled = 0.0f;
        ps.selMaskMode = 1.0f;
        ps.selMaskView = 1.0f;
    }
    m_canvas->setPreviewState(ps);
    m_canvas->setCurveLuts(doc().graph.previewLut());
    m_canvas->setLut3D(doc().graph.previewLook());
    m_canvas->setVignette(doc().graph.vignette()); // creative vignette (present pass)

    // A capped-resolution sRGB copy of the source, for evaluating data-driven
    // (luminosity/colour) layer masks; geometric masks ignore it. Building it
    // means a smooth-scale + format convert of the full source, so we only do it
    // when some layer actually carries a mask. Skipping it keeps the live GPU
    // sliders (tone/curves/mono on the Base) instant — they hit updatePreview()
    // every tick and would otherwise stall on this per-tick copy.
    // A layer needs mask evaluation if it has a mask type or an exclusive zone
    // (a zone gates coverage even on a None/geometric mask).
    const auto hasMask = [](const MaskSpec &m) {
        return m.type != MaskSpec::None || !m.zones.empty();
    };
    bool needsMaskSrc = false;
    for (int i = 1; i < doc().graph.layerCount(); ++i) {
        if (hasMask(doc().graph.layer(i).mask())) {
            needsMaskSrc = true;
            break;
        }
    }
    constexpr int cap = 1280;
    QImage maskSrc;
    if (needsMaskSrc && !doc().sourceQImage.isNull()) {
        maskSrc = doc().sourceQImage;
        if (std::max(maskSrc.width(), maskSrc.height()) > cap)
            maskSrc = maskSrc.scaled(cap, cap, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
        maskSrc = maskSrc.convertToFormat(QImage::Format_RGBA8888);
    }
    const int mw = maskSrc.width(), mh = maskSrc.height();
    const uint8_t *maskRgba = maskSrc.isNull() ? nullptr : maskSrc.constBits();

    // Layers above the Base, composited as extra GPU passes.
    const int activeIdx = doc().graph.activeLayerIndex();
    std::vector<LayerPreview> extras;
    for (int i = 1; i < doc().graph.layerCount(); ++i) {
        Layer &layer = doc().graph.layer(i);
        LayerPreview lp;
        layer.contributeToPreview(lp.state);
        layer.contributeToPreviewLut(lp.curves);
        lp.look = layer.look();
        lp.opacity = layer.enabled() ? layer.opacity() : 0.0f;
        // While showing the active layer's mask overlay, suppress that layer's
        // composite so the overlay reads cleanly.
        if (overlayView != 0 && i == activeIdx)
            lp.opacity = 0.0f;
        if (hasMask(layer.mask()) && mw > 0)
            lp.layerMask = evaluateMask(layer.mask(), mw, mh, maskRgba, 4);
        extras.push_back(std::move(lp));
    }
    m_canvas->setExtraLayers(extras);

    // Refresh the histogram a beat after edits settle (it consumes the full
    // composite, so we don't want it on every drag tick).
    if (m_histogram && m_histogram->isVisible())
        m_histTimer->start();
}

TuneNode *MainWindow::activeTune() const
{
    return static_cast<TuneNode *>(doc().graph.activeLayer().nodeOfType(QStringLiteral("tune")));
}

CurvesNode *MainWindow::activeCurves() const
{
    return static_cast<CurvesNode *>(doc().graph.activeLayer().nodeOfType(QStringLiteral("curves")));
}

LutNode *MainWindow::activeLut() const
{
    return static_cast<LutNode *>(doc().graph.activeLayer().nodeOfType(QStringLiteral("lut")));
}

MonoNode *MainWindow::activeMono() const
{
    return static_cast<MonoNode *>(doc().graph.activeLayer().nodeOfType(QStringLiteral("mono")));
}

ColorGradeNode *MainWindow::activeColorGrade() const
{
    return static_cast<ColorGradeNode *>(
        doc().graph.activeLayer().nodeOfType(QStringLiteral("colorgrade")));
}

ColorMixerNode *MainWindow::activeColorMixer() const
{
    return static_cast<ColorMixerNode *>(
        doc().graph.activeLayer().nodeOfType(QStringLiteral("colormixer")));
}

void MainWindow::openLayersTool()
{
    if (m_layersPanel->isVisible())
        hideLayersPanel();
    else
        showLayersPanel();
}

void MainWindow::showLayersPanel()
{
    const bool wasVisible = m_layersPanel->isVisible();
    if (!wasVisible)
        m_layersPanel->move(18, 18); // top-left, opposite the tool panels
    m_layersPanel->show();
    m_layersPanel->raise();
    refreshLayersPanel();
    updateCropView(); // full-frame rule: masks/zones edit the un-oriented frame
}

void MainWindow::hideLayersPanel()
{
    endMaskBrushSession();           // commit any in-progress mask-brush strokes
    m_layersPanel->hide();
    m_canvas->setColorPickMode(false);
    if (m_brushTarget == BrushTarget::Selective) {
        m_brushTarget = BrushTarget::None;
        m_canvas->setBrushMode(false);
    }
    if (doc().maskView != 0) {           // clear the mask overlay
        doc().maskView = 0;
        updatePreview();
    }
    syncMaskGizmo();                 // hide the gizmo
    syncZoneGizmo();                 // hide the zone editor too
    updateCropView();                // back to the cropped browse view
}

void MainWindow::refreshLayersPanel()
{
    QVector<LayersPanel::Row> rows;
    for (int i = 0; i < doc().graph.layerCount(); ++i)
        rows.append({doc().graph.layer(i).name(), doc().graph.layer(i).enabled()});
    const int active = doc().graph.activeLayerIndex();
    m_layersPanel->setLayers(rows, active,
                             static_cast<int>(std::lround(doc().graph.activeLayer().opacity() * 100)));
    m_layersPanel->setMaskState(doc().graph.activeLayer().mask(), active == 0, m_brushSize,
                                m_brushHardness, m_brushAdd, doc().maskView);
    syncMaskGizmo();
    syncZoneGizmo();
    updateMaskEditing();
    recomputeSelectiveMask(); // keep the overlay in sync with the active layer
}

void MainWindow::reseedOpenPanels()
{
    // Guarded: a layer may not carry every node type (e.g. a selective layer).
    if (m_tonePanel->isVisible()) {
        if (auto *t = activeTune())
            m_tonePanel->reveal(toneValuesOf(t));
    }
    if (m_curvesPanel->isVisible()) {
        if (auto *c = activeCurves())
            m_curvesPanel->reveal(c->curves());
    }
    if (m_looksPanel->isVisible()) {
        if (auto *l = activeLut())
            m_looksPanel->reveal(QFileInfo(l->sourcePath()).fileName(), l->intensity());
    }
    if (m_monoPanel->isVisible()) {
        if (auto *mono = activeMono())
            m_monoPanel->reveal(mono->values());
    }
    if (m_colorMixerPanel->isVisible()) {
        if (auto *cm = activeColorMixer())
            m_colorMixerPanel->reveal(cm->values());
    }
    if (m_colorGradePanel->isVisible()) {
        if (auto *g = activeColorGrade())
            m_colorGradePanel->reveal(g->values());
    }
}

Layer &MainWindow::addMaskedAdjustmentLayer(const QString &name)
{
    Layer &layer = doc().graph.addLayer(name);
    // Full adjustment node set so any tool (Tone/Mixer/Curves/Grade/Looks/Mono)
    // works on the layer (added to it as it is now the active layer).
    doc().graph.addNode(std::make_unique<TuneNode>());
    doc().graph.addNode(std::make_unique<ColorMixerNode>());
    doc().graph.addNode(std::make_unique<CurvesNode>());
    doc().graph.addNode(std::make_unique<ColorGradeNode>());
    doc().graph.addNode(std::make_unique<LutNode>());
    doc().graph.addNode(std::make_unique<MonoNode>());
    // Start with a full-range luminosity mask (a no-op until dialled in) so the
    // panel's mask editor — including the Shadows/Midtones/Highlights presets — is
    // available immediately, matching the "Selective adjustment" command.
    MaskSpec mask;
    mask.type = MaskSpec::Luminosity;
    layer.setMask(mask);
    return layer;
}

void MainWindow::addAdjustmentLayer()
{
    addMaskedAdjustmentLayer(QStringLiteral("Layer %1").arg(doc().graph.layerCount()));
    refreshLayersPanel();
    updatePreview();
    doc().graph.commit();
}

void MainWindow::deleteActiveLayer()
{
    if (doc().graph.removeLayer(doc().graph.activeLayerIndex())) {
        refreshLayersPanel();
        updatePreview();
        doc().graph.commit();
    }
}

void MainWindow::selectLayer(int index)
{
    endMaskBrushSession(); // commit the current layer's mask-brush before switching
    doc().graph.setActiveLayer(index);
    refreshLayersPanel();
    // Reseed any open adjustment tool with the newly-active layer's values
    // (guarded — a layer may not carry every node type, e.g. a selective layer).
    if (m_tonePanel->isVisible()) {
        if (auto *t = activeTune())
            m_tonePanel->reveal(toneValuesOf(t));
    }
    if (m_curvesPanel->isVisible()) {
        if (auto *c = activeCurves())
            m_curvesPanel->reveal(c->curves());
    }
    if (m_looksPanel->isVisible()) {
        if (auto *l = activeLut())
            m_looksPanel->reveal(QFileInfo(l->sourcePath()).fileName(), l->intensity());
    }
    if (m_monoPanel->isVisible()) {
        if (!activeMono()) {
            doc().graph.addNode(std::make_unique<MonoNode>());
            doc().graph.commit();
        }
        m_monoPanel->reveal(activeMono()->values());
    }
    if (m_colorMixerPanel->isVisible()) {
        if (!activeColorMixer()) {
            doc().graph.addNode(std::make_unique<ColorMixerNode>());
            doc().graph.commit();
        }
        m_colorMixerPanel->reveal(activeColorMixer()->values());
    }
    if (m_colorGradePanel->isVisible()) {
        if (!activeColorGrade()) {
            doc().graph.addNode(std::make_unique<ColorGradeNode>());
            doc().graph.commit();
        }
        m_colorGradePanel->reveal(activeColorGrade()->values());
    }
}

void MainWindow::setActiveLayerMaskType(int maskType)
{
    if (doc().graph.activeLayerIndex() == 0)
        return; // the Base layer has no mask
    MaskSpec spec = doc().graph.activeLayer().mask();
    const auto type = static_cast<MaskSpec::Type>(maskType);
    if (spec.type == type)
        return;

    endMaskBrushSession(); // committing the previous brush if we're leaving it

    // Seed sensible defaults when first switching to a given mask type.
    if (type == MaskSpec::LinearGradient && spec.type != MaskSpec::LinearGradient) {
        spec.gradFrom = {0.2, 0.5};
        spec.gradTo = {0.8, 0.5};
    } else if (type == MaskSpec::Radial && spec.type != MaskSpec::Radial) {
        spec.center = {0.5, 0.5};
        spec.radiusX = 0.3f;
        spec.radiusY = 0.3f;
    } else if (type == MaskSpec::Brush && spec.brush.isEmpty()) {
        initBrushMask();
        spec.brush = m_brushMask;
    }
    spec.type = type;
    doc().graph.activeLayer().setMask(spec);

    m_layersPanel->setMaskState(spec, /*isBaseActive=*/false, m_brushSize, m_brushHardness,
                                m_brushAdd, doc().maskView);
    syncMaskGizmo();
    syncZoneGizmo();
    updateMaskEditing(); // turn the canvas brush on/off for a Brush mask
    recomputeSelectiveMask();
    updatePreview();
    doc().graph.commit();
}

void MainWindow::onLayerMaskEdited(const MaskSpec &spec, bool commit)
{
    if (doc().graph.activeLayerIndex() == 0)
        return;
    doc().graph.activeLayer().setMask(spec);
    recomputeSelectiveMask(); // redraw the overlay as the gizmo drags (if shown)
    updatePreview();
    if (commit)
        doc().graph.commit();
}

void MainWindow::syncMaskGizmo()
{
    // The gizmo shows itself only for gradient/radial masks on a non-Base layer —
    // and never while the user has hidden the overlay geometry.
    if (doc().overlaysHidden || doc().graph.activeLayerIndex() == 0)
        m_maskGizmo->setSpec(MaskSpec{}); // None → hides
    else
        m_maskGizmo->setSpec(doc().graph.activeLayer().mask());
    layoutOverlays(); // re-apply gizmo geometry + z-order
}

void MainWindow::syncZoneGizmo()
{
    // Zones are edited only on a non-Base layer while the Layers panel (which
    // hosts the zone controls) is open, and never while overlays are hidden.
    const bool active = !doc().overlaysHidden && doc().graph.activeLayerIndex() != 0
                        && m_layersPanel->isVisible();
    if (!active) {
        m_zoneGizmo->setVisible(false);
        layoutOverlays();
        return;
    }
    m_zoneGizmo->setShapes(doc().graph.activeLayer().mask().zones);
    m_zoneGizmo->setVisible(true);
    layoutOverlays();
}

void MainWindow::setOverlayGeometryVisible(bool visible)
{
    doc().overlaysHidden = !visible;
    m_layersPanel->setOverlayVisible(visible); // keep the panel toggle in sync
    syncMaskGizmo();
    syncZoneGizmo();
    showHint(visible ? QStringLiteral("Overlay shapes shown")
                     : QStringLiteral("Overlay shapes hidden"));
}

void MainWindow::openToneTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    // Default position: top-right with a margin. The user can drag it from here.
    m_tonePanel->adjustSize();
    const int margin = 18;
    m_tonePanel->move(width() - m_tonePanel->width() - margin, margin);
    m_tonePanel->reveal(toneValuesOf(activeTune()));
}

void MainWindow::closeToneTool()
{
    m_tonePanel->hide();
    doc().graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openCurvesTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    m_curvesPanel->adjustSize();
    const int margin = 18;
    m_curvesPanel->move(width() - m_curvesPanel->width() - margin, margin);
    m_curvesPanel->reveal(activeCurves()->curves());
}

void MainWindow::closeCurvesTool()
{
    m_curvesPanel->hide();
    doc().graph.commit();
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openLooksTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    m_looksPanel->adjustSize();
    const int margin = 18;
    m_looksPanel->move(width() - m_looksPanel->width() - margin, margin);
    m_looksPanel->reveal(QFileInfo(activeLut()->sourcePath()).fileName(), activeLut()->intensity());
}

void MainWindow::closeLooksTool()
{
    m_looksPanel->hide();
    doc().graph.commit();
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openPresetsTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    m_presetsPanel->setAmount(doc().presetAmount); // reflect the current blend
    refreshPresetThumbnails();                 // preview each look against the current photo
    m_presetsPanel->adjustSize();
    const int margin = 18;
    m_presetsPanel->move(width() - m_presetsPanel->width() - margin, margin);
    m_presetsPanel->reveal();
}

void MainWindow::closePresetsTool()
{
    m_presetsPanel->hide();
    doc().graph.commit(); // capture any live Amount-slider change as one undo step
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::invalidatePresetThumbCache()
{
    doc().thumbCache.clear();
    doc().thumbCacheSig.clear();
}

void MainWindow::refreshPresetThumbnails()
{
    const QVector<preset::Builtin> presets = preset::library();
    QVector<PresetsPanel::Item> items;
    items.reserve(presets.size());

    // Thumbnails depend only on the source, the Base-layer edits and the crop (the
    // active preset layer is suppressed during the render and each preset swaps in
    // its own vignette/structure). Build a signature from exactly those inputs; when
    // it matches the last render we reuse cached pixmaps and only render presets the
    // cache is missing (e.g. a newly-saved user preset).
    const QByteArray signature = [this] {
        QJsonObject sig;
        sig.insert(QStringLiteral("src"), QString::number(doc().sourceGeneration));
        sig.insert(QStringLiteral("base"), doc().graph.baseLayer().saveState());
        sig.insert(QStringLiteral("crop"), doc().graph.crop().toJson());
        return QJsonDocument(sig).toJson(QJsonDocument::Compact);
    }();
    if (signature != doc().thumbCacheSig) {
        doc().thumbCache.clear();
        doc().thumbCacheSig = signature;
    }

    // Render each preset exactly as applying it would: on a temporary full-coverage
    // layer at 100% over the current photo (tone/colour/grain), plus the preset's
    // graph-global stages (vignette, and structure on the Base node) swapped in for
    // the render — so the thumbnail matches the applied result.
    const bool haveSource = !doc().graph.source().isNull();
    // Suppress the active preset layer so thumbnails show each preset alone rather
    // than stacked on top of whichever preset is applied right now.
    const int presetIdx = presetLayerIndex();
    const float savedOpacity = presetIdx >= 0 ? doc().graph.layer(presetIdx).opacity() : 0.0f;
    if (presetIdx >= 0)
        doc().graph.layer(presetIdx).setOpacity(0.0f);
    const int savedActive = doc().graph.activeLayerIndex();
    // Vignette (graph-global) and structure (a Base node) can't ride the temp
    // layer, so swap each preset's own in for its render, then restore afterwards.
    const VignetteParams savedVignette = doc().graph.vignette();
    const StructureNode::Values savedStructure =
        doc().structure ? doc().structure->values() : StructureNode::Values{};
    constexpr int kThumbDim = 200;

    for (const preset::Builtin &b : presets) {
        PresetsPanel::Item item{b.id, b.name, b.category, {},
                                b.id.startsWith(QStringLiteral("user:")),
                                b.id == doc().activePresetId};
        if (haveSource) {
            auto cached = doc().thumbCache.constFind(b.id);
            if (cached != doc().thumbCache.constEnd()) {
                item.thumb = cached.value();
            } else {
                Layer &temp = addPresetLayer(QStringLiteral("__thumb"));
                const int tempIdx = doc().graph.activeLayerIndex();
                preset::applyToLayer(b.data, temp); // tone/colour/grain onto the layer
                temp.setOpacity(1.0f);
                doc().graph.setVignette(preset::vignetteOf(b.data)); // and its vignette
                if (doc().structure)
                    doc().structure->setValues(structureFromPreset(b.data)); // and its structure
                const QImage img = doc().graph.resultDownsampled(kThumbDim).toQImage();
                if (!img.isNull()) {
                    item.thumb = QPixmap::fromImage(img);
                    doc().thumbCache.insert(b.id, item.thumb);
                }
                doc().graph.removeLayer(tempIdx);
            }
        }
        items.push_back(item);
    }

    doc().graph.setActiveLayer(savedActive);
    doc().graph.setVignette(savedVignette);
    if (doc().structure)
        doc().structure->setValues(savedStructure);
    if (presetIdx >= 0)
        doc().graph.layer(presetIdx).setOpacity(savedOpacity);

    m_presetsPanel->setItems(items);
}

int MainWindow::presetLayerIndex() const
{
    for (int i = 1; i < doc().graph.layerCount(); ++i)
        if (doc().graph.layer(i).name().startsWith(kPresetLayerPrefix))
            return i;
    return -1;
}

Layer &MainWindow::addPresetLayer(const QString &name)
{
    // Full-coverage (None mask) adjustment layer, so its opacity blends the whole
    // preset look uniformly. Carries every creative node the preset can drive —
    // including grain, so grain rides the same opacity blend as the tone/colour.
    Layer &layer = doc().graph.addLayer(name);
    doc().graph.addNode(std::make_unique<TuneNode>());
    doc().graph.addNode(std::make_unique<ColorMixerNode>());
    doc().graph.addNode(std::make_unique<CurvesNode>());
    doc().graph.addNode(std::make_unique<ColorGradeNode>());
    doc().graph.addNode(std::make_unique<LutNode>());
    doc().graph.addNode(std::make_unique<MonoNode>());
    doc().graph.addNode(std::make_unique<GrainNode>());
    return layer;
}

void MainWindow::applyPresetLook(const preset::Builtin &b, int amountPct)
{
    if (doc().graph.source().isNull()) {
        showHint(QStringLiteral("Open an image before applying a preset"));
        return;
    }
    // Replace any existing preset layer with a fresh one carrying this preset.
    // Snapshot the pre-preset vignette as the blend baseline, but only when no
    // preset is currently applied — when swapping one preset for another the graph
    // vignette is already the outgoing preset's blend, not the user's original.
    const int existing = presetLayerIndex();
    if (existing < 0) {
        doc().presetBaselineVignette = doc().graph.vignette();
        doc().presetBaselineStructure = doc().structure ? doc().structure->values() : StructureNode::Values{};
    }
    if (existing >= 0)
        doc().graph.removeLayer(existing);
    Layer &layer = addPresetLayer(kPresetLayerPrefix + b.name);
    preset::applyToLayer(b.data, layer);
    layer.setOpacity(std::clamp(amountPct, 0, 100) / 100.0f);
    doc().graph.setActiveLayer(0); // leave editing focused on the Base, not the preset

    doc().activePresetId = b.id;
    doc().presetAmount = amountPct;
    doc().presetVignette = preset::vignetteOf(b.data);
    doc().presetStructure = structureFromPreset(b.data);
    applyPresetVignette(amountPct);  // fold the preset's vignette into the blend
    applyPresetStructure(amountPct); // and its local-contrast (baked on the Base)
    doc().graph.commit();
    afterHistoryChange();     // rebuild the preview (incl. the baked structure) + reseed tools
    refreshLayersPanel();     // the new preset layer shows in the stack
    refreshPresetThumbnails(); // re-render (baseline may have shifted)
    showHint(QStringLiteral("Applied “%1”").arg(b.name));
}

void MainWindow::setPresetAmount(int amountPct)
{
    doc().presetAmount = amountPct;
    const int idx = presetLayerIndex();
    if (idx < 0)
        return; // no preset applied yet; the value seeds the next apply
    doc().graph.layer(idx).setOpacity(std::clamp(amountPct, 0, 100) / 100.0f);
    applyPresetVignette(amountPct); // keep the vignette in step with the blend
    const bool structureChanged = applyPresetStructure(amountPct);
    updatePreview(); // live; one undo step is committed when the tool closes
    if (structureChanged) {
        // Structure is baked; coalesce the (expensive) full-res re-bake so dragging
        // the Amount slider stays smooth — it fires once the slider settles.
        m_bakeOp = BakeOp::Structure;
        m_bakeTimer->start();
    }
    if (m_layersPanel->isVisible())
        refreshLayersPanel();
}

void MainWindow::applyPresetVignette(int amountPct)
{
    // Only steer the vignette when a preset was applied this session — otherwise
    // the baseline/preset snapshots are empty and we'd wipe a vignette that was
    // loaded with a project (whose preset context isn't restored).
    if (doc().activePresetId.isEmpty())
        return;
    const float t = std::clamp(amountPct, 0, 100) / 100.0f;
    const VignetteParams v = blendVignette(doc().presetBaselineVignette, doc().presetVignette, t);
    doc().graph.setVignette(v);
    m_canvas->setVignette(v); // present-pass op; no base re-bake needed
}

bool MainWindow::applyPresetStructure(int amountPct)
{
    // As with the vignette, only steer structure when a preset is active this
    // session. Returns whether the Base structure node actually changed (so the
    // caller knows a re-bake is needed).
    if (doc().activePresetId.isEmpty() || !doc().structure)
        return false;
    const float t = std::clamp(amountPct, 0, 100) / 100.0f;
    const StructureNode::Values v =
        blendStructure(doc().presetBaselineStructure, doc().presetStructure, t);
    const bool changed = v != doc().structure->values();
    doc().structure->setValues(v);
    return changed;
}

void MainWindow::openMonoTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    // Most layers carry a mono node; a layer added by another tool (e.g. a
    // selective layer) may not, so add one on demand.
    if (!activeMono()) {
        doc().graph.addNode(std::make_unique<MonoNode>());
        doc().graph.commit();
    }
    m_monoPanel->adjustSize();
    const int margin = 18;
    m_monoPanel->move(width() - m_monoPanel->width() - margin, margin);
    m_monoPanel->reveal(activeMono()->values());
}

void MainWindow::closeMonoTool()
{
    m_monoPanel->hide();
    doc().graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openColorMixerTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    // Most layers carry a colour-mixer node; a layer added by another tool may
    // not, so add one on demand.
    if (!activeColorMixer()) {
        doc().graph.addNode(std::make_unique<ColorMixerNode>());
        doc().graph.commit();
    }
    m_colorMixerPanel->adjustSize();
    const int margin = 18;
    m_colorMixerPanel->move(width() - m_colorMixerPanel->width() - margin, margin);
    m_colorMixerPanel->reveal(activeColorMixer()->values());
}

void MainWindow::closeColorMixerTool()
{
    m_colorMixerPanel->hide();
    doc().graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openColorGradeTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    // A layer added by another tool may not carry a colour-grade node yet.
    if (!activeColorGrade()) {
        doc().graph.addNode(std::make_unique<ColorGradeNode>());
        doc().graph.commit();
    }
    m_colorGradePanel->adjustSize();
    const int margin = 18;
    m_colorGradePanel->move(width() - m_colorGradePanel->width() - margin, margin);
    m_colorGradePanel->reveal(activeColorGrade()->values());
}

void MainWindow::closeColorGradeTool()
{
    m_colorGradePanel->hide();
    doc().graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openLensTool()
{
    if (!doc().lens)
        return;
    m_input.setMode(InputController::Mode::ToolActive);
    m_lensPanel->adjustSize();
    const int margin = 18;
    m_lensPanel->move(width() - m_lensPanel->width() - margin, margin);
    const LensCorrectionNode::Params &p = doc().lens->params();
    // Show the matched Lensfun profile name (which, for fixed-lens compacts,
    // comes from the camera rather than an EXIF lens string).
    m_lensPanel->reveal(p, doc().lens->lensMatched(), doc().lens->matchedLensName());
}

void MainWindow::closeLensTool()
{
    m_lensPanel->hide();
    doc().graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openSharpenTool()
{
    if (!doc().sharpen)
        return;
    m_input.setMode(InputController::Mode::ToolActive);
    m_sharpenPanel->adjustSize();
    const int margin = 18;
    m_sharpenPanel->move(width() - m_sharpenPanel->width() - margin, margin);
    m_sharpenPanel->reveal(doc().sharpen->values());
}

void MainWindow::closeSharpenTool()
{
    m_sharpenPanel->hide();
    doc().graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openStructureTool()
{
    if (!doc().structure)
        return;
    m_input.setMode(InputController::Mode::ToolActive);
    m_structurePanel->adjustSize();
    const int margin = 18;
    m_structurePanel->move(width() - m_structurePanel->width() - margin, margin);
    m_structurePanel->reveal(doc().structure->values());
}

void MainWindow::closeStructureTool()
{
    m_structurePanel->hide();
    doc().graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openGrainTool()
{
    if (!doc().grain)
        return;
    m_input.setMode(InputController::Mode::ToolActive);
    m_grainPanel->adjustSize();
    const int margin = 18;
    m_grainPanel->move(width() - m_grainPanel->width() - margin, margin);
    m_grainPanel->reveal(doc().grain->values());
}

void MainWindow::closeGrainTool()
{
    m_grainPanel->hide();
    doc().graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openVignetteTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    m_vignettePanel->adjustSize();
    const int margin = 18;
    m_vignettePanel->move(width() - m_vignettePanel->width() - margin, margin);
    m_vignettePanel->reveal(doc().graph.vignette());
}

void MainWindow::closeVignetteTool()
{
    m_vignettePanel->hide();
    doc().graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::updateTitle(const QString &document)
{
    if (document.isEmpty())
        setWindowTitle(QStringLiteral("Lumen %1").arg(QStringLiteral(LUMEN_VERSION)));
    else
        setWindowTitle(QStringLiteral("Lumen — %1").arg(document));
}

double MainWindow::sourceAspect() const
{
    if (doc().sourceQImage.isNull() || doc().sourceQImage.height() == 0)
        return 1.0;
    return static_cast<double>(doc().sourceQImage.width())
           / static_cast<double>(doc().sourceQImage.height());
}

QRectF MainWindow::straightenSafeCropRect(const CropState &c) const
{
    if (std::abs(c.straighten) < 1e-6)
        return QRectF(0.0, 0.0, 1.0, 1.0);
    const bool swap = (c.rotation == 90 || c.rotation == 270);
    const double sw = doc().sourceQImage.width();
    const double sh = doc().sourceQImage.height();
    const double ow = swap ? sh : sw; // oriented frame dims (pre-straighten)
    const double oh = swap ? sw : sh;
    return straightenSafeRect(c.straighten, ow, oh, m_cropAspect);
}

void MainWindow::openCropTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    m_cropPanel->adjustSize();
    const int margin = 18;
    m_cropPanel->move(width() - m_cropPanel->width() - margin, margin);
    m_cropPanel->reveal(doc().graph.crop(), sourceAspect());
    m_cropAspect = 0.0;
    m_cropGizmo->setAspect(0.0); // free until the user picks a preset
    m_cropGizmo->setStraighten(doc().graph.crop().straighten);
    m_cropGizmo->setRect(doc().graph.crop().rect);
    updateCropView(); // switches the canvas into Editing (full-frame) mode
    m_canvas->setFitZoom(0.85f); // pull the frame in so the handles clear the edges
    m_cropGizmo->setGeometry(m_canvas->geometry());
    m_cropGizmo->show();
    m_cropGizmo->raise();
    m_cropPanel->raise(); // keep the panel above the gizmo so its buttons stay clickable
}

void MainWindow::closeCropTool()
{
    m_cropPanel->hide();
    m_cropGizmo->hide();
    doc().graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    updateCropView(); // Applied (if cropped) or None
    m_canvas->resetView(); // fit the (now cropped) result to the window
    updatePreview();
    m_canvas->setFocus();
}

void MainWindow::updateCropView()
{
    CanvasWidget::CropViewMode mode;
    if (m_cropPanel->isVisible()) {
        // The crop tool itself wants the full oriented frame to edit against.
        mode = CanvasWidget::CropEditing;
    } else if (m_layersPanel->isVisible() || m_healPanel->isVisible()) {
        // Gizmo/pick tools (mask/zone/heal/eyedropper) operate against the full,
        // un-cropped frame. Use CropMaskEdit rather than CropNone so the user's
        // orientation (rotation/flip) stays applied on screen while the canvas
        // still maps coordinates back to the un-oriented source the masks live in.
        mode = CanvasWidget::CropMaskEdit;
    } else if (!doc().graph.crop().isIdentity() && doc().graph.crop().enabled) {
        mode = CanvasWidget::CropApplied;
    } else {
        // No crop, or the crop is toggled off in the Adjustments panel → full frame.
        mode = CanvasWidget::CropNone;
    }
    m_canvas->setCropState(doc().graph.crop(), mode);
}

void MainWindow::openDenoiseTool()
{
    if (!doc().denoise)
        return;
    m_input.setMode(InputController::Mode::ToolActive);
    m_denoisePanel->adjustSize();
    const int margin = 18;
    m_denoisePanel->move(width() - m_denoisePanel->width() - margin, margin);
    m_denoisePanel->reveal(doc().denoise->values());
}

void MainWindow::closeDenoiseTool()
{
    m_denoisePanel->hide();
    doc().graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openDefringeTool()
{
    if (!doc().defringe)
        return;
    m_input.setMode(InputController::Mode::ToolActive);
    m_defringePanel->adjustSize();
    const int margin = 18;
    m_defringePanel->move(width() - m_defringePanel->width() - margin, margin);
    m_defringePanel->reveal(doc().defringe->values());
}

void MainWindow::closeDefringeTool()
{
    m_defringePanel->hide();
    doc().graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openRawTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    m_rawPanel->adjustSize();
    const int margin = 18;
    m_rawPanel->move(width() - m_rawPanel->width() - margin, margin);
    m_rawPanel->reveal(doc().rawOptions, m_rawLensDefaults);
}

void MainWindow::closeRawTool()
{
    m_rawPanel->hide();
    // Flush a pending debounced re-decode so closing doesn't drop the last change
    // (it runs in the background, with the busy badge).
    if (m_redecodeTimer->isActive()) {
        m_redecodeTimer->stop();
        redecodeCurrent();
    }
    doc().graph.commit(); // capture any lens-correction toggle as one undo step
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::toggleHistogram()
{
    if (!m_histogram)
        return;
    if (m_histogram->isVisible()) {
        m_histogram->hide();
        syncViewToggles();
        return;
    }
    m_histogram->show();
    m_histogram->raise();
    layoutOverlays();   // place it
    updateHistogram();  // fill it immediately
    syncViewToggles();
}

void MainWindow::toggleClipping()
{
    doc().showClipping = !doc().showClipping;
    m_canvas->setClipping(doc().showClipping);
    showHint(doc().showClipping ? QStringLiteral("Clipping warnings on — red = highlights, blue = shadows")
                            : QStringLiteral("Clipping warnings off"));
    syncViewToggles();
}

void MainWindow::syncViewToggles()
{
    if (!m_viewToggles)
        return;
    m_histToggleBtn->setChecked(m_histogram && m_histogram->isVisible());
    m_clipToggleBtn->setChecked(doc().showClipping);
    m_historyToggleBtn->setChecked(m_adjustmentsPanel && m_adjustmentsPanel->isVisible());
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Keep a panel's ✕ pinned to its top-right corner when the panel resizes.
    if (event->type() == QEvent::Resize) {
        if (auto it = m_panelClose.constFind(static_cast<QWidget *>(watched));
            it != m_panelClose.constEnd()) {
            QWidget *p = static_cast<QWidget *>(watched);
            QPushButton *btn = it.value();
            btn->move(p->width() - btn->width() - 8, 8);
            btn->raise();
        }
        // fall through (don't consume the resize)
    }

    // Drag the persistent overlays: the histogram by its surface, the view-toggle
    // cluster by its grip (moving the whole cluster). Delta math in global coords
    // is independent of which child delivered the event.
    QWidget *target = nullptr;
    bool *moved = nullptr;
    if (watched == m_histogram) {
        target = m_histogram;
        moved = &m_histMoved;
    } else if (watched == m_clusterGrip) {
        target = m_viewToggles;
        moved = &m_clusterMoved;
    }

    if (target) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_draggingOverlay = target;
                m_overlayDragStartGlobal = me->globalPosition().toPoint();
                m_overlayStartPos = target->pos();
                return true;
            }
            break;
        }
        case QEvent::MouseMove:
            if (m_draggingOverlay == target) {
                auto *me = static_cast<QMouseEvent *>(event);
                const QPoint delta = me->globalPosition().toPoint() - m_overlayDragStartGlobal;
                QPoint p = m_overlayStartPos + delta;
                p.setX(std::clamp(p.x(), 0, std::max(0, width() - target->width())));
                p.setY(std::clamp(p.y(), 0, std::max(0, height() - target->height())));
                target->move(p);
                *moved = true;
                return true;
            }
            break;
        case QEvent::MouseButtonRelease:
            if (m_draggingOverlay == target) {
                m_draggingOverlay = nullptr;
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// Longest edge the histogram is computed at — plenty for a 256-bin plot and cheap
// to recompute live.
static constexpr int kHistogramMaxDim = 512;

void MainWindow::updateHistogram()
{
    if (!m_histogram || !m_histogram->isVisible() || doc().graph.source().isNull())
        return;
    // Don't overlap an in-flight bake — building result() would be a second
    // concurrent full-res inpaint. Defer; the bake's finished handler retriggers.
    if (m_healWatcher.isRunning() || m_decodeWatcher.isRunning())
        return;

    Document *d = &doc();
    m_histJobDoc = d->id;
    const quint64 gen = ++d->histGen;
    const bool healActive =
        doc().heal && doc().heal->isEnabled() && !doc().heal->healMask().isEmpty();
    if (healActive) {
        // result() is EAGER when a heal mask is active — HealNode::apply runs the
        // inpaint inline (see HealNode.cpp), so it must NEVER be built on the UI
        // thread (that froze the app when toggling the histogram mid-heal). Build
        // and materialise it on a worker. No bake is in flight (guarded above), so
        // the graph is quiescent for the snapshot.
        m_histWatcher.setFuture(QtConcurrent::run([d, gen]() -> HistogramData {
            if (gen != d->histGen)
                return HistogramData{};
            const Image result = d->graph.result();
            if (result.isNull() || gen != d->histGen)
                return HistogramData{};
            return computeHistogram(result);
        }));
    } else {
        // No eager node: composite over a cached downsampled source so each
        // recompute is cheap (pointwise edits are resolution-independent). Build
        // the snapshot on the UI thread (race-free) and materialise on the worker.
        const Image result = doc().graph.resultDownsampled(kHistogramMaxDim);
        if (result.isNull())
            return;
        m_histWatcher.setFuture(QtConcurrent::run([d, gen, result]() -> HistogramData {
            if (gen != d->histGen)
                return HistogramData{}; // superseded
            return computeHistogram(result, kHistogramMaxDim);
        }));
    }
}

void MainWindow::rebuildPreviewFromGraph()
{
    refreshWorkingSource();  // re-apply lens (honours its toggle)
    refreshBaseImage(true);  // re-bake heal/denoise/defringe/sharpen, keep the view
    recomputeSelectiveMask();
    updateCropView();        // reflect crop on/off
    updatePreview();         // pointwise/look/vignette uniforms
}

const QImage &MainWindow::originalImage()
{
    // The decoded source with no edits at all (not even lens), cached. Invalidated
    // in refreshWorkingSource whenever the source/lens changes.
    if (doc().originalQImage.isNull() && !doc().graph.source().isNull())
        doc().originalQImage = doc().graph.source().toQImage();
    return doc().originalQImage;
}

void MainWindow::setCompareOriginal(bool on)
{
    if (doc().compareOriginal == on || doc().graph.source().isNull())
        return;
    if (on && doc().peeking)
        exitPeek(); // compare against the real edited image, not a peeked view
    doc().compareOriginal = on;
    refreshBaseImage(true); // swap the base texture (original ↔ edited)
    updatePreview();        // swap the shader stage (identity ↔ edits)
    if (m_adjustmentsPanel->isVisible())
        rebuildAdjustments(); // keep the panel's Before/After toggle in sync (e.g. '\')
    showHint(on ? QStringLiteral("Before (original)") : QStringLiteral("After (edited)"));
}

// --- Adjustments panel -----------------------------------------------------

bool MainWindow::nodeIsActive(const EditNode *node) const
{
    if (!node)
        return false;
    std::unique_ptr<EditNode> def = createNode(node->typeName());
    if (!def)
        return false;
    // "Active" = parameters differ from a neutral node of the same type. Ignore the
    // pipeline-enable flag so a toggled-off (but configured) edit still lists.
    QJsonObject a = node->saveState();
    QJsonObject b = def->saveState();
    a.remove(QStringLiteral("enabled"));
    b.remove(QStringLiteral("enabled"));
    return a != b;
}

void MainWindow::rebuildAdjustments()
{
    m_adjustments.clear();

    auto addNodeAdj = [this](const QString &name, int order, EditNode *n) {
        m_adjustments.push_back(
            {name, order, [n] { return n->isEnabled(); },
             [n](bool on) { n->setEnabled(on); },
             [this, n] { n->restoreState(createNode(n->typeName())->saveState()); }});
    };

    if (!doc().graph.source().isNull()) {
        // Base pipeline, in processing order. Heal and the LAB neighbourhood ops use
        // an effect predicate (so a configured-but-zero op doesn't list); the rest
        // use the generic params-differ test.
        if (nodeIsActive(doc().lens))
            addNodeAdj(QStringLiteral("Lens"), 0, doc().lens);
        if (doc().heal && !doc().heal->healMask().isEmpty())
            addNodeAdj(QStringLiteral("Heal"), 1, doc().heal);
        if (const auto v = doc().denoise ? doc().denoise->values() : DenoiseNode::Values{};
            v.enabled && (v.luma > 0.0f || v.chroma > 0.0f))
            addNodeAdj(QStringLiteral("Noise Reduction"), 2, doc().denoise);
        if (const auto v = doc().defringe ? doc().defringe->values() : DefringeNode::Values{};
            v.enabled && (v.purple > 0.0f || v.green > 0.0f))
            addNodeAdj(QStringLiteral("Defringe"), 3, doc().defringe);
        if (const auto v = doc().sharpen ? doc().sharpen->values() : SharpenNode::Values{};
            v.enabled && v.amount > 0.0f)
            addNodeAdj(QStringLiteral("Sharpen"), 4, doc().sharpen);
        if (const auto v = doc().structure ? doc().structure->values() : StructureNode::Values{};
            v.enabled && v.amount != 0.0f)
            addNodeAdj(QStringLiteral("Structure"), 5, doc().structure);
        if (nodeIsActive(doc().tune))
            addNodeAdj(QStringLiteral("Tone"), 6, doc().tune);
        if (nodeIsActive(doc().colorMixer))
            addNodeAdj(QStringLiteral("Color Mixer"), 7, doc().colorMixer);
        if (nodeIsActive(doc().curves))
            addNodeAdj(QStringLiteral("Curves"), 8, doc().curves);
        if (nodeIsActive(doc().colorGrade))
            addNodeAdj(QStringLiteral("Color Grade"), 9, doc().colorGrade);
        if (nodeIsActive(doc().lut))
            addNodeAdj(QStringLiteral("Look"), 10, doc().lut);
        if (nodeIsActive(doc().mono))
            addNodeAdj(QStringLiteral("B&W"), 11, doc().mono);
        if (nodeIsActive(doc().grain))
            addNodeAdj(QStringLiteral("Grain"), 12, doc().grain);

        // Selective layers (non-Base), then the final geometric/finishing stages.
        for (int i = 1; i < doc().graph.layerCount(); ++i) {
            const QString name = doc().graph.layer(i).name();
            m_adjustments.push_back(
                {name.isEmpty() ? QStringLiteral("Adjustment layer") : name, 100 + i,
                 [this, i] { return doc().graph.layer(i).enabled(); },
                 [this, i](bool on) { doc().graph.layer(i).setEnabled(on); },
                 [this, i] { doc().graph.removeLayer(i); }});
        }
        if (!doc().graph.crop().isIdentity()) {
            m_adjustments.push_back(
                {QStringLiteral("Crop"), 200, [this] { return doc().graph.crop().enabled; },
                 [this](bool on) { CropState c = doc().graph.crop(); c.enabled = on; doc().graph.setCrop(c); },
                 [this] { doc().graph.setCrop(CropState{}); }});
        }
        if (std::abs(doc().graph.vignette().amount) > 0.0f) {
            m_adjustments.push_back(
                {QStringLiteral("Vignette"), 210, [this] { return doc().graph.vignette().enabled; },
                 [this](bool on) { VignetteParams v = doc().graph.vignette(); v.enabled = on; doc().graph.setVignette(v); },
                 [this] { doc().graph.setVignette(VignetteParams{}); }});
        }
    }

    QVector<AdjustmentsPanel::Item> items;
    items.reserve(static_cast<int>(m_adjustments.size()));
    for (const Adjustment &a : m_adjustments)
        items.push_back({a.name, a.isEnabled()});
    m_adjustmentsPanel->setItems(items, doc().viewCeiling, doc().compareOriginal);
    layoutOverlays(); // size may have changed
}

void MainWindow::onAdjustmentToggle(int index, bool on)
{
    if (doc().peeking)
        exitPeek(); // editing resolves the transient peek
    if (index < 0 || index >= static_cast<int>(m_adjustments.size()))
        return;
    m_adjustments[index].setEnabled(on);
    doc().graph.commit();
    rebuildPreviewFromGraph();
    rebuildAdjustments();
}

void MainWindow::onAdjustmentDelete(int index)
{
    if (doc().peeking)
        exitPeek();
    if (index < 0 || index >= static_cast<int>(m_adjustments.size()))
        return;
    m_adjustments[index].reset();
    doc().graph.commit();
    rebuildPreviewFromGraph();
    if (m_layersPanel->isVisible())
        refreshLayersPanel(); // a layer may have been removed
    reseedOpenPanels();       // an open tool now shows the reset (neutral) values
    rebuildAdjustments();
}

void MainWindow::peekUpTo(int index)
{
    if (index < 0 || index >= static_cast<int>(m_adjustments.size()))
        return;
    if (doc().peeking && index == doc().viewCeiling) { // clicking the selected row exits
        exitPeek();
        return;
    }
    if (doc().compareOriginal)
        setCompareOriginal(false); // Before/After and peek are mutually exclusive
    if (!doc().peeking) {
        doc().peekSnapshot = doc().graph.saveState(); // the real state to restore on exit
        doc().peeking = true;
    } else {
        doc().graph.restoreState(doc().peekSnapshot); // reset before applying a new ceiling
    }
    doc().viewCeiling = index;
    const int ceilingOrder = m_adjustments[index].order;
    // Hide every adjustment that comes after the selected one in the pipeline.
    for (const Adjustment &a : m_adjustments)
        if (a.order > ceilingOrder)
            a.setEnabled(false);
    rebuildPreviewFromGraph();
    rebuildAdjustments(); // repaint with the ceiling row highlighted
    showHint(QStringLiteral("Showing up to “%1”").arg(m_adjustments[index].name));
}

void MainWindow::exitPeek()
{
    if (!doc().peeking)
        return;
    doc().graph.restoreState(doc().peekSnapshot); // back to the real, committed state
    doc().peeking = false;
    doc().viewCeiling = -1;
    doc().peekSnapshot = QJsonObject{};
    rebuildPreviewFromGraph();
    if (m_adjustmentsPanel->isVisible())
        rebuildAdjustments();
}

void MainWindow::openAdjustmentsTool()
{
    if (m_adjustmentsPanel->isVisible()) {
        closeAdjustmentsTool();
        return;
    }
    const int margin = 18;
    m_adjustmentsPanel->move(margin, margin); // top-left corner
    m_adjustmentsPanel->reveal();
    rebuildAdjustments();
    layoutOverlays();
    syncViewToggles();
}

void MainWindow::closeAdjustmentsTool()
{
    if (doc().compareOriginal)
        setCompareOriginal(false);
    exitPeek();
    m_adjustmentsPanel->hide();
    layoutOverlays();
    syncViewToggles();
}

void MainWindow::refreshWorkingSource()
{
    const Image &src = doc().graph.source();
    if (src.isNull()) {
        doc().workingSource = Image();
        return;
    }
    // The lens node is a pure function of the source; apply it once and cache so
    // heal dabs (which call refreshBaseImage repeatedly) don't re-warp full res.
    // Honour the pipeline toggle so the Adjustments panel can disable lens.
    doc().workingSource = (doc().lens && doc().lens->isEnabled()) ? doc().lens->apply(src) : src;
    if (doc().workingSource.isNull())
        doc().workingSource = src; // defensive: never lose the image
    doc().sourceQImage = doc().workingSource.toQImage(); // display + colour sampling
    doc().originalQImage = QImage(); // Before/After cache: recompute on next compare
}

void MainWindow::loadLookFile()
{
    const QString dir = lastDir(
        QStringLiteral("lookFile"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load LUT"), dir,
        QStringLiteral("LUT files (*.cube *.png *.tif *.tiff *.jpg *.jpeg)"));
    if (path.isEmpty())
        return;
    rememberDir(QStringLiteral("lookFile"), path);

    QString error;
    if (!activeLut()->loadLut(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("Lumen"),
                             QStringLiteral("Could not load look: %1").arg(error));
        return;
    }
    m_looksPanel->setLookName(QFileInfo(path).fileName());
    updatePreview();
}

void MainWindow::updateMaskEditing()
{
    // The active layer's mask is a Brush mask and the Layers panel is open:
    // enable the canvas brush so left-drag paints. (Heal owns the brush when its
    // tool is active, so don't fight it.)
    if (m_brushTarget == BrushTarget::Heal)
        return;
    const int idx = doc().graph.activeLayerIndex();
    const bool brushLayer = m_layersPanel->isVisible() && idx > 0
        && doc().graph.activeLayer().mask().type == MaskSpec::Brush;
    if (brushLayer) {
        if (m_brushTarget != BrushTarget::Selective) {
            m_brushMask = doc().graph.activeLayer().mask().brush;
            if (m_brushMask.isEmpty())
                initBrushMask();
            m_brushUndo.clear();
            m_brushHasLast = false;
            m_brushTarget = BrushTarget::Selective;
        }
        m_canvas->setBrushCursor(m_brushSize, m_brushHardness / 100.0f);
        m_canvas->setBrushMode(true);
        m_canvas->setSelectiveMask(m_brushMask); // show the mask being painted
    } else if (m_brushTarget == BrushTarget::Selective) {
        m_brushTarget = BrushTarget::None;
        m_selectivePainting = false;
        m_canvas->setBrushMode(false);
    }
}

void MainWindow::endMaskBrushSession()
{
    // The strokes are already synced into the active layer's mask (brushAt), so
    // committing snapshots them as one undo step.
    if (m_brushTarget == BrushTarget::Selective) {
        doc().graph.commit();
        m_brushUndo.clear();
    }
}

void MainWindow::openHealTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    m_healPanel->adjustSize();
    const int margin = 18;
    m_healPanel->move(width() - m_healPanel->width() - margin, margin);
    m_healPanel->reveal(m_brushSize, m_brushHardness, m_brushAdd, doc().heal->highQuality());

    // Restore the heal session from the node (may be empty).
    m_brushMask = doc().heal->healMask();
    m_brushUndo.clear();
    m_brushHasLast = false;
    if (m_brushMask.isEmpty())
        initBrushMask();
    m_brushTarget = BrushTarget::Heal;
    m_canvas->setBrushCursor(m_brushSize, m_brushHardness / 100.0f);
    m_canvas->setBrushMode(true);
    updateCropView(); // full-frame rule: heal paints in the un-oriented frame
    refreshBaseImage();
    updatePreview();
}

void MainWindow::closeHealTool()
{
    m_healPanel->hide();
    m_canvas->setBrushMode(false);
    m_brushTarget = BrushTarget::None;
    m_healPainting = false;
    doc().heal->setHealMask(m_brushMask); // commit (one global undo step)
    m_brushUndo.clear();
    updateCropView(); // back to the cropped browse view
    refreshBaseImage();
    updatePreview();
    doc().graph.commit();
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::refreshBaseImage(bool keepView)
{
    // Consume the triggering-op hint (set by the panel that kicked this bake) so
    // the badge labels the user's actual action, not whichever active pass ranks
    // highest. Reset regardless of the path taken below.
    const BakeOp triggeredBy = m_bakeOp;
    m_bakeOp = BakeOp::Auto;

    // Before/After: show the un-edited original (no baked ops) within the current
    // crop framing. updatePreview neutralises the pointwise/look/vignette stage.
    if (doc().compareOriginal) {
        const QImage &orig = originalImage();
        if (!orig.isNull())
            m_canvas->setImage(orig, keepView);
        return;
    }

    // The GPU preview base = the lens-corrected source with the other "baked"
    // neighbourhood ops applied (heal -> denoise -> defringe -> sharpen); the
    // shader then applies the pointwise/LUT ops on top. With no baked op active,
    // the corrected source (doc().sourceQImage) is already the base.
    // Each baked op also respects its pipeline toggle (Adjustments panel), so a
    // disabled node bakes nothing — matching the pointwise/preview-LUT path.
    const bool healActive =
        doc().heal && doc().heal->isEnabled() && !doc().heal->healMask().isEmpty();
    const DenoiseNode::Values dv =
        doc().denoise ? doc().denoise->values() : DenoiseNode::Values{};
    const bool denoiseActive = doc().denoise && doc().denoise->isEnabled() && dv.enabled
                               && (dv.luma > 0.0f || dv.chroma > 0.0f);
    const DefringeNode::Values fv =
        doc().defringe ? doc().defringe->values() : DefringeNode::Values{};
    const bool defringeActive = doc().defringe && doc().defringe->isEnabled() && fv.enabled
                                && (fv.purple > 0.0f || fv.green > 0.0f);
    const SharpenNode::Values sv =
        doc().sharpen ? doc().sharpen->values() : SharpenNode::Values{};
    const bool sharpenActive =
        doc().sharpen && doc().sharpen->isEnabled() && sv.enabled && sv.amount > 0.0f;
    const StructureNode::Values stv =
        doc().structure ? doc().structure->values() : StructureNode::Values{};
    const bool structureActive =
        doc().structure && doc().structure->isEnabled() && stv.enabled && stv.amount != 0.0f;
    if (doc().graph.source().isNull()
        || (!healActive && !denoiseActive && !defringeActive && !sharpenActive
            && !structureActive)) {
        if (!doc().sourceQImage.isNull())
            m_canvas->setImage(doc().sourceQImage, keepView);
        return;
    }

    // These passes (esp. Detailed/Criminisi heal) are expensive, so run them off
    // the UI thread; the latest request wins (the document's healGen guards stale
    // results, and the watcher only delivers its current future).
    Document *d = &doc();
    m_healJobDoc = d->id;
    const quint64 gen = ++d->healGen;
    // Baked ops run on top of the lens-corrected source (geometry already baked
    // into doc().workingSource), so painted spots track the corrected image.
    const Image src = doc().workingSource.isNull() ? doc().graph.source() : doc().workingSource;
    const MaskBuffer mask = healActive ? doc().heal->healMask() : MaskBuffer();
    const bool hq = doc().heal && doc().heal->highQuality();
    if (healActive || denoiseActive || defringeActive || sharpenActive || structureActive) {
        // Label by the op the user triggered (if it's actually active); otherwise
        // fall back to precedence (heal first) for unattributed refreshes.
        QString label;
        if (triggeredBy == BakeOp::Structure && structureActive)
            label = QStringLiteral("Structuring…");
        else if (triggeredBy == BakeOp::Sharpen && sharpenActive)
            label = QStringLiteral("Sharpening…");
        else if (triggeredBy == BakeOp::Defringe && defringeActive)
            label = QStringLiteral("Defringing…");
        else if (triggeredBy == BakeOp::Denoise && denoiseActive)
            label = QStringLiteral("Denoising…");
        else if (triggeredBy == BakeOp::Heal && healActive)
            label = QStringLiteral("Healing…");
        if (label.isEmpty())
            label = healActive ? QStringLiteral("Healing…")
                  : denoiseActive ? QStringLiteral("Denoising…")
                  : defringeActive ? QStringLiteral("Defringing…")
                  : sharpenActive ? QStringLiteral("Sharpening…")
                                  : QStringLiteral("Structuring…");
        auto *badge = static_cast<BusyBadge *>(m_healBusy);
        badge->setLabel(label);
        badge->start();
        layoutOverlays(); // position the badge + dim
    }

    m_healWatcher.setFuture(
        QtConcurrent::run([d, gen, src, mask, hq, dv, fv, sv, stv]() -> QImage {
            if (gen != d->healGen)
                return QImage(); // superseded before we even started
            Image img = src;
            if (!mask.isEmpty()) {
                HealNode heal;
                heal.setHealMask(mask);
                heal.setHighQuality(hq);
                img = heal.apply(img);
            }
            if (dv.enabled && (dv.luma > 0.0f || dv.chroma > 0.0f)) {
                DenoiseNode denoise;
                denoise.setValues(dv);
                img = denoise.apply(img);
            }
            if (fv.enabled && (fv.purple > 0.0f || fv.green > 0.0f)) {
                DefringeNode defringe;
                defringe.setValues(fv);
                img = defringe.apply(img);
            }
            if (sv.enabled && sv.amount > 0.0f) {
                SharpenNode sharpen;
                sharpen.setValues(sv);
                img = sharpen.apply(img);
            }
            if (stv.enabled && stv.amount != 0.0f) {
                StructureNode structure;
                structure.setValues(stv);
                img = structure.apply(img);
            }
            return img.toQImage();
        }));
}

int MainWindow::ensureSelectiveLayer()
{
    // A selective edit operates on a non-Base layer whose mask is data-driven
    // (luminosity/colour/brush) or still unset. If the active layer isn't such a
    // layer (it's the Base, or carries a geometric gradient/radial mask), add a
    // fresh adjustment layer so we never clobber a geometric mask.
    const auto selectable = [](MaskSpec::Type t) {
        return t == MaskSpec::None || t == MaskSpec::Luminosity
            || t == MaskSpec::Colour || t == MaskSpec::Brush;
    };
    int idx = doc().graph.activeLayerIndex();
    if (idx == 0 || !selectable(doc().graph.layer(idx).mask().type)) {
        addMaskedAdjustmentLayer(QStringLiteral("Selective %1").arg(doc().graph.layerCount()));
        idx = doc().graph.activeLayerIndex();
    }
    // Default a still-unset mask to a full-range luminosity mask (a no-op until
    // the range or adjustment is dialled in), so the panel has something to edit.
    Layer &layer = doc().graph.layer(idx);
    if (layer.mask().type == MaskSpec::None) {
        MaskSpec m;
        m.type = MaskSpec::Luminosity;
        layer.setMask(m);
    }
    doc().graph.commit();
    return idx;
}

void MainWindow::syncBrushMaskToLayer()
{
    if (doc().graph.activeLayerIndex() == 0)
        return;
    Layer &layer = doc().graph.activeLayer();
    if (layer.mask().type != MaskSpec::Brush)
        return;
    MaskSpec m = layer.mask();
    m.brush = m_brushMask;
    layer.setMask(m);
}

void MainWindow::recomputeSelectiveMask()
{
    // Drives the preview-only "show mask" overlay (Base shader binding 4). Show
    // the active layer's mask; brush mode uses the live working mask. The
    // texture is only consumed while the overlay is on, so skip the work
    // otherwise (the brush paints into the texture directly via brushAt).
    if (doc().maskView == 0)
        return;
    if (doc().graph.activeLayerIndex() == 0) {
        m_canvas->setSelectiveMask({});
        return;
    }
    const MaskSpec &m = doc().graph.activeLayer().mask();
    if (m.type == MaskSpec::Brush) {
        m_canvas->setSelectiveMask(m_brushMask);
        return;
    }
    if ((m.type == MaskSpec::None && m.zones.empty()) || doc().sourceQImage.isNull()) {
        m_canvas->setSelectiveMask({});
        return;
    }
    // Evaluate at a capped resolution for a responsive overlay (export/composite
    // recompute at full res).
    constexpr int cap = 1280;
    int w = doc().sourceQImage.width(), h = doc().sourceQImage.height();
    if (std::max(w, h) > cap) {
        const double s = double(cap) / std::max(w, h);
        w = std::max(1, static_cast<int>(std::lround(w * s)));
        h = std::max(1, static_cast<int>(std::lround(h * s)));
    }
    // Geometric masks (gradient/radial) don't need pixels — skip the source copy
    // so live gizmo dragging stays smooth. Luminosity/Colour need the rgba.
    if (m.type == MaskSpec::Luminosity || m.type == MaskSpec::Colour) {
        const QImage img =
            doc().sourceQImage.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_RGBA8888);
        m_canvas->setSelectiveMask(
            evaluateMask(m, img.width(), img.height(), img.constBits(), 4));
    } else {
        m_canvas->setSelectiveMask(evaluateMask(m, w, h, nullptr, 4));
    }
}

void MainWindow::initBrushMask()
{
    if (doc().sourceQImage.isNull())
        return;
    constexpr int cap = 1280;
    int w = doc().sourceQImage.width();
    int h = doc().sourceQImage.height();
    if (std::max(w, h) > cap) {
        const double s = double(cap) / std::max(w, h);
        w = std::max(1, static_cast<int>(std::lround(w * s)));
        h = std::max(1, static_cast<int>(std::lround(h * s)));
    }
    m_brushMask.width = w;
    m_brushMask.height = h;
    m_brushMask.data.assign(static_cast<size_t>(w) * h, 0.0f);
}

void MainWindow::beginBrushStroke()
{
    if (m_brushMask.isEmpty())
        return;
    m_brushUndo.push_back(m_brushMask.data); // snapshot for session undo
    if (m_brushUndo.size() > 50)
        m_brushUndo.erase(m_brushUndo.begin());
    // Fresh, empty footprint for this stroke — the heal overlay tints wherever the
    // brush touches this stroke, independent of what earlier strokes already healed.
    m_strokeMask.width = m_brushMask.width;
    m_strokeMask.height = m_brushMask.height;
    m_strokeMask.data.assign(m_brushMask.data.size(), 0.0f);
    m_brushHasLast = false;
}

void MainWindow::brushAt(const QPointF &norm)
{
    if (m_brushMask.isEmpty())
        return;
    const int w = m_brushMask.width;
    const int h = m_brushMask.height;
    const float cx = static_cast<float>(norm.x()) * (w - 1);
    const float cy = static_cast<float>(norm.y()) * (h - 1);
    const float radius = std::max(1.0f, (m_brushSize / 100.0f) * 0.3f * std::min(w, h));
    const float hardness = m_brushHardness / 100.0f;
    const bool heal = (m_brushTarget == BrushTarget::Heal);

    // Stamp into the mask being painted, and (for heal) mirror the footprint into
    // m_strokeMask. The footprint is always additive, so it records every spot the
    // brush touched this stroke — even ones a previous stroke already healed, which
    // is what makes the overlay tint them (a brushMask-minus-base delta would not).
    const auto stamp = [&](float px, float py) {
        stampBrush(m_brushMask, px, py, radius, hardness, m_brushAdd);
        if (heal)
            stampBrush(m_strokeMask, px, py, radius, hardness, /*add=*/true);
    };

    if (m_brushHasLast) {
        // Stamp along the segment so fast drags don't leave gaps.
        const float dx = cx - m_lastBrushPoint.x();
        const float dy = cy - m_lastBrushPoint.y();
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float step = std::max(1.0f, radius * 0.25f);
        const int steps = static_cast<int>(dist / step);
        for (int k = 1; k < steps; ++k) {
            const float t = k / float(steps);
            stamp(m_lastBrushPoint.x() + dx * t, m_lastBrushPoint.y() + dy * t);
        }
    }
    stamp(cx, cy);
    m_lastBrushPoint = QPointF(cx, cy);
    m_brushHasLast = true;

    // Live feedback. The selective brush shows the whole mask (you're building a
    // selection). The heal brush shows this stroke's footprint as a red overlay (it
    // inpaints on stroke end) — including where it overlaps already-healed spots, so
    // the brush always reads as "painting". Erasing un-marks, so it shows no tint.
    if (heal) {
        m_healPainting = true;
        if (m_brushAdd) {
            m_canvas->setSelectiveMask(m_strokeMask);
        } else {
            MaskBuffer empty;
            empty.width = w;
            empty.height = h;
            empty.data.assign(m_brushMask.data.size(), 0.0f);
            m_canvas->setSelectiveMask(empty);
        }
        updatePreview();
    } else {
        // Selective brush: the painted mask is the active layer's mask, so push
        // it down and refresh the masked adjustment live (overlay too if shown).
        syncBrushMaskToLayer();
        m_canvas->setSelectiveMask(m_brushMask);
        m_selectivePainting = true; // force the red overlay for the duration of the stroke
        updatePreview();
    }
}

void MainWindow::endBrushStroke()
{
    m_brushHasLast = false;
    if (m_brushTarget == BrushTarget::Heal) {
        // Inpaint and show the result; restore the selective texture afterwards.
        m_healPainting = false;
        doc().heal->setHealMask(m_brushMask);
        refreshBaseImage();
        recomputeSelectiveMask();
        updatePreview();
    } else if (m_brushTarget == BrushTarget::Selective) {
        // Stroke finished: drop the transient "show strokes" overlay so the
        // highlight follows the Show toggle and the layer's adjustment/opacity
        // becomes visible again.
        m_selectivePainting = false;
        recomputeSelectiveMask();
        updatePreview();
    }
}

bool MainWindow::brushSessionUndo()
{
    if (m_brushUndo.empty())
        return false;
    m_brushMask.data = m_brushUndo.back();
    m_brushUndo.pop_back();
    if (m_brushTarget == BrushTarget::Heal) {
        doc().heal->setHealMask(m_brushMask);
        refreshBaseImage();
        recomputeSelectiveMask();
        updatePreview();
    } else {
        syncBrushMaskToLayer();
        m_canvas->setSelectiveMask(m_brushMask);
        updatePreview();
    }
    return true;
}

void MainWindow::onColorPicked(const QPointF &norm)
{
    if (doc().sourceQImage.isNull())
        return;
    const int x = std::clamp(static_cast<int>(std::lround(norm.x() * (doc().sourceQImage.width() - 1))),
                             0, doc().sourceQImage.width() - 1);
    const int y = std::clamp(static_cast<int>(std::lround(norm.y() * (doc().sourceQImage.height() - 1))),
                             0, doc().sourceQImage.height() - 1);
    const QColor c = doc().sourceQImage.pixelColor(x, y);

    // White-balance eyedropper: make the sampled (as-shot baseline) pixel neutral.
    if (m_pickPurpose == PickPurpose::WhiteBalance) {
        m_pickPurpose = PickPurpose::MaskColour; // reset to the default purpose
        if (auto *t = activeTune()) {
            t->pickNeutral(static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
                           static_cast<float>(c.blueF()));
            updatePreview();
            if (m_tonePanel->isVisible())
                m_tonePanel->reveal(toneValuesOf(t));
            doc().graph.commit();
        }
        return;
    }

    if (doc().graph.activeLayerIndex() == 0)
        return;
    Layer &layer = doc().graph.activeLayer();
    MaskSpec m = layer.mask();
    m.type = MaskSpec::Colour;
    m.targetR = static_cast<float>(c.redF());
    m.targetG = static_cast<float>(c.greenF());
    m.targetB = static_cast<float>(c.blueF());
    layer.setMask(m);
    m_layersPanel->setTargetColor(c);
    recomputeSelectiveMask();
    updatePreview();
    doc().graph.commit();
}

void MainWindow::closeActiveTool()
{
    if (m_tonePanel->isVisible())
        closeToneTool();
    else if (m_curvesPanel->isVisible())
        closeCurvesTool();
    else if (m_looksPanel->isVisible())
        closeLooksTool();
    else if (m_presetsPanel->isVisible())
        closePresetsTool();
    else if (m_monoPanel->isVisible())
        closeMonoTool();
    else if (m_colorMixerPanel->isVisible())
        closeColorMixerTool();
    else if (m_colorGradePanel->isVisible())
        closeColorGradeTool();
    else if (m_lensPanel->isVisible())
        closeLensTool();
    else if (m_sharpenPanel->isVisible())
        closeSharpenTool();
    else if (m_structurePanel->isVisible())
        closeStructureTool();
    else if (m_denoisePanel->isVisible())
        closeDenoiseTool();
    else if (m_defringePanel->isVisible())
        closeDefringeTool();
    else if (m_rawPanel->isVisible())
        closeRawTool();
    else if (m_grainPanel->isVisible())
        closeGrainTool();
    else if (m_vignettePanel->isVisible())
        closeVignetteTool();
    else if (m_cropPanel->isVisible())
        closeCropTool();
    else if (m_healPanel->isVisible())
        closeHealTool();
    else {
        m_input.setMode(InputController::Mode::Browse);
        m_canvas->setFocus();
    }
}

void MainWindow::doUndo()
{
    if (doc().peeking)
        exitPeek(); // history nav resolves the transient peek first
    // While brush-painting, Ctrl+Z is a per-stroke session undo.
    if (m_brushTarget != BrushTarget::None && brushSessionUndo())
        return;
    if (doc().graph.undo())
        afterHistoryChange();
}

void MainWindow::doRedo()
{
    if (doc().peeking)
        exitPeek();
    if (doc().graph.redo())
        afterHistoryChange();
}

void MainWindow::afterHistoryChange()
{
    // Lens/perspective and heal both bake into the base; rebuild the corrected
    // source first, then the heal-on-top base, so undo/redo of either is visible.
    refreshWorkingSource();
    refreshBaseImage();
    updatePreview();
    if (m_layersPanel->isVisible())
        refreshLayersPanel();
    if (m_healPanel->isVisible()) {
        m_brushMask = doc().heal->healMask(); // sync session to restored state
        m_brushUndo.clear();
    }
    // If a tool is open, reseed its control from the restored state (guarded —
    // a layer may not carry every node type, e.g. a selective layer has tune only).
    if (m_tonePanel->isVisible()) {
        if (auto *t = activeTune())
            m_tonePanel->reveal(toneValuesOf(t));
    }
    if (m_curvesPanel->isVisible()) {
        if (auto *c = activeCurves())
            m_curvesPanel->reveal(c->curves());
    }
    if (m_looksPanel->isVisible()) {
        if (auto *l = activeLut())
            m_looksPanel->reveal(QFileInfo(l->sourcePath()).fileName(), l->intensity());
    }
    if (m_monoPanel->isVisible()) {
        if (auto *mono = activeMono())
            m_monoPanel->reveal(mono->values());
    }
    if (m_colorMixerPanel->isVisible()) {
        if (auto *cm = activeColorMixer())
            m_colorMixerPanel->reveal(cm->values());
    }
    if (m_colorGradePanel->isVisible()) {
        if (auto *g = activeColorGrade())
            m_colorGradePanel->reveal(g->values());
    }
    if (m_lensPanel->isVisible() && doc().lens) {
        m_lensPanel->reveal(doc().lens->params(), doc().lens->lensMatched(),
                            doc().lens->matchedLensName());
    }
    if (m_sharpenPanel->isVisible() && doc().sharpen)
        m_sharpenPanel->reveal(doc().sharpen->values());
    if (m_denoisePanel->isVisible() && doc().denoise)
        m_denoisePanel->reveal(doc().denoise->values());
    if (m_defringePanel->isVisible() && doc().defringe)
        m_defringePanel->reveal(doc().defringe->values());
    if (m_grainPanel->isVisible() && doc().grain)
        m_grainPanel->reveal(doc().grain->values());
    if (m_vignettePanel->isVisible())
        m_vignettePanel->reveal(doc().graph.vignette());
    if (m_cropPanel->isVisible()) {
        m_cropPanel->reveal(doc().graph.crop(), sourceAspect());
        m_cropGizmo->setRect(doc().graph.crop().rect);
    }
    updateCropView(); // push the restored crop/orientation to the canvas
    updateHistogram(); // reflect the restored state (no-op when hidden)
    // If a mask brush is active, resync the working mask to the restored state.
    if (m_brushTarget == BrushTarget::Selective
        && doc().graph.activeLayer().mask().type == MaskSpec::Brush) {
        m_brushMask = doc().graph.activeLayer().mask().brush;
        m_brushUndo.clear();
        recomputeSelectiveMask();
    }
    if (m_adjustmentsPanel->isVisible())
        rebuildAdjustments(); // history nav changes the active-edit set
}

void MainWindow::addPanelCloseButton(QWidget *panel, std::function<void()> onClose)
{
    auto *btn = new QPushButton(QStringLiteral("✕"), panel);
    btn->setObjectName(QStringLiteral("panelClose"));
    btn->setFocusPolicy(Qt::NoFocus); // never steal keyboard focus from the canvas
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedSize(20, 20);
    btn->setToolTip(QStringLiteral("Close (Esc)"));
    // Fully specify every property: each panel sets its own generic `QPushButton`
    // rule (padding/border/bg), which would otherwise leak in and clip the glyph.
    btn->setStyleSheet(QStringLiteral(
        "#panelClose { background: #2a2a2e; color: #d6d6d9;"
        " border: 1px solid #38383d; border-radius: 10px; padding: 0;"
        " font-size: 12px; font-weight: bold; }"
        "#panelClose:hover { background: #c0444a; color: #ffffff; border-color: #c0444a; }"));
    connect(btn, &QPushButton::clicked, this, [onClose] { onClose(); });
    m_panelClose.insert(panel, btn);
    panel->installEventFilter(this); // reposition on resize (see eventFilter)
    btn->move(panel->width() - btn->width() - 8, 8); // fixed-width panels: place now
    btn->raise();
}

void MainWindow::showHint(const QString &text)
{
    m_hint->setText(text);
    m_hint->adjustSize();
    layoutOverlays();
}

QString MainWindow::modeHintText() const
{
    switch (m_input.mode()) {
    case InputController::Mode::CommandPalette:
        return QStringLiteral(
            "↑ ↓  browse   ·   type to fuzzy-search   ·   ↵  run   ·   Esc  close");
    case InputController::Mode::ToolActive:
        return QStringLiteral(
            "drag to adjust · arrows nudge   ·   ↵ / Esc  done   ·   /  switch tool");
    case InputController::Mode::MaskEditing:
        return QStringLiteral(
            "drag to paint · Alt subtract · [ ] brush size   ·   ↵ / Esc  done");
    case InputController::Mode::Browse:
    default:
        return QStringLiteral(
            "/  or right-click  ·  command palette       "
            "G histogram · J clipping · A history · \\ before/after       "
            "Ctrl+O open · Ctrl+S save · F11 fullscreen");
    }
}

void MainWindow::updateModeHint()
{
    showHint(modeHintText());
}

void MainWindow::layoutOverlays()
{
    // Scrim covers the whole window, behind the palette.
    m_scrim->setGeometry(rect());
    m_brushRing->setGeometry(rect());

    // Tab strip along the top edge (only shown when >1 document is open).
    int topInset = 0;
    if (m_tabBar && m_tabBar->isVisible()) {
        const int h = m_tabBar->sizeHint().height();
        m_tabBar->setGeometry(0, 0, width(), h);
        m_tabBar->raise();
        topInset = h;
    }

    // Busy badge: top-centre of the canvas, clear of the tab strip.
    m_healBusy->move((width() - m_healBusy->width()) / 2, 16 + topInset);
    if (m_healBusy->isVisible())
        m_healBusy->raise();

    // Palette: fixed width, near the top-centre.
    const int pw = 360;
    const int ph = 320;
    m_palette->resize(pw, ph);
    m_palette->move((width() - pw) / 2, height() / 8);

    // The tool panel floats and is user-draggable (placed on open), so don't
    // reposition it here — just clamp it back into view if the window shrank.
    const auto clampIntoView = [this](QWidget *panel) {
        if (!panel->isVisible())
            return;
        QPoint p = panel->pos();
        p.setX(std::clamp(p.x(), 0, std::max(0, width() - panel->width())));
        p.setY(std::clamp(p.y(), 0, std::max(0, height() - panel->height())));
        panel->move(p);
    };
    clampIntoView(m_tonePanel);
    clampIntoView(m_curvesPanel);
    clampIntoView(m_looksPanel);
    clampIntoView(m_monoPanel);
    clampIntoView(m_colorMixerPanel);
    clampIntoView(m_colorGradePanel);
    clampIntoView(m_lensPanel);
    clampIntoView(m_sharpenPanel);
    clampIntoView(m_grainPanel);
    clampIntoView(m_vignettePanel);
    clampIntoView(m_cropPanel);
    clampIntoView(m_denoisePanel);
    clampIntoView(m_defringePanel);
    clampIntoView(m_rawPanel);
    clampIntoView(m_healPanel);
    clampIntoView(m_layersPanel);

    // The crop gizmo overlays the full canvas area while the Crop tool is open.
    if (m_cropGizmo && m_cropGizmo->isVisible()) {
        m_cropGizmo->setGeometry(m_canvas->geometry());
        m_cropGizmo->raise();
        if (m_cropPanel->isVisible())
            m_cropPanel->raise(); // keep the panel clickable above the gizmo
    }

    // Histogram: bottom-left of the canvas, above the hint bar — unless the user
    // has dragged it, in which case keep their spot (just clamp it into view).
    if (m_histogram && m_histogram->isVisible()) {
        if (m_histMoved) {
            QPoint p = m_histogram->pos();
            p.setX(std::clamp(p.x(), 0, std::max(0, width() - m_histogram->width())));
            p.setY(std::clamp(p.y(), 0, std::max(0, height() - m_histogram->height())));
            m_histogram->move(p);
        } else {
            m_histogram->move(18, height() - m_histogram->height() - 64);
        }
        m_histogram->raise();
    }

    // The mask gizmo overlays the canvas area. Keep it above the canvas but below
    // the floating panels so their controls stay clickable (the gizmo passes
    // unhandled clicks through to the canvas underneath).
    if (m_maskGizmo) {
        m_maskGizmo->setGeometry(m_canvas->geometry());
        m_maskGizmo->raise();
        // Zone editor overlays (and sits above) the mask gizmo; it forwards
        // unconsumed clicks down to it so a gradient/radial mask stays editable.
        if (m_zoneGizmo) {
            m_zoneGizmo->setGeometry(m_canvas->geometry());
            if (m_zoneGizmo->isVisible())
                m_zoneGizmo->raise();
        }
        for (QWidget *panel : {static_cast<QWidget *>(m_tonePanel),
                               static_cast<QWidget *>(m_curvesPanel),
                               static_cast<QWidget *>(m_looksPanel),
                               static_cast<QWidget *>(m_monoPanel),
                               static_cast<QWidget *>(m_colorMixerPanel),
                               static_cast<QWidget *>(m_colorGradePanel),
                               static_cast<QWidget *>(m_lensPanel),
                               static_cast<QWidget *>(m_sharpenPanel),
                               static_cast<QWidget *>(m_grainPanel),
                               static_cast<QWidget *>(m_vignettePanel),
                               static_cast<QWidget *>(m_cropPanel),
                               static_cast<QWidget *>(m_denoisePanel),
                               static_cast<QWidget *>(m_defringePanel),
                               static_cast<QWidget *>(m_rawPanel),
                               static_cast<QWidget *>(m_healPanel),
                               static_cast<QWidget *>(m_layersPanel),
                               static_cast<QWidget *>(m_adjustmentsPanel)}) {
            if (panel && panel->isVisible())
                panel->raise();
        }
        // The corner overlays float above the gizmo too, so their controls stay
        // usable while a mask is active (the view-toggle buttons were unclickable
        // because the gizmo covered them).
        if (m_histogram && m_histogram->isVisible())
            m_histogram->raise();
        if (m_viewToggles)
            m_viewToggles->raise();
        m_brushRing->raise(); // brush cursor stays on top of the gizmo
        m_healBusy->raise();
    }

    // Hint bar: bottom-centre.
    m_hint->move((width() - m_hint->width()) / 2, height() - m_hint->height() - 18);

    // View-toggle cluster: bottom-right, opposite the histogram (bottom-left).
    // Once dragged, keep the user's position (clamped into view).
    if (m_viewToggles) {
        m_viewToggles->adjustSize();
        if (m_clusterMoved) {
            QPoint p = m_viewToggles->pos();
            p.setX(std::clamp(p.x(), 0, std::max(0, width() - m_viewToggles->width())));
            p.setY(std::clamp(p.y(), 0, std::max(0, height() - m_viewToggles->height())));
            m_viewToggles->move(p);
        } else {
            m_viewToggles->move(width() - m_viewToggles->width() - 18,
                                height() - m_viewToggles->height() - 18);
        }
    }
}

void MainWindow::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);
    layoutOverlays();
}

void MainWindow::openCommandPalette()
{
    m_input.setMode(InputController::Mode::CommandPalette);
    layoutOverlays();
    m_scrim->show();
    m_scrim->raise();   // above the canvas
    m_palette->reveal();
    m_palette->raise(); // above the scrim
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    // Tab navigation: Ctrl+Tab / Ctrl+Shift+Tab cycle documents; Ctrl+W closes
    // the active one. (Qt emits Key_Backtab for Shift+Tab.)
    if (e->modifiers() & Qt::ControlModifier) {
        if (e->key() == Qt::Key_Tab) {
            if (tabCount() > 1)
                switchToTab((m_activeTab + 1) % tabCount());
            return;
        }
        if (e->key() == Qt::Key_Backtab) {
            if (tabCount() > 1)
                switchToTab((m_activeTab - 1 + tabCount()) % tabCount());
            return;
        }
        if (e->key() == Qt::Key_W) {
            closeTab(m_activeTab);
            return;
        }
    }

    // Hold s / h and use the wheel to change brush size / hardness — works
    // whenever a brush is active (the mask brush runs in Browse mode).
    if (m_brushTarget != BrushTarget::None && !e->isAutoRepeat()
        && (e->key() == Qt::Key_S || e->key() == Qt::Key_H)) {
        m_adjustHardness = (e->key() == Qt::Key_H);
        m_canvas->setBrushAdjusting(true);
        return;
    }

    // Before/After: '\' flips between the edited image and the original.
    if (e->key() == Qt::Key_Backslash && !e->isAutoRepeat()
        && !doc().graph.source().isNull()) {
        setCompareOriginal(!doc().compareOriginal);
        return;
    }

    // View-toggle keys, available whenever an image is loaded (any mode): 'J'
    // clipping, 'G' histo(G)ram, 'A' history (Adjustments panel). Their on/off
    // state is mirrored in the bottom-right cluster and the hint legend.
    if (e->key() == Qt::Key_J && !e->isAutoRepeat() && !doc().graph.source().isNull()) {
        toggleClipping();
        return;
    }
    if (e->key() == Qt::Key_G && !e->isAutoRepeat() && !doc().graph.source().isNull()) {
        toggleHistogram();
        return;
    }
    if (e->key() == Qt::Key_A && !e->isAutoRepeat() && !doc().graph.source().isNull()) {
        openAdjustmentsTool(); // toggles the history panel
        return;
    }

    // H toggles the overlay geometry while a selective layer is being edited.
    // (Reached only when no brush is active — the brush guard above returns first,
    // so this never clashes with the brush-hardness H binding.)
    if (e->key() == Qt::Key_H && !e->isAutoRepeat() && m_layersPanel->isVisible()
        && doc().graph.activeLayerIndex() != 0) {
        setOverlayGeometryVisible(doc().overlaysHidden);
        return;
    }

    // Zone editing: Delete removes the selected shape; Esc disarms a draw tool
    // before falling through to the panel-dismiss behaviour below.
    if (m_zoneGizmo && m_zoneGizmo->isVisible()) {
        if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)
            && m_zoneGizmo->hasSelection()) {
            m_zoneGizmo->deleteSelected();
            return;
        }
        if (e->key() == Qt::Key_Escape && m_zoneGizmo->tool() != ZoneGizmo::Select) {
            m_zoneGizmo->setTool(ZoneGizmo::Select);
            m_layersPanel->resetZoneTool();
            return;
        }
    }

    switch (m_input.mode()) {
    case InputController::Mode::Browse:
        if (e->key() == Qt::Key_Slash) {
            openCommandPalette();
            return;
        }
        // Esc leaves a peek view, then dismisses the persistent panels.
        if (e->key() == Qt::Key_Escape && doc().peeking) {
            exitPeek();
            return;
        }
        if (e->key() == Qt::Key_Escape && m_adjustmentsPanel->isVisible()) {
            closeAdjustmentsTool();
            return;
        }
        if (e->key() == Qt::Key_Escape && m_layersPanel->isVisible()) {
            hideLayersPanel();
            return;
        }
        break;
    case InputController::Mode::ToolActive:
        // Esc/Enter close the active tool; "/" swaps to the palette.
        if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Return
            || e->key() == Qt::Key_Enter) {
            closeActiveTool();
            return;
        }
        if (e->key() == Qt::Key_Slash) {
            closeActiveTool();
            openCommandPalette();
            return;
        }
        break;
    default:
        break;
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::keyReleaseEvent(QKeyEvent *e)
{
    if (!e->isAutoRepeat() && (e->key() == Qt::Key_S || e->key() == Qt::Key_H)) {
        m_canvas->setBrushAdjusting(false);
        return;
    }
    QMainWindow::keyReleaseEvent(e);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    // Accept a drag that carries at least one local file — each will open in a tab.
    if (!e->mimeData()->hasUrls())
        return;
    for (const QUrl &url : e->mimeData()->urls()) {
        if (url.isLocalFile()) {
            e->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent *e)
{
    if (!e->mimeData()->hasUrls())
        return;
    // openPath opens the first file (reusing the empty placeholder) and queues the
    // rest; each lands in its own tab.
    bool any = false;
    for (const QUrl &url : e->mimeData()->urls()) {
        if (url.isLocalFile()) {
            openPath(url.toLocalFile());
            any = true;
        }
    }
    if (any)
        e->acceptProposedAction();
}

void MainWindow::adjustBrush(int steps)
{
    if (m_brushTarget == BrushTarget::None)
        return;
    if (m_adjustHardness)
        m_brushHardness = std::clamp(m_brushHardness + steps * 5, 1, 100);
    else
        m_brushSize = std::clamp(m_brushSize + steps * 4, 1, 100);
    m_canvas->setBrushCursor(m_brushSize, m_brushHardness / 100.0f); // live ring
    syncBrushPanel();
}

void MainWindow::syncBrushPanel()
{
    if (m_brushTarget == BrushTarget::Heal)
        m_healPanel->setBrushParams(m_brushSize, m_brushHardness);
    else if (m_brushTarget == BrushTarget::Selective)
        m_layersPanel->setBrushParams(m_brushSize, m_brushHardness);
}
