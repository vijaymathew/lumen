#include "ui/VignettePanel.h"

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int kPanelWidth = 248;
} // namespace

VignettePanel::VignettePanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("vignettePanel"), QStringLiteral("Vignette"), kPanelWidth,
                        parent)
{
    m_enable = new QPushButton(QStringLiteral("Vignette"), this);
    m_enable->setCheckable(true);
    connect(m_enable, &QPushButton::toggled, this, &VignettePanel::onChanged);
    contentLayout()->addWidget(m_enable);

    m_amount = addRow(QStringLiteral("Amount"), -100, 100, &m_amountValue);
    connect(m_amount, &QSlider::valueChanged, this, &VignettePanel::onChanged);
    m_midpoint = addRow(QStringLiteral("Midpoint"), 0, 100, &m_midpointValue);
    connect(m_midpoint, &QSlider::valueChanged, this, &VignettePanel::onChanged);
    m_roundness = addRow(QStringLiteral("Roundness"), -100, 100, &m_roundnessValue);
    connect(m_roundness, &QSlider::valueChanged, this, &VignettePanel::onChanged);
    m_feather = addRow(QStringLiteral("Feather"), 0, 100, &m_featherValue);
    connect(m_feather, &QSlider::valueChanged, this, &VignettePanel::onChanged);
}

VignetteParams VignettePanel::currentValues() const
{
    VignetteParams v;
    v.enabled = m_enable->isChecked();
    v.amount = static_cast<float>(m_amount->value());
    v.midpoint = static_cast<float>(m_midpoint->value());
    v.roundness = static_cast<float>(m_roundness->value());
    v.feather = static_cast<float>(m_feather->value());
    return v;
}

void VignettePanel::refreshLabels()
{
    const VignetteParams v = currentValues();
    m_amountValue->setText(QStringLiteral("%1").arg(static_cast<int>(v.amount)));
    m_midpointValue->setText(QStringLiteral("%1").arg(static_cast<int>(v.midpoint)));
    m_roundnessValue->setText(QStringLiteral("%1").arg(static_cast<int>(v.roundness)));
    m_featherValue->setText(QStringLiteral("%1").arg(static_cast<int>(v.feather)));
    const bool on = v.enabled;
    m_amount->setEnabled(on);
    m_midpoint->setEnabled(on);
    m_roundness->setEnabled(on);
    m_feather->setEnabled(on);
}

void VignettePanel::reveal(const VignetteParams &values)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b1(m_amount);
    const QSignalBlocker b2(m_midpoint);
    const QSignalBlocker b3(m_roundness);
    const QSignalBlocker b4(m_feather);
    m_enable->setChecked(values.enabled);
    m_amount->setValue(static_cast<int>(std::lround(values.amount)));
    m_midpoint->setValue(static_cast<int>(std::lround(values.midpoint)));
    m_roundness->setValue(static_cast<int>(std::lround(values.roundness)));
    m_feather->setValue(static_cast<int>(std::lround(values.feather)));
    refreshLabels();

    adjustSize();
    show();
    raise();
    m_enable->setFocus(Qt::ShortcutFocusReason);
}

void VignettePanel::onChanged()
{
    refreshLabels();
    emit valuesChanged(currentValues());
}
