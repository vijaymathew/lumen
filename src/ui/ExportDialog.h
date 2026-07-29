#pragma once

#include "core/Image.h"

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;

// The dialog's controls as a plain value, so the last export can be stored and
// handed back next time. Every control is kept — including the ones the current
// format greys out (quality for PNG, depth for JPEG) and the long-edge figure
// while the limit is switched off — so nothing is silently forgotten when the
// user switches formats or unticks the resize box.
struct ExportSettings {
    QString extension = QStringLiteral("jpg"); // "jpg", "png", "tiff", "webp"
    int quality = 90;                          // 1-100, for the lossy formats
    int bits = 8;                              // 8 or 16, for the lossless formats
    bool limitLongEdge = false;                // is the resize limit switched on?
    int longEdge = 2048;                       // px, remembered while the limit is off
    Image::ColorSpace colorSpace = Image::ColorSpace::SRGB;
};

// ExportDialog collects the export format, quality, output size and colour space
// before the file is written. The file path is chosen afterwards with a save
// dialog.
class ExportDialog : public QDialog {
    Q_OBJECT

public:
    explicit ExportDialog(QWidget *parent = nullptr);

    // Seed the controls from, and read them back into, a remembered export.
    void setSettings(const ExportSettings &s);
    ExportSettings settings() const;

    // The choices as the encoder needs them, with the rules of the chosen format
    // applied (no quality for lossless, no 16-bit for lossy).
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
