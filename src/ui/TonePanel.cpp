#include "ui/TonePanel.h"

#include "core/TuneNode.h"

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
// Exposure works in integer hundredths of an EV stop; contrast/saturation in
// whole slider units.
constexpr int kExposureScale = 100;
constexpr int kPanelWidth = 248;
} // namespace

TonePanel::TonePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("tonePanel"));
    // A bare QWidget needs this to paint its stylesheet background.
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Tone"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(title);

    m_exposure = addRow(QStringLiteral("Exposure"),
                        static_cast<int>(TuneNode::kMinExposure * kExposureScale),
                        static_cast<int>(TuneNode::kMaxExposure * kExposureScale),
                        &m_exposureValue);
    m_contrast = addRow(QStringLiteral("Contrast"),
                        static_cast<int>(TuneNode::kMinAmount),
                        static_cast<int>(TuneNode::kMaxAmount), &m_contrastValue);
    m_saturation = addRow(QStringLiteral("Saturation"),
                          static_cast<int>(TuneNode::kMinAmount),
                          static_cast<int>(TuneNode::kMaxAmount), &m_saturationValue);
    m_vibrance = addRow(QStringLiteral("Vibrance"),
                        static_cast<int>(TuneNode::kMinAmount),
                        static_cast<int>(TuneNode::kMaxAmount), &m_vibranceValue);
    m_kelvin = addRow(QStringLiteral("Temperature"),
                      static_cast<int>(TuneNode::kMinKelvin),
                      static_cast<int>(TuneNode::kMaxKelvin), &m_kelvinValue);
    m_tint = addRow(QStringLiteral("Tint"), static_cast<int>(TuneNode::kMinAmount),
                    static_cast<int>(TuneNode::kMaxAmount), &m_tintValue);

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
    static_cast<QVBoxLayout *>(this->layout())->addLayout(wbButtons);
    connect(m_wbAsShot, &QPushButton::clicked, this,
            &TonePanel::whiteBalanceResetRequested);
    connect(m_wbPicker, &QPushButton::clicked, this,
            &TonePanel::whiteBalancePickRequested);

    setStyleSheet(QStringLiteral(R"(
        #tonePanel {
            background: #1c1c1f;
            border: 1px solid #38383d;
            border-radius: 10px;
        }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        #rowValue { color: #d6d6d9; font-size: 12px; }
        #wbButton {
            color: #d6d6d9; font-size: 12px;
            background: #2a2a2e; border: 1px solid #3c3c42;
            border-radius: 6px; padding: 5px 8px;
        }
        #wbButton:hover { background: #34343a; }
        #wbButton:pressed { background: #3c3c44; }
    )"));

    hide();
}

QSlider *TonePanel::addRow(const QString &name, int min, int max, QLabel **valueOut)
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
    connect(slider, &QSlider::valueChanged, this, &TonePanel::onSliderChanged);

    auto *layout = static_cast<QVBoxLayout *>(this->layout());
    layout->addLayout(header);
    layout->addWidget(slider);

    *valueOut = valueLabel;
    return slider;
}

ToneValues TonePanel::currentValues() const
{
    ToneValues v;
    v.exposure = static_cast<float>(m_exposure->value()) / kExposureScale;
    v.contrast = static_cast<float>(m_contrast->value());
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
    m_exposure->setValue(static_cast<int>(std::lround(values.exposure * kExposureScale)));
    m_contrast->setValue(static_cast<int>(std::lround(values.contrast)));
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

void TonePanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void TonePanel::mouseMoveEvent(QMouseEvent *event)
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

void TonePanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}

bool TonePanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress
        && (watched == m_exposure || watched == m_contrast || watched == m_saturation
            || watched == m_vibrance || watched == m_kelvin || watched == m_tint)) {
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
