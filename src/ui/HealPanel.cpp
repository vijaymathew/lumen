#include "ui/HealPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace {
constexpr int kPanelWidth = 248;
}

HealPanel::HealPanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("healPanel"), QStringLiteral("Heal"), kPanelWidth, parent)
{
    m_addButton = new QPushButton(QStringLiteral("Paint"), this);
    m_subButton = new QPushButton(QStringLiteral("Erase"), this);
    m_addButton->setCheckable(true);
    m_subButton->setCheckable(true);
    auto *clear = new QPushButton(QStringLiteral("Clear"), this);
    connect(m_addButton, &QPushButton::clicked, this, [this] {
        m_add = true;
        m_addButton->setChecked(true);
        m_subButton->setChecked(false);
        emitSettings();
    });
    connect(m_subButton, &QPushButton::clicked, this, [this] {
        m_add = false;
        m_addButton->setChecked(false);
        m_subButton->setChecked(true);
        emitSettings();
    });
    connect(clear, &QPushButton::clicked, this, &HealPanel::clearRequested);

    auto *modeRow = new QHBoxLayout;
    modeRow->setContentsMargins(0, 0, 0, 0);
    modeRow->addWidget(m_addButton);
    modeRow->addWidget(m_subButton);
    modeRow->addStretch(1);
    modeRow->addWidget(clear);
    contentLayout()->addLayout(modeRow);

    m_size = addBrushRow(QStringLiteral("Size"), 30, &m_sizeValue);
    m_size->setToolTip(QStringLiteral("Hold S and scroll the wheel over the image"));
    m_hardness = addBrushRow(QStringLiteral("Hardness"), 50, &m_hardnessValue);
    m_hardness->setToolTip(QStringLiteral("Hold H and scroll the wheel over the image"));

    // Fill quality: Detailed (Criminisi exemplar) vs Fast (Telea diffusion).
    m_qualityButton = new QPushButton(QStringLiteral("Fill: Detailed"), this);
    connect(m_qualityButton, &QPushButton::clicked, this, [this] {
        m_highQuality = !m_highQuality;
        m_qualityButton->setText(m_highQuality ? QStringLiteral("Fill: Detailed")
                                               : QStringLiteral("Fill: Fast"));
        emit qualityChanged(m_highQuality);
    });
    auto *qualityRow = new QHBoxLayout;
    qualityRow->setContentsMargins(0, 0, 0, 0);
    qualityRow->addWidget(m_qualityButton);
    qualityRow->addStretch(1);
    contentLayout()->addLayout(qualityRow);

    auto *hint = new QLabel(
        QStringLiteral("paint over a blemish · S / H + scroll resize the brush · "
                       "Ctrl+Z undoes a stroke"),
        this);
    hint->setObjectName(QStringLiteral("section"));
    hint->setWordWrap(true);
    contentLayout()->addWidget(hint);

    appendStyleSheet(QStringLiteral(R"(
        #section { color: #8a8a90; font-size: 11px; }
        QPushButton { padding: 2px 8px; font-size: 11px; }
    )"));
}

// Deliberately not FloatingToolPanel::addRow(): that installs an event filter
// that consumes Esc/Return/Enter to emit closed() — which HealPanel doesn't
// have. It closes via the normal keyPress bubbling to MainWindow instead
// (there's no per-tool closed() signal for it to intercept and eat), so its
// sliders must not swallow that key first.
QSlider *HealPanel::addBrushRow(const QString &name, int def, QLabel **valueOut)
{
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    auto *nameLabel = new QLabel(name, this);
    nameLabel->setObjectName(QStringLiteral("rowName"));
    auto *valueLabel = new QLabel(QString::number(def), this);
    valueLabel->setObjectName(QStringLiteral("rowValue"));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(nameLabel);
    header->addStretch(1);
    header->addWidget(valueLabel);

    auto *slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(1, 100);
    slider->setValue(def);
    connect(slider, &QSlider::valueChanged, this, [this, valueLabel](int v) {
        valueLabel->setText(QString::number(v));
        emitSettings();
    });

    contentLayout()->addLayout(header);
    contentLayout()->addWidget(slider);
    *valueOut = valueLabel;
    return slider;
}

void HealPanel::emitSettings()
{
    emit settingsChanged(m_size->value(), m_hardness->value(), m_add);
}

void HealPanel::reveal(int size, int hardness, bool add, bool highQuality)
{
    {
        const QSignalBlocker b1(m_size);
        const QSignalBlocker b2(m_hardness);
        m_size->setValue(size);
        m_hardness->setValue(hardness);
        m_sizeValue->setText(QString::number(size));
        m_hardnessValue->setText(QString::number(hardness));
    }
    m_add = add;
    m_addButton->setChecked(add);
    m_subButton->setChecked(!add);
    m_highQuality = highQuality;
    m_qualityButton->setText(highQuality ? QStringLiteral("Fill: Detailed")
                                         : QStringLiteral("Fill: Fast"));
    adjustSize();
    show();
    raise();
    setFocus(Qt::ShortcutFocusReason);
}

void HealPanel::setBrushParams(int size, int hardness)
{
    const QSignalBlocker b1(m_size);
    const QSignalBlocker b2(m_hardness);
    m_size->setValue(size);
    m_hardness->setValue(hardness);
    m_sizeValue->setText(QString::number(size));
    m_hardnessValue->setText(QString::number(hardness));
}
