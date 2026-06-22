#pragma once

#include <QMainWindow>

#include "core/EditGraph.h"
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
    void exportImage();

    InputController m_input;
    CanvasWidget *m_canvas = nullptr;
    QWidget *m_scrim = nullptr; // dims the image behind the command palette
    CommandPalette *m_palette = nullptr;
    ExposurePanel *m_exposurePanel = nullptr;
    QLabel *m_hint = nullptr;

    // The non-destructive edit graph. The GPU preview reads the tune node's
    // exposure live; Export walks the graph at full resolution via libvips.
    EditGraph m_graph;
    TuneNode *m_tune = nullptr; // owned by m_graph
    QString m_sourcePath;       // for a sensible default export name
};
