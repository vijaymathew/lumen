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

    m_mixR = addRow(QStringLiteral("Red"), -50, 200, &m_mixRValue);
    m_mixG = addRow(QStringLiteral("Green"), -50, 200, &m_mixGValue);
    m_mixB = addRow(QStringLiteral("Blue"), -50, 200, &m_mixBValue);
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
    MonoValues v;
    v.enabled = m_enable->isChecked();
    v.mixR = static_cast<float>(m_mixR->value()) / 100.0f;
    v.mixG = static_cast<float>(m_mixG->value()) / 100.0f;
    v.mixB = static_cast<float>(m_mixB->value()) / 100.0f;
    v.toneStrength = static_cast<float>(m_toneStrength->value()) / 100.0f;
    v.toneHue = static_cast<float>(m_toneHue->value());
    return v;
}

void MonoPanel::refreshLabels()
{
    const MonoValues v = currentValues();
    const auto pct = [](float a) { return QStringLiteral("%1%").arg(std::lround(a * 100)); };
    m_mixRValue->setText(pct(v.mixR));
    m_mixGValue->setText(pct(v.mixG));
    m_mixBValue->setText(pct(v.mixB));
    m_toneStrengthValue->setText(pct(v.toneStrength));
    m_toneHueValue->setText(QStringLiteral("%1°").arg(static_cast<int>(v.toneHue)));

    // Mixer/toning rows are meaningful only when conversion is on.
    const bool on = v.enabled;
    for (QSlider *s : {m_mixR, m_mixG, m_mixB, m_toneStrength, m_toneHue})
        s->setEnabled(on);
}

void MonoPanel::reveal(const MonoValues &values)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b1(m_mixR);
    const QSignalBlocker b2(m_mixG);
    const QSignalBlocker b3(m_mixB);
    const QSignalBlocker b4(m_toneStrength);
    const QSignalBlocker b5(m_toneHue);
    m_enable->setChecked(values.enabled);
    m_mixR->setValue(static_cast<int>(std::lround(values.mixR * 100)));
    m_mixG->setValue(static_cast<int>(std::lround(values.mixG * 100)));
    m_mixB->setValue(static_cast<int>(std::lround(values.mixB * 100)));
    m_toneStrength->setValue(static_cast<int>(std::lround(values.toneStrength * 100)));
    m_toneHue->setValue(static_cast<int>(std::lround(values.toneHue)));
    refreshLabels();

    adjustSize();
    show();
    raise();
    m_enable->setFocus(Qt::ShortcutFocusReason);
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
