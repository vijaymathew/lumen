#include "ui/ColorMixerPanel.h"

#include <QColor>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kPanelWidth = 248;

// The 8 bands: display name + centre hue (for the swatch colour). Centres match
// ColorMixerNode::bandInterp / texture.frag.
struct Band {
    const char *name;
    int hue;
};
const Band kBands[8] = {
    {"Red", 0},    {"Orange", 30}, {"Yellow", 60},  {"Green", 120},
    {"Aqua", 180}, {"Blue", 240},  {"Purple", 270}, {"Magenta", 330},
};
} // namespace

ColorMixerPanel::ColorMixerPanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("colorMixerPanel"), QStringLiteral("Color Mixer (HSL)"),
                        kPanelWidth, parent)
{
    // Eight colour swatches, two rows of four; picking one loads its sliders.
    auto *swatches = new QGridLayout;
    swatches->setContentsMargins(0, 0, 0, 0);
    swatches->setHorizontalSpacing(4);
    swatches->setVerticalSpacing(4);
    for (int i = 0; i < 8; ++i) {
        auto *b = new QPushButton(this);
        b->setObjectName(QStringLiteral("swatch"));
        b->setCheckable(true);
        b->setFixedHeight(24);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(QString::fromLatin1(kBands[i].name));
        const QColor col = QColor::fromHsv(kBands[i].hue, 235, 235);
        // Per-swatch background; the shared stylesheet paints the selected border.
        b->setStyleSheet(
            QStringLiteral("QPushButton#swatch { background: %1; border: 2px solid #38383d;"
                           " border-radius: 6px; }"
                           "QPushButton#swatch:checked { border: 2px solid #ffffff; }")
                .arg(col.name()));
        connect(b, &QPushButton::clicked, this, [this, i] { selectBand(i); });
        m_bandBtn[i] = b;
        swatches->addWidget(b, i / 4, i % 4);
    }
    contentLayout()->addLayout(swatches);

    m_bandLabel = new QLabel(this);
    m_bandLabel->setObjectName(QStringLiteral("rowName"));
    contentLayout()->addWidget(m_bandLabel);

    m_hue = addRow(QStringLiteral("Hue"), static_cast<int>(ColorMixerNode::kMinAmount),
                   static_cast<int>(ColorMixerNode::kMaxAmount), &m_hueValue);
    m_sat = addRow(QStringLiteral("Saturation"), static_cast<int>(ColorMixerNode::kMinAmount),
                   static_cast<int>(ColorMixerNode::kMaxAmount), &m_satValue);
    m_lum = addRow(QStringLiteral("Luminance"), static_cast<int>(ColorMixerNode::kMinAmount),
                   static_cast<int>(ColorMixerNode::kMaxAmount), &m_lumValue);
    for (QSlider *s : {m_hue, m_sat, m_lum})
        connect(s, &QSlider::valueChanged, this, &ColorMixerPanel::onSliderChanged);
}

void ColorMixerPanel::selectBand(int index)
{
    m_selected = std::clamp(index, 0, 7);
    for (int i = 0; i < 8; ++i) {
        const QSignalBlocker b(m_bandBtn[i]);
        m_bandBtn[i]->setChecked(i == m_selected);
    }
    const QSignalBlocker bh(m_hue);
    const QSignalBlocker bs(m_sat);
    const QSignalBlocker bl(m_lum);
    m_hue->setValue(static_cast<int>(std::lround(m_values.hue[m_selected])));
    m_sat->setValue(static_cast<int>(std::lround(m_values.sat[m_selected])));
    m_lum->setValue(static_cast<int>(std::lround(m_values.lum[m_selected])));
    refreshLabels();
}

void ColorMixerPanel::refreshLabels()
{
    m_bandLabel->setText(QStringLiteral("Editing: %1").arg(QString::fromLatin1(kBands[m_selected].name)));
    const auto signedInt = [](int a) {
        return QStringLiteral("%1%2").arg(a > 0 ? QStringLiteral("+") : QString()).arg(a);
    };
    m_hueValue->setText(signedInt(m_hue->value()));
    m_satValue->setText(signedInt(m_sat->value()));
    m_lumValue->setText(signedInt(m_lum->value()));
}

void ColorMixerPanel::reveal(const ColorMixerValues &values)
{
    m_values = values;
    selectBand(m_selected); // loads the sliders for the active band

    adjustSize(); // size to content so nothing is clipped
    show();
    raise();
    m_hue->setFocus(Qt::ShortcutFocusReason);
}

void ColorMixerPanel::onSliderChanged()
{
    m_values.hue[m_selected] = static_cast<float>(m_hue->value());
    m_values.sat[m_selected] = static_cast<float>(m_sat->value());
    m_values.lum[m_selected] = static_cast<float>(m_lum->value());
    refreshLabels();
    emit valuesChanged(m_values);
}
