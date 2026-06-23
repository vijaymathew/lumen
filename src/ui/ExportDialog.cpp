#include "ui/ExportDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
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

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Format"), m_format);
    m_qualityName = new QLabel(QStringLiteral("Quality"), this);
    form->addRow(m_qualityName, qualityRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Export…"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    connect(m_format, &QComboBox::currentIndexChanged, this, &ExportDialog::syncQualityRow);
    connect(m_quality, &QSlider::valueChanged, this, [this](int v) {
        m_qualityValue->setText(QString::number(v));
    });
    m_qualityValue->setText(QString::number(m_quality->value()));
    syncQualityRow();
}

void ExportDialog::syncQualityRow()
{
    const bool lossy = kFormats[m_format->currentIndex()].lossy;
    m_qualityName->setEnabled(lossy);
    m_quality->setEnabled(lossy);
    m_qualityValue->setEnabled(lossy);
}

void ExportDialog::setSelection(const QString &extension, int quality)
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
    syncQualityRow();
}

QString ExportDialog::extension() const
{
    return m_format->currentData().toString();
}

int ExportDialog::quality() const
{
    return kFormats[m_format->currentIndex()].lossy ? m_quality->value() : -1;
}
