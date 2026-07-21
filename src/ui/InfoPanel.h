#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QLabel;

// InfoPanel is a floating, read-only card showing the open image's metadata:
// file path, dimensions, and (for RAW) the camera/lens identity and capture
// settings. It's purely a view — MainWindow gathers the values from the active
// Document and pushes them as label/value rows via setRows(). Draggable, like
// the other corner overlays; toggled from the bottom-right view cluster.
class InfoPanel : public QWidget {
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

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void clearRows();

    QLabel *m_empty = nullptr;      // "No image open" placeholder
    QGridLayout *m_grid = nullptr;
    QVector<QWidget *> m_cells;     // label + value widgets, cleared on rebuild

    bool m_dragging = false;
    QPoint m_dragOffset;
};
