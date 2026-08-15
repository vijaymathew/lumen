#include "ui/DefringePanel.h"

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int kPanelWidth = 248;
} // namespace

DefringePanel::DefringePanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("defringePanel"), QStringLiteral("Defringe"), kPanelWidth,
                        parent)
{
    m_enable = new QPushButton(QStringLiteral("Remove fringing"), this);
    m_enable->setCheckable(true);
    connect(m_enable, &QPushButton::toggled, this, &DefringePanel::onChanged);
    contentLayout()->addWidget(m_enable);

    m_purple = addRow(QStringLiteral("Purple"), 0, 100, &m_purpleValue);
    connect(m_purple, &QSlider::valueChanged, this, &DefringePanel::onChanged);
    m_green = addRow(QStringLiteral("Green"), 0, 100, &m_greenValue);
    connect(m_green, &QSlider::valueChanged, this, &DefringePanel::onChanged);
    m_threshold = addRow(QStringLiteral("Threshold"), 0, 100, &m_thresholdValue);
    connect(m_threshold, &QSlider::valueChanged, this, &DefringePanel::onChanged);
}

DefringeNode::Values DefringePanel::currentValues() const
{
    DefringeNode::Values v;
    v.enabled = m_enable->isChecked();
    v.purple = static_cast<float>(m_purple->value());
    v.green = static_cast<float>(m_green->value());
    v.threshold = static_cast<float>(m_threshold->value());
    return v;
}

void DefringePanel::refreshLabels()
{
    const DefringeNode::Values v = currentValues();
    m_purpleValue->setText(QStringLiteral("%1%").arg(static_cast<int>(v.purple)));
    m_greenValue->setText(QStringLiteral("%1%").arg(static_cast<int>(v.green)));
    m_thresholdValue->setText(QStringLiteral("%1%").arg(static_cast<int>(v.threshold)));
    const bool on = v.enabled;
    m_purple->setEnabled(on);
    m_green->setEnabled(on);
    m_threshold->setEnabled(on);
}

void DefringePanel::reveal(const DefringeNode::Values &values)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b1(m_purple);
    const QSignalBlocker b2(m_green);
    const QSignalBlocker b3(m_threshold);
    m_enable->setChecked(values.enabled);
    m_purple->setValue(static_cast<int>(std::lround(values.purple)));
    m_green->setValue(static_cast<int>(std::lround(values.green)));
    m_threshold->setValue(static_cast<int>(std::lround(values.threshold)));
    refreshLabels();

    adjustSize();
    show();
    raise();
    m_enable->setFocus(Qt::ShortcutFocusReason);
}

void DefringePanel::onChanged()
{
    refreshLabels();
    emit valuesChanged(currentValues());
}
