#pragma once

#include "core/Image.h"

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;

// ExportDialog collects the export format, quality, output size and colour space
// before the file is written. The file path is chosen afterwards with a save
// dialog.
class ExportDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExportDialog(QWidget *parent = nullptr);

    // Seed the controls (e.g. remember the last export).
    void setSelection(const QString &extension, int quality, int longEdge,
                      Image::ColorSpace colorSpace);

    QString extension() const;          // "jpg", "png", "tiff", "webp"
    int quality() const;                // 0-100, or -1 if the format is lossless
    int bits() const;                   // 8 or 16 (16 only for lossless PNG/TIFF)
    int longEdge() const;               // max long-edge px, or 0 for full size
    Image::ColorSpace colorSpace() const;

private:
    void syncRows();

    QComboBox *m_format = nullptr;
    QSlider *m_quality = nullptr;
    QLabel *m_qualityValue = nullptr;
    QLabel *m_qualityName = nullptr;
    QComboBox *m_bits = nullptr;
    QLabel *m_bitsName = nullptr;
    QCheckBox *m_resize = nullptr;
    QSpinBox *m_longEdge = nullptr;
    QComboBox *m_colorSpace = nullptr;
};
