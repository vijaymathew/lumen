#include "ui/LensPanel.h"

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
constexpr int kPanelWidth = 260;
} // namespace

LensPanel::LensPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("lensPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Lens & Perspective"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(title);

    m_detected = new QLabel(this);
    m_detected->setObjectName(QStringLiteral("detected"));
    m_detected->setWordWrap(true);
    layout->addWidget(m_detected);

    auto *autoHeader = new QLabel(QStringLiteral("Automatic"), this);
    autoHeader->setObjectName(QStringLiteral("section"));
    layout->addWidget(autoHeader);

    m_distortion = addToggle(QStringLiteral("Distortion"));
    m_distortionAmount = addRow(QStringLiteral("Amount"), 0, 100, &m_distortionValue);
    m_tca = addToggle(QStringLiteral("Chromatic aberration"));
    m_vignetting = addToggle(QStringLiteral("Vignetting"));
    m_vignettingAmount = addRow(QStringLiteral("Amount"), 0, 100, &m_vignettingValue);

    auto *perspHeader = new QLabel(QStringLiteral("Perspective"), this);
    perspHeader->setObjectName(QStringLiteral("section"));
    layout->addWidget(perspHeader);

    m_keystoneV = addRow(QStringLiteral("Vertical"), -45, 45, &m_keystoneVValue);
    m_keystoneH = addRow(QStringLiteral("Horizontal"), -45, 45, &m_keystoneHValue);
    m_rotate = addRow(QStringLiteral("Rotate"), -45, 45, &m_rotateValue);
    m_scale = addRow(QStringLiteral("Zoom"), 25, 400, &m_scaleValue); // /100

    setStyleSheet(QStringLiteral(R"(
        #lensPanel {
            background: #1c1c1f;
            border: 1px solid #38383d;
            border-radius: 10px;
        }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #section { color: #8a8a90; font-size: 11px; text-transform: uppercase; }
        #detected { color: #b4b4b8; font-size: 12px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        #rowValue { color: #d6d6d9; font-size: 12px; }
        QPushButton {
            background: #2a2a2e; color: #e8e8ea; border: 1px solid #38383d;
            border-radius: 6px; padding: 4px 8px; font-size: 12px; text-align: left;
        }
        QPushButton:hover { background: #34343a; }
        QPushButton:checked { background: #3a3550; border-color: #7F77DD; }
        QPushButton:disabled { color: #6a6a70; }
    )"));

    hide();
}

QPushButton *LensPanel::addToggle(const QString &text)
{
    auto *btn = new QPushButton(text, this);
    btn->setCheckable(true);
    connect(btn, &QPushButton::toggled, this, &LensPanel::onChanged);
    static_cast<QVBoxLayout *>(layout())->addWidget(btn);
    return btn;
}

QSlider *LensPanel::addRow(const QString &name, int min, int max, QLabel **valueOut)
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
    connect(slider, &QSlider::valueChanged, this, &LensPanel::onChanged);

    auto *layout = static_cast<QVBoxLayout *>(this->layout());
    layout->addLayout(header);
    layout->addWidget(slider);

    *valueOut = valueLabel;
    return slider;
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

void LensPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void LensPanel::mouseMoveEvent(QMouseEvent *event)
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

void LensPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}

bool LensPanel::eventFilter(QObject *watched, QEvent *event)
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
