#pragma once

#include <QMainWindow>

#include <memory>

#include "core/TuneNode.h"
#include "input/InputController.h"

class CanvasWidget;
class CommandPalette;
class ExposurePanel;
class QLabel;

// MainWindow is the immersive shell: a fullscreen canvas with a "/"-triggered
// command palette floating over it and a dismissible hint bar. It owns the
// InputController and routes commands to actions. Milestone 1 wires the
// navigation/shell commands; editing tools arrive in later milestones.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Loads an image at startup (e.g. a path passed on the command line).
    bool openPath(const QString &path);

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildCommands();
    void runCommand(const QString &id);
    void openImageDialog();
    void toggleFullScreen();
    void showHint(const QString &text);
    void layoutOverlays();

    void openExposureTool();
    void closeExposureTool();

    InputController m_input;
    CanvasWidget *m_canvas = nullptr;
    CommandPalette *m_palette = nullptr;
    ExposurePanel *m_exposurePanel = nullptr;
    QLabel *m_hint = nullptr;

    // Exposure model node (the GPU preview reads its value; the libvips export
    // path will walk it in a later phase).
    std::unique_ptr<TuneNode> m_tune;
};
