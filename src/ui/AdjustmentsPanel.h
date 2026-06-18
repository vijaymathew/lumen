#pragma once

#include <QVector>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

// AdjustmentsPanel is the floating card listing the edits actually applied to the
// image (in pipeline order). Each row can be toggled on/off, deleted, or clicked to
// preview the image up to that point. A header toggle flips Before/After (original
// vs edited). The panel is purely a view: MainWindow owns the data, pushes rows via
// setItems(), and acts on the emitted row index.
class AdjustmentsPanel : public QWidget {
    Q_OBJECT

public:
    struct Item {
        QString name;
        bool enabled = true;
    };

    explicit AdjustmentsPanel(QWidget *parent = nullptr);

    // Rebuilds the row list. `selected` is the "view up to here" row (-1 = none, so
    // the full image is shown); `comparing` reflects the Before/After state.
    void setItems(const QVector<Item> &items, int selected, bool comparing);
    void reveal(); // show + raise

signals:
    void compareToggled(bool on);       // Before/After
    void toggleRequested(int index, bool on);
    void deleteRequested(int index);
    void viewUpToRequested(int index);  // show the image up to this adjustment
    void closed();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void clearRows();

    QPushButton *m_compare = nullptr;
    QLabel *m_empty = nullptr;       // "No adjustments yet" placeholder
    QVBoxLayout *m_listLayout = nullptr;
    QVector<QWidget *> m_rows;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
