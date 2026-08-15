#include "ui/SharpenPanel.h"

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int kPanelWidth = 248;
} // namespace

SharpenPanel::SharpenPanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("sharpenPanel"), QStringLiteral("Sharpen"), kPanelWidth,
                        parent)
{
    m_enable = new QPushButton(QStringLiteral("Sharpen"), this);
    m_enable->setCheckable(true);
    connect(m_enable, &QPushButton::toggled, this, &SharpenPanel::onChanged);
    contentLayout()->addWidget(m_enable);

    m_amount = addRow(QStringLiteral("Amount"), 0, 100, &m_amountValue);
    connect(m_amount, &QSlider::valueChanged, this, &SharpenPanel::onChanged);
    m_radius = addRow(QStringLiteral("Radius"), 3, 40, &m_radiusValue); // radius*10
    connect(m_radius, &QSlider::valueChanged, this, &SharpenPanel::onChanged);
}

SharpenNode::Values SharpenPanel::currentValues() const
{
    SharpenNode::Values v;
    v.enabled = m_enable->isChecked();
    v.amount = static_cast<float>(m_amount->value());
    v.radius = static_cast<float>(m_radius->value()) / 10.0f;
    return v;
}

void SharpenPanel::refreshLabels()
{
    const SharpenNode::Values v = currentValues();
    m_amountValue->setText(QStringLiteral("%1%").arg(static_cast<int>(v.amount)));
    m_radiusValue->setText(QStringLiteral("%1 px").arg(v.radius, 0, 'f', 1));
    const bool on = v.enabled;
    m_amount->setEnabled(on);
    m_radius->setEnabled(on);
}

void SharpenPanel::reveal(const SharpenNode::Values &values)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b1(m_amount);
    const QSignalBlocker b2(m_radius);
    m_enable->setChecked(values.enabled);
    m_amount->setValue(static_cast<int>(std::lround(values.amount)));
    m_radius->setValue(static_cast<int>(std::lround(values.radius * 10.0f)));
    refreshLabels();

    adjustSize();
    show();
    raise();
    m_enable->setFocus(Qt::ShortcutFocusReason);
}

void SharpenPanel::onChanged()
{
    refreshLabels();
    emit valuesChanged(currentValues());
}
