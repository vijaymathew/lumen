#include "ui/MainWindow.h"

#include "core/Image.h"
#include "gpu/CanvasWidget.h"
#include "input/CommandPalette.h"
#include "ui/CurvesPanel.h"
#include "ui/LooksPanel.h"
#include "ui/TonePanel.h"

#include <memory>

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPainter>
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

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Lumen"));

    // The graph owns the nodes; we keep raw pointers to drive them. Order is
    // tune -> curves -> lut (the shader applies tone ops, then the curve LUT,
    // then the 3D look LUT, to match).
    m_tune = static_cast<TuneNode *>(m_graph.addNode(std::make_unique<TuneNode>()));
    m_curves = static_cast<CurvesNode *>(m_graph.addNode(std::make_unique<CurvesNode>()));
    m_lutNode = static_cast<LutNode *>(m_graph.addNode(std::make_unique<LutNode>()));

    m_canvas = new CanvasWidget(this);
    setCentralWidget(m_canvas);

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

    m_graph.setSource(source);             // full-res source for export
    m_canvas->setImage(source.toQImage()); // unedited image for the GPU preview
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
    m_canvas->setPreviewState(m_graph.previewState());
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

void MainWindow::closeActiveTool()
{
    if (m_tonePanel->isVisible())
        closeToneTool();
    else if (m_curvesPanel->isVisible())
        closeCurvesTool();
    else if (m_looksPanel->isVisible())
        closeLooksTool();
    else {
        m_input.setMode(InputController::Mode::Browse);
        m_canvas->setFocus();
    }
}

void MainWindow::doUndo()
{
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
    updatePreview();
    // If a tool is open, reseed its control from the restored state.
    if (m_tonePanel->isVisible())
        m_tonePanel->reveal({m_tune->exposure(), m_tune->contrast(), m_tune->saturation()});
    if (m_curvesPanel->isVisible())
        m_curvesPanel->reveal(m_curves->curves());
    if (m_looksPanel->isVisible())
        m_looksPanel->reveal(QFileInfo(m_lutNode->sourcePath()).fileName(),
                             m_lutNode->intensity());
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
        break;
    default:
        break;
    }
    QMainWindow::keyPressEvent(e);
}
