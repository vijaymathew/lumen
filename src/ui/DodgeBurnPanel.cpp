#include "ui/DodgeBurnPanel.h"

#include "core/DodgeBurnNode.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace {
constexpr int kPanelWidth = 248;

QPushButton *makeToggle(const QString &text, QWidget *parent)
{
    auto *b = new QPushButton(text, parent);
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
} // namespace

DodgeBurnPanel::DodgeBurnPanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("dodgeBurnPanel"), QStringLiteral("Dodge & Burn"),
                        kPanelWidth, parent)
{
    m_dodgeButton = makeToggle(QStringLiteral("Dodge"), this);
    m_burnButton = makeToggle(QStringLiteral("Burn"), this);
    connect(m_dodgeButton, &QPushButton::clicked, this, [this] { setDodge(true); });
    connect(m_burnButton, &QPushButton::clicked, this, [this] { setDodge(false); });
    auto *toolRow = new QHBoxLayout;
    toolRow->setContentsMargins(0, 0, 0, 0);
    toolRow->addWidget(m_dodgeButton);
    toolRow->addWidget(m_burnButton);
    contentLayout()->addLayout(toolRow);

    m_paintButton = makeToggle(QStringLiteral("Paint"), this);
    m_eraseButton = makeToggle(QStringLiteral("Erase"), this);
    auto *clear = new QPushButton(QStringLiteral("Clear"), this);
    clear->setCursor(Qt::PointingHandCursor);
    connect(m_paintButton, &QPushButton::clicked, this, [this] { setAdd(true); });
    connect(m_eraseButton, &QPushButton::clicked, this, [this] { setAdd(false); });
    connect(clear, &QPushButton::clicked, this, &DodgeBurnPanel::clearRequested);
    auto *modeRow = new QHBoxLayout;
    modeRow->setContentsMargins(0, 0, 0, 0);
    modeRow->addWidget(m_paintButton);
    modeRow->addWidget(m_eraseButton);
    modeRow->addStretch(1);
    modeRow->addWidget(clear);
    contentLayout()->addLayout(modeRow);

    m_size = addBrushRow(QStringLiteral("Size"), 1, 100, 30, &m_sizeValue);
    m_size->setToolTip(QStringLiteral("Hold S and scroll the wheel over the image"));
    m_hardness = addBrushRow(QStringLiteral("Hardness"), 1, 100, 50, &m_hardnessValue);
    m_hardness->setToolTip(QStringLiteral("Hold H and scroll the wheel over the image"));
    m_exposure = addBrushRow(QStringLiteral("Exposure"), DodgeBurnNode::kMinExposure,
                             DodgeBurnNode::kMaxExposure, DodgeBurnNode::kDefaultExposure,
                             &m_exposureValue);

    auto *rangeLabel = new QLabel(QStringLiteral("Range"), this);
    rangeLabel->setObjectName(QStringLiteral("rowName"));
    contentLayout()->addWidget(rangeLabel);
    auto *rangeRow = new QHBoxLayout;
    rangeRow->setContentsMargins(0, 0, 0, 0);
    const QString names[3] = {QStringLiteral("Shadows"), QStringLiteral("Midtones"),
                              QStringLiteral("Highlights")};
    for (int i = 0; i < 3; ++i) {
        m_rangeButtons[i] = makeToggle(names[i], this);
        connect(m_rangeButtons[i], &QPushButton::clicked, this, [this, i] { setRange(i); });
        rangeRow->addWidget(m_rangeButtons[i]);
    }
    contentLayout()->addLayout(rangeRow);

    auto *hint = new QLabel(
        QStringLiteral("paint to lighten / darken · S / H + scroll resize the brush · "
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
// that consumes Esc/Return/Enter to emit closed(), which this panel doesn't
// have — it closes via the key press bubbling to MainWindow (see HealPanel).
QSlider *DodgeBurnPanel::addBrushRow(const QString &name, int min, int max, int def,
                                     QLabel **valueOut)
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
    slider->setRange(min, max);
    slider->setValue(def);
    connect(slider, &QSlider::valueChanged, this, [this, slider, valueLabel](int v) {
        valueLabel->setText(QString::number(v));
        if (slider == m_exposure)
            emitEffect();
        else
            emitBrush();
    });

    contentLayout()->addLayout(header);
    contentLayout()->addWidget(slider);
    *valueOut = valueLabel;
    return slider;
}

void DodgeBurnPanel::setDodge(bool dodge)
{
    m_dodge = dodge;
    m_dodgeButton->setChecked(dodge);
    m_burnButton->setChecked(!dodge);
    emit modeChanged(dodge);
}

void DodgeBurnPanel::setAdd(bool add)
{
    m_add = add;
    m_paintButton->setChecked(add);
    m_eraseButton->setChecked(!add);
    emitBrush();
}

void DodgeBurnPanel::setRange(int range)
{
    m_range = range;
    for (int i = 0; i < 3; ++i)
        m_rangeButtons[i]->setChecked(i == range);
    emitEffect();
}

void DodgeBurnPanel::emitBrush()
{
    emit brushChanged(m_size->value(), m_hardness->value(), m_add);
}

void DodgeBurnPanel::emitEffect()
{
    emit effectChanged(m_exposure->value(), m_range);
}

void DodgeBurnPanel::reveal(bool dodge, int size, int hardness, bool add, int exposure,
                            int range)
{
    {
        const QSignalBlocker b1(m_size);
        const QSignalBlocker b2(m_hardness);
        const QSignalBlocker b3(m_exposure);
        m_size->setValue(size);
        m_hardness->setValue(hardness);
        m_exposure->setValue(exposure);
        m_sizeValue->setText(QString::number(size));
        m_hardnessValue->setText(QString::number(hardness));
        m_exposureValue->setText(QString::number(exposure));
    }
    m_dodge = dodge;
    m_add = add;
    m_range = range;
    m_dodgeButton->setChecked(dodge);
    m_burnButton->setChecked(!dodge);
    m_paintButton->setChecked(add);
    m_eraseButton->setChecked(!add);
    for (int i = 0; i < 3; ++i)
        m_rangeButtons[i]->setChecked(i == range);
    adjustSize();
    show();
    raise();
    setFocus(Qt::ShortcutFocusReason);
}

void DodgeBurnPanel::setBrushParams(int size, int hardness)
{
    const QSignalBlocker b1(m_size);
    const QSignalBlocker b2(m_hardness);
    m_size->setValue(size);
    m_hardness->setValue(hardness);
    m_sizeValue->setText(QString::number(size));
    m_hardnessValue->setText(QString::number(hardness));
}
