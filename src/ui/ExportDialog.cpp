#include "ui/ExportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
// Each format: display name, extension, and whether quality applies (lossy).
struct Fmt {
    const char *label;
    const char *ext;
    bool lossy;
};
const Fmt kFormats[] = {
    {"JPEG", "jpg", true},
    {"PNG", "png", false},
    {"TIFF", "tiff", false},
    {"WebP", "webp", true},
};
} // namespace

ExportDialog::ExportDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Export image"));

    m_format = new QComboBox(this);
    for (const Fmt &f : kFormats)
        m_format->addItem(QString::fromLatin1(f.label), QString::fromLatin1(f.ext));

    m_quality = new QSlider(Qt::Horizontal, this);
    m_quality->setRange(1, 100);
    m_quality->setValue(90);
    m_qualityValue = new QLabel(this);
    m_qualityValue->setFixedWidth(32);
    m_qualityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *qualityRow = new QHBoxLayout;
    qualityRow->setContentsMargins(0, 0, 0, 0);
    qualityRow->addWidget(m_quality, 1);
    qualityRow->addWidget(m_qualityValue);

    m_bits = new QComboBox(this);
    m_bits->addItem(QStringLiteral("8-bit"), 8);
    m_bits->addItem(QStringLiteral("16-bit"), 16);

    // Resize: cap the longest edge (downscale only). Unchecked keeps full size.
    m_resize = new QCheckBox(QStringLiteral("Limit long edge to"), this);
    m_longEdge = new QSpinBox(this);
    m_longEdge->setRange(16, 60000);
    m_longEdge->setSingleStep(256);
    m_longEdge->setValue(2048);
    m_longEdge->setSuffix(QStringLiteral(" px"));

    auto *resizeRow = new QHBoxLayout;
    resizeRow->setContentsMargins(0, 0, 0, 0);
    resizeRow->addWidget(m_resize);
    resizeRow->addWidget(m_longEdge, 1);

    // Output colour space. Non-sRGB choices need colour management (lcms); when
    // unavailable the control is disabled and export stays sRGB.
    m_colorSpace = new QComboBox(this);
    m_colorSpace->addItem(QStringLiteral("sRGB"),
                          static_cast<int>(Image::ColorSpace::SRGB));
    m_colorSpace->addItem(QStringLiteral("Display P3"),
                          static_cast<int>(Image::ColorSpace::DisplayP3));
    m_colorSpace->addItem(QStringLiteral("Adobe RGB"),
                          static_cast<int>(Image::ColorSpace::AdobeRGB));
    if (!Image::colorManagementAvailable()) {
        m_colorSpace->setEnabled(false);
        m_colorSpace->setToolTip(
            QStringLiteral("Colour management unavailable in this build — output is sRGB"));
    }

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Format"), m_format);
    m_qualityName = new QLabel(QStringLiteral("Quality"), this);
    form->addRow(m_qualityName, qualityRow);
    m_bitsName = new QLabel(QStringLiteral("Depth"), this);
    form->addRow(m_bitsName, m_bits);
    form->addRow(QStringLiteral("Size"), resizeRow);
    form->addRow(QStringLiteral("Colour"), m_colorSpace);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Export…"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(m_format, &QComboBox::currentIndexChanged, this, &ExportDialog::syncRows);
    connect(m_resize, &QCheckBox::toggled, this, &ExportDialog::syncRows);
    connect(m_quality, &QSlider::valueChanged, this, [this](int v) {
        m_qualityValue->setText(QString::number(v));
    });
    m_qualityValue->setText(QString::number(m_quality->value()));
    syncRows();
}

void ExportDialog::syncRows()
{
    // Quality applies to lossy formats; 16-bit depth only to lossless (PNG/TIFF).
    const bool lossy = kFormats[m_format->currentIndex()].lossy;
    m_qualityName->setEnabled(lossy);
    m_quality->setEnabled(lossy);
    m_qualityValue->setEnabled(lossy);
    m_bitsName->setEnabled(!lossy);
    m_bits->setEnabled(!lossy);
    m_longEdge->setEnabled(m_resize->isChecked());
}

void ExportDialog::setSelection(const QString &extension, int quality, int longEdge,
                                Image::ColorSpace colorSpace)
{
    const int n = m_format->count();
    for (int i = 0; i < n; ++i) {
        if (m_format->itemData(i).toString() == extension.toLower()) {
            m_format->setCurrentIndex(i);
            break;
        }
    }
    if (quality >= 1 && quality <= 100)
        m_quality->setValue(quality);
    m_resize->setChecked(longEdge > 0);
    if (longEdge > 0)
        m_longEdge->setValue(longEdge);
    if (m_colorSpace->isEnabled()) {
        const int idx = m_colorSpace->findData(static_cast<int>(colorSpace));
        if (idx >= 0)
            m_colorSpace->setCurrentIndex(idx);
    }
    syncRows();
}

QString ExportDialog::extension() const
{
    return m_format->currentData().toString();
}

int ExportDialog::quality() const
{
    return kFormats[m_format->currentIndex()].lossy ? m_quality->value() : -1;
}

int ExportDialog::bits() const
{
    // 16-bit only meaningful for the lossless formats; lossy stay 8-bit.
    return kFormats[m_format->currentIndex()].lossy ? 8 : m_bits->currentData().toInt();
}

int ExportDialog::longEdge() const
{
    return m_resize->isChecked() ? m_longEdge->value() : 0;
}

Image::ColorSpace ExportDialog::colorSpace() const
{
    if (!m_colorSpace->isEnabled())
        return Image::ColorSpace::SRGB;
    return static_cast<Image::ColorSpace>(m_colorSpace->currentData().toInt());
}
