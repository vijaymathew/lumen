#include "ui/MainWindow.h"

#include "core/Image.h"
#include "core/SelectiveMask.h"
#include "gpu/CanvasWidget.h"
#include "input/CommandPalette.h"
#include "ui/CurvesPanel.h"
#include "ui/HealPanel.h"
#include "ui/LooksPanel.h"
#include "ui/SelectivePanel.h"
#include "ui/TonePanel.h"

#include <memory>

#include <QColor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPainter>

#include <cmath>
#include <QLabel>
#include <QMessageBox>
#include <QShortcut>
#include <QStandardPaths>

#include <algorithm>

namespace {

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

    // The graph owns the nodes; we keep raw pointers to drive them. Heal is
    // first (it edits source pixels, baked into the preview base), then the
    // pointwise/LUT ops the shader replicates: tune -> curves -> lut -> selective.
    m_heal = static_cast<HealNode *>(m_graph.addNode(std::make_unique<HealNode>()));
    m_tune = static_cast<TuneNode *>(m_graph.addNode(std::make_unique<TuneNode>()));
    m_curves = static_cast<CurvesNode *>(m_graph.addNode(std::make_unique<CurvesNode>()));
    m_lutNode = static_cast<LutNode *>(m_graph.addNode(std::make_unique<LutNode>()));
    m_selective = static_cast<SelectiveNode *>(m_graph.addNode(std::make_unique<SelectiveNode>()));

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
        m_tune->setExposure(v.exposure); // update the model node
        m_tune->setContrast(v.contrast);
        m_tune->setSaturation(v.saturation);
        updatePreview(); // preview is driven by walking the graph
    });
    connect(m_tonePanel, &TonePanel::closed, this, &MainWindow::closeToneTool);

    m_curvesPanel = new CurvesPanel(this);
    connect(m_curvesPanel, &CurvesPanel::curveChanged, this, [this](const ChannelCurves &c) {
        m_curves->setCurves(c);
        updatePreview();
    });

    m_looksPanel = new LooksPanel(this);
    connect(m_looksPanel, &LooksPanel::loadRequested, this, &MainWindow::loadLookFile);
    connect(m_looksPanel, &LooksPanel::clearRequested, this, [this] {
        m_lutNode->clear();
        m_looksPanel->setLookName(QString());
        updatePreview();
    });
    connect(m_looksPanel, &LooksPanel::intensityChanged, this, [this](double v) {
        m_lutNode->setIntensity(static_cast<float>(v));
        updatePreview();
    });

    m_selectivePanel = new SelectivePanel(this);
    connect(m_selectivePanel, &SelectivePanel::valuesChanged, this,
            [this](const SelectiveValues &v) {
                m_selective->setValues(v);
                const bool brush = v.maskMode == 2;
                m_brushTarget = brush ? BrushTarget::Selective : BrushTarget::None;
                if (brush)
                    m_canvas->setBrushCursor(m_brushSize, m_brushHardness / 100.0f);
                m_canvas->setBrushMode(brush);
                if (brush && m_brushMask.isEmpty())
                    initBrushMask();
                recomputeSelectiveMask();
                updatePreview();
            });
    connect(m_selectivePanel, &SelectivePanel::maskViewChanged, this, [this](int mode) {
        m_maskView = mode;
        updatePreview();
    });
    connect(m_selectivePanel, &SelectivePanel::pickColorRequested, this,
            [this] { m_canvas->setColorPickMode(true); });
    connect(m_canvas, &CanvasWidget::colorPointPicked, this, &MainWindow::onColorPicked);
    connect(m_selectivePanel, &SelectivePanel::brushSettingsChanged, this,
            [this](int size, int hardness, bool add) {
                m_brushSize = size;
                m_brushHardness = hardness;
                m_brushAdd = add;
                m_canvas->setBrushCursor(size, hardness / 100.0f);
            });
    connect(m_selectivePanel, &SelectivePanel::brushClearRequested, this, [this] {
        if (m_brushMask.isEmpty())
            return;
        m_brushUndo.push_back(m_brushMask.data); // clear is undoable
        std::fill(m_brushMask.data.begin(), m_brushMask.data.end(), 0.0f);
        m_canvas->setSelectiveMask(m_brushMask);
    });
    connect(m_canvas, &CanvasWidget::brushStrokeBegan, this, &MainWindow::beginBrushStroke);
    connect(m_canvas, &CanvasWidget::brushPoint, this, &MainWindow::brushAt);
    connect(m_canvas, &CanvasWidget::brushStrokeEnded, this, &MainWindow::endBrushStroke);
    connect(m_canvas, &CanvasWidget::brushAdjustRequested, this, &MainWindow::adjustBrush);

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

    // Dismissible hint bar, bottom-centre over the canvas.
    m_hint = new QLabel(this);
    m_hint->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_hint->setStyleSheet(QStringLiteral(
        "background: rgba(20,20,22,0.85); border-radius: 8px;"
        "padding: 6px 16px; color: #c8c8cc; font-size: 12px;"));
    m_hint->setText(QStringLiteral("/  command palette   ·   Ctrl+O open   ·   F11 fullscreen"));
    m_hint->adjustSize();

    buildCommands();

    // Shell shortcuts. Bare keys are avoided so they don't clash with typing in
    // the palette; "/" and Esc are handled in keyPressEvent instead.
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+O")), this, [this] { openImageDialog(); });
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
        {QStringLiteral("export"), QStringLiteral("Export image…")},
        {QStringLiteral("tone"), QStringLiteral("Tone (exposure, contrast, saturation)")},
        {QStringLiteral("curves"), QStringLiteral("Curves")},
        {QStringLiteral("undo"), QStringLiteral("Undo")},
        {QStringLiteral("redo"), QStringLiteral("Redo")},
        {QStringLiteral("reset-view"), QStringLiteral("Reset view")},
        {QStringLiteral("fullscreen"), QStringLiteral("Toggle fullscreen")},
        {QStringLiteral("looks"), QStringLiteral("Looks (LUT)")},
        {QStringLiteral("selective"), QStringLiteral("Selective adjustment")},
        {QStringLiteral("heal"), QStringLiteral("Healing brush")},
        {QStringLiteral("quit"), QStringLiteral("Quit Lumen")},
    });
}

