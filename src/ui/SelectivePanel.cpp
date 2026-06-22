#include "ui/SelectivePanel.h"

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
constexpr int kExposureScale = 100;
constexpr int kPanelWidth = 248;
} // namespace

SelectivePanel::SelectivePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("selectivePanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Selective"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    m_maskButton = new QPushButton(QStringLiteral("Mask: Off"), this);
    connect(m_maskButton, &QPushButton::clicked, this, &SelectivePanel::cycleMaskView);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    titleRow->addWidget(m_maskButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(8);
    layout->addLayout(titleRow);

    addSection(QStringLiteral("Mask (luminosity)"));
    m_low = addRow(QStringLiteral("Range low"), 0, 100, &m_lowValue);
    m_high = addRow(QStringLiteral("Range high"), 0, 100, &m_highValue);
    m_feather = addRow(QStringLiteral("Feather"), 0, 100, &m_featherValue);

    addSection(QStringLiteral("Adjust"));
    m_exposure = addRow(QStringLiteral("Exposure"),
                        static_cast<int>(SelectiveNode::kMinExposure * kExposureScale),
                        static_cast<int>(SelectiveNode::kMaxExposure * kExposureScale),
                        &m_exposureValue);
    m_contrast = addRow(QStringLiteral("Contrast"),
                        static_cast<int>(SelectiveNode::kMinAmount),
                        static_cast<int>(SelectiveNode::kMaxAmount), &m_contrastValue);
    m_saturation = addRow(QStringLiteral("Saturation"),
                          static_cast<int>(SelectiveNode::kMinAmount),
                          static_cast<int>(SelectiveNode::kMaxAmount), &m_saturationValue);

    setStyleSheet(QStringLiteral(R"(
        #selectivePanel {
            background: #1c1c1f;
            border: 1px solid #38383d;
            border-radius: 10px;
        }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #section { color: #8a8a90; font-size: 11px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        #rowValue { color: #d6d6d9; font-size: 12px; }
        QPushButton {
            background: #2a2a2e; color: #e8e8ea; border: 1px solid #38383d;
            border-radius: 6px; padding: 2px 8px; font-size: 11px;
        }
        QPushButton:hover { background: #34343a; }
    )"));

    hide();
}

void SelectivePanel::cycleMaskView()
{
    m_maskMode = (m_maskMode + 1) % 3;
    const char *labels[] = {"Mask: Off", "Mask: Red", "Mask: Gray"};
    m_maskButton->setText(QString::fromLatin1(labels[m_maskMode]));
    emit maskViewChanged(m_maskMode);
}

void SelectivePanel::addSection(const QString &name)
{
    auto *label = new QLabel(name, this);
    label->setObjectName(QStringLiteral("section"));
    static_cast<QVBoxLayout *>(layout())->addWidget(label);
}

QSlider *SelectivePanel::addRow(const QString &name, int min, int max, QLabel **valueOut)
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
    connect(slider, &QSlider::valueChanged, this, &SelectivePanel::onSliderChanged);

    auto *l = static_cast<QVBoxLayout *>(layout());
    l->addLayout(header);
    l->addWidget(slider);

    *valueOut = valueLabel;
    return slider;
}

SelectiveValues SelectivePanel::currentValues() const
{
    SelectiveValues v;
    v.low = static_cast<float>(m_low->value()) / 100.0f;
    v.high = static_cast<float>(m_high->value()) / 100.0f;
    v.feather = static_cast<float>(m_feather->value()) / 100.0f;
    v.exposure = static_cast<float>(m_exposure->value()) / kExposureScale;
    v.contrast = static_cast<float>(m_contrast->value());
    v.saturation = static_cast<float>(m_saturation->value());
    return v;
}

void SelectivePanel::refreshLabels()
{
    m_lowValue->setText(QString::number(m_low->value()));
    m_highValue->setText(QString::number(m_high->value()));
    m_featherValue->setText(QString::number(m_feather->value()));
    const double ev = m_exposure->value() / double(kExposureScale);
    m_exposureValue->setText(QStringLiteral("%1%2 EV")
                                 .arg(ev >= 0 ? QStringLiteral("+") : QString())
                                 .arg(ev, 0, 'f', 2));
    const auto signedInt = [](int a) {
        return QStringLiteral("%1%2").arg(a > 0 ? QStringLiteral("+") : QString()).arg(a);
    };
    m_contrastValue->setText(signedInt(m_contrast->value()));
    m_saturationValue->setText(signedInt(m_saturation->value()));
}

void SelectivePanel::reveal(const SelectiveValues &v)
{
    QSlider *sliders[] = {m_low, m_high, m_feather, m_exposure, m_contrast, m_saturation};
    for (QSlider *s : sliders)
        s->blockSignals(true);
    m_low->setValue(static_cast<int>(std::lround(v.low * 100.0f)));
    m_high->setValue(static_cast<int>(std::lround(v.high * 100.0f)));
    m_feather->setValue(static_cast<int>(std::lround(v.feather * 100.0f)));
    m_exposure->setValue(static_cast<int>(std::lround(v.exposure * kExposureScale)));
    m_contrast->setValue(static_cast<int>(std::lround(v.contrast)));
    m_saturation->setValue(static_cast<int>(std::lround(v.saturation)));
    for (QSlider *s : sliders)
        s->blockSignals(false);

    m_maskMode = 0; // reset overlay each time the tool opens
    m_maskButton->setText(QStringLiteral("Mask: Off"));

    refreshLabels();
    adjustSize();
    show();
    raise();
    m_low->setFocus(Qt::ShortcutFocusReason);
}

void SelectivePanel::onSliderChanged()
{
    refreshLabels();
    emit valuesChanged(currentValues());
}

void SelectivePanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void SelectivePanel::mouseMoveEvent(QMouseEvent *event)
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

void SelectivePanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}
