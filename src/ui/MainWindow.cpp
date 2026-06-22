#include "ui/MainWindow.h"

#include "core/ImageBuffer.h"
#include "gpu/CanvasWidget.h"
#include "input/CommandPalette.h"
#include "ui/ExposurePanel.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QShortcut>
#include <QStandardPaths>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_tune(std::make_unique<TuneNode>())
{
    setWindowTitle(QStringLiteral("Lumen"));

    m_canvas = new CanvasWidget(this);
    setCentralWidget(m_canvas);
    m_canvas->installEventFilter(this); // catch "/" while in Browse mode

    m_palette = new CommandPalette(this);
    connect(m_palette, &CommandPalette::commandTriggered, this, &MainWindow::runCommand);
    connect(m_palette, &CommandPalette::dismissed, this, [this] {
        m_input.setMode(InputController::Mode::Browse);
        m_canvas->setFocus();
    });

    m_exposurePanel = new ExposurePanel(this);
    connect(m_exposurePanel, &ExposurePanel::exposureChanged, this, [this](double ev) {
        m_tune->setExposure(static_cast<float>(ev)); // update the model node
        m_canvas->setExposure(static_cast<float>(ev)); // live GPU preview
    });
    connect(m_exposurePanel, &ExposurePanel::closed, this, &MainWindow::closeExposureTool);

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
    // the palette; "/" is handled via the canvas event filter instead.
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+O")), this, [this] { openImageDialog(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Q")), this, [this] { close(); });
    new QShortcut(QKeySequence(Qt::Key_F11), this, [this] { toggleFullScreen(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+0")), this, [this] { m_canvas->resetView(); });

    resize(1280, 800);
}

void MainWindow::buildCommands()
{
    // ids consumed by runCommand(). Editing tools are placeholders for now.
    m_palette->setCommands({
        {QStringLiteral("open"), QStringLiteral("Open image…")},
        {QStringLiteral("exposure"), QStringLiteral("Exposure")},
        {QStringLiteral("reset-view"), QStringLiteral("Reset view")},
        {QStringLiteral("fullscreen"), QStringLiteral("Toggle fullscreen")},
        {QStringLiteral("curves"), QStringLiteral("Curves")},
        {QStringLiteral("selective"), QStringLiteral("Selective adjustment")},
        {QStringLiteral("heal"), QStringLiteral("Healing brush")},
        {QStringLiteral("looks"), QStringLiteral("Looks (LUT)")},
        {QStringLiteral("quit"), QStringLiteral("Quit Lumen")},
    });
}

void MainWindow::runCommand(const QString &id)
{
    if (id == QLatin1String("open")) {
        openImageDialog();
    } else if (id == QLatin1String("exposure")) {
        openExposureTool();
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
    ImageBuffer buffer;
    if (!buffer.load(path)) {
        QMessageBox::warning(this, QStringLiteral("Lumen"), buffer.errorString());
        return false;
    }
    m_canvas->setImage(buffer.image());
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

void MainWindow::toggleFullScreen()
{
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}

void MainWindow::openExposureTool()
{
    m_input.setMode(InputController::Mode::ToolActive);
    layoutOverlays();
    m_exposurePanel->reveal(m_tune->exposure());
}

void MainWindow::closeExposureTool()
{
    m_exposurePanel->hide();
    m_input.setMode(InputController::Mode::Browse);
    m_canvas->setFocus();
}

void MainWindow::showHint(const QString &text)
{
    m_hint->setText(text);
    m_hint->adjustSize();
    layoutOverlays();
}

void MainWindow::layoutOverlays()
{
    // Palette: fixed width, near the top-centre.
    const int pw = 360;
    const int ph = 320;
    m_palette->resize(pw, ph);
    m_palette->move((width() - pw) / 2, height() / 8);

    // Exposure tool panel: full-width strip docked at the bottom.
    const int panelH = 64;
    m_exposurePanel->setGeometry(0, height() - panelH, width(), panelH);

    // Hint bar: bottom-centre, lifted above the tool panel when it's visible.
    const int hintBottom = m_exposurePanel->isVisible() ? panelH + 14 : 18;
    m_hint->move((width() - m_hint->width()) / 2,
                 height() - m_hint->height() - hintBottom);
}

void MainWindow::resizeEvent(QResizeEvent *e)
{
    QMainWindow::resizeEvent(e);
    layoutOverlays();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_canvas && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Slash
            && m_input.mode() == InputController::Mode::Browse) {
            m_input.setMode(InputController::Mode::CommandPalette);
            layoutOverlays();
            m_palette->reveal();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