void MainWindow::runCommand(const QString &id)
{
    if (id == QLatin1String("open")) {
        openImageDialog();
    } else if (id == QLatin1String("export")) {
        exportImage();
    } else if (id == QLatin1String("tone")) {
        openToneTool();
    } else if (id == QLatin1String("curves")) {
        openCurvesTool();
    } else if (id == QLatin1String("looks")) {
        openLooksTool();
    } else if (id == QLatin1String("selective")) {
        openSelectiveTool();
    } else if (id == QLatin1String("heal")) {
        openHealTool();
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
    QString error;
    Image source = Image::fromFile(path, &error);
    if (source.isNull()) {
        QMessageBox::warning(this, QStringLiteral("Lumen"), error);
        return false;
    }

    m_sourceQImage = source.toQImage();
    m_graph.setSource(source);            // full-res source for export
    refreshBaseImage();                   // base = source (healed if a mask exists)
    recomputeSelectiveMask();
    updatePreview();        // apply any existing edits
    m_graph.resetHistory(); // fresh undo timeline for this image
    m_sourcePath = path;
    setWindowTitle(QStringLiteral("Lumen — %1").arg(QFileInfo(path).fileName()));
    return true;
}

void MainWindow::openImageDialog()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open image"), dir,
        QStringLiteral("Images (*.jpg *.jpeg *.png *.tif *.tiff *.webp)"));
    if (!path.isEmpty())
        openPath(path);
}

