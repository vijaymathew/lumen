#include "ui/MonoPanel.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
// Mix weights are shown as percentages (weight × 100); toning strength 0-100%.
constexpr int kPanelWidth = 248;
} // namespace

MonoPanel::MonoPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("monoPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Monochrome"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(title);

    m_enable = new QPushButton(QStringLiteral("Convert to B&W"), this);
    m_enable->setCheckable(true);
    connect(m_enable, &QPushButton::toggled, this, &MonoPanel::onChanged);
    layout->addWidget(m_enable);

    // Filter presets: classic B&W "colour filter" looks, expressed as 8-band
    // color-mix sets (Red/Orange/Yellow/Green/Blue darken complementary colours).
    // {R, O, Y, G, Aqua, Blue, Purple, Magenta} in [-1,1].
    struct Preset { const char *label; float bands[8]; };
    static const Preset kPresets[] = {
        {"Neutral", {0, 0, 0, 0, 0, 0, 0, 0}},
        {"Red", {0.6f, 0.4f, 0.2f, -0.3f, -0.5f, -0.6f, -0.2f, 0.3f}},
        {"Orange", {0.4f, 0.6f, 0.4f, -0.2f, -0.4f, -0.5f, -0.3f, 0.1f}},
        {"Yellow", {0.2f, 0.4f, 0.6f, 0.2f, -0.3f, -0.5f, -0.4f, -0.2f}},
        {"Green", {-0.4f, -0.1f, 0.3f, 0.6f, 0.3f, -0.2f, -0.4f, -0.4f}},
        {"Blue", {-0.5f, -0.5f, -0.3f, -0.1f, 0.4f, 0.6f, 0.3f, -0.1f}},
    };
    auto *presetLabel = new QLabel(QStringLiteral("Presets"), this);
    presetLabel->setObjectName(QStringLiteral("rowName"));
    layout->addWidget(presetLabel);
    auto *presetRow1 = new QHBoxLayout;
    auto *presetRow2 = new QHBoxLayout;
    presetRow1->setContentsMargins(0, 0, 0, 0);
    presetRow2->setContentsMargins(0, 0, 0, 0);
    presetRow1->setSpacing(4);
    presetRow2->setSpacing(4);
    int pi = 0;
    for (const Preset &p : kPresets) {
        auto *b = new QPushButton(QString::fromLatin1(p.label), this);
        b->setObjectName(QStringLiteral("presetButton"));
        connect(b, &QPushButton::clicked, this, [this, bands = p.bands] { applyPreset(bands); });
        (pi++ < 3 ? presetRow1 : presetRow2)->addWidget(b);
    }
    layout->addLayout(presetRow1);
    layout->addLayout(presetRow2);

    // 8-color mixer: how each colour renders as a tone.
    static const char *kBandNames[8] = {"Red",  "Orange", "Yellow", "Green",
                                         "Aqua", "Blue",   "Purple", "Magenta"};
    for (int i = 0; i < 8; ++i)
        m_band[i] = addRow(QString::fromLatin1(kBandNames[i]), -100, 100, &m_bandValue[i]);

    // Split-toning presets: {shadowHue, shadowSat, highHue, highSat, balance}.
    struct TonePreset { const char *label; float shHue, shSat, hiHue, hiSat, bal; };
    static const TonePreset kTonePresets[] = {
        {"None", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
        {"Sepia", 30.0f, 0.50f, 45.0f, 0.35f, 0.0f},
        {"Selenium", 280.0f, 0.40f, 40.0f, 0.15f, 0.0f},
        {"Cyanotype", 210.0f, 0.70f, 200.0f, 0.45f, -0.1f},
        {"Split", 215.0f, 0.40f, 40.0f, 0.35f, 0.0f}, // cool shadows / warm highlights
    };
    auto *toningLabel = new QLabel(QStringLiteral("Toning"), this);
    toningLabel->setObjectName(QStringLiteral("rowName"));
    layout->addWidget(toningLabel);
    auto *toneRow1 = new QHBoxLayout;
    auto *toneRow2 = new QHBoxLayout;
    toneRow1->setContentsMargins(0, 0, 0, 0);
    toneRow2->setContentsMargins(0, 0, 0, 0);
    toneRow1->setSpacing(4);
    toneRow2->setSpacing(4);
    int ti = 0;
    for (const TonePreset &p : kTonePresets) {
        auto *b = new QPushButton(QString::fromLatin1(p.label), this);
        b->setObjectName(QStringLiteral("presetButton"));
        connect(b, &QPushButton::clicked, this,
                [this, p] { applyTonePreset(p.shHue, p.shSat, p.hiHue, p.hiSat, p.bal); });
        (ti++ < 3 ? toneRow1 : toneRow2)->addWidget(b);
    }
    layout->addLayout(toneRow1);
    layout->addLayout(toneRow2);

    m_shadowHue = addRow(QStringLiteral("Shadow hue"), 0, 359, &m_shadowHueValue);
    m_shadowSat = addRow(QStringLiteral("Shadow sat"), 0, 100, &m_shadowSatValue);
    m_highHue = addRow(QStringLiteral("Highlight hue"), 0, 359, &m_highHueValue);
    m_highSat = addRow(QStringLiteral("Highlight sat"), 0, 100, &m_highSatValue);
    m_balance = addRow(QStringLiteral("Balance"), -100, 100, &m_balanceValue);

    setStyleSheet(QStringLiteral(R"(
        #monoPanel {
            background: #1c1c1f;
            border: 1px solid #38383d;
            border-radius: 10px;
        }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        #rowValue { color: #d6d6d9; font-size: 12px; }
        QPushButton {
            background: #2a2a2e; color: #e8e8ea; border: 1px solid #38383d;
            border-radius: 6px; padding: 4px 8px; font-size: 12px;
        }
        QPushButton:hover { background: #34343a; }
        QPushButton:checked { background: #3a3550; border-color: #7F77DD; }
    )"));

    hide();
}

QSlider *MonoPanel::addRow(const QString &name, int min, int max, QLabel **valueOut)
{
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    auto *nameLabel = new QLabel(name, this);
    nameLabel->setObjectName(QStringLiteral("rowName"));
    auto *valueLabel = new QLabel(this);
    valueLabel->setObjectName(QStringLiteral("rowValue"));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(nameLabel);
    header->addStretch(1);
    header->addWidget(valueLabel);

    auto *slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(min, max);
    slider->installEventFilter(this);
    connect(slider, &QSlider::valueChanged, this, &MonoPanel::onChanged);

    auto *layout = static_cast<QVBoxLayout *>(this->layout());
    layout->addLayout(header);
    layout->addWidget(slider);

    *valueOut = valueLabel;
    return slider;
}

MonoValues MonoPanel::currentValues() const
{
    MonoValues v; // mixR/G/B left at the luma defaults (base grey)
    v.enabled = m_enable->isChecked();
    for (int i = 0; i < 8; ++i)
        v.band[i] = static_cast<float>(m_band[i]->value()) / 100.0f;
    v.shadowHue = static_cast<float>(m_shadowHue->value());
    v.shadowSat = static_cast<float>(m_shadowSat->value()) / 100.0f;
    v.highHue = static_cast<float>(m_highHue->value());
    v.highSat = static_cast<float>(m_highSat->value()) / 100.0f;
    v.balance = static_cast<float>(m_balance->value()) / 100.0f;
    return v;
}

void MonoPanel::refreshLabels()
{
    const MonoValues v = currentValues();
    const auto pct = [](float a) { return QStringLiteral("%1%").arg(std::lround(a * 100)); };
    const auto signedPct = [](float a) {
        const int p = static_cast<int>(std::lround(a * 100));
        return QStringLiteral("%1%2%").arg(p > 0 ? QStringLiteral("+") : QString()).arg(p);
    };
    for (int i = 0; i < 8; ++i)
        m_bandValue[i]->setText(signedPct(v.band[i]));
    const auto deg = [](float a) { return QStringLiteral("%1°").arg(static_cast<int>(a)); };
    const auto signedInt = [](float a) {
        const int p = static_cast<int>(std::lround(a * 100));
        return QStringLiteral("%1%2").arg(p > 0 ? QStringLiteral("+") : QString()).arg(p);
    };
    m_shadowHueValue->setText(deg(v.shadowHue));
    m_shadowSatValue->setText(pct(v.shadowSat));
    m_highHueValue->setText(deg(v.highHue));
    m_highSatValue->setText(pct(v.highSat));
    m_balanceValue->setText(signedInt(v.balance));

    // Mixer/toning rows are meaningful only when conversion is on.
    const bool on = v.enabled;
    for (QSlider *s : m_band)
        s->setEnabled(on);
    for (QSlider *s : {m_shadowHue, m_shadowSat, m_highHue, m_highSat, m_balance})
        s->setEnabled(on);
}

void MonoPanel::reveal(const MonoValues &values)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b1(m_shadowHue);
    const QSignalBlocker b2(m_shadowSat);
    const QSignalBlocker b3(m_highHue);
    const QSignalBlocker b4(m_highSat);
    const QSignalBlocker b5(m_balance);
    m_enable->setChecked(values.enabled);
    for (int i = 0; i < 8; ++i) {
        const QSignalBlocker b(m_band[i]);
        m_band[i]->setValue(static_cast<int>(std::lround(values.band[i] * 100)));
    }
    m_shadowHue->setValue(static_cast<int>(std::lround(values.shadowHue)));
    m_shadowSat->setValue(static_cast<int>(std::lround(values.shadowSat * 100)));
    m_highHue->setValue(static_cast<int>(std::lround(values.highHue)));
    m_highSat->setValue(static_cast<int>(std::lround(values.highSat * 100)));
    m_balance->setValue(static_cast<int>(std::lround(values.balance * 100)));
    refreshLabels();

    adjustSize();
    show();
    raise();
    m_enable->setFocus(Qt::ShortcutFocusReason);
}

void MonoPanel::applyPreset(const float bands[8])
{
    for (int i = 0; i < 8; ++i) {
        const QSignalBlocker b(m_band[i]);
        m_band[i]->setValue(static_cast<int>(std::lround(bands[i] * 100)));
    }
    onChanged(); // refresh labels + emit the new values
}

void MonoPanel::applyTonePreset(float shHue, float shSat, float hiHue, float hiSat, float balance)
{
    {
        const QSignalBlocker b1(m_shadowHue);
        const QSignalBlocker b2(m_shadowSat);
        const QSignalBlocker b3(m_highHue);
        const QSignalBlocker b4(m_highSat);
        const QSignalBlocker b5(m_balance);
        m_shadowHue->setValue(static_cast<int>(std::lround(shHue)));
        m_shadowSat->setValue(static_cast<int>(std::lround(shSat * 100)));
        m_highHue->setValue(static_cast<int>(std::lround(hiHue)));
        m_highSat->setValue(static_cast<int>(std::lround(hiSat * 100)));
        m_balance->setValue(static_cast<int>(std::lround(balance * 100)));
    }
    onChanged(); // refresh labels + emit the new values
}

void MonoPanel::onChanged()
{
    refreshLabels();
    emit valuesChanged(currentValues());
}

void MonoPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void MonoPanel::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || !parentWidget())
        return;
    const QPoint cursorInParent =
        parentWidget()->mapFromGlobal(event->globalPosition().toPoint());
    QPoint topLeft = cursorInParent - m_dragOffset;
    const QRect bounds = parentWidget()->rect();
    topLeft.setX(std::clamp(topLeft.x(), 0, bounds.width() - width()));
    topLeft.setY(std::clamp(topLeft.y(), 0, bounds.height() - height()));
    move(topLeft);
}

void MonoPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}

bool MonoPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        switch (ke->key()) {
        case Qt::Key_Escape:
        case Qt::Key_Return:
        case Qt::Key_Enter:
            emit closed();
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
