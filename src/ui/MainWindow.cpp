#include "ui/MainWindow.h"

#include "core/HealNode.h"
#include "core/Image.h"
#include "core/LayerPreview.h"
#include "core/MaskSpec.h"
#include "core/Project.h"
#include "core/RawLoader.h"
#include "core/SelectiveMask.h"
#include "gpu/CanvasWidget.h"
#include "input/CommandPalette.h"
#include "ui/CurvesPanel.h"
#include "ui/ExportDialog.h"
#include "core/Histogram.h"
#include "ui/DenoisePanel.h"
#include "ui/HealPanel.h"
#include "ui/HistogramWidget.h"
#include "ui/LayersPanel.h"
#include "ui/LensPanel.h"
#include "ui/MaskGizmo.h"
#include "ui/LooksPanel.h"
#include "ui/MonoPanel.h"
#include "ui/SharpenPanel.h"
#include "ui/TonePanel.h"

#include <memory>
#include <utility>

#include <cstdio>

#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPainter>

#include <cmath>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QTimer>
#include <QtConcurrent>

#include <algorithm>

namespace {

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

// Dim opacity for overlays (0–255). A tuning value, not a fixed constant
// (DESIGN.md §4.6): too dark loses context, too light hurts contrast.
constexpr int kScrimAlpha = 140; // ~0.55

// A translucent layer that dims whatever is behind it. It paints a
// semi-transparent fill without clearing to an opaque background first, so the
// (RHI-composited) canvas shows through, dimmed.
class Scrim : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(10, 10, 11, kScrimAlpha));
    }
};

// A small "Healing…" badge with an animated spinner, shown while a background
// inpaint is running. Mouse-transparent.
class BusyBadge : public QWidget {
public:
    explicit BusyBadge(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFixedSize(132, 32);
        m_spin.setInterval(70);
        connect(&m_spin, &QTimer::timeout, this, [this] {
            m_angle = (m_angle + 30) % 360;
            update();
        });
        // Only show once the heal has run longer than this, so quick heals
        // (small blemishes) don't flash the badge.
        m_delay.setSingleShot(true);
        m_delay.setInterval(150);
        connect(&m_delay, &QTimer::timeout, this, [this] {
            show();
            raise();
            m_spin.start();
        });
        hide();
    }