void MainWindow::exportImage()
{
    if (m_graph.source().isNull()) {
        showHint(QStringLiteral("Open an image before exporting"));
        return;
    }

    // Suggest "<name>-edited.<ext>" next to the original.
    const QFileInfo src(m_sourcePath);
    const QString suffix = src.suffix().isEmpty() ? QStringLiteral("jpg") : src.suffix();
    const QString suggested = src.dir().filePath(
        src.completeBaseName() + QStringLiteral("-edited.") + suffix);

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export image"), suggested,
        QStringLiteral("Images (*.jpg *.jpeg *.png *.tif *.tiff *.webp)"));
    if (path.isEmpty())
        return;

    // Walk the graph at full resolution, then write via libvips.
    const Image result = m_graph.result();
    QString error;
    if (!result.saveToFile(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("Lumen"),
                             QStringLiteral("Export failed: %1").arg(error));
        return;
    }
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
    PreviewState ps = m_graph.previewState();
    ps.selMaskView = static_cast<float>(m_maskView); // preview-only overlay
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
}

void MainWindow::openToneTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    // Default position: top-right with a margin. The user can drag it from here.
    m_tonePanel->adjustSize();
    const int margin = 18;
    m_tonePanel->move(width() - m_tonePanel->width() - margin, margin);
    m_tonePanel->reveal({m_tune->exposure(), m_tune->contrast(), m_tune->saturation()});
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
    m_curvesPanel->reveal(m_curves->curves());
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
    m_looksPanel->reveal(QFileInfo(m_lutNode->sourcePath()).fileName(),
                         m_lutNode->intensity());
}

void MainWindow::closeLooksTool()
{
    m_looksPanel->hide();
    m_graph.commit();
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::loadLookFile()
{
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Load HALD CLUT"), dir,
        QStringLiteral("HALD CLUT images (*.png *.tif *.tiff *.jpg *.jpeg)"));
    if (path.isEmpty())
        return;

    QString error;
    if (!m_lutNode->loadHald(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("Lumen"),
                             QStringLiteral("Could not load look: %1").arg(error));
        return;
    }
    m_looksPanel->setLookName(QFileInfo(path).fileName());
    updatePreview();
}

void MainWindow::openSelectiveTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    m_maskView = 0; // panel resets its toggle on reveal too
    m_selectivePanel->adjustSize();
    const int margin = 18;
    m_selectivePanel->move(width() - m_selectivePanel->width() - margin, margin);
    m_selectivePanel->reveal(m_selective->values());

    // Restore the brush session from the node (may be empty).
    m_brushMask = m_selective->brushMask();
    m_brushUndo.clear();
    m_brushHasLast = false;
    const bool brush = m_selective->values().maskMode == 2;
    m_brushTarget = brush ? BrushTarget::Selective : BrushTarget::None;
    if (brush && m_brushMask.isEmpty())
        initBrushMask();
    if (brush)
        m_canvas->setBrushCursor(m_brushSize, m_brushHardness / 100.0f);
    m_canvas->setBrushMode(brush);

    recomputeSelectiveMask();
    updatePreview();
}

