#include "ui/DenoisePanel.h"

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int kPanelWidth = 248;
} // namespace

DenoisePanel::DenoisePanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("denoisePanel"), QStringLiteral("Denoise"), kPanelWidth,
                        parent)
{
    m_enable = new QPushButton(QStringLiteral("Reduce noise"), this);
    m_enable->setCheckable(true);
    connect(m_enable, &QPushButton::toggled, this, &DenoisePanel::onChanged);
    contentLayout()->addWidget(m_enable);

    m_luma = addRow(QStringLiteral("Luminance"), 0, 100, &m_lumaValue);
    connect(m_luma, &QSlider::valueChanged, this, &DenoisePanel::onChanged);
    m_chroma = addRow(QStringLiteral("Color"), 0, 100, &m_chromaValue);
    connect(m_chroma, &QSlider::valueChanged, this, &DenoisePanel::onChanged);
}

DenoiseNode::Values DenoisePanel::currentValues() const
{
    DenoiseNode::Values v;
    v.enabled = m_enable->isChecked();
    v.luma = static_cast<float>(m_luma->value());
    v.chroma = static_cast<float>(m_chroma->value());
    return v;
}

void DenoisePanel::refreshLabels()
{
    const DenoiseNode::Values v = currentValues();
    m_lumaValue->setText(QStringLiteral("%1%").arg(static_cast<int>(v.luma)));
    m_chromaValue->setText(QStringLiteral("%1%").arg(static_cast<int>(v.chroma)));
    const bool on = v.enabled;
    m_luma->setEnabled(on);
    m_chroma->setEnabled(on);
}

void DenoisePanel::reveal(const DenoiseNode::Values &values)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b1(m_luma);
    const QSignalBlocker b2(m_chroma);
    m_enable->setChecked(values.enabled);
    m_luma->setValue(static_cast<int>(std::lround(values.luma)));
    m_chroma->setValue(static_cast<int>(std::lround(values.chroma)));
    refreshLabels();

    adjustSize();
    show();
    raise();
    m_enable->setFocus(Qt::ShortcutFocusReason);
}

void DenoisePanel::onChanged()
{
    refreshLabels();
    emit valuesChanged(currentValues());
}
