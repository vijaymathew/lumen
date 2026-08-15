#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QLabel;

// The metadata sidebar docked in ImageOpenDialog: shows dimensions and (for
// RAW) camera/lens/capture settings for whatever is currently selected.
// Unlike InfoPanel (the floating card for the *open* document), this is a
// plain embedded widget and reads metadata straight off disk via
// raw::readMetadata's cheap header-only path, so it can update on every
// selection change without stalling the UI.
class ImageMetaPanel : public QWidget {
    Q_OBJECT

public:
    explicit ImageMetaPanel(QWidget *parent = nullptr);

    // Updates the panel for the given selection: empty shows a placeholder,
    // one path shows its full metadata, several show just a count.
    void setSelection(const QStringList &paths);

private:
    struct Row {
        QString label;
        QString value;
    };

    void setRows(const QVector<Row> &rows);
    void clearRows();

    QLabel *m_placeholder = nullptr;
    QGridLayout *m_grid = nullptr;
    QVector<QWidget *> m_cells;
};
