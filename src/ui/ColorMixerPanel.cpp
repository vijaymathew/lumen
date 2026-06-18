#include "ui/ColorMixerPanel.h"

#include <QColor>
#include <QGridLayout>
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
    : QWidget(parent)
{
    setObjectName(QStringLiteral("colorMixerPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Color Mixer (HSL)"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(title);

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
    layout->addLayout(swatches);

    m_bandLabel = new QLabel(this);
    m_bandLabel->setObjectName(QStringLiteral("rowName"));
    layout->addWidget(m_bandLabel);

    m_hue = addRow(QStringLiteral("Hue"), static_cast<int>(ColorMixerNode::kMinAmount),
                   static_cast<int>(ColorMixerNode::kMaxAmount), &m_hueValue);
    m_sat = addRow(QStringLiteral("Saturation"), static_cast<int>(ColorMixerNode::kMinAmount),
                   static_cast<int>(ColorMixerNode::kMaxAmount), &m_satValue);
    m_lum = addRow(QStringLiteral("Luminance"), static_cast<int>(ColorMixerNode::kMinAmount),
                   static_cast<int>(ColorMixerNode::kMaxAmount), &m_lumValue);

    setStyleSheet(QStringLiteral(R"(
        #colorMixerPanel {
            background: #1c1c1f;
            border: 1px solid #38383d;
            border-radius: 10px;
        }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        #rowValue { color: #d6d6d9; font-size: 12px; }
    )"));

    hide();
}

QSlider *ColorMixerPanel::addRow(const QString &name, int min, int max, QLabel **valueOut)
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
    connect(slider, &QSlider::valueChanged, this, &ColorMixerPanel::onSliderChanged);

    auto *layout = static_cast<QVBoxLayout *>(this->layout());
    layout->addLayout(header);
    layout->addWidget(slider);

    *valueOut = valueLabel;
    return slider;
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

void ColorMixerPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void ColorMixerPanel::mouseMoveEvent(QMouseEvent *event)
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

void ColorMixerPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}

bool ColorMixerPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress
        && (watched == m_hue || watched == m_sat || watched == m_lum)) {
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
