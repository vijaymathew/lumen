#include "ui/MainWindow.h"

#include "core/Autosave.h"
#include "core/NodeFactory.h"
#include "core/HealNode.h"
#include "core/Image.h"
#include "core/LayerPreview.h"
#include "core/MaskSpec.h"
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
#include "ui/ColorGradePanel.h"
#include "ui/MonoPanel.h"
#include "core/ColorMixerNode.h"
#include "ui/ColorMixerPanel.h"
#include "ui/GrainPanel.h"
#include "ui/VignettePanel.h"
#include "ui/DefringePanel.h"
#include "ui/RawSettingsPanel.h"
#include "ui/SharpenPanel.h"
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
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <cmath>
#include <QLabel>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent>

#include <algorithm>

namespace {

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
    setWindowTitle(QStringLiteral("Lumen"));

    // Seed the automatic-RAW configuration from the global preference; the first
    // RAW opened uses these (and each .lumen carries its own per-project copy).
    loadRawDefaults(m_rawOptions, m_rawLensDefaults);

    // The graph owns the nodes; we keep raw pointers to drive them. The "baked"
    // group runs first in libvips and is rendered into the preview base texture:
    // lens (warps geometry) -> heal (edits pixels) -> denoise -> sharpen (both
    // neighbourhood ops; denoise before sharpen so noise isn't amplified). Then
    // the pointwise/LUT ops the shader replicates: tune -> colormixer -> curves
    // -> colorgrade -> lut -> mono. Selective adjustments are masked layers above
    // the Base.
    m_lens =
        static_cast<LensCorrectionNode *>(m_graph.addNode(std::make_unique<LensCorrectionNode>()));
    m_heal = static_cast<HealNode *>(m_graph.addNode(std::make_unique<HealNode>()));
    m_denoise = static_cast<DenoiseNode *>(m_graph.addNode(std::make_unique<DenoiseNode>()));
    m_defringe = static_cast<DefringeNode *>(m_graph.addNode(std::make_unique<DefringeNode>()));
    m_sharpen = static_cast<SharpenNode *>(m_graph.addNode(std::make_unique<SharpenNode>()));
    m_tune = static_cast<TuneNode *>(m_graph.addNode(std::make_unique<TuneNode>()));
    m_colorMixer =
        static_cast<ColorMixerNode *>(m_graph.addNode(std::make_unique<ColorMixerNode>()));
    m_curves = static_cast<CurvesNode *>(m_graph.addNode(std::make_unique<CurvesNode>()));
    m_colorGrade =
        static_cast<ColorGradeNode *>(m_graph.addNode(std::make_unique<ColorGradeNode>()));
    m_lutNode = static_cast<LutNode *>(m_graph.addNode(std::make_unique<LutNode>()));
    m_mono = static_cast<MonoNode *>(m_graph.addNode(std::make_unique<MonoNode>()));
    // Film grain is the final finishing step (after mono), applied over the
    // whole image. A pointwise-in-shader op, so it bakes nothing.
    m_grain = static_cast<GrainNode *>(m_graph.addNode(std::make_unique<GrainNode>()));

    // Remember the pristine graph (neutral Base nodes, no selective layers) so
    // openPath() can reset to it for each newly opened image.
    m_defaultGraphState = m_graph.saveState();

