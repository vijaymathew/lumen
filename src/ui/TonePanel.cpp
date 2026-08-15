#include "ui/TonePanel.h"

#include "core/TuneNode.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {
// Exposure works in integer hundredths of an EV stop; contrast/saturation in
// whole slider units.
constexpr int kExposureScale = 100;
constexpr int kPanelWidth = 248;
} // namespace

TonePanel::TonePanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("tonePanel"), QStringLiteral("Tone"), kPanelWidth, parent)
{
    m_exposure = addRow(QStringLiteral("Exposure"),
                        static_cast<int>(TuneNode::kMinExposure * kExposureScale),
                        static_cast<int>(TuneNode::kMaxExposure * kExposureScale),
                        &m_exposureValue);
    m_contrast = addRow(QStringLiteral("Contrast"), static_cast<int>(TuneNode::kMinAmount),
                        static_cast<int>(TuneNode::kMaxAmount), &m_contrastValue);
    m_highlights = addRow(QStringLiteral("Highlights"), static_cast<int>(TuneNode::kMinAmount),
                          static_cast<int>(TuneNode::kMaxAmount), &m_highlightsValue);
    m_shadows = addRow(QStringLiteral("Shadows"), static_cast<int>(TuneNode::kMinAmount),
                       static_cast<int>(TuneNode::kMaxAmount), &m_shadowsValue);
    m_whites = addRow(QStringLiteral("Whites"), static_cast<int>(TuneNode::kMinAmount),
                      static_cast<int>(TuneNode::kMaxAmount), &m_whitesValue);
    m_blacks = addRow(QStringLiteral("Blacks"), static_cast<int>(TuneNode::kMinAmount),
                      static_cast<int>(TuneNode::kMaxAmount), &m_blacksValue);
    m_saturation = addRow(QStringLiteral("Saturation"), static_cast<int>(TuneNode::kMinAmount),
                          static_cast<int>(TuneNode::kMaxAmount), &m_saturationValue);
    m_vibrance = addRow(QStringLiteral("Vibrance"), static_cast<int>(TuneNode::kMinAmount),
                        static_cast<int>(TuneNode::kMaxAmount), &m_vibranceValue);
    m_kelvin = addRow(QStringLiteral("Temperature"), static_cast<int>(TuneNode::kMinKelvin),
                      static_cast<int>(TuneNode::kMaxKelvin), &m_kelvinValue);
    m_tint = addRow(QStringLiteral("Tint"), static_cast<int>(TuneNode::kMinAmount),
                    static_cast<int>(TuneNode::kMaxAmount), &m_tintValue);
    for (QSlider *s : {m_exposure, m_contrast, m_highlights, m_shadows, m_whites, m_blacks,
                       m_saturation, m_vibrance, m_kelvin, m_tint})
        connect(s, &QSlider::valueChanged, this, &TonePanel::onSliderChanged);

    // White-balance helpers: "As shot" resets temperature/tint to the camera's
    // as-shot point; the picker arms the canvas eyedropper for a neutral patch.
    auto *wbButtons = new QHBoxLayout;
    wbButtons->setContentsMargins(0, 0, 0, 0);
    wbButtons->setSpacing(8);
    m_wbAsShot = new QPushButton(QStringLiteral("As shot"), this);
    m_wbPicker = new QPushButton(QStringLiteral("Pick neutral"), this);
    m_wbAsShot->setObjectName(QStringLiteral("wbButton"));
    m_wbPicker->setObjectName(QStringLiteral("wbButton"));
    m_wbAsShot->setCursor(Qt::PointingHandCursor);
    m_wbPicker->setCursor(Qt::PointingHandCursor);
    wbButtons->addWidget(m_wbAsShot);
    wbButtons->addWidget(m_wbPicker);
    contentLayout()->addLayout(wbButtons);
    connect(m_wbAsShot, &QPushButton::clicked, this, &TonePanel::whiteBalanceResetRequested);
    connect(m_wbPicker, &QPushButton::clicked, this, &TonePanel::whiteBalancePickRequested);

    appendStyleSheet(QStringLiteral(R"(
        #wbButton {
            color: #d6d6d9; font-size: 12px;
            background: #2a2a2e; border: 1px solid #3c3c42;
            border-radius: 6px; padding: 5px 8px;
        }
        #wbButton:hover { background: #34343a; }
        #wbButton:pressed { background: #3c3c44; }
    )"));
}

