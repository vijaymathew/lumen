#include "ui/ColorGradePanel.h"

#include "ui/ColorWheel.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int kMasterScale = 100; // slider units per master unit ([-100,100] → [-1,1])
} // namespace

ColorGradePanel::ColorGradePanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("gradePanel"), QStringLiteral("Color grade"), 0, parent)
{
    m_enable = new QCheckBox(QStringLiteral("Enable"), this);
    headerRow()->addStretch(1);
    headerRow()->addWidget(m_enable);

    auto *wheels = new QHBoxLayout;
    wheels->setContentsMargins(0, 0, 0, 0);
    wheels->setSpacing(14);
    contentLayout()->addLayout(wheels);

    m_lift = addColumn(wheels, QStringLiteral("Lift"));
    m_gamma = addColumn(wheels, QStringLiteral("Gamma"));
    m_gain = addColumn(wheels, QStringLiteral("Gain"));
    for (const Wheel &w : {m_lift, m_gamma, m_gain}) {
        connect(w.wheel, &ColorWheel::changed, this, [this](float, float) { onChanged(); });
        connect(w.master, &QSlider::valueChanged, this, [this] { onChanged(); });
    }

    connect(m_enable, &QCheckBox::toggled, this, [this] { onChanged(); });

    appendStyleSheet(QStringLiteral("QCheckBox { color: #b4b4b8; font-size: 12px; }"));
}

ColorGradePanel::Wheel ColorGradePanel::addColumn(QHBoxLayout *wheels, const QString &name)
{
    auto *col = new QVBoxLayout;
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(6);
    auto *label = new QLabel(name, this);
    label->setObjectName(QStringLiteral("rowName"));
    label->setAlignment(Qt::AlignHCenter);
    auto *wheel = new ColorWheel(this);
    auto *master = new QSlider(Qt::Horizontal, this);
    master->setRange(-kMasterScale, kMasterScale);
    closesOnEscape(master);
    closesOnEscape(wheel);
    col->addWidget(label);
    col->addWidget(wheel, 0, Qt::AlignHCenter);
    col->addWidget(master);
    wheels->addLayout(col);
    return {wheel, master};
}

ColorGradeValues ColorGradePanel::currentValues() const
{
    ColorGradeValues v;
    v.enabled = m_enable->isChecked();
    v.liftX = m_lift.wheel->x();
    v.liftY = m_lift.wheel->y();
    v.liftMaster = static_cast<float>(m_lift.master->value()) / kMasterScale;
    v.gammaX = m_gamma.wheel->x();
    v.gammaY = m_gamma.wheel->y();
    v.gammaMaster = static_cast<float>(m_gamma.master->value()) / kMasterScale;
    v.gainX = m_gain.wheel->x();
    v.gainY = m_gain.wheel->y();
    v.gainMaster = static_cast<float>(m_gain.master->value()) / kMasterScale;
    return v;
}

void ColorGradePanel::onChanged()
{
    emit valuesChanged(currentValues());
}

void ColorGradePanel::reveal(const ColorGradeValues &v)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b1(m_lift.master);
    const QSignalBlocker b2(m_gamma.master);
    const QSignalBlocker b3(m_gain.master);
    m_enable->setChecked(v.enabled);
    m_lift.wheel->setValue(v.liftX, v.liftY);
    m_gamma.wheel->setValue(v.gammaX, v.gammaY);
    m_gain.wheel->setValue(v.gainX, v.gainY);
    m_lift.master->setValue(static_cast<int>(std::lround(v.liftMaster * kMasterScale)));
    m_gamma.master->setValue(static_cast<int>(std::lround(v.gammaMaster * kMasterScale)));
    m_gain.master->setValue(static_cast<int>(std::lround(v.gainMaster * kMasterScale)));

    adjustSize();
    show();
    raise();
    setFocus(Qt::ShortcutFocusReason);
}
