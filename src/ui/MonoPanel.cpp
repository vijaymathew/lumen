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

    m_toneStrength = addRow(QStringLiteral("Tone"), 0, 100, &m_toneStrengthValue);
    m_toneHue = addRow(QStringLiteral("Hue"), 0, 359, &m_toneHueValue);

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
    v.toneStrength = static_cast<float>(m_toneStrength->value()) / 100.0f;
    v.toneHue = static_cast<float>(m_toneHue->value());
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
    m_toneStrengthValue->setText(pct(v.toneStrength));
    m_toneHueValue->setText(QStringLiteral("%1°").arg(static_cast<int>(v.toneHue)));

    // Mixer/toning rows are meaningful only when conversion is on.
    const bool on = v.enabled;
    for (QSlider *s : m_band)
        s->setEnabled(on);
    m_toneStrength->setEnabled(on);
    m_toneHue->setEnabled(on);
}

void MonoPanel::reveal(const MonoValues &values)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b4(m_toneStrength);
    const QSignalBlocker b5(m_toneHue);
    m_enable->setChecked(values.enabled);
    for (int i = 0; i < 8; ++i) {
        const QSignalBlocker b(m_band[i]);
        m_band[i]->setValue(static_cast<int>(std::lround(values.band[i] * 100)));
    }
    m_toneStrength->setValue(static_cast<int>(std::lround(values.toneStrength * 100)));
    m_toneHue->setValue(static_cast<int>(std::lround(values.toneHue)));
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