ToneValues TonePanel::currentValues() const
{
    ToneValues v;
    v.exposure = static_cast<float>(m_exposure->value()) / kExposureScale;
    v.contrast = static_cast<float>(m_contrast->value());
    v.highlights = static_cast<float>(m_highlights->value());
    v.shadows = static_cast<float>(m_shadows->value());
    v.whites = static_cast<float>(m_whites->value());
    v.blacks = static_cast<float>(m_blacks->value());
    v.saturation = static_cast<float>(m_saturation->value());
    v.vibrance = static_cast<float>(m_vibrance->value());
    v.kelvin = static_cast<float>(m_kelvin->value());
    v.tint = static_cast<float>(m_tint->value());
    return v;
}

void TonePanel::refreshLabels()
{
    const ToneValues v = currentValues();
    m_exposureValue->setText(QStringLiteral("%1%2 EV")
                                 .arg(v.exposure >= 0 ? QStringLiteral("+") : QString())
                                 .arg(v.exposure, 0, 'f', 2));
    const auto signedInt = [](float a) {
        return QStringLiteral("%1%2").arg(a > 0 ? QStringLiteral("+") : QString())
            .arg(static_cast<int>(a));
    };
    m_contrastValue->setText(signedInt(v.contrast));
    m_highlightsValue->setText(signedInt(v.highlights));
    m_shadowsValue->setText(signedInt(v.shadows));
    m_whitesValue->setText(signedInt(v.whites));
    m_blacksValue->setText(signedInt(v.blacks));
    m_saturationValue->setText(signedInt(v.saturation));
    m_vibranceValue->setText(signedInt(v.vibrance));
    m_kelvinValue->setText(QStringLiteral("%1 K").arg(static_cast<int>(v.kelvin)));
    m_tintValue->setText(signedInt(v.tint));
}

void TonePanel::reveal(const ToneValues &values)
{
    const QSignalBlocker b1(m_exposure);
    const QSignalBlocker b2(m_contrast);
    const QSignalBlocker b3(m_saturation);
    const QSignalBlocker b4(m_kelvin);
    const QSignalBlocker b5(m_tint);
    const QSignalBlocker b6(m_vibrance);
    const QSignalBlocker b7(m_highlights);
    const QSignalBlocker b8(m_shadows);
    const QSignalBlocker b9(m_whites);
    const QSignalBlocker b10(m_blacks);
    m_exposure->setValue(static_cast<int>(std::lround(values.exposure * kExposureScale)));
    m_contrast->setValue(static_cast<int>(std::lround(values.contrast)));
    m_highlights->setValue(static_cast<int>(std::lround(values.highlights)));
    m_shadows->setValue(static_cast<int>(std::lround(values.shadows)));
    m_whites->setValue(static_cast<int>(std::lround(values.whites)));
    m_blacks->setValue(static_cast<int>(std::lround(values.blacks)));
    m_saturation->setValue(static_cast<int>(std::lround(values.saturation)));
    m_vibrance->setValue(static_cast<int>(std::lround(values.vibrance)));
    m_kelvin->setValue(static_cast<int>(std::lround(values.kelvin)));
    m_tint->setValue(static_cast<int>(std::lround(values.tint)));
    refreshLabels();

    adjustSize(); // size to content so nothing is clipped
    show();
    raise();
    m_exposure->setFocus(Qt::ShortcutFocusReason);
}

void TonePanel::onSliderChanged()
{
    refreshLabels();
    emit valuesChanged(currentValues());
}
