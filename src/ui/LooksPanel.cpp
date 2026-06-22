#include "ui/LooksPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kPanelWidth = 280;
}

LooksPanel::LooksPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("looksPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Looks"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    m_name = new QLabel(this);
    m_name->setObjectName(QStringLiteral("lookName"));
    m_name->setWordWrap(false);

    auto *load = new QPushButton(QStringLiteral("Load…"), this);
    auto *clear = new QPushButton(QStringLiteral("Clear"), this);
    connect(load, &QPushButton::clicked, this, &LooksPanel::loadRequested);
    connect(clear, &QPushButton::clicked, this, &LooksPanel::clearRequested);

    auto *fileRow = new QHBoxLayout;
    fileRow->setContentsMargins(0, 0, 0, 0);
    fileRow->addWidget(m_name, 1);
    fileRow->addWidget(load);
    fileRow->addWidget(clear);

    auto *intLabel = new QLabel(QStringLiteral("Intensity"), this);
    intLabel->setObjectName(QStringLiteral("rowName"));
    m_intensityValue = new QLabel(this);
    m_intensityValue->setObjectName(QStringLiteral("rowValue"));
    m_intensityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *intHeader = new QHBoxLayout;
    intHeader->setContentsMargins(0, 0, 0, 0);
    intHeader->addWidget(intLabel);
    intHeader->addStretch(1);
    intHeader->addWidget(m_intensityValue);

    m_intensity = new QSlider(Qt::Horizontal, this);
    m_intensity->setRange(0, 100);
    connect(m_intensity, &QSlider::valueChanged, this, &LooksPanel::onSliderChanged);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addLayout(fileRow);
    layout->addLayout(intHeader);
    layout->addWidget(m_intensity);

    setStyleSheet(QStringLiteral(R"(
        #looksPanel {
            background: #1c1c1f;
            border: 1px solid #38383d;
            border-radius: 10px;
        }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #lookName { color: #b4b4b8; font-size: 12px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        #rowValue { color: #d6d6d9; font-size: 12px; }
        QPushButton {
            background: #2a2a2e; color: #e8e8ea; border: 1px solid #38383d;
            border-radius: 6px; padding: 3px 10px; font-size: 12px;
        }
        QPushButton:hover { background: #34343a; }
    )"));

    setLookName(QString());
    hide();
}

void LooksPanel::setLookName(const QString &name)
{
    m_name->setText(name.isEmpty() ? QStringLiteral("No look loaded") : name);
}

void LooksPanel::reveal(const QString &lookName, double intensity)
{
    setLookName(lookName);
    {
        const QSignalBlocker block(m_intensity);
        m_intensity->setValue(static_cast<int>(std::lround(intensity * 100.0)));
        onSliderChanged(m_intensity->value()); // refresh label without emitting
    }
    adjustSize();
    show();
    raise();
    m_intensity->setFocus(Qt::ShortcutFocusReason);
}

void LooksPanel::onSliderChanged(int value)
{
    m_intensityValue->setText(QStringLiteral("%1%").arg(value));
    if (!m_intensity->signalsBlocked())
        emit intensityChanged(value / 100.0);
}

void LooksPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void LooksPanel::mouseMoveEvent(QMouseEvent *event)
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

void LooksPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}