    void start()
    {
        if (isVisible() || m_delay.isActive())
            return;
        m_delay.start();
    }
    void stop()
    {
        m_delay.stop(); // cancel a pending show if the heal finished in time
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
        p.drawText(QRect(34, 0, width() - 40, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Healing…"));
    }

private:
    QTimer m_spin;  // animation
    QTimer m_delay; // show-after delay
    int m_angle = 0;
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

    // The graph owns the nodes; we keep raw pointers to drive them. The "baked"
    // group runs first in libvips and is rendered into the preview base texture:
    // lens (warps geometry) -> heal (edits pixels) -> denoise -> sharpen (both
    // neighbourhood ops; denoise before sharpen so noise isn't amplified). Then
    // the pointwise/LUT ops the shader replicates: tune -> curves -> lut -> mono.
    // Selective adjustments are masked layers above the Base, not Base nodes.
    m_lens =
        static_cast<LensCorrectionNode *>(m_graph.addNode(std::make_unique<LensCorrectionNode>()));
    m_heal = static_cast<HealNode *>(m_graph.addNode(std::make_unique<HealNode>()));
    m_denoise = static_cast<DenoiseNode *>(m_graph.addNode(std::make_unique<DenoiseNode>()));
    m_sharpen = static_cast<SharpenNode *>(m_graph.addNode(std::make_unique<SharpenNode>()));
    m_tune = static_cast<TuneNode *>(m_graph.addNode(std::make_unique<TuneNode>()));
    m_curves = static_cast<CurvesNode *>(m_graph.addNode(std::make_unique<CurvesNode>()));
    m_lutNode = static_cast<LutNode *>(m_graph.addNode(std::make_unique<LutNode>()));
    m_mono = static_cast<MonoNode *>(m_graph.addNode(std::make_unique<MonoNode>()));

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

    m_healBusy = new BusyBadge(this);

    // Created before the palette so the palette stacks above it.
    m_scrim = new Scrim(this);
    m_scrim->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_scrim->hide();

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
            t->setSaturation(v.saturation);
            t->setKelvin(v.kelvin);
            t->setTint(v.tint);
        }
        updatePreview(); // preview is driven by walking the graph
    });
    connect(m_tonePanel, &TonePanel::closed, this, &MainWindow::closeToneTool);

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
                m_bakeTimer->start();
            });
    connect(m_sharpenPanel, &SharpenPanel::closed, this, &MainWindow::closeSharpenTool);

    m_denoisePanel = new DenoisePanel(this);
    connect(m_denoisePanel, &DenoisePanel::valuesChanged, this,
            [this](const DenoiseNode::Values &v) {
                if (!m_denoise)
                    return;
                m_denoise->setValues(v);
                // Denoise bakes into the base; coalesce drags (it's a full-res
                // LAB pass) just like sharpen.
                m_bakeTimer->start();
            });
    connect(m_denoisePanel, &DenoisePanel::closed, this, &MainWindow::closeDenoiseTool);

    // Debounce timers: the histogram recompute and the sharpen base re-bake both
    // settle shortly after the user stops dragging.
    m_bakeTimer = new QTimer(this);
    m_bakeTimer->setSingleShot(true);
    m_bakeTimer->setInterval(140);
    connect(m_bakeTimer, &QTimer::timeout, this, [this] {
        refreshBaseImage();
        updatePreview();
    });

    m_histogram = new HistogramWidget(this);
    m_histTimer = new QTimer(this);
    m_histTimer->setSingleShot(true);
    m_histTimer->setInterval(160);
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

    // Background heal preview finished: apply the result (the watcher only
    // delivers the latest request's future).
    connect(&m_healWatcher, &QFutureWatcher<QImage>::finished, this, [this] {
        static_cast<BusyBadge *>(m_healBusy)->stop();
        if (!m_healWatcher.future().isValid())
            return;
        const QImage healed = m_healWatcher.result();
        if (!healed.isNull())
            m_canvas->setImage(healed, /*keepView=*/true); // preserve zoom/pan
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

    m_layersPanel = new LayersPanel(this);
    connect(m_layersPanel, &LayersPanel::addRequested, this, &MainWindow::addAdjustmentLayer);
    connect(m_layersPanel, &LayersPanel::deleteRequested, this, &MainWindow::deleteActiveLayer);
    connect(m_layersPanel, &LayersPanel::layerSelected, this, &MainWindow::selectLayer);
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
    connect(m_layersPanel, &LayersPanel::maskPickColorRequested, this,
            [this] { m_canvas->setColorPickMode(true); });
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

    // Dismissible hint bar, bottom-centre over the canvas.
    m_hint = new QLabel(this);
    m_hint->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_hint->setStyleSheet(QStringLiteral(
        "background: rgba(20,20,22,0.85); border-radius: 8px;"
        "padding: 6px 16px; color: #c8c8cc; font-size: 12px;"));
    m_hint->setText(QStringLiteral(
        "/  command palette   ·   Ctrl+O open   ·   Ctrl+S save project   ·   F11 fullscreen"));
    m_hint->adjustSize();

    buildCommands();

    // Shell shortcuts. Bare keys are avoided so they don't clash with typing in
    // the palette; "/" and Esc are handled in keyPressEvent instead.
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+O")), this, [this] { openImageDialog(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+S")), this, [this] { saveProject(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")), this, [this] { openProject(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Q")), this, [this] { close(); });
    new QShortcut(QKeySequence(Qt::Key_F11), this, [this] { toggleFullScreen(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+0")), this, [this] { m_canvas->resetView(); });
    new QShortcut(QKeySequence::Undo, this, [this] { doUndo(); });
    new QShortcut(QKeySequence::Redo, this, [this] { doRedo(); });

    resize(1280, 800);
}

void MainWindow::buildCommands()
{
    // ids consumed by runCommand(). Editing tools are placeholders for now.
    m_palette->setCommands({
        {QStringLiteral("open"), QStringLiteral("Open image…")},
        {QStringLiteral("open-project"), QStringLiteral("Open project (.lumen)…")},
        {QStringLiteral("save-project"), QStringLiteral("Save project (.lumen)…")},
        {QStringLiteral("export"), QStringLiteral("Export image…")},
        {QStringLiteral("tone"), QStringLiteral("Tone (exposure, contrast, saturation)")},
        {QStringLiteral("curves"), QStringLiteral("Curves")},
        {QStringLiteral("undo"), QStringLiteral("Undo")},
        {QStringLiteral("redo"), QStringLiteral("Redo")},
        {QStringLiteral("reset-view"), QStringLiteral("Reset view")},
        {QStringLiteral("fullscreen"), QStringLiteral("Toggle fullscreen")},
        {QStringLiteral("looks"), QStringLiteral("Looks (LUT)")},
        {QStringLiteral("monochrome"), QStringLiteral("Monochrome (B&W + toning)")},
        {QStringLiteral("selective"), QStringLiteral("Selective adjustment")},
        {QStringLiteral("lens"), QStringLiteral("Lens & perspective")},
        {QStringLiteral("denoise"), QStringLiteral("Denoise")},
        {QStringLiteral("sharpen"), QStringLiteral("Sharpen")},
        {QStringLiteral("heal"), QStringLiteral("Healing brush")},
        {QStringLiteral("histogram"), QStringLiteral("Histogram (toggle)")},
        {QStringLiteral("layers"), QStringLiteral("Layers")},
        {QStringLiteral("quit"), QStringLiteral("Quit Lumen")},
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
    } else if (id == QLatin1String("histogram")) {
        toggleHistogram();
    } else if (id == QLatin1String("heal")) {
        openHealTool();
    } else if (id == QLatin1String("layers")) {
        openLayersTool();
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

bool MainWindow::openPath(const QString &path)
{
    if (path.endsWith(QStringLiteral(".lumen"), Qt::CaseInsensitive))
        return loadProjectFile(path); // a project, not a raw image

    QString error;
    const bool isRaw = raw::isRawPath(path);
    raw::LensMetadata meta;
    Image source = isRaw ? raw::decodeFile(path, &error, &meta)
                         : Image::fromFile(path, &error);
    if (source.isNull()) {
        QMessageBox::warning(this, QStringLiteral("Lumen"), error);
        return false;
    }

    // Keep the original encoded bytes so we can embed them verbatim in a .lumen.
    QFile srcFile(path);
    m_sourceBytes = srcFile.open(QIODevice::ReadOnly) ? srcFile.readAll() : QByteArray();
    m_sourceName = QFileInfo(path).fileName();
    m_projectPath.clear(); // opening a raw image starts a new (unsaved) project

    // A freshly opened image starts from a clean slate: clear any in-progress
    // brush editing and reset the graph to its pristine state so the previous
    // image's adjustments (and any selective layers) don't carry over.
    m_brushTarget = BrushTarget::None;
    m_maskView = 0;
    m_brushUndo.clear();
    m_brushMask = MaskBuffer();
    m_graph.restoreState(m_defaultGraphState);

    m_graph.setSource(source); // full-res original; the lens node corrects on top
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
    }
    m_lens->setParams(lp);
    // Camera-accurate white balance: install the colour profile and seed the
    // slider at the as-shot temperature (non-RAW keeps the sRGB defaults).
    if (isRaw)
        applyCameraProfile(meta.color, /*seedKelvin=*/true);
    refreshWorkingSource();  // build the corrected source + display QImage
    refreshBaseImage(false); // new image → fit the view
    recomputeSelectiveMask();
    updatePreview();        // apply any existing edits
    m_graph.resetHistory(); // fresh undo timeline for this image
    m_sourcePath = path;
    if (m_layersPanel->isVisible())
        refreshLayersPanel();   // the reset dropped any selective layers
    reseedOpenPanels();         // re-sync open tools with the neutral defaults
    setWindowTitle(QStringLiteral("Lumen — %1").arg(QFileInfo(path).fileName()));
    return true;
}

void MainWindow::saveProject()
{
    if (m_graph.source().isNull()) {
        showHint(QStringLiteral("Open an image before saving a project"));
        return;
    }

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
        return;
    if (!path.endsWith(QStringLiteral(".lumen"), Qt::CaseInsensitive))
        path += QStringLiteral(".lumen");

    // Embed the original bytes; fall back to a PNG of the source if unavailable.
    QByteArray bytes = m_sourceBytes;
    QString name = m_sourceName;
    if (bytes.isEmpty() && !m_sourceQImage.isNull()) {
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        m_sourceQImage.save(&buf, "PNG");
        name = QStringLiteral("source.png");
    }

    QString error;
    if (!project::save(path, m_graph.saveState(), bytes, name, &error)) {
        QMessageBox::warning(this, QStringLiteral("Lumen"),
                             QStringLiteral("Could not save project: %1").arg(error));
        return;
    }
    m_projectPath = path;
    rememberDir(QStringLiteral("saveProject"), path);
    setWindowTitle(QStringLiteral("Lumen — %1").arg(QFileInfo(path).fileName()));
    showHint(QStringLiteral("Saved %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::openProject()
{
    const QString dir = lastDir(
        QStringLiteral("openProject"),
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open project"), dir,
        QStringLiteral("Lumen project (*.lumen)"));
    if (!path.isEmpty()) {
        rememberDir(QStringLiteral("openProject"), path);
        loadProjectFile(path);
    }
}

bool MainWindow::loadProjectFile(const QString &path)
{
    QString error;
    project::Project proj;
    if (!project::load(path, &proj, &error)) {
        QMessageBox::warning(this, QStringLiteral("Lumen"), error);
        return false;
    }
    const bool isRaw = raw::isRawPath(proj.sourceName);
    raw::LensMetadata meta;
    Image source =
        isRaw ? raw::decodeBytes(proj.sourceBytes.constData(), proj.sourceBytes.size(),
                                 &error, &meta)
              : Image::fromBytes(proj.sourceBytes.constData(), proj.sourceBytes.size(), &error);
    if (source.isNull()) {
        QMessageBox::warning(this, QStringLiteral("Lumen"),
                             QStringLiteral("Could not decode the embedded image: %1").arg(error));
        return false;
    }

    // Reset any active editing state, then load the document.
    m_brushTarget = BrushTarget::None;
    m_maskView = 0;
    m_brushUndo.clear();
    m_sourceBytes = proj.sourceBytes;
    m_sourceName = proj.sourceName;
    m_graph.setSource(source);
    m_graph.loadProjectState(proj.graph); // restores the lens node's params too
    // Refresh the camera profile from the actual decode, keeping the restored
    // WB temperature (don't reseed to as-shot).
    if (isRaw)
        applyCameraProfile(meta.color, /*seedKelvin=*/false);

    refreshWorkingSource();  // rebuild the corrected source from the restored lens
    refreshBaseImage(false); // re-applies the heal mask, fits the view
    recomputeSelectiveMask();
    updatePreview();
    m_graph.resetHistory();

    m_projectPath = path;
    m_sourcePath = path; // export defaults to "<project>-edited.<ext>"
    if (m_layersPanel->isVisible())
        refreshLayersPanel();
    reseedOpenPanels(); // re-sync any open adjustment tool with the restored layers
    setWindowTitle(QStringLiteral("Lumen — %1").arg(QFileInfo(path).fileName()));
    showHint(QStringLiteral("Opened %1").arg(QFileInfo(path).fileName()));
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
    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("Open image"), dir, filter);
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

    // 1. Choose format + quality.
    ExportDialog dlg(this);
    dlg.setSelection(m_exportExt, m_exportQuality);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_exportExt = dlg.extension();
    const int quality = dlg.quality();
    const int bits = dlg.bits();
    if (quality >= 0)
        m_exportQuality = quality;

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

    // 3. Walk the graph at full resolution, then write via libvips.
    const Image result = m_graph.result();
    QString error;
    if (!result.saveToFile(path, quality, bits, &error)) {
        QMessageBox::warning(this, QStringLiteral("Lumen"),
                             QStringLiteral("Export failed: %1").arg(error));
        return;
    }
    rememberDir(QStringLiteral("export"), path);
    showHint(QStringLiteral("Exported to %1").arg(QFileInfo(path).fileName()));
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

    // A capped-resolution sRGB copy of the source, for evaluating data-driven
    // (luminosity/colour) layer masks; geometric masks ignore it.
    constexpr int cap = 1280;
    QImage maskSrc;
    if (!m_sourceQImage.isNull()) {
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
        if (layer.mask().type != MaskSpec::None && mw > 0)
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
    updateMaskEditing();
    recomputeSelectiveMask(); // keep the overlay in sync with the active layer
}

void MainWindow::reseedOpenPanels()
{
    // Guarded: a layer may not carry every node type (e.g. a selective layer).
    if (m_tonePanel->isVisible()) {
        if (auto *t = activeTune())
            m_tonePanel->reveal({t->exposure(), t->contrast(), t->saturation(),
                                 t->kelvin(), t->tint()});
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
}

void MainWindow::addAdjustmentLayer()
{
    Layer &layer = m_graph.addLayer(
        QStringLiteral("Layer %1").arg(m_graph.layerCount()));
    // Every adjustment layer gets a tone/curves/look node set (added to it as
    // it is now the active layer).
    m_graph.addNode(std::make_unique<TuneNode>());
    m_graph.addNode(std::make_unique<CurvesNode>());
    m_graph.addNode(std::make_unique<LutNode>());
    m_graph.addNode(std::make_unique<MonoNode>());
    Q_UNUSED(layer);
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
            m_tonePanel->reveal({t->exposure(), t->contrast(), t->saturation(),
                                 t->kelvin(), t->tint()});
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
    // The gizmo shows itself only for gradient/radial masks on a non-Base layer.
    if (m_graph.activeLayerIndex() == 0)
        m_maskGizmo->setSpec(MaskSpec{}); // None → hides
    else
        m_maskGizmo->setSpec(m_graph.activeLayer().mask());
    layoutOverlays(); // re-apply gizmo geometry + z-order
}

void MainWindow::openToneTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    // Default position: top-right with a margin. The user can drag it from here.
    m_tonePanel->adjustSize();
    const int margin = 18;
    m_tonePanel->move(width() - m_tonePanel->width() - margin, margin);
    m_tonePanel->reveal({activeTune()->exposure(), activeTune()->contrast(),
                         activeTune()->saturation(), activeTune()->kelvin(),
                         activeTune()->tint()});
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

void MainWindow::toggleHistogram()
{
    if (!m_histogram)
        return;
    if (m_histogram->isVisible()) {
        m_histogram->hide();
        return;
    }
    m_histogram->show();
    m_histogram->raise();
    layoutOverlays();   // place it
    updateHistogram();  // fill it immediately
}

void MainWindow::updateHistogram()
{
    if (!m_histogram || !m_histogram->isVisible())
        return;
    // The full-res composite is the source of truth (preview == export), but
    // pulling its pixels is slow, so compute the histogram off the UI thread. The
    // result() Image is a self-contained, ref-counted lazy pipeline — safe to
    // materialise on a worker while the UI thread carries on.
    const Image result = m_graph.result();
    if (result.isNull())
        return;
    const quint64 gen = ++m_histGen;
    m_histWatcher.setFuture(QtConcurrent::run([this, gen, result]() -> HistogramData {
        if (gen != m_histGen)
            return HistogramData{}; // superseded
        return computeHistogram(result);
    }));
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
    m_workingSource = m_lens ? m_lens->apply(src) : src;
    if (m_workingSource.isNull())
        m_workingSource = src; // defensive: never lose the image
    m_sourceQImage = m_workingSource.toQImage(); // display + colour sampling
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
    refreshBaseImage();
    updatePreview();
    m_graph.commit();
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::refreshBaseImage(bool keepView)
{
    // The GPU preview base = the lens-corrected source with the other "baked"
    // neighbourhood ops applied (heal -> denoise -> sharpen); the shader then
    // applies the pointwise/LUT ops on top. With no baked op active, the
    // corrected source (m_sourceQImage) is already the base.
    const bool healActive = m_heal && !m_heal->healMask().isEmpty();
    const DenoiseNode::Values dv =
        m_denoise ? m_denoise->values() : DenoiseNode::Values{};
    const bool denoiseActive = dv.enabled && (dv.luma > 0.0f || dv.chroma > 0.0f);
    const SharpenNode::Values sv =
        m_sharpen ? m_sharpen->values() : SharpenNode::Values{};
    const bool sharpenActive = sv.enabled && sv.amount > 0.0f;
    if (m_graph.source().isNull() || (!healActive && !denoiseActive && !sharpenActive)) {
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
    if (healActive) {
        static_cast<BusyBadge *>(m_healBusy)->start();
        layoutOverlays(); // position the badge
    }

    m_healWatcher.setFuture(
        QtConcurrent::run([this, gen, src, mask, hq, dv, sv]() -> QImage {
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
        m_graph.addLayer(QStringLiteral("Selective %1").arg(m_graph.layerCount()));
        // Full adjustment node set so any tool (Tone/Curves/Looks/Mono) works.
        m_graph.addNode(std::make_unique<TuneNode>());
        m_graph.addNode(std::make_unique<CurvesNode>());
        m_graph.addNode(std::make_unique<LutNode>());
        m_graph.addNode(std::make_unique<MonoNode>());
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
    if (m.type == MaskSpec::None || m_sourceQImage.isNull()) {
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
    else if (m_lensPanel->isVisible())
        closeLensTool();
    else if (m_sharpenPanel->isVisible())
        closeSharpenTool();
    else if (m_denoisePanel->isVisible())
        closeDenoiseTool();
    else if (m_healPanel->isVisible())
        closeHealTool();
    else {
        m_input.setMode(InputController::Mode::Browse);
        m_canvas->setFocus();
    }
}

void MainWindow::doUndo()
{
    // While brush-painting, Ctrl+Z is a per-stroke session undo.
    if (m_brushTarget != BrushTarget::None && brushSessionUndo())
        return;
    if (m_graph.undo())
        afterHistoryChange();
}

void MainWindow::doRedo()
{
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
            m_tonePanel->reveal({t->exposure(), t->contrast(), t->saturation(),
                                 t->kelvin(), t->tint()});
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
    if (m_lensPanel->isVisible() && m_lens) {
        m_lensPanel->reveal(m_lens->params(), m_lens->lensMatched(),
                            m_lens->matchedLensName());
    }
    if (m_sharpenPanel->isVisible() && m_sharpen)
        m_sharpenPanel->reveal(m_sharpen->values());
    if (m_denoisePanel->isVisible() && m_denoise)
        m_denoisePanel->reveal(m_denoise->values());
    updateHistogram(); // reflect the restored state (no-op when hidden)
    // If a mask brush is active, resync the working mask to the restored state.
    if (m_brushTarget == BrushTarget::Selective
        && m_graph.activeLayer().mask().type == MaskSpec::Brush) {
        m_brushMask = m_graph.activeLayer().mask().brush;
        m_brushUndo.clear();
        recomputeSelectiveMask();
    }
}

void MainWindow::showHint(const QString &text)
{
    m_hint->setText(text);
    m_hint->adjustSize();
    layoutOverlays();
}

void MainWindow::layoutOverlays()
{
    // Scrim covers the whole window, behind the palette.
    m_scrim->setGeometry(rect());
    m_brushRing->setGeometry(rect());

    // Busy badge: top-centre of the canvas.
    m_healBusy->move((width() - m_healBusy->width()) / 2, 16);

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
    clampIntoView(m_lensPanel);
    clampIntoView(m_sharpenPanel);
    clampIntoView(m_denoisePanel);
    clampIntoView(m_healPanel);
    clampIntoView(m_layersPanel);

    // Histogram: bottom-left of the canvas, above the hint bar.
    if (m_histogram && m_histogram->isVisible()) {
        m_histogram->move(18, height() - m_histogram->height() - 64);
        m_histogram->raise();
    }

    // The mask gizmo overlays the canvas area. Keep it above the canvas but below
    // the floating panels so their controls stay clickable (the gizmo passes
    // unhandled clicks through to the canvas underneath).
    if (m_maskGizmo) {
        m_maskGizmo->setGeometry(m_canvas->geometry());
        m_maskGizmo->raise();
        for (QWidget *panel : {static_cast<QWidget *>(m_tonePanel),
                               static_cast<QWidget *>(m_curvesPanel),
                               static_cast<QWidget *>(m_looksPanel),
                               static_cast<QWidget *>(m_monoPanel),
                               static_cast<QWidget *>(m_lensPanel),
                               static_cast<QWidget *>(m_sharpenPanel),
                               static_cast<QWidget *>(m_denoisePanel),
                               static_cast<QWidget *>(m_healPanel),
                               static_cast<QWidget *>(m_layersPanel)}) {
            if (panel && panel->isVisible())
                panel->raise();
        }
        m_brushRing->raise(); // brush cursor stays on top of the gizmo
        m_healBusy->raise();
    }

    // Hint bar: bottom-centre.
    m_hint->move((width() - m_hint->width()) / 2, height() - m_hint->height() - 18);
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

    switch (m_input.mode()) {
    case InputController::Mode::Browse:
        if (e->key() == Qt::Key_Slash) {
            openCommandPalette();
            return;
        }
        // Esc dismisses the persistent Layers panel (and its mask editing).
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
