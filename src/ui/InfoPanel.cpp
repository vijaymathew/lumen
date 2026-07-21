#include "ui/InfoPanel.h"

#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kPanelWidth = 320;
}

InfoPanel::InfoPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("infoPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Image info"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    m_empty = new QLabel(QStringLiteral("No image open"), this);
    m_empty->setObjectName(QStringLiteral("rowName"));

    m_grid = new QGridLayout;
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(14);
    m_grid->setVerticalSpacing(6);
    m_grid->setColumnStretch(1, 1); // let the value column take the slack

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addWidget(m_empty);
    layout->addLayout(m_grid);

    setStyleSheet(QStringLiteral(R"(
        #infoPanel {
            background: #1c1c1f;
            border: 1px solid #38383d;
            border-radius: 10px;
        }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        #infoKey { color: #8a8a90; font-size: 12px; }
        #infoValue { color: #e2e2e5; font-size: 12px; }
    )"));

    hide();
}

void InfoPanel::clearRows()
{
    for (QWidget *w : m_cells)
        w->deleteLater();
    m_cells.clear();
}

void InfoPanel::setRows(const QVector<Row> &rows)
{
    clearRows();
    m_empty->setVisible(rows.isEmpty());

    for (int i = 0; i < rows.size(); ++i) {
        auto *key = new QLabel(rows[i].label, this);
        key->setObjectName(QStringLiteral("infoKey"));
        key->setAlignment(Qt::AlignTop | Qt::AlignLeft);

        auto *val = new QLabel(rows[i].value, this);
        val->setObjectName(QStringLiteral("infoValue"));
        val->setWordWrap(true); // long paths wrap rather than widen the card
        val->setTextInteractionFlags(Qt::TextSelectableByMouse);
        val->setAlignment(Qt::AlignTop | Qt::AlignLeft);

        m_grid->addWidget(key, i, 0);
        m_grid->addWidget(val, i, 1);
        key->show();
        val->show();
        m_cells.push_back(key);
        m_cells.push_back(val);
    }
    adjustSize();
}

void InfoPanel::reveal()
{
    show();
    raise();
}

void InfoPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void InfoPanel::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || !parentWidget())
        return;
    QPoint topLeft = mapToParent(event->pos() - m_dragOffset);
    const QRect bounds = parentWidget()->rect();
    topLeft.setX(std::clamp(topLeft.x(), 0, bounds.width() - width()));
    topLeft.setY(std::clamp(topLeft.y(), 0, bounds.height() - height()));
    move(topLeft);
}

void InfoPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}
