#pragma once

#include <QMainWindow>

#include "core/CurvesNode.h"
#include "core/EditGraph.h"
#include "core/LutNode.h"
#include "core/TuneNode.h"
#include "input/InputController.h"

class CanvasWidget;
class CommandPalette;
class CurvesPanel;
class TonePanel;
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
    // Central key handling: catches keys via propagation no matter which child
    // widget has focus, so the active tool can always be closed.
    void keyPressEvent(QKeyEvent *e) override;

private:
    void buildCommands();
    void runCommand(const QString &id);
    void openImageDialog();
    void toggleFullScreen();
    void showHint(const QString &text);
    void layoutOverlays();

    void openCommandPalette();
    void openToneTool();
    void closeToneTool();
    void openCurvesTool();
    void closeCurvesTool();
    void closeActiveTool();
    void updatePreview(); // push tone state + curve LUT to the canvas
    void exportImage();

    void doUndo();
    void doRedo();
    void afterHistoryChange(); // refresh preview + any open tool after undo/redo

    InputController m_input;
    CanvasWidget *m_canvas = nullptr;
    QWidget *m_scrim = nullptr; // dims the image behind the command palette
    CommandPalette *m_palette = nullptr;
    TonePanel *m_tonePanel = nullptr;
    CurvesPanel *m_curvesPanel = nullptr;
    QLabel *m_hint = nullptr;

    // The non-destructive edit graph. The GPU preview reads the tune node's
    // exposure live; Export walks the graph at full resolution via libvips.
    EditGraph m_graph;
    TuneNode *m_tune = nullptr;     // owned by m_graph
    CurvesNode *m_curves = nullptr; // owned by m_graph
    LutNode *m_lutNode = nullptr;   // owned by m_graph
    QString m_sourcePath;           // for a sensible default export name
};
