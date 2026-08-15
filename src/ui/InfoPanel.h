#pragma once

#include "ui/FloatingToolPanel.h"

#include <QVector>

class QGridLayout;
class QLabel;

// InfoPanel is a floating, read-only card showing the open image's metadata:
// file path, dimensions, and (for RAW) the camera/lens identity and capture
// settings. It's purely a view — MainWindow gathers the values from the active
// Document and pushes them as label/value rows via setRows(). Draggable, like
// the other corner overlays; toggled from the bottom-right view cluster.
class InfoPanel : public FloatingToolPanel {
    Q_OBJECT

public:
    struct Row {
        QString label;
        QString value;
    };

    explicit InfoPanel(QWidget *parent = nullptr);

    // Rebuilds the label/value grid (an empty list shows the placeholder).
    void setRows(const QVector<Row> &rows);
    void reveal(); // show + raise

private:
    void clearRows();

    QLabel *m_empty = nullptr;      // "No image open" placeholder
    QGridLayout *m_grid = nullptr;
    QVector<QWidget *> m_cells;     // label + value widgets, cleared on rebuild
};