    m_canvas = new CanvasWidget(this);
    setCentralWidget(m_canvas);

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
                if (!m_lens)
                    return;
                m_lens->setParams(p);
                refreshWorkingSource();   // re-render the corrected base
                refreshBaseImage();       // re-apply heal onto it, keep the view
                updatePreview();
            });
    connect(m_lensPanel, &LensPanel::closed, this, &MainWindow::closeLensTool);

    m_sharpenPanel = new SharpenPanel(this);
    connect(m_sharpenPanel, &SharpenPanel::valuesChanged, this,
            [this](const SharpenNode::Values &v) {
                if (!m_sharpen)
                    return;
                m_sharpen->setValues(v);
                // Sharpen bakes into the base; coalesce drags so we don't kick a
                // full-res pass per tick.
                m_bakeOp = BakeOp::Sharpen;
                m_bakeTimer->start();
            });
    connect(m_sharpenPanel, &SharpenPanel::closed, this, &MainWindow::closeSharpenTool);

    m_grainPanel = new GrainPanel(this);
    connect(m_grainPanel, &GrainPanel::valuesChanged, this,
            [this](const GrainNode::Values &v) {
                if (!m_grain)
                    return;
                m_grain->setValues(v);
                updatePreview(); // grain is a live GPU step (no base re-bake)
            });
    connect(m_grainPanel, &GrainPanel::closed, this, &MainWindow::closeGrainTool);

    m_vignettePanel = new VignettePanel(this);
    connect(m_vignettePanel, &VignettePanel::valuesChanged, this,
            [this](const VignetteParams &v) {
                m_graph.setVignette(v);
                m_canvas->setVignette(v);
                updatePreview(); // vignette is a live present-pass op (no base re-bake)
            });
    connect(m_vignettePanel, &VignettePanel::closed, this, &MainWindow::closeVignetteTool);

    m_cropPanel = new CropPanel(this);
    connect(m_cropPanel, &CropPanel::aspectChanged, this, [this](double aspect) {
        if (m_cropGizmo)
            m_cropGizmo->setAspect(aspect);
    });
    connect(m_cropPanel, &CropPanel::rotateRequested, this, [this](int deltaCW) {
        CropState c = m_graph.crop();
        c.rotation = ((c.rotation + deltaCW) % 360 + 360) % 360;
        c.rect = QRectF(0.0, 0.0, 1.0, 1.0); // a re-orient resets the rectangle
        m_graph.setCrop(c);
        if (m_cropGizmo)
            m_cropGizmo->setRect(c.rect);
        updateCropView();
        updatePreview();
    });
    connect(m_cropPanel, &CropPanel::flipRequested, this, [this](bool horizontal) {
        CropState c = m_graph.crop();
        if (horizontal)
            c.flipH = !c.flipH;
        else
            c.flipV = !c.flipV;
        m_graph.setCrop(c);
        updateCropView();
        updatePreview();
    });
    connect(m_cropPanel, &CropPanel::resetRequested, this, [this] {
        m_graph.setCrop(CropState{});
        if (m_cropGizmo)
            m_cropGizmo->setRect(QRectF(0.0, 0.0, 1.0, 1.0));
        if (m_cropPanel->isVisible())
            m_cropPanel->reveal(m_graph.crop(), sourceAspect());
        updateCropView();
        updatePreview();
    });
    connect(m_cropPanel, &CropPanel::closed, this, &MainWindow::closeCropTool);

    m_denoisePanel = new DenoisePanel(this);
    connect(m_denoisePanel, &DenoisePanel::valuesChanged, this,
            [this](const DenoiseNode::Values &v) {
                if (!m_denoise)
                    return;
                m_denoise->setValues(v);
                // Denoise bakes into the base; coalesce drags (it's a full-res
                // LAB pass) just like sharpen.
                m_bakeOp = BakeOp::Denoise;
                m_bakeTimer->start();
            });
    connect(m_denoisePanel, &DenoisePanel::closed, this, &MainWindow::closeDenoiseTool);

    m_defringePanel = new DefringePanel(this);
    connect(m_defringePanel, &DefringePanel::valuesChanged, this,
            [this](const DefringeNode::Values &v) {
                if (!m_defringe)
                    return;
                m_defringe->setValues(v);
                // Defringe bakes into the base (a full-res LAB pass); coalesce
                // drags like denoise/sharpen.
                m_bakeOp = BakeOp::Defringe;
                m_bakeTimer->start();
            });
    connect(m_defringePanel, &DefringePanel::closed, this, &MainWindow::closeDefringeTool);

    m_rawPanel = new RawSettingsPanel(this);
    connect(m_rawPanel, &RawSettingsPanel::valuesChanged, this,
            [this](const raw::RawDecodeOptions &opts, const raw::RawLensDefaults &lens) {
                const bool decodeChanged = !(opts == m_rawOptions);
                const bool lensChanged = !(lens == m_rawLensDefaults);
                m_rawOptions = opts;
                m_rawLensDefaults = lens;
                saveRawDefaults(m_rawOptions, m_rawLensDefaults); // new global default
                // Apply the lens-correction toggles to the open photo, preserving
                // its perspective and other lens params.
                if (lensChanged && m_lens) {
                    LensCorrectionNode::Params p = m_lens->params();
                    p.distortion = lens.distortion;
                    p.tca = lens.tca;
                    p.vignetting = lens.vignetting;
                    m_lens->setParams(p);
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
        static_cast<BusyBadge *>(m_healBusy)->stop();
        if (!m_healWatcher.future().isValid())
            return;
        const QImage healed = m_healWatcher.result();
        if (!healed.isNull())
            m_canvas->setImage(healed, /*keepView=*/true); // preserve zoom/pan
        // The histogram defers itself while a bake runs; recompute now (debounced).
        if (m_histogram->isVisible())
            m_histTimer->start();
    });

    // Background export finished: stop the badge and report the outcome.
    connect(&m_exportWatcher, &QFutureWatcher<ExportResult>::finished, this, [this] {
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
        static_cast<BusyBadge *>(m_healBusy)->stop();
        if (!m_saveWatcher.future().isValid())
            return;
        const SaveResult r = m_saveWatcher.result();
        if (!r.ok) {
            QMessageBox::warning(this, QStringLiteral("Lumen"),
                                 QStringLiteral("Could not save project: %1").arg(r.error));
            return;
        }
        applySaveSuccess(r.path);
    });

    // Background image open finished: install the decoded source + rebuild.
    connect(&m_openImageWatcher, &QFutureWatcher<OpenImageResult>::finished, this, [this] {
        static_cast<BusyBadge *>(m_healBusy)->stop();
        if (!m_openImageWatcher.future().isValid())
            return;
        finishOpenImage(m_openImageWatcher.result());
    });

    // Background project open finished: install the decoded document.
    connect(&m_openProjectWatcher, &QFutureWatcher<OpenProjectResult>::finished, this, [this] {
        static_cast<BusyBadge *>(m_healBusy)->stop();
        if (!m_openProjectWatcher.future().isValid())
            return;
        const OpenProjectResult r = m_openProjectWatcher.result();
        if (!r.loaded) {
            QMessageBox::warning(this, QStringLiteral("Lumen"), r.error);
            return;
        }
        if (r.source.isNull()) {
            QMessageBox::warning(
                this, QStringLiteral("Lumen"),
                QStringLiteral("Could not decode the embedded image: %1").arg(r.error));
            return;
        }
        applyProjectResult(r);
    });

    // Background RAW re-decode finished: install the new source and rebuild the
    // pipeline (the watcher only delivers the latest request's future).
    connect(&m_decodeWatcher, &QFutureWatcher<DecodeResult>::finished, this, [this] {
        static_cast<BusyBadge *>(m_healBusy)->stop();
        if (!m_decodeWatcher.future().isValid())
            return;
        const DecodeResult r = m_decodeWatcher.result();
        if (r.image.isNull()) {
            if (!r.error.isEmpty())
                showHint(QStringLiteral("Could not re-decode RAW: %1").arg(r.error));
            return;
        }
        m_graph.setSource(r.image);
        // Keep the current WB temperature (don't reseed to as-shot).
        applyCameraProfile(r.meta.color, /*seedKelvin=*/false);
        refreshWorkingSource();  // rebuild the corrected source + display QImage
        refreshBaseImage(true);  // keep zoom/pan (itself async if a bake is active)
        recomputeSelectiveMask();
        updatePreview();
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
        m_heal->setHealMask(m_brushMask);
        refreshBaseImage();
        updatePreview();
    });
    connect(m_healPanel, &HealPanel::qualityChanged, this, [this](bool hq) {
        m_heal->setHighQuality(hq);
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
        if (i <= 0 || i >= m_graph.layerCount())
            return; // the Base layer (0) is not renamable
        const QString t = name.trimmed();
        if (t.isEmpty() || t == m_graph.layer(i).name()) {
            refreshLayersPanel(); // restore the row (rejects empty/unchanged)
            return;
        }
        m_graph.layer(i).setName(t);
        refreshLayersPanel();
        m_graph.commit();
    });
    connect(m_layersPanel, &LayersPanel::visibilityToggled, this, [this](int i, bool on) {
        if (i >= 0 && i < m_graph.layerCount()) {
            m_graph.layer(i).setEnabled(on);
            refreshLayersPanel();
            updatePreview();
            m_graph.commit();
        }
    });
    connect(m_layersPanel, &LayersPanel::opacityChanged, this, [this](int percent) {
        m_graph.activeLayer().setOpacity(percent / 100.0f);
        updatePreview();
    });
    connect(m_layersPanel, &LayersPanel::maskTypeChanged, this,
            &MainWindow::setActiveLayerMaskType);
    connect(m_layersPanel, &LayersPanel::maskFeatherChanged, this, [this](int percent) {
        MaskSpec spec = m_graph.activeLayer().mask();
        spec.feather = percent / 100.0f;
        m_graph.activeLayer().setMask(spec);
        recomputeSelectiveMask();
        updatePreview();
        m_graph.commit();
    });
    connect(m_layersPanel, &LayersPanel::maskInvertChanged, this, [this](bool on) {
        MaskSpec spec = m_graph.activeLayer().mask();
        spec.invert = on;
        m_graph.activeLayer().setMask(spec);
        recomputeSelectiveMask();
        updatePreview();
        m_graph.commit();
    });
    connect(m_layersPanel, &LayersPanel::maskRangeChanged, this, [this](int low, int high) {
        MaskSpec spec = m_graph.activeLayer().mask();
        spec.low = low / 100.0f;
        spec.high = high / 100.0f;
        m_graph.activeLayer().setMask(spec);
        recomputeSelectiveMask();
        updatePreview();
        m_graph.commit();
    });
    connect(m_layersPanel, &LayersPanel::maskColorRangeChanged, this, [this](int percent) {
        MaskSpec spec = m_graph.activeLayer().mask();
        spec.colorRange = percent / 100.0f;
        m_graph.activeLayer().setMask(spec);
        recomputeSelectiveMask();
        updatePreview();
        m_graph.commit();
    });
    connect(m_layersPanel, &LayersPanel::maskPickColorRequested, this, [this] {
        m_pickPurpose = PickPurpose::MaskColour;
        m_canvas->setColorPickMode(true);
    });
    connect(m_layersPanel, &LayersPanel::maskShowChanged, this, [this](int mode) {
        m_maskView = mode;
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
        if (m_graph.activeLayerIndex() == 0)
            return;
        MaskSpec spec = m_graph.activeLayer().mask();
        spec.zones = shapes;
        m_graph.activeLayer().setMask(spec);
        m_layersPanel->setZoneCount(static_cast<int>(shapes.size()));
        recomputeSelectiveMask();
        updatePreview();
        if (commit)
            m_graph.commit();
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
        CropState c = m_graph.crop();
        c.rect = r;
        m_graph.setCrop(c);
        // Live: don't commit yet (editFinished commits); the present pass stays in
        // Editing mode (full frame) so the gizmo keeps mapping 1:1.
    });
    connect(m_cropGizmo, &CropGizmo::editFinished, this, [this](const QRectF &r) {
        CropState c = m_graph.crop();
        c.rect = r;
        m_graph.setCrop(c);
        m_graph.commit();
    });
    connect(m_canvas, &CanvasWidget::viewChanged, m_cropGizmo, [this] { m_cropGizmo->update(); });

    connect(m_layersPanel, &LayersPanel::zoneToolChanged, this, [this](int tool) {
        // Engaging a tool implies you want to see the overlay: un-hide first.
        if (m_overlaysHidden)
            setOverlayGeometryVisible(true);
        m_zoneGizmo->setTool(static_cast<ZoneGizmo::Tool>(tool));
        syncZoneGizmo(); // ensure it is shown while a tool is engaged
    });
    connect(m_layersPanel, &LayersPanel::overlayVisibilityChanged, this,
            [this](bool visible) { setOverlayGeometryVisible(visible); });
    connect(m_layersPanel, &LayersPanel::zoneModeChanged, this,
            [this](bool subtract) { m_zoneGizmo->setSubtract(subtract); });
    connect(m_layersPanel, &LayersPanel::zoneFeatherChanged, this, [this](int percent) {
        if (m_graph.activeLayerIndex() == 0)
            return;
        MaskSpec spec = m_graph.activeLayer().mask();
        spec.zoneFeather = percent / 100.0f;
        m_graph.activeLayer().setMask(spec);
        recomputeSelectiveMask();
        updatePreview();
        m_graph.commit();
    });
    connect(m_layersPanel, &LayersPanel::zoneClearRequested, this, [this] {
        if (m_graph.activeLayerIndex() == 0)
            return;
        MaskSpec spec = m_graph.activeLayer().mask();
        if (spec.zones.empty())
            return;
        spec.zones.clear();
        m_graph.activeLayer().setMask(spec);
        m_layersPanel->setZoneCount(0);
        syncZoneGizmo();
        recomputeSelectiveMask();
        updatePreview();
        m_graph.commit();
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
                             m_monoPanel,     m_colorMixerPanel, m_colorGradePanel,
                             m_lensPanel,     m_sharpenPanel, m_denoisePanel,
                             m_defringePanel, m_rawPanel,     m_grainPanel,
                             m_vignettePanel, m_cropPanel,    m_healPanel};
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
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")), this, [this] { openProject(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")), this, [this] { copySettings(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")), this, [this] { pasteSettings(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Q")), this, [this] { close(); });
    new QShortcut(QKeySequence(Qt::Key_F11), this, [this] { toggleFullScreen(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+0")), this, [this] { m_canvas->resetView(); });
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
        m_autosaveInFlight = false;
        if (m_autosaveWatcher.result())
            m_lastAutosaveDoc = m_pendingAutosaveDoc; // committed; skip identical writes
        else
            showHint(QStringLiteral("Autosave failed — your work is not yet saved"));
        m_pendingAutosaveDoc.clear();
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
    ++m_healGen; // signal in-flight tasks to bail at their next checkpoint
    ++m_histGen;
    ++m_decodeGen;
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
        {QStringLiteral("save-project"), QStringLiteral("Save project (.lumen)…"), file},
        {QStringLiteral("export"), QStringLiteral("Export image…"), file},
        {QStringLiteral("tone"), QStringLiteral("Tone (exposure, contrast, saturation)"), toneColor},
        {QStringLiteral("curves"), QStringLiteral("Curves"), toneColor},
        {QStringLiteral("colormixer"), QStringLiteral("Color mixer (HSL)"), toneColor},
        {QStringLiteral("colorgrade"), QStringLiteral("Color grading (wheels)"), toneColor},
        {QStringLiteral("monochrome"), QStringLiteral("Monochrome (B&W + toning)"), toneColor},
        {QStringLiteral("looks"), QStringLiteral("Looks (LUT)"), toneColor},
        {QStringLiteral("sharpen"), QStringLiteral("Sharpen"), detail},
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
        {QStringLiteral("copy-settings"), QStringLiteral("Copy edit settings"), presets},
        {QStringLiteral("paste-settings"), QStringLiteral("Paste edit settings"), presets},
        {QStringLiteral("save-preset"), QStringLiteral("Save preset…"), presets},
        {QStringLiteral("apply-preset"), QStringLiteral("Apply preset…"), presets},
        {QStringLiteral("undo"), QStringLiteral("Undo"), edit},
        {QStringLiteral("redo"), QStringLiteral("Redo"), edit},
        {QStringLiteral("histogram"), QStringLiteral("Histogram (toggle)"), view},
        {QStringLiteral("clipping"), QStringLiteral("Clipping warnings (toggle)"), view},
        {QStringLiteral("adjustments"), QStringLiteral("Adjustments (history)"), view},
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
    if (!maybeSaveBeforeDiscard()) // guard unsaved work on the current document
        return false;
    if (path.endsWith(QStringLiteral(".lumen"), Qt::CaseInsensitive)) {
        loadProjectFile(path); // a project, not a raw image (async)
        return true;
    }
    if (openBusy()) {
        showHint(QStringLiteral("Already opening a file"));
        return false;
    }

    // Decode off the UI thread so the app stays responsive and the "Opening…"
    // badge animates (a RAW demosaic is slow); finishOpenImage installs it.
    auto *badge = static_cast<BusyBadge *>(m_healBusy);
    badge->setLabel(QStringLiteral("Opening…"));
    badge->start();
    layoutOverlays();
    const raw::RawDecodeOptions opts = m_rawOptions;
    m_openImageWatcher.setFuture(
        QtConcurrent::run([path, opts] { return decodeImageFile(path, opts); }));
    return true;
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

    // Keep the original encoded bytes so we can embed them verbatim in a .lumen.
    m_sourceBytes = r.bytes;
    m_sourceName = QFileInfo(r.path).fileName();
    m_projectPath.clear(); // opening a raw image starts a new (unsaved) project

    // A freshly opened image starts from a clean slate: clear any in-progress
    // brush editing and reset the graph to its pristine state so the previous
    // image's adjustments (and any selective layers) don't carry over.
    m_brushTarget = BrushTarget::None;
    m_maskView = 0;
    m_brushUndo.clear();
    m_brushMask = MaskBuffer();
    m_graph.restoreState(m_defaultGraphState);

    m_graph.setSource(r.source); // full-res original; the lens node corrects on top
    // Seed the lens node: a RAW carries EXIF identity for automatic correction;
    // anything else starts neutral (manual perspective still available).
    LensCorrectionNode::Params lp; // defaults: corrections on, perspective neutral
    if (r.isRaw) {
        lp.cameraMaker = r.meta.cameraMaker;
        lp.cameraModel = r.meta.cameraModel;
        lp.lensModel = r.meta.lensModel;
        lp.focalLength = r.meta.focalLength;
        lp.aperture = r.meta.aperture;
        lp.focusDistance = r.meta.focusDistance;
        // Seed the automatic lens corrections from the user's RAW defaults.
        lp.distortion = m_rawLensDefaults.distortion;
        lp.tca = m_rawLensDefaults.tca;
        lp.vignetting = m_rawLensDefaults.vignetting;
    }
    m_lens->setParams(lp);
    // Camera-accurate white balance: install the colour profile and seed the
    // slider at the as-shot temperature (non-RAW keeps the sRGB defaults).
    if (r.isRaw)
        applyCameraProfile(r.meta.color, /*seedKelvin=*/true);
    refreshWorkingSource();  // build the corrected source + display QImage
    refreshBaseImage(false); // new image → fit the view
    recomputeSelectiveMask();
    updatePreview();        // apply any existing edits
    m_graph.resetHistory(); // fresh undo timeline for this image
    m_sourcePath = r.path;
    // New unsaved document: clear any prior recovery file, baseline the pristine
    // state (so an unedited open won't autosave), and arm the autosave timer.
    m_recoveryPath.clear();
    resetAutosaveBaseline();
    startAutosave();
    if (m_layersPanel->isVisible())
        refreshLayersPanel();   // the reset dropped any selective layers
    reseedOpenPanels();         // re-sync open tools with the neutral defaults
    setWindowTitle(QStringLiteral("Lumen — %1").arg(QFileInfo(r.path).fileName()));
}

void MainWindow::redecodeCurrent()
{
    // Only RAW sources can be re-decoded (others have no decode-time options).
    if (m_sourceBytes.isEmpty() || !raw::isRawPath(m_sourceName))
        return;

    // Run the demosaic off the UI thread so the app stays responsive and the busy
    // badge animates; the latest request wins (m_decodeGen guards stale results).
    const quint64 gen = ++m_decodeGen;
    auto *badge = static_cast<BusyBadge *>(m_healBusy);
    badge->setLabel(QStringLiteral("Decoding…"));
    badge->start();
    layoutOverlays(); // position the badge + dim

    const QByteArray bytes = m_sourceBytes;
    const raw::RawDecodeOptions opts = m_rawOptions;
    m_decodeWatcher.setFuture(QtConcurrent::run([this, gen, bytes, opts]() -> DecodeResult {
        if (gen != m_decodeGen)
            return {}; // superseded before we even started
        DecodeResult r;
        r.image = raw::decodeBytes(bytes.constData(), bytes.size(), &r.error, &r.meta, opts);
        return r;
    }));
}

QString MainWindow::promptSaveProjectPath()
{
    // Default name "<source>.lumen", in the last-used project folder (falling
    // back to next-to-the-source on first save).
    const QFileInfo src(m_projectPath.isEmpty() ? m_sourcePath : m_projectPath);
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

void MainWindow::applySaveSuccess(const QString &path)
{
    m_projectPath = path;
    rememberDir(QStringLiteral("saveProject"), path);
    // The work now lives in the user's file; autosave targets it from here on and
    // the transient recovery file is no longer needed.
    deleteRecoveryFile();
    resetAutosaveBaseline();
    startAutosave();
    setWindowTitle(QStringLiteral("Lumen — %1").arg(QFileInfo(path).fileName()));
    showHint(QStringLiteral("Saved %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::saveProject()
{
    if (m_graph.source().isNull()) {
        showHint(QStringLiteral("Open an image before saving a project"));
        return;
    }
    if (m_saveWatcher.isRunning()) {
        showHint(QStringLiteral("A save is already in progress"));
        return;
    }
    const QString path = promptSaveProjectPath();
    if (path.isEmpty())
        return;

    // Snapshot the document on the UI thread (reads the graph), then write it off
    // the UI thread behind the "Saving…" badge — the write can be slow to external
    // or network drives.
    QString name;
    const QByteArray bytes = sourceForSave(&name);
    const QJsonObject graph = buildDocGraph();

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

bool MainWindow::saveProjectSync()
{
    // The blocking save the quit/discard flow needs: it must complete before the
    // document can be discarded, so it can't go through the async path.
    if (m_graph.source().isNull())
        return true; // nothing to save
    const QString path = promptSaveProjectPath();
    if (path.isEmpty())
        return false; // user cancelled the dialog → don't discard their work

    QString name;
    const QByteArray bytes = sourceForSave(&name);
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QString error;
    const bool ok = autosave::writeProjectAtomic(path, buildDocGraph(), bytes, name, &error);
    QGuiApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Lumen"),
                             QStringLiteral("Could not save project: %1").arg(error));
        return false;
    }
    applySaveSuccess(path);
    return true;
}

// --- Autosave & crash recovery ---------------------------------------------

QJsonObject MainWindow::buildDocGraph() const
{
    // The edit graph plus the per-project RAW decode options — the exact document
    // a project file persists. (The graph stays decode-agnostic; loadProjectFile
    // reads the rawOptions key back.)
    QJsonObject graph = m_graph.saveState();
    graph[QStringLiteral("rawOptions")] = m_rawOptions.toJson();
    return graph;
}

QByteArray MainWindow::currentDocBytes() const
{
    return QJsonDocument(buildDocGraph()).toJson(QJsonDocument::Compact);
}

QByteArray MainWindow::sourceForSave(QString *name) const
{
    // Embed the original encoded bytes; fall back to a PNG of the source if they
    // aren't available (e.g. a source we only hold as a decoded QImage).
    QByteArray bytes = m_sourceBytes;
    QString outName = m_sourceName;
    if (bytes.isEmpty() && !m_sourceQImage.isNull()) {
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        m_sourceQImage.save(&buf, "PNG");
        outName = QStringLiteral("source.png");
    }
    if (name)
        *name = outName;
    return bytes;
}

void MainWindow::resetAutosaveBaseline()
{
    m_openDoc = m_lastAutosaveDoc = currentDocBytes();
}

void MainWindow::startAutosave()
{
    if (!m_graph.source().isNull())
        m_autosaveTimer->start();
}

void MainWindow::deleteRecoveryFile()
{
    if (m_recoveryPath.isEmpty())
        return;
    QFile::remove(m_recoveryPath);
    QFile::remove(m_recoveryPath + QStringLiteral(".tmp"));
    m_recoveryPath.clear();
}

void MainWindow::performAutosave()
{
    // Never persist a transient "show up to here" view — its disabled flags aren't
    // part of the real document.
    if (m_graph.source().isNull() || m_autosaveInFlight || m_peeking)
        return;
    const QByteArray doc = currentDocBytes();
    if (doc == m_lastAutosaveDoc)
        return; // nothing changed since the last write
    if (m_projectPath.isEmpty() && doc == m_openDoc)
        return; // opened but never edited — don't spawn a recovery file

    QString target = m_projectPath;
    if (target.isEmpty()) {
        if (m_recoveryPath.isEmpty())
            m_recoveryPath = autosave::newRecoveryPath(
                autosave::projectsDir(), m_sourceName, QDateTime::currentDateTime());
        target = m_recoveryPath;
    }
    if (target.isEmpty())
        return; // no writable location (projectsDir failed)

    QString name;
    const QByteArray bytes = sourceForSave(&name);
    const QJsonObject graph = buildDocGraph();
    m_pendingAutosaveDoc = doc;
    m_autosaveInFlight = true;
    // Write off the UI thread: embedding the full source can be tens of MB. The
    // snapshot args are value copies, so the worker touches nothing the UI mutates.
    m_autosaveWatcher.setFuture(QtConcurrent::run([target, graph, bytes, name] {
        return autosave::writeProjectAtomic(target, graph, bytes, name);
    }));
}

bool MainWindow::flushAutosaveSync()
{
    if (m_graph.source().isNull())
        return true;
    // Don't race a background write to the same target.
    if (m_autosaveWatcher.isRunning())
        m_autosaveWatcher.waitForFinished();
    m_autosaveInFlight = false;

    const QByteArray doc = currentDocBytes();
    if (doc == m_lastAutosaveDoc)
        return true; // already persisted
    QString target = m_projectPath.isEmpty() ? m_recoveryPath : m_projectPath;
    if (target.isEmpty()) // unsaved & no recovery file yet → nothing to flush to
        return true;
    QString name;
    const QByteArray bytes = sourceForSave(&name);
    QString error;
    if (!autosave::writeProjectAtomic(target, buildDocGraph(), bytes, name, &error)) {
        QMessageBox::warning(this, QStringLiteral("Lumen"),
                             QStringLiteral("Could not save your work: %1").arg(error));
        return false;
    }
    m_lastAutosaveDoc = doc;
    return true;
}

bool MainWindow::maybeSaveBeforeDiscard()
{
    exitPeek(); // restore the real document before any save / serialise / discard
    if (m_graph.source().isNull())
        return true;
    if (!m_projectPath.isEmpty()) {
        // A saved document is continuously autosaved to the user's file: flush the
        // latest edits and discard silently (no prompt). On a write failure, fall
        // through to the prompt so the user can choose another location.
        if (flushAutosaveSync())
            return true;
    } else if (currentDocBytes() == m_openDoc) {
        return true; // opened but never edited — nothing to lose
    }

    const QMessageBox::StandardButton choice = QMessageBox::question(
        this, QStringLiteral("Lumen"), QStringLiteral("Do you want to save your work?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel)
        return false;
    if (choice == QMessageBox::Discard) {
        deleteRecoveryFile();
        return true;
    }
    // Save: route through the dialog synchronously — the document must actually be
    // persisted before we allow the discard, so this can't use the async path.
    return saveProjectSync();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!maybeSaveBeforeDiscard()) {
        e->ignore();
        return;
    }
    // Clean exit: drop this session's recovery file so the next launch doesn't
    // mistake it for a crash.
    if (m_autosaveTimer)
        m_autosaveTimer->stop();
    deleteRecoveryFile();
    e->accept();
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
    if (!maybeSaveBeforeDiscard()) // don't lose unsaved work on the current doc
        return;
    rememberDir(QStringLiteral("openProject"), path);
    loadProjectFile(path);
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
    m_rawOptions = r.rawOptions; // adopt the project's decode options

    // Reset any active editing state, then load the document.
    m_brushTarget = BrushTarget::None;
    m_maskView = 0;
    m_brushUndo.clear();
    m_sourceBytes = r.sourceBytes;
    m_sourceName = r.sourceName;
    m_graph.setSource(r.source);
    m_graph.loadProjectState(r.graph); // restores the lens node's params too
    // Refresh the camera profile from the actual decode, keeping the restored
    // WB temperature (don't reseed to as-shot).
    if (r.isRaw)
        applyCameraProfile(r.meta.color, /*seedKelvin=*/false);

    refreshWorkingSource();  // rebuild the corrected source from the restored lens
    refreshBaseImage(false); // re-applies the heal mask, fits the view
    recomputeSelectiveMask();
    updateCropView();        // push the restored crop/orientation to the canvas
    updatePreview();
    m_graph.resetHistory();

    m_projectPath = r.path;
    m_sourcePath = r.path; // export defaults to "<project>-edited.<ext>"
    // A fresh document session: autosave now targets this user file (no recovery
    // file), and the baseline is the just-loaded state.
    m_recoveryPath.clear();
    resetAutosaveBaseline();
    startAutosave();
    if (m_layersPanel->isVisible())
        refreshLayersPanel();
    reseedOpenPanels(); // re-sync any open adjustment tool with the restored layers
    setWindowTitle(QStringLiteral("Lumen — %1").arg(QFileInfo(r.path).fileName()));
    showHint(QStringLiteral("Opened %1").arg(QFileInfo(r.path).fileName()));
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
    m_openProjectWatcher.setFuture(
        QtConcurrent::run([path] { return decodeProjectFile(path); }));
}

// --- Reusable presets / copy-paste settings --------------------------------

void MainWindow::copySettings()
{
    if (m_graph.source().isNull()) {
        showHint(QStringLiteral("Open an image before copying settings"));
        return;
    }
    m_copiedSettings = preset::fromGraph(m_graph);
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
    if (m_graph.source().isNull()) {
        showHint(QStringLiteral("Open an image before saving a preset"));
        return;
    }
    const QFileInfo src(m_sourcePath);
    const QString dir = lastDir(QStringLiteral("preset"), src.dir().path());
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
    if (!preset::save(path, preset::fromGraph(m_graph, name), &error)) {
        QMessageBox::warning(this, QStringLiteral("Lumen"),
                             QStringLiteral("Could not save preset: %1").arg(error));
        return;
    }
    rememberDir(QStringLiteral("preset"), path);
    showHint(QStringLiteral("Saved preset “%1”").arg(name));
}

void MainWindow::applyPresetFile()
{
    if (m_graph.source().isNull()) {
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
    if (!preset::applyToGraph(presetObj, m_graph)) {
        showHint(QStringLiteral("That isn't a valid preset"));
        return;
    }
    // One undoable step, then refresh the preview and reseed any open tools — the
    // same path undo/redo uses to reflect a graph change in the UI.
    m_graph.commit();
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
    m_projectPath.clear();
    m_recoveryPath = path;
    m_sourcePath = m_sourceName; // export naming from the original, not the temp file
    m_openDoc = QByteArray();    // sentinel: recovered work is treated as unsaved
    setWindowTitle(
        QStringLiteral("Lumen — %1 (recovered)").arg(QFileInfo(m_sourceName).fileName()));
    showHint(QStringLiteral("Restored unsaved work — save it to keep a copy"));
    return true;
}

bool MainWindow::offerCrashRecovery()
{
    const QStringList files = autosave::findRecoveryFiles(autosave::projectsDir());
    if (files.isEmpty())
        return false;
    const QString newest = files.first();

    // Peek the original source name for a friendlier prompt.
    QString sourceLabel;
    project::Project peek;
    if (project::load(newest, &peek))
        sourceLabel = peek.sourceName;
    const QString what = sourceLabel.isEmpty()
                             ? QStringLiteral("your unsaved work")
                             : QStringLiteral("your unsaved work on “%1”").arg(sourceLabel);

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

    if (!restoreRecovery(newest))
        return false;
    for (const QString &f : files) { // the newest is now live; drop the older ones
        if (f != newest) {
            QFile::remove(f);
            QFile::remove(f + QStringLiteral(".tmp"));
        }
    }
    return true;
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
    if (m_graph.source().isNull()) {
        showHint(QStringLiteral("Open an image before exporting"));
        return;
    }
    if (m_exportWatcher.isRunning()) {
        showHint(QStringLiteral("An export is already in progress"));
        return;
    }

    // 1. Choose format + quality + size + colour space.
    ExportDialog dlg(this);
    dlg.setSelection(m_exportExt, m_exportQuality, m_exportLongEdge, m_exportColorSpace);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_exportExt = dlg.extension();
    const int quality = dlg.quality();
    if (quality >= 0)
        m_exportQuality = quality;
    m_exportLongEdge = dlg.longEdge();
    m_exportColorSpace = dlg.colorSpace();
    const Image::ExportOptions exportOpts{quality, dlg.bits(), m_exportLongEdge,
                                          m_exportColorSpace};

    // 2. Choose the path, defaulting to "<name>-edited.<ext>" in the last-used
    //    export folder (falling back to next-to-the-source).
    const QFileInfo src(m_sourcePath);
    const QString dir = lastDir(QStringLiteral("export"), src.dir().path());
    const QString suggested = QDir(dir).filePath(
        src.completeBaseName() + QStringLiteral("-edited.") + m_exportExt);
    const QString filter =
        QStringLiteral("%1 (*.%2)").arg(m_exportExt.toUpper(), m_exportExt);
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export image"),
                                                suggested, filter);
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".") + m_exportExt;

    // 3. Walk the graph and write at full resolution OFF the UI thread: a full-res
    //    libvips encode is slow, and with an active heal mask result() runs the
    //    inpaint inline (HealNode::apply is eager) — either would freeze the app.
    //    The busy badge shows progress; re-entry is blocked above. The finished
    //    handler reports success/failure.
    const bool healActive =
        m_heal && m_heal->isEnabled() && !m_heal->healMask().isEmpty();
    // For the non-heal case the pipeline is lazy, so snapshot it on the UI thread
    // (race-free) and only materialise on the worker; with heal active, build it
    // on the worker so the eager inpaint never blocks the UI.
    const Image snapshot = healActive ? Image() : m_graph.result();
    auto *badge = static_cast<BusyBadge *>(m_healBusy);
    badge->setLabel(QStringLiteral("Exporting…"));
    badge->start();
    layoutOverlays();
    m_exportWatcher.setFuture(QtConcurrent::run(
        [this, path, exportOpts, healActive, snapshot]() -> ExportResult {
            ExportResult r;
            r.path = path;
            const Image result = healActive ? m_graph.result() : snapshot;
            if (result.isNull()) {
                r.error = QStringLiteral("nothing to export");
                return r;
            }
            r.ok = result.saveToFile(path, exportOpts, &r.error);
            return r;
        }));
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
    if (m_compareOriginal) {
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
    const int overlayView = (m_maskView != 0) ? m_maskView : (m_selectivePainting ? 1 : 0);

    PreviewState ps = m_graph.previewState();
    ps.selMaskView = static_cast<float>(overlayView); // preview-only overlay
    if (overlayView != 0)
        ps.selMaskMode = 1.0f; // overlay samples the uploaded mask texture
    // Fade the explicit "Show mask" highlight by the active layer's opacity so
    // opacity has a visible effect while inspecting the mask. While painting we
    // keep it full strength so the strokes you're drawing stay visible.
    if (!m_selectivePainting && m_maskView != 0 && m_graph.activeLayerIndex() > 0)
        ps.selMaskOpacity = m_graph.activeLayer().opacity();
    if (m_healPainting) {
        // Show the in-progress heal stroke as a red overlay (the mask texture),
        // without any selective adjustment.
        ps.selEnabled = 0.0f;
        ps.selMaskMode = 1.0f;
        ps.selMaskView = 1.0f;
    }
    m_canvas->setPreviewState(ps);
    m_canvas->setCurveLuts(m_graph.previewLut());
    m_canvas->setLut3D(m_graph.previewLook());
    m_canvas->setVignette(m_graph.vignette()); // creative vignette (present pass)

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
    for (int i = 1; i < m_graph.layerCount(); ++i) {
        if (hasMask(m_graph.layer(i).mask())) {
            needsMaskSrc = true;
            break;
        }
    }
    constexpr int cap = 1280;
    QImage maskSrc;
    if (needsMaskSrc && !m_sourceQImage.isNull()) {
        maskSrc = m_sourceQImage;
        if (std::max(maskSrc.width(), maskSrc.height()) > cap)
            maskSrc = maskSrc.scaled(cap, cap, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
        maskSrc = maskSrc.convertToFormat(QImage::Format_RGBA8888);
    }
    const int mw = maskSrc.width(), mh = maskSrc.height();
    const uint8_t *maskRgba = maskSrc.isNull() ? nullptr : maskSrc.constBits();

    // Layers above the Base, composited as extra GPU passes.
    const int activeIdx = m_graph.activeLayerIndex();
    std::vector<LayerPreview> extras;
    for (int i = 1; i < m_graph.layerCount(); ++i) {
        Layer &layer = m_graph.layer(i);
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

void MainWindow::applyCameraProfile(const raw::ColorProfile &profile, bool seedKelvin)
{
    if (!profile.valid)
        return;
    for (int i = 0; i < m_graph.layerCount(); ++i) {
        if (auto *t = static_cast<TuneNode *>(
                m_graph.layer(i).nodeOfType(QStringLiteral("tune"))))
            t->setCameraProfile(profile.camToRgb, profile.xyzToCam, profile.asShotMul,
                                seedKelvin);
    }
}

TuneNode *MainWindow::activeTune() const
{
    return static_cast<TuneNode *>(m_graph.activeLayer().nodeOfType(QStringLiteral("tune")));
}

CurvesNode *MainWindow::activeCurves() const
{
    return static_cast<CurvesNode *>(m_graph.activeLayer().nodeOfType(QStringLiteral("curves")));
}

LutNode *MainWindow::activeLut() const
{
    return static_cast<LutNode *>(m_graph.activeLayer().nodeOfType(QStringLiteral("lut")));
}

MonoNode *MainWindow::activeMono() const
{
    return static_cast<MonoNode *>(m_graph.activeLayer().nodeOfType(QStringLiteral("mono")));
}

ColorGradeNode *MainWindow::activeColorGrade() const
{
    return static_cast<ColorGradeNode *>(
        m_graph.activeLayer().nodeOfType(QStringLiteral("colorgrade")));
}

ColorMixerNode *MainWindow::activeColorMixer() const
{
    return static_cast<ColorMixerNode *>(
        m_graph.activeLayer().nodeOfType(QStringLiteral("colormixer")));
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
    if (m_maskView != 0) {           // clear the mask overlay
        m_maskView = 0;
        updatePreview();
    }
    syncMaskGizmo();                 // hide the gizmo
    syncZoneGizmo();                 // hide the zone editor too
    updateCropView();                // back to the cropped browse view
}

void MainWindow::refreshLayersPanel()
{
    QVector<LayersPanel::Row> rows;
    for (int i = 0; i < m_graph.layerCount(); ++i)
        rows.append({m_graph.layer(i).name(), m_graph.layer(i).enabled()});
    const int active = m_graph.activeLayerIndex();
    m_layersPanel->setLayers(rows, active,
                             static_cast<int>(std::lround(m_graph.activeLayer().opacity() * 100)));
    m_layersPanel->setMaskState(m_graph.activeLayer().mask(), active == 0, m_brushSize,
                                m_brushHardness, m_brushAdd, m_maskView);
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
    Layer &layer = m_graph.addLayer(name);
    // Full adjustment node set so any tool (Tone/Mixer/Curves/Grade/Looks/Mono)
    // works on the layer (added to it as it is now the active layer).
    m_graph.addNode(std::make_unique<TuneNode>());
    m_graph.addNode(std::make_unique<ColorMixerNode>());
    m_graph.addNode(std::make_unique<CurvesNode>());
    m_graph.addNode(std::make_unique<ColorGradeNode>());
    m_graph.addNode(std::make_unique<LutNode>());
    m_graph.addNode(std::make_unique<MonoNode>());
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
    addMaskedAdjustmentLayer(QStringLiteral("Layer %1").arg(m_graph.layerCount()));
    refreshLayersPanel();
    updatePreview();
    m_graph.commit();
}

void MainWindow::deleteActiveLayer()
{
    if (m_graph.removeLayer(m_graph.activeLayerIndex())) {
        refreshLayersPanel();
        updatePreview();
        m_graph.commit();
    }
}

void MainWindow::selectLayer(int index)
{
    endMaskBrushSession(); // commit the current layer's mask-brush before switching
    m_graph.setActiveLayer(index);
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
            m_graph.addNode(std::make_unique<MonoNode>());
            m_graph.commit();
        }
        m_monoPanel->reveal(activeMono()->values());
    }
    if (m_colorMixerPanel->isVisible()) {
        if (!activeColorMixer()) {
            m_graph.addNode(std::make_unique<ColorMixerNode>());
            m_graph.commit();
        }
        m_colorMixerPanel->reveal(activeColorMixer()->values());
    }
    if (m_colorGradePanel->isVisible()) {
        if (!activeColorGrade()) {
            m_graph.addNode(std::make_unique<ColorGradeNode>());
            m_graph.commit();
        }
        m_colorGradePanel->reveal(activeColorGrade()->values());
    }
}

void MainWindow::setActiveLayerMaskType(int maskType)
{
    if (m_graph.activeLayerIndex() == 0)
        return; // the Base layer has no mask
    MaskSpec spec = m_graph.activeLayer().mask();
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
    m_graph.activeLayer().setMask(spec);

    m_layersPanel->setMaskState(spec, /*isBaseActive=*/false, m_brushSize, m_brushHardness,
                                m_brushAdd, m_maskView);
    syncMaskGizmo();
    syncZoneGizmo();
    updateMaskEditing(); // turn the canvas brush on/off for a Brush mask
    recomputeSelectiveMask();
    updatePreview();
    m_graph.commit();
}

void MainWindow::onLayerMaskEdited(const MaskSpec &spec, bool commit)
{
    if (m_graph.activeLayerIndex() == 0)
        return;
    m_graph.activeLayer().setMask(spec);
    recomputeSelectiveMask(); // redraw the overlay as the gizmo drags (if shown)
    updatePreview();
    if (commit)
        m_graph.commit();
}

void MainWindow::syncMaskGizmo()
{
    // The gizmo shows itself only for gradient/radial masks on a non-Base layer —
    // and never while the user has hidden the overlay geometry.
    if (m_overlaysHidden || m_graph.activeLayerIndex() == 0)
        m_maskGizmo->setSpec(MaskSpec{}); // None → hides
    else
        m_maskGizmo->setSpec(m_graph.activeLayer().mask());
    layoutOverlays(); // re-apply gizmo geometry + z-order
}

void MainWindow::syncZoneGizmo()
{
    // Zones are edited only on a non-Base layer while the Layers panel (which
    // hosts the zone controls) is open, and never while overlays are hidden.
    const bool active = !m_overlaysHidden && m_graph.activeLayerIndex() != 0
                        && m_layersPanel->isVisible();
    if (!active) {
        m_zoneGizmo->setVisible(false);
        layoutOverlays();
        return;
    }
    m_zoneGizmo->setShapes(m_graph.activeLayer().mask().zones);
    m_zoneGizmo->setVisible(true);
    layoutOverlays();
}

void MainWindow::setOverlayGeometryVisible(bool visible)
{
    m_overlaysHidden = !visible;
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
    m_graph.commit(); // one undo step per editing session (no-op if unchanged)
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
    m_graph.commit();
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
    m_graph.commit();
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openMonoTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    // Most layers carry a mono node; a layer added by another tool (e.g. a
    // selective layer) may not, so add one on demand.
    if (!activeMono()) {
        m_graph.addNode(std::make_unique<MonoNode>());
        m_graph.commit();
    }
    m_monoPanel->adjustSize();
    const int margin = 18;
    m_monoPanel->move(width() - m_monoPanel->width() - margin, margin);
    m_monoPanel->reveal(activeMono()->values());
}

void MainWindow::closeMonoTool()
{
    m_monoPanel->hide();
    m_graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openColorMixerTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    // Most layers carry a colour-mixer node; a layer added by another tool may
    // not, so add one on demand.
    if (!activeColorMixer()) {
        m_graph.addNode(std::make_unique<ColorMixerNode>());
        m_graph.commit();
    }
    m_colorMixerPanel->adjustSize();
    const int margin = 18;
    m_colorMixerPanel->move(width() - m_colorMixerPanel->width() - margin, margin);
    m_colorMixerPanel->reveal(activeColorMixer()->values());
}

void MainWindow::closeColorMixerTool()
{
    m_colorMixerPanel->hide();
    m_graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openColorGradeTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    // A layer added by another tool may not carry a colour-grade node yet.
    if (!activeColorGrade()) {
        m_graph.addNode(std::make_unique<ColorGradeNode>());
        m_graph.commit();
    }
    m_colorGradePanel->adjustSize();
    const int margin = 18;
    m_colorGradePanel->move(width() - m_colorGradePanel->width() - margin, margin);
    m_colorGradePanel->reveal(activeColorGrade()->values());
}

void MainWindow::closeColorGradeTool()
{
    m_colorGradePanel->hide();
    m_graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openLensTool()
{
    if (!m_lens)
        return;
    m_input.setMode(InputController::Mode::ToolActive);
    m_lensPanel->adjustSize();
    const int margin = 18;
    m_lensPanel->move(width() - m_lensPanel->width() - margin, margin);
    const LensCorrectionNode::Params &p = m_lens->params();
    // Show the matched Lensfun profile name (which, for fixed-lens compacts,
    // comes from the camera rather than an EXIF lens string).
    m_lensPanel->reveal(p, m_lens->lensMatched(), m_lens->matchedLensName());
}

void MainWindow::closeLensTool()
{
    m_lensPanel->hide();
    m_graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openSharpenTool()
{
    if (!m_sharpen)
        return;
    m_input.setMode(InputController::Mode::ToolActive);
    m_sharpenPanel->adjustSize();
    const int margin = 18;
    m_sharpenPanel->move(width() - m_sharpenPanel->width() - margin, margin);
    m_sharpenPanel->reveal(m_sharpen->values());
}

void MainWindow::closeSharpenTool()
{
    m_sharpenPanel->hide();
    m_graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openGrainTool()
{
    if (!m_grain)
        return;
    m_input.setMode(InputController::Mode::ToolActive);
    m_grainPanel->adjustSize();
    const int margin = 18;
    m_grainPanel->move(width() - m_grainPanel->width() - margin, margin);
    m_grainPanel->reveal(m_grain->values());
}

void MainWindow::closeGrainTool()
{
    m_grainPanel->hide();
    m_graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openVignetteTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    m_vignettePanel->adjustSize();
    const int margin = 18;
    m_vignettePanel->move(width() - m_vignettePanel->width() - margin, margin);
    m_vignettePanel->reveal(m_graph.vignette());
}

void MainWindow::closeVignetteTool()
{
    m_vignettePanel->hide();
    m_graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

double MainWindow::sourceAspect() const
{
    if (m_sourceQImage.isNull() || m_sourceQImage.height() == 0)
        return 1.0;
    return static_cast<double>(m_sourceQImage.width())
           / static_cast<double>(m_sourceQImage.height());
}

void MainWindow::openCropTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    m_cropPanel->adjustSize();
    const int margin = 18;
    m_cropPanel->move(width() - m_cropPanel->width() - margin, margin);
    m_cropPanel->reveal(m_graph.crop(), sourceAspect());
    m_cropGizmo->setAspect(0.0); // free until the user picks a preset
    m_cropGizmo->setRect(m_graph.crop().rect);
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
    m_graph.commit(); // one undo step per editing session (no-op if unchanged)
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
    } else if (!m_graph.crop().isIdentity() && m_graph.crop().enabled) {
        mode = CanvasWidget::CropApplied;
    } else {
        // No crop, or the crop is toggled off in the Adjustments panel → full frame.
        mode = CanvasWidget::CropNone;
    }
    m_canvas->setCropState(m_graph.crop(), mode);
}

void MainWindow::openDenoiseTool()
{
    if (!m_denoise)
        return;
    m_input.setMode(InputController::Mode::ToolActive);
    m_denoisePanel->adjustSize();
    const int margin = 18;
    m_denoisePanel->move(width() - m_denoisePanel->width() - margin, margin);
    m_denoisePanel->reveal(m_denoise->values());
}

void MainWindow::closeDenoiseTool()
{
    m_denoisePanel->hide();
    m_graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openDefringeTool()
{
    if (!m_defringe)
        return;
    m_input.setMode(InputController::Mode::ToolActive);
    m_defringePanel->adjustSize();
    const int margin = 18;
    m_defringePanel->move(width() - m_defringePanel->width() - margin, margin);
    m_defringePanel->reveal(m_defringe->values());
}

void MainWindow::closeDefringeTool()
{
    m_defringePanel->hide();
    m_graph.commit(); // one undo step per editing session (no-op if unchanged)
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openRawTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    m_rawPanel->adjustSize();
    const int margin = 18;
    m_rawPanel->move(width() - m_rawPanel->width() - margin, margin);
    m_rawPanel->reveal(m_rawOptions, m_rawLensDefaults);
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
    m_graph.commit(); // capture any lens-correction toggle as one undo step
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
    m_showClipping = !m_showClipping;
    m_canvas->setClipping(m_showClipping);
    showHint(m_showClipping ? QStringLiteral("Clipping warnings on — red = highlights, blue = shadows")
                            : QStringLiteral("Clipping warnings off"));
    syncViewToggles();
}

void MainWindow::syncViewToggles()
{
    if (!m_viewToggles)
        return;
    m_histToggleBtn->setChecked(m_histogram && m_histogram->isVisible());
    m_clipToggleBtn->setChecked(m_showClipping);
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
    if (!m_histogram || !m_histogram->isVisible() || m_graph.source().isNull())
        return;
    // Don't overlap an in-flight bake — building result() would be a second
    // concurrent full-res inpaint. Defer; the bake's finished handler retriggers.
    if (m_healWatcher.isRunning() || m_decodeWatcher.isRunning())
        return;

    const quint64 gen = ++m_histGen;
    const bool healActive =
        m_heal && m_heal->isEnabled() && !m_heal->healMask().isEmpty();
    if (healActive) {
        // result() is EAGER when a heal mask is active — HealNode::apply runs the
        // inpaint inline (see HealNode.cpp), so it must NEVER be built on the UI
        // thread (that froze the app when toggling the histogram mid-heal). Build
        // and materialise it on a worker. No bake is in flight (guarded above), so
        // the graph is quiescent for the snapshot.
        m_histWatcher.setFuture(QtConcurrent::run([this, gen]() -> HistogramData {
            if (gen != m_histGen)
                return HistogramData{};
            const Image result = m_graph.result();
            if (result.isNull() || gen != m_histGen)
                return HistogramData{};
            return computeHistogram(result);
        }));
    } else {
        // No eager node: composite over a cached downsampled source so each
        // recompute is cheap (pointwise edits are resolution-independent). Build
        // the snapshot on the UI thread (race-free) and materialise on the worker.
        const Image result = m_graph.resultDownsampled(kHistogramMaxDim);
        if (result.isNull())
            return;
        m_histWatcher.setFuture(QtConcurrent::run([this, gen, result]() -> HistogramData {
            if (gen != m_histGen)
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
    if (m_originalQImage.isNull() && !m_graph.source().isNull())
        m_originalQImage = m_graph.source().toQImage();
    return m_originalQImage;
}

void MainWindow::setCompareOriginal(bool on)
{
    if (m_compareOriginal == on || m_graph.source().isNull())
        return;
    if (on && m_peeking)
        exitPeek(); // compare against the real edited image, not a peeked view
    m_compareOriginal = on;
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

    if (!m_graph.source().isNull()) {
        // Base pipeline, in processing order. Heal and the LAB neighbourhood ops use
        // an effect predicate (so a configured-but-zero op doesn't list); the rest
        // use the generic params-differ test.
        if (nodeIsActive(m_lens))
            addNodeAdj(QStringLiteral("Lens"), 0, m_lens);
        if (m_heal && !m_heal->healMask().isEmpty())
            addNodeAdj(QStringLiteral("Heal"), 1, m_heal);
        if (const auto v = m_denoise ? m_denoise->values() : DenoiseNode::Values{};
            v.enabled && (v.luma > 0.0f || v.chroma > 0.0f))
            addNodeAdj(QStringLiteral("Noise Reduction"), 2, m_denoise);
        if (const auto v = m_defringe ? m_defringe->values() : DefringeNode::Values{};
            v.enabled && (v.purple > 0.0f || v.green > 0.0f))
            addNodeAdj(QStringLiteral("Defringe"), 3, m_defringe);
        if (const auto v = m_sharpen ? m_sharpen->values() : SharpenNode::Values{};
            v.enabled && v.amount > 0.0f)
            addNodeAdj(QStringLiteral("Sharpen"), 4, m_sharpen);
        if (nodeIsActive(m_tune))
            addNodeAdj(QStringLiteral("Tone"), 5, m_tune);
        if (nodeIsActive(m_colorMixer))
            addNodeAdj(QStringLiteral("Color Mixer"), 6, m_colorMixer);
        if (nodeIsActive(m_curves))
            addNodeAdj(QStringLiteral("Curves"), 7, m_curves);
        if (nodeIsActive(m_colorGrade))
            addNodeAdj(QStringLiteral("Color Grade"), 8, m_colorGrade);
        if (nodeIsActive(m_lutNode))
            addNodeAdj(QStringLiteral("Look"), 9, m_lutNode);
        if (nodeIsActive(m_mono))
            addNodeAdj(QStringLiteral("B&W"), 10, m_mono);
        if (nodeIsActive(m_grain))
            addNodeAdj(QStringLiteral("Grain"), 11, m_grain);

        // Selective layers (non-Base), then the final geometric/finishing stages.
        for (int i = 1; i < m_graph.layerCount(); ++i) {
            const QString name = m_graph.layer(i).name();
            m_adjustments.push_back(
                {name.isEmpty() ? QStringLiteral("Adjustment layer") : name, 100 + i,
                 [this, i] { return m_graph.layer(i).enabled(); },
                 [this, i](bool on) { m_graph.layer(i).setEnabled(on); },
                 [this, i] { m_graph.removeLayer(i); }});
        }
        if (!m_graph.crop().isIdentity()) {
            m_adjustments.push_back(
                {QStringLiteral("Crop"), 200, [this] { return m_graph.crop().enabled; },
                 [this](bool on) { CropState c = m_graph.crop(); c.enabled = on; m_graph.setCrop(c); },
                 [this] { m_graph.setCrop(CropState{}); }});
        }
        if (std::abs(m_graph.vignette().amount) > 0.0f) {
            m_adjustments.push_back(
                {QStringLiteral("Vignette"), 210, [this] { return m_graph.vignette().enabled; },
                 [this](bool on) { VignetteParams v = m_graph.vignette(); v.enabled = on; m_graph.setVignette(v); },
                 [this] { m_graph.setVignette(VignetteParams{}); }});
        }
    }

    QVector<AdjustmentsPanel::Item> items;
    items.reserve(static_cast<int>(m_adjustments.size()));
    for (const Adjustment &a : m_adjustments)
        items.push_back({a.name, a.isEnabled()});
    m_adjustmentsPanel->setItems(items, m_viewCeiling, m_compareOriginal);
    layoutOverlays(); // size may have changed
}

void MainWindow::onAdjustmentToggle(int index, bool on)
{
    if (m_peeking)
        exitPeek(); // editing resolves the transient peek
    if (index < 0 || index >= static_cast<int>(m_adjustments.size()))
        return;
    m_adjustments[index].setEnabled(on);
    m_graph.commit();
    rebuildPreviewFromGraph();
    rebuildAdjustments();
}

void MainWindow::onAdjustmentDelete(int index)
{
    if (m_peeking)
        exitPeek();
    if (index < 0 || index >= static_cast<int>(m_adjustments.size()))
        return;
    m_adjustments[index].reset();
    m_graph.commit();
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
    if (m_peeking && index == m_viewCeiling) { // clicking the selected row exits
        exitPeek();
        return;
    }
    if (m_compareOriginal)
        setCompareOriginal(false); // Before/After and peek are mutually exclusive
    if (!m_peeking) {
        m_peekSnapshot = m_graph.saveState(); // the real state to restore on exit
        m_peeking = true;
    } else {
        m_graph.restoreState(m_peekSnapshot); // reset before applying a new ceiling
    }
    m_viewCeiling = index;
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
    if (!m_peeking)
        return;
    m_graph.restoreState(m_peekSnapshot); // back to the real, committed state
    m_peeking = false;
    m_viewCeiling = -1;
    m_peekSnapshot = QJsonObject{};
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
    if (m_compareOriginal)
        setCompareOriginal(false);
    exitPeek();
    m_adjustmentsPanel->hide();
    layoutOverlays();
    syncViewToggles();
}

void MainWindow::refreshWorkingSource()
{
    const Image &src = m_graph.source();
    if (src.isNull()) {
        m_workingSource = Image();
        return;
    }
    // The lens node is a pure function of the source; apply it once and cache so
    // heal dabs (which call refreshBaseImage repeatedly) don't re-warp full res.
    // Honour the pipeline toggle so the Adjustments panel can disable lens.
    m_workingSource = (m_lens && m_lens->isEnabled()) ? m_lens->apply(src) : src;
    if (m_workingSource.isNull())
        m_workingSource = src; // defensive: never lose the image
    m_sourceQImage = m_workingSource.toQImage(); // display + colour sampling
    m_originalQImage = QImage(); // Before/After cache: recompute on next compare
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
    const int idx = m_graph.activeLayerIndex();
    const bool brushLayer = m_layersPanel->isVisible() && idx > 0
        && m_graph.activeLayer().mask().type == MaskSpec::Brush;
    if (brushLayer) {
        if (m_brushTarget != BrushTarget::Selective) {
            m_brushMask = m_graph.activeLayer().mask().brush;
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
        m_graph.commit();
        m_brushUndo.clear();
    }
}

void MainWindow::openHealTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    m_healPanel->adjustSize();
    const int margin = 18;
    m_healPanel->move(width() - m_healPanel->width() - margin, margin);
    m_healPanel->reveal(m_brushSize, m_brushHardness, m_brushAdd, m_heal->highQuality());

    // Restore the heal session from the node (may be empty).
    m_brushMask = m_heal->healMask();
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
    m_heal->setHealMask(m_brushMask); // commit (one global undo step)
    m_brushUndo.clear();
    updateCropView(); // back to the cropped browse view
    refreshBaseImage();
    updatePreview();
    m_graph.commit();
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
    if (m_compareOriginal) {
        const QImage &orig = originalImage();
        if (!orig.isNull())
            m_canvas->setImage(orig, keepView);
        return;
    }

    // The GPU preview base = the lens-corrected source with the other "baked"
    // neighbourhood ops applied (heal -> denoise -> defringe -> sharpen); the
    // shader then applies the pointwise/LUT ops on top. With no baked op active,
    // the corrected source (m_sourceQImage) is already the base.
    // Each baked op also respects its pipeline toggle (Adjustments panel), so a
    // disabled node bakes nothing — matching the pointwise/preview-LUT path.
    const bool healActive =
        m_heal && m_heal->isEnabled() && !m_heal->healMask().isEmpty();
    const DenoiseNode::Values dv =
        m_denoise ? m_denoise->values() : DenoiseNode::Values{};
    const bool denoiseActive = m_denoise && m_denoise->isEnabled() && dv.enabled
                               && (dv.luma > 0.0f || dv.chroma > 0.0f);
    const DefringeNode::Values fv =
        m_defringe ? m_defringe->values() : DefringeNode::Values{};
    const bool defringeActive = m_defringe && m_defringe->isEnabled() && fv.enabled
                                && (fv.purple > 0.0f || fv.green > 0.0f);
    const SharpenNode::Values sv =
        m_sharpen ? m_sharpen->values() : SharpenNode::Values{};
    const bool sharpenActive =
        m_sharpen && m_sharpen->isEnabled() && sv.enabled && sv.amount > 0.0f;
    if (m_graph.source().isNull()
        || (!healActive && !denoiseActive && !defringeActive && !sharpenActive)) {
        if (!m_sourceQImage.isNull())
            m_canvas->setImage(m_sourceQImage, keepView);
        return;
    }

    // These passes (esp. Detailed/Criminisi heal) are expensive, so run them off
    // the UI thread; the latest request wins (m_healGen guards stale results, and
    // the watcher only delivers its current future).
    const quint64 gen = ++m_healGen;
    // Baked ops run on top of the lens-corrected source (geometry already baked
    // into m_workingSource), so painted spots track the corrected image.
    const Image src = m_workingSource.isNull() ? m_graph.source() : m_workingSource;
    const MaskBuffer mask = healActive ? m_heal->healMask() : MaskBuffer();
    const bool hq = m_heal && m_heal->highQuality();
    if (healActive || denoiseActive || defringeActive || sharpenActive) {
        // Label by the op the user triggered (if it's actually active); otherwise
        // fall back to precedence (heal first) for unattributed refreshes.
        QString label;
        if (triggeredBy == BakeOp::Sharpen && sharpenActive)
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
                                  : QStringLiteral("Sharpening…");
        auto *badge = static_cast<BusyBadge *>(m_healBusy);
        badge->setLabel(label);
        badge->start();
        layoutOverlays(); // position the badge + dim
    }

    m_healWatcher.setFuture(
        QtConcurrent::run([this, gen, src, mask, hq, dv, fv, sv]() -> QImage {
            if (gen != m_healGen)
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
    int idx = m_graph.activeLayerIndex();
    if (idx == 0 || !selectable(m_graph.layer(idx).mask().type)) {
        addMaskedAdjustmentLayer(QStringLiteral("Selective %1").arg(m_graph.layerCount()));
        idx = m_graph.activeLayerIndex();
    }
    // Default a still-unset mask to a full-range luminosity mask (a no-op until
    // the range or adjustment is dialled in), so the panel has something to edit.
    Layer &layer = m_graph.layer(idx);
    if (layer.mask().type == MaskSpec::None) {
        MaskSpec m;
        m.type = MaskSpec::Luminosity;
        layer.setMask(m);
    }
    m_graph.commit();
    return idx;
}

void MainWindow::syncBrushMaskToLayer()
{
    if (m_graph.activeLayerIndex() == 0)
        return;
    Layer &layer = m_graph.activeLayer();
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
    if (m_maskView == 0)
        return;
    if (m_graph.activeLayerIndex() == 0) {
        m_canvas->setSelectiveMask({});
        return;
    }
    const MaskSpec &m = m_graph.activeLayer().mask();
    if (m.type == MaskSpec::Brush) {
        m_canvas->setSelectiveMask(m_brushMask);
        return;
    }
    if ((m.type == MaskSpec::None && m.zones.empty()) || m_sourceQImage.isNull()) {
        m_canvas->setSelectiveMask({});
        return;
    }
    // Evaluate at a capped resolution for a responsive overlay (export/composite
    // recompute at full res).
    constexpr int cap = 1280;
    int w = m_sourceQImage.width(), h = m_sourceQImage.height();
    if (std::max(w, h) > cap) {
        const double s = double(cap) / std::max(w, h);
        w = std::max(1, static_cast<int>(std::lround(w * s)));
        h = std::max(1, static_cast<int>(std::lround(h * s)));
    }
    // Geometric masks (gradient/radial) don't need pixels — skip the source copy
    // so live gizmo dragging stays smooth. Luminosity/Colour need the rgba.
    if (m.type == MaskSpec::Luminosity || m.type == MaskSpec::Colour) {
        const QImage img =
            m_sourceQImage.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_RGBA8888);
        m_canvas->setSelectiveMask(
            evaluateMask(m, img.width(), img.height(), img.constBits(), 4));
    } else {
        m_canvas->setSelectiveMask(evaluateMask(m, w, h, nullptr, 4));
    }
}

void MainWindow::initBrushMask()
{
    if (m_sourceQImage.isNull())
        return;
    constexpr int cap = 1280;
    int w = m_sourceQImage.width();
    int h = m_sourceQImage.height();
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
    m_strokeBaseMask = m_brushMask.data; // for the heal overlay (current stroke only)
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

    if (m_brushHasLast) {
        // Stamp along the segment so fast drags don't leave gaps.
        const float dx = cx - m_lastBrushPoint.x();
        const float dy = cy - m_lastBrushPoint.y();
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float step = std::max(1.0f, radius * 0.25f);
        const int steps = static_cast<int>(dist / step);
        for (int k = 1; k < steps; ++k) {
            const float t = k / float(steps);
            stampBrush(m_brushMask, m_lastBrushPoint.x() + dx * t,
                       m_lastBrushPoint.y() + dy * t, radius, hardness, m_brushAdd);
        }
    }
    stampBrush(m_brushMask, cx, cy, radius, hardness, m_brushAdd);
    m_lastBrushPoint = QPointF(cx, cy);
    m_brushHasLast = true;

    // Live feedback. The selective brush shows the whole mask (you're building a
    // selection). The heal brush shows only the CURRENT stroke as a red overlay
    // (it inpaints on stroke end) — so already-healed spots aren't re-tinted.
    if (m_brushTarget == BrushTarget::Heal) {
        MaskBuffer strokeOnly;
        strokeOnly.width = w;
        strokeOnly.height = h;
        strokeOnly.data.resize(m_brushMask.data.size());
        for (size_t i = 0; i < strokeOnly.data.size(); ++i) {
            const float base = i < m_strokeBaseMask.size() ? m_strokeBaseMask[i] : 0.0f;
            strokeOnly.data[i] = std::clamp(m_brushMask.data[i] - base, 0.0f, 1.0f);
        }
        m_canvas->setSelectiveMask(strokeOnly);
        m_healPainting = true;
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
        m_heal->setHealMask(m_brushMask);
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
        m_heal->setHealMask(m_brushMask);
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
    if (m_sourceQImage.isNull())
        return;
    const int x = std::clamp(static_cast<int>(std::lround(norm.x() * (m_sourceQImage.width() - 1))),
                             0, m_sourceQImage.width() - 1);
    const int y = std::clamp(static_cast<int>(std::lround(norm.y() * (m_sourceQImage.height() - 1))),
                             0, m_sourceQImage.height() - 1);
    const QColor c = m_sourceQImage.pixelColor(x, y);

    // White-balance eyedropper: make the sampled (as-shot baseline) pixel neutral.
    if (m_pickPurpose == PickPurpose::WhiteBalance) {
        m_pickPurpose = PickPurpose::MaskColour; // reset to the default purpose
        if (auto *t = activeTune()) {
            t->pickNeutral(static_cast<float>(c.redF()), static_cast<float>(c.greenF()),
                           static_cast<float>(c.blueF()));
            updatePreview();
            if (m_tonePanel->isVisible())
                m_tonePanel->reveal(toneValuesOf(t));
            m_graph.commit();
        }
        return;
    }

    if (m_graph.activeLayerIndex() == 0)
        return;
    Layer &layer = m_graph.activeLayer();
    MaskSpec m = layer.mask();
    m.type = MaskSpec::Colour;
    m.targetR = static_cast<float>(c.redF());
    m.targetG = static_cast<float>(c.greenF());
    m.targetB = static_cast<float>(c.blueF());
    layer.setMask(m);
    m_layersPanel->setTargetColor(c);
    recomputeSelectiveMask();
    updatePreview();
    m_graph.commit();
}

void MainWindow::closeActiveTool()
{
    if (m_tonePanel->isVisible())
        closeToneTool();
    else if (m_curvesPanel->isVisible())
        closeCurvesTool();
    else if (m_looksPanel->isVisible())
        closeLooksTool();
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
    if (m_peeking)
        exitPeek(); // history nav resolves the transient peek first
    // While brush-painting, Ctrl+Z is a per-stroke session undo.
    if (m_brushTarget != BrushTarget::None && brushSessionUndo())
        return;
    if (m_graph.undo())
        afterHistoryChange();
}

void MainWindow::doRedo()
{
    if (m_peeking)
        exitPeek();
    if (m_graph.redo())
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
        m_brushMask = m_heal->healMask(); // sync session to restored state
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
    if (m_lensPanel->isVisible() && m_lens) {
        m_lensPanel->reveal(m_lens->params(), m_lens->lensMatched(),
                            m_lens->matchedLensName());
    }
    if (m_sharpenPanel->isVisible() && m_sharpen)
        m_sharpenPanel->reveal(m_sharpen->values());
    if (m_denoisePanel->isVisible() && m_denoise)
        m_denoisePanel->reveal(m_denoise->values());
    if (m_defringePanel->isVisible() && m_defringe)
        m_defringePanel->reveal(m_defringe->values());
    if (m_grainPanel->isVisible() && m_grain)
        m_grainPanel->reveal(m_grain->values());
    if (m_vignettePanel->isVisible())
        m_vignettePanel->reveal(m_graph.vignette());
    if (m_cropPanel->isVisible()) {
        m_cropPanel->reveal(m_graph.crop(), sourceAspect());
        m_cropGizmo->setRect(m_graph.crop().rect);
    }
    updateCropView(); // push the restored crop/orientation to the canvas
    updateHistogram(); // reflect the restored state (no-op when hidden)
    // If a mask brush is active, resync the working mask to the restored state.
    if (m_brushTarget == BrushTarget::Selective
        && m_graph.activeLayer().mask().type == MaskSpec::Brush) {
        m_brushMask = m_graph.activeLayer().mask().brush;
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

    // Busy badge: top-centre of the canvas.
    m_healBusy->move((width() - m_healBusy->width()) / 2, 16);
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
        && !m_graph.source().isNull()) {
        setCompareOriginal(!m_compareOriginal);
        return;
    }

    // View-toggle keys, available whenever an image is loaded (any mode): 'J'
    // clipping, 'G' histo(G)ram, 'A' history (Adjustments panel). Their on/off
    // state is mirrored in the bottom-right cluster and the hint legend.
    if (e->key() == Qt::Key_J && !e->isAutoRepeat() && !m_graph.source().isNull()) {
        toggleClipping();
        return;
    }
    if (e->key() == Qt::Key_G && !e->isAutoRepeat() && !m_graph.source().isNull()) {
        toggleHistogram();
        return;
    }
    if (e->key() == Qt::Key_A && !e->isAutoRepeat() && !m_graph.source().isNull()) {
        openAdjustmentsTool(); // toggles the history panel
        return;
    }

    // H toggles the overlay geometry while a selective layer is being edited.
    // (Reached only when no brush is active — the brush guard above returns first,
    // so this never clashes with the brush-hardness H binding.)
    if (e->key() == Qt::Key_H && !e->isAutoRepeat() && m_layersPanel->isVisible()
        && m_graph.activeLayerIndex() != 0) {
        setOverlayGeometryVisible(m_overlaysHidden);
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
        if (e->key() == Qt::Key_Escape && m_peeking) {
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
