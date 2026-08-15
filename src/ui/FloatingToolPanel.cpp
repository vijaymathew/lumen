#include "ui/FloatingToolPanel.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QString baseStyleSheet(const QString &objectName)
{
    return QStringLiteral(R"(
        #%1 {
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
    )")
        .arg(objectName);
}
} // namespace

FloatingToolPanel::FloatingToolPanel(const QString &objectName, const QString &title,
                                     int fixedWidth, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(objectName);
    setAttribute(Qt::WA_StyledBackground, true);
    if (fixedWidth > 0)
        setFixedWidth(fixedWidth);

    auto *titleLabel = new QLabel(title, this);
    titleLabel->setObjectName(QStringLiteral("toolTitle"));

    m_headerRow = new QHBoxLayout;
    m_headerRow->setContentsMargins(0, 0, 0, 0);
    m_headerRow->addWidget(titleLabel);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(16, 14, 16, 16);
    m_layout->setSpacing(10);
    m_layout->addLayout(m_headerRow);

    setStyleSheet(baseStyleSheet(objectName));
    hide();
}

void FloatingToolPanel::appendStyleSheet(const QString &extraCss)
{
    setStyleSheet(styleSheet() + extraCss);
}

void FloatingToolPanel::closesOnEscape(QWidget *w)
{
    w->installEventFilter(this);
}

QSlider *FloatingToolPanel::addRow(const QString &name, int min, int max, QLabel **valueOut)
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
    closesOnEscape(slider);

    m_layout->addLayout(header);
    m_layout->addWidget(slider);

    *valueOut = valueLabel;
    return slider;
}

bool FloatingToolPanel::eventFilter(QObject *watched, QEvent *event)
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

void FloatingToolPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void FloatingToolPanel::mouseMoveEvent(QMouseEvent *event)
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

void FloatingToolPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}
