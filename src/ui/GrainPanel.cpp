#include "ui/GrainPanel.h"

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int kPanelWidth = 248;
} // namespace

GrainPanel::GrainPanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("grainPanel"), QStringLiteral("Film Grain"), kPanelWidth,
                        parent)
{
    m_enable = new QPushButton(QStringLiteral("Grain"), this);
    m_enable->setCheckable(true);
    connect(m_enable, &QPushButton::toggled, this, &GrainPanel::onChanged);
    contentLayout()->addWidget(m_enable);

    m_amount = addRow(QStringLiteral("Amount"), 0, 100, &m_amountValue);
    connect(m_amount, &QSlider::valueChanged, this, &GrainPanel::onChanged);
    m_size = addRow(QStringLiteral("Grain size"), static_cast<int>(GrainNode::kMinSize * 10),
                    static_cast<int>(GrainNode::kMaxSize * 10), &m_sizeValue); // size*10
    connect(m_size, &QSlider::valueChanged, this, &GrainPanel::onChanged);
}

GrainNode::Values GrainPanel::currentValues() const
{
    GrainNode::Values v;
    v.enabled = m_enable->isChecked();
    v.amount = static_cast<float>(m_amount->value());
    v.size = static_cast<float>(m_size->value()) / 10.0f;
    return v;
}

void GrainPanel::refreshLabels()
{
    const GrainNode::Values v = currentValues();
    m_amountValue->setText(QStringLiteral("%1%").arg(static_cast<int>(v.amount)));
    m_sizeValue->setText(QStringLiteral("%1 px").arg(v.size, 0, 'f', 1));
    const bool on = v.enabled;
    m_amount->setEnabled(on);
    m_size->setEnabled(on);
}

void GrainPanel::reveal(const GrainNode::Values &values)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b1(m_amount);
    const QSignalBlocker b2(m_size);
    m_enable->setChecked(values.enabled);
    m_amount->setValue(static_cast<int>(std::lround(values.amount)));
    m_size->setValue(static_cast<int>(std::lround(values.size * 10.0f)));
    refreshLabels();

    adjustSize();
    show();
    raise();
    m_enable->setFocus(Qt::ShortcutFocusReason);
}

void GrainPanel::onChanged()
{
    refreshLabels();
    emit valuesChanged(currentValues());
}
