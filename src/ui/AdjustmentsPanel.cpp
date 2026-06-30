#include "ui/AdjustmentsPanel.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kPanelWidth = 260;
}

AdjustmentsPanel::AdjustmentsPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("adjustmentsPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Adjustments"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    m_compare = new QPushButton(QStringLiteral("Before / After"), this);
    m_compare->setCheckable(true);
    m_compare->setToolTip(QStringLiteral("Show the original (\\)"));
    connect(m_compare, &QPushButton::toggled, this, &AdjustmentsPanel::compareToggled);

    auto *headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->addWidget(title);
    headerRow->addStretch(1);
    headerRow->addWidget(m_compare);

    m_empty = new QLabel(QStringLiteral("No adjustments yet"), this);
    m_empty->setObjectName(QStringLiteral("rowName"));

    m_listLayout = new QVBoxLayout;
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(4);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addLayout(headerRow);
    layout->addWidget(m_empty);
    layout->addLayout(m_listLayout);

    setStyleSheet(QStringLiteral(R"(
        #adjustmentsPanel {
            background: #1c1c1f;
            border: 1px solid #38383d;
            border-radius: 10px;
        }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        QPushButton {
            background: #2a2a2e; color: #e8e8ea; border: 1px solid #38383d;
            border-radius: 6px; padding: 3px 10px; font-size: 12px;
        }
        QPushButton:hover { background: #34343a; }
        QPushButton:checked { background: #3a5a8a; border-color: #4a6aa0; }
        QPushButton#adjName { text-align: left; border: none; background: transparent;
                              padding: 3px 6px; }
        QPushButton#adjName:hover { background: #2a2a2e; }
        QPushButton#adjName[selected="true"] { background: #3a5a8a; color: #ffffff; }
        QPushButton#adjDelete { padding: 2px 8px; color: #d08a8a; }
        QPushButton#adjDelete:hover { background: #4a2a2a; }
    )"));

    hide();
}

void AdjustmentsPanel::clearRows()
{
    for (QWidget *row : m_rows)
        row->deleteLater();
    m_rows.clear();
}

void AdjustmentsPanel::setItems(const QVector<Item> &items, int selected, bool comparing)
{
    {
        const QSignalBlocker block(m_compare);
        m_compare->setChecked(comparing);
    }
    clearRows();
    m_empty->setVisible(items.isEmpty());

    for (int i = 0; i < items.size(); ++i) {
        const Item &it = items[i];
        auto *row = new QWidget(this);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);

        auto *check = new QCheckBox(row);
        check->setChecked(it.enabled);
        check->setToolTip(QStringLiteral("Turn this adjustment on/off"));
        connect(check, &QCheckBox::toggled, this,
                [this, i](bool on) { emit toggleRequested(i, on); });

        auto *name = new QPushButton(it.name, row);
        name->setObjectName(QStringLiteral("adjName"));
        name->setProperty("selected", i == selected);
        name->setToolTip(QStringLiteral("Show the image up to here"));
        // A disabled adjustment reads dimmer.
        if (!it.enabled)
            name->setStyleSheet(QStringLiteral("color: #6f6f74;"));
        connect(name, &QPushButton::clicked, this,
                [this, i] { emit viewUpToRequested(i); });

        auto *del = new QPushButton(QStringLiteral("✕"), row);
        del->setObjectName(QStringLiteral("adjDelete"));
        del->setToolTip(QStringLiteral("Delete this adjustment"));
        connect(del, &QPushButton::clicked, this,
                [this, i] { emit deleteRequested(i); });

        h->addWidget(check);
        h->addWidget(name, 1);
        h->addWidget(del);
        m_listLayout->addWidget(row);
        m_rows.push_back(row);
        row->show();
    }
    adjustSize();
}

void AdjustmentsPanel::reveal()
{
    adjustSize();
    show();
    raise();
}

void AdjustmentsPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void AdjustmentsPanel::mouseMoveEvent(QMouseEvent *event)
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

void AdjustmentsPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}
