#pragma once

#include <QPixmap>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QSlider;
class QVBoxLayout;

// PresetsPanel is the floating browser for shipped "looks": a scrollable list of
// preset cards, grouped by category (B&W / Color), each showing a thumbnail of
// the current photo with that preset applied. Clicking a card emits
// applyRequested(id); MainWindow owns the presets and does the actual apply +
// thumbnail rendering (the panel is purely presentational).
class PresetsPanel : public QWidget {
    Q_OBJECT

public:
    explicit PresetsPanel(QWidget *parent = nullptr);

    struct Item {
        QString id;
        QString name;
        QString category;         // section header text
        QPixmap thumb;            // may be null (renders a placeholder)
        bool userEditable = false; // true for user presets → right-click rename/delete
    };

    // Rebuilds the list. Items are shown in order, with a header inserted each
    // time the category changes.
    void setItems(const QVector<Item> &items);

    void reveal();

    // Current blend amount [0,100]. Shown by the Amount slider; applied to the
    // active preset's layer opacity.
    int amount() const;
    void setAmount(int percent);

signals:
    void applyRequested(const QString &id);
    void renameRequested(const QString &id, const QString &newName);
    void deleteRequested(const QString &id);
    void amountChanged(int percent); // live drag of the Amount slider

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QWidget *m_list = nullptr;    // holds the rows
    QVBoxLayout *m_listLayout = nullptr;
    QSlider *m_amount = nullptr;
    QLabel *m_amountValue = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