void MainWindow::closeSelectiveTool()
{
    m_selectivePanel->hide();
    m_canvas->setColorPickMode(false);
    m_canvas->setBrushMode(false);
    m_brushTarget = BrushTarget::None;
    // Commit the painted mask into the node (one global undo step).
    if (m_selective->values().maskMode == 2)
        m_selective->setBrushMask(m_brushMask);
    m_brushUndo.clear();
    m_maskView = 0; // clear the mask overlay
    updatePreview();
    m_graph.commit();
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::openHealTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    m_healPanel->adjustSize();
    const int margin = 18;
    m_healPanel->move(width() - m_healPanel->width() - margin, margin);
    m_healPanel->reveal(m_brushSize, m_brushHardness, m_brushAdd);

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

void MainWindow::refreshBaseImage()
{
    // The GPU preview base is the source healed by the heal node; the shader
    // then applies the pointwise/LUT ops on top (heal is first in the graph).
    if (m_heal && !m_heal->healMask().isEmpty() && !m_graph.source().isNull())
        m_canvas->setImage(m_heal->apply(m_graph.source()).toQImage());
    else if (!m_sourceQImage.isNull())
        m_canvas->setImage(m_sourceQImage);
}

void MainWindow::recomputeSelectiveMask()
{
    const SelectiveValues v = m_selective->values();
    if (v.maskMode == 2) {
        m_canvas->setSelectiveMask(m_brushMask); // painted mask
        return;
    }
    if (v.maskMode != 1 || m_sourceQImage.isNull()) {
        m_canvas->setSelectiveMask({}); // luminosity mode is parametric in-shader
        return;
    }

    // Compute the colour-affinity mask from the source at a capped resolution
    // for a responsive preview (export recomputes at full res in the node).
    constexpr int cap = 1280;
    QImage img = m_sourceQImage;
    if (std::max(img.width(), img.height()) > cap)
        img = img.scaled(cap, cap, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    img = img.convertToFormat(QImage::Format_RGBA8888);
    MaskBuffer mask = colorAffinityMask(img.constBits(), img.width(), img.height(), 4,
                                        v.targetR, v.targetG, v.targetB, v.colorRange);
    m_canvas->setSelectiveMask(mask);
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

    // Live feedback: the selective brush shows the mask directly; the heal brush
    // shows a red overlay of the painted region (it inpaints on stroke end).
    m_canvas->setSelectiveMask(m_brushMask);
    if (m_brushTarget == BrushTarget::Heal) {
        m_healPainting = true;
        updatePreview();
    }
}

void MainWindow::endBrushStroke()
{
    m_brushHasLast = false;
    if (m_brushTarget != BrushTarget::Heal)
        return;
    // Inpaint and show the result; restore the selective texture afterwards.
    m_healPainting = false;
    m_heal->setHealMask(m_brushMask);
    refreshBaseImage();
    recomputeSelectiveMask();
    updatePreview();
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
        m_canvas->setSelectiveMask(m_brushMask);
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

    SelectiveValues v = m_selective->values();
    v.maskMode = 1;
    v.targetR = static_cast<float>(c.redF());
    v.targetG = static_cast<float>(c.greenF());
    v.targetB = static_cast<float>(c.blueF());
    m_selective->setValues(v);
    m_selectivePanel->setTargetColor(c);
    recomputeSelectiveMask();
    updatePreview();
}

void MainWindow::closeActiveTool()
{
    if (m_tonePanel->isVisible())
        closeToneTool();
    else if (m_curvesPanel->isVisible())
        closeCurvesTool();
    else if (m_looksPanel->isVisible())
        closeLooksTool();
    else if (m_selectivePanel->isVisible())
        closeSelectiveTool();
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
    // Heal edits the base; rebuild it so undo/redo of a heal is visible.
    refreshBaseImage();
    updatePreview();
    if (m_healPanel->isVisible()) {
        m_brushMask = m_heal->healMask(); // sync session to restored state
        m_brushUndo.clear();
    }
    // If a tool is open, reseed its control from the restored state.
    if (m_tonePanel->isVisible())
        m_tonePanel->reveal({m_tune->exposure(), m_tune->contrast(), m_tune->saturation()});
    if (m_curvesPanel->isVisible())
        m_curvesPanel->reveal(m_curves->curves());
    if (m_looksPanel->isVisible())
        m_looksPanel->reveal(QFileInfo(m_lutNode->sourcePath()).fileName(),
                             m_lutNode->intensity());
    if (m_selectivePanel->isVisible()) {
        m_selectivePanel->reveal(m_selective->values());
        if (m_selective->values().maskMode == 2) {
            m_brushMask = m_selective->brushMask(); // sync session to restored state
            m_brushUndo.clear();
            m_canvas->setBrushMode(true);
        }
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
    clampIntoView(m_selectivePanel);
    clampIntoView(m_healPanel);

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
    switch (m_input.mode()) {
    case InputController::Mode::Browse:
        if (e->key() == Qt::Key_Slash) {
            openCommandPalette();
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
        // Hold s / h and use the wheel to change brush size / hardness.
        if (m_brushTarget != BrushTarget::None && !e->isAutoRepeat()
            && (e->key() == Qt::Key_S || e->key() == Qt::Key_H)) {
            m_adjustHardness = (e->key() == Qt::Key_H);
            m_canvas->setBrushAdjusting(true);
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
        m_selectivePanel->setBrushParams(m_brushSize, m_brushHardness);
}
