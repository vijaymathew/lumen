#include "ui/LensPanel.h"

#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int kPanelWidth = 260;
} // namespace

LensPanel::LensPanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("lensPanel"), QStringLiteral("Lens & Perspective"),
                        kPanelWidth, parent)
{
    m_detected = new QLabel(this);
    m_detected->setObjectName(QStringLiteral("detected"));
    m_detected->setWordWrap(true);
    contentLayout()->addWidget(m_detected);

    auto *autoHeader = new QLabel(QStringLiteral("Automatic"), this);
    autoHeader->setObjectName(QStringLiteral("section"));
    contentLayout()->addWidget(autoHeader);

    m_distortion = addToggle(QStringLiteral("Distortion"));
    m_distortionAmount = addRow(QStringLiteral("Amount"), 0, 100, &m_distortionValue);
    m_tca = addToggle(QStringLiteral("Chromatic aberration"));
    m_vignetting = addToggle(QStringLiteral("Vignetting"));
    m_vignettingAmount = addRow(QStringLiteral("Amount"), 0, 100, &m_vignettingValue);

    auto *perspHeader = new QLabel(QStringLiteral("Perspective"), this);
    perspHeader->setObjectName(QStringLiteral("section"));
    contentLayout()->addWidget(perspHeader);

    m_keystoneV = addRow(QStringLiteral("Vertical"), -45, 45, &m_keystoneVValue);
    m_keystoneH = addRow(QStringLiteral("Horizontal"), -45, 45, &m_keystoneHValue);
    m_rotate = addRow(QStringLiteral("Rotate"), -45, 45, &m_rotateValue);
    m_scale = addRow(QStringLiteral("Zoom"), 25, 400, &m_scaleValue); // /100

    for (QSlider *s : {m_distortionAmount, m_vignettingAmount, m_keystoneV, m_keystoneH, m_rotate,
                       m_scale})
        connect(s, &QSlider::valueChanged, this, &LensPanel::onChanged);

    appendStyleSheet(QStringLiteral(R"(
        #section { color: #8a8a90; font-size: 11px; text-transform: uppercase; }
        #detected { color: #b4b4b8; font-size: 12px; }
        QPushButton { text-align: left; }
        QPushButton:disabled { color: #6a6a70; }
    )"));
}

QPushButton *LensPanel::addToggle(const QString &text)
{
    auto *btn = new QPushButton(text, this);
    btn->setCheckable(true);
    connect(btn, &QPushButton::toggled, this, &LensPanel::onChanged);
    contentLayout()->addWidget(btn);
    return btn;
}

LensCorrectionNode::Params LensPanel::currentParams() const
{
    LensCorrectionNode::Params p = m_base; // keep the identity
    p.distortion = m_distortion->isChecked();
    p.distortionAmount = static_cast<float>(m_distortionAmount->value()) / 100.0f;
    p.tca = m_tca->isChecked();
    p.vignetting = m_vignetting->isChecked();
    p.vignettingAmount = static_cast<float>(m_vignettingAmount->value()) / 100.0f;
    p.keystoneV = static_cast<float>(m_keystoneV->value());
    p.keystoneH = static_cast<float>(m_keystoneH->value());
    p.rotate = static_cast<float>(m_rotate->value());
    p.scale = static_cast<float>(m_scale->value()) / 100.0f;
    return p;
}

void LensPanel::refreshLabels()
{
    m_distortionValue->setText(QStringLiteral("%1%").arg(m_distortionAmount->value()));
    m_vignettingValue->setText(QStringLiteral("%1%").arg(m_vignettingAmount->value()));
    m_keystoneVValue->setText(QStringLiteral("%1°").arg(m_keystoneV->value()));
    m_keystoneHValue->setText(QStringLiteral("%1°").arg(m_keystoneH->value()));
    m_rotateValue->setText(QStringLiteral("%1°").arg(m_rotate->value()));
    m_scaleValue->setText(QStringLiteral("%1%").arg(m_scale->value()));

    // Automatic controls only mean something with a matched profile.
    for (QPushButton *w : {m_distortion, m_tca, m_vignetting})
        w->setEnabled(m_matched);
    m_distortionAmount->setEnabled(m_matched && m_distortion->isChecked());
    m_vignettingAmount->setEnabled(m_matched && m_vignetting->isChecked());
}

void LensPanel::reveal(const LensCorrectionNode::Params &params, bool matched,
                       const QString &detected)
{
    m_base = params;
    m_matched = matched;
    m_detected->setText(matched
                            ? QStringLiteral("Lens: %1").arg(detected)
                            : QStringLiteral("No matching lens profile — perspective only"));

    QWidget *all[] = {m_distortion,    m_distortionAmount, m_tca,
                      m_vignetting,     m_vignettingAmount, m_keystoneV,
                      m_keystoneH,      m_rotate,           m_scale};
    for (QWidget *w : all)
        w->blockSignals(true);
    m_distortion->setChecked(params.distortion);
    m_distortionAmount->setValue(static_cast<int>(std::lround(params.distortionAmount * 100)));
    m_tca->setChecked(params.tca);
    m_vignetting->setChecked(params.vignetting);
    m_vignettingAmount->setValue(static_cast<int>(std::lround(params.vignettingAmount * 100)));
    m_keystoneV->setValue(static_cast<int>(std::lround(params.keystoneV)));
    m_keystoneH->setValue(static_cast<int>(std::lround(params.keystoneH)));
    m_rotate->setValue(static_cast<int>(std::lround(params.rotate)));
    m_scale->setValue(static_cast<int>(std::lround(params.scale * 100)));
    for (QWidget *w : all)
        w->blockSignals(false);
    refreshLabels();

    adjustSize();
    show();
    raise();
    setFocus(Qt::ShortcutFocusReason);
}

void LensPanel::onChanged()
{
    refreshLabels();
    emit paramsChanged(currentParams());
}
