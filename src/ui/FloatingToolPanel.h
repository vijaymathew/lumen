#pragma once

#include <QPoint>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QSlider;
class QVBoxLayout;

// Common chrome shared by Lumen's floating tool cards (Sharpen, Denoise,
// Crop, ...): the dark rounded-card look, a "<Name>" title in a header row,
// drag-to-move from anywhere on the card, Esc/Enter-closes for whichever
// child widgets opt in via closesOnEscape(), and the addRow() slider-row
// builder most of these panels share. A handful of panels don't fit this
// shape well enough to be worth forcing (CurvesPanel is a single custom-
// painted card; LayersPanel's eventFilter serves inline rename-editing, not
// closing) — they stay plain QWidgets and just don't inherit this.
class FloatingToolPanel : public QWidget {
    Q_OBJECT

public:
    // `objectName` is also the stylesheet selector for the card's background
    // (each panel keeps its own, though nothing outside its own stylesheet
    // depends on the exact name). `fixedWidth` of 0 leaves the width to
    // layout/adjustSize() (only ColorGradePanel does this).
    FloatingToolPanel(const QString &objectName, const QString &title, int fixedWidth,
                      QWidget *parent = nullptr);

signals:
    void closed(); // emitted for a widget passed to closesOnEscape() on Esc/Return/Enter

protected:
    // The title lives in this row; panels with an extra header widget (an
    // enable checkbox, a Before/After toggle) add it here too, typically
    // after an addStretch(1).
    QHBoxLayout *headerRow() const { return m_headerRow; }
    // The panel's top-level content layout (holds headerRow() as its first
    // item); subclasses add their own rows/widgets below it.
    QVBoxLayout *contentLayout() const { return m_layout; }

    // Appends to the panel's stylesheet — QSS cascades within one stylesheet
    // string, so a rule here for a selector the base already set (e.g. a
    // different QPushButton:checked colour) overrides it for this panel only.
    void appendStyleSheet(const QString &extraCss);

    // Installs this panel as `w`'s event filter, so Esc/Return/Enter while it
    // has focus emits closed() (see eventFilter()).
    void closesOnEscape(QWidget *w);

    // Appends a "<name> ... <value>" header + a horizontal QSlider to
    // contentLayout(), and calls closesOnEscape() on the slider. The caller
    // wires up valueChanged itself — every panel maps a differently-shaped
    // Values struct, so there's no one generic slot to connect here.
    QSlider *addRow(const QString &name, int min, int max, QLabel **valueOut);

    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QVBoxLayout *m_layout = nullptr;
    QHBoxLayout *m_headerRow = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
