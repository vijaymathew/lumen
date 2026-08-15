#include "ui/StructurePanel.h"

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int kPanelWidth = 248;
} // namespace

StructurePanel::StructurePanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("structurePanel"), QStringLiteral("Structure"),
                        kPanelWidth, parent)
{
    m_enable = new QPushButton(QStringLiteral("Structure"), this);
    m_enable->setCheckable(true);
    connect(m_enable, &QPushButton::toggled, this, &StructurePanel::onChanged);
    contentLayout()->addWidget(m_enable);

    m_amount = addRow(QStringLiteral("Amount"), -100, 100, &m_amountValue);
    connect(m_amount, &QSlider::valueChanged, this, &StructurePanel::onChanged);
    m_radius = addRow(QStringLiteral("Radius"), static_cast<int>(StructureNode::kMinRadius),
                      static_cast<int>(StructureNode::kMaxRadius), &m_radiusValue);
    connect(m_radius, &QSlider::valueChanged, this, &StructurePanel::onChanged);
}

StructureNode::Values StructurePanel::currentValues() const
{
    StructureNode::Values v;
    v.enabled = m_enable->isChecked();
    v.amount = static_cast<float>(m_amount->value());
    v.radius = static_cast<float>(m_radius->value());
    return v;
}

void StructurePanel::refreshLabels()
{
    const StructureNode::Values v = currentValues();
    m_amountValue->setText(QStringLiteral("%1").arg(static_cast<int>(v.amount)));
    m_radiusValue->setText(QStringLiteral("%1 px").arg(static_cast<int>(v.radius)));
    const bool on = v.enabled;
    m_amount->setEnabled(on);
    m_radius->setEnabled(on);
}

void StructurePanel::reveal(const StructureNode::Values &values)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b1(m_amount);
    const QSignalBlocker b2(m_radius);
    m_enable->setChecked(values.enabled);
    m_amount->setValue(static_cast<int>(std::lround(values.amount)));
    m_radius->setValue(static_cast<int>(std::lround(values.radius)));
    refreshLabels();

    adjustSize();
    show();
    raise();
    m_enable->setFocus(Qt::ShortcutFocusReason);
}

void StructurePanel::onChanged()
{
    refreshLabels();
    emit valuesChanged(currentValues());
}
