#include "ui/PresetsPanel.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kPanelWidth = 260;
constexpr int kThumbW = 150;
constexpr int kThumbH = 100;
constexpr int kMaxListHeight = 460; // scroll beyond this
} // namespace

PresetsPanel::PresetsPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("presetsPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Presets"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    // Amount: blends the applied preset toward the original (its layer opacity).
    auto *amountLabel = new QLabel(QStringLiteral("Amount"), this);
    amountLabel->setObjectName(QStringLiteral("rowName"));
    m_amountValue = new QLabel(QStringLiteral("100%"), this);
    m_amountValue->setObjectName(QStringLiteral("rowValue"));
    m_amountValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *amountHeader = new QHBoxLayout;
    amountHeader->setContentsMargins(0, 0, 0, 0);
    amountHeader->addWidget(amountLabel);
    amountHeader->addStretch(1);
    amountHeader->addWidget(m_amountValue);

    m_amount = new QSlider(Qt::Horizontal, this);
    m_amount->setRange(0, 100);
    m_amount->setValue(100);
    connect(m_amount, &QSlider::valueChanged, this, [this](int v) {
        m_amountValue->setText(QStringLiteral("%1%").arg(v));
        if (!m_amount->signalsBlocked())
            emit amountChanged(v);
    });

    // The rows live inside a scroll area so a long library stays usable.
    m_list = new QWidget;
    m_listLayout = new QVBoxLayout(m_list);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(6);
    m_listLayout->addStretch(1);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("presetsScroll"));
    scroll->setWidget(m_list);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMaximumHeight(kMaxListHeight);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(title);
    layout->addLayout(amountHeader);
    layout->addWidget(m_amount);
    layout->addWidget(scroll);

    setStyleSheet(QStringLiteral(R"(
        #presetsPanel {
            background: #1c1c1f;
            border: 1px solid #38383d;
            border-radius: 10px;
        }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #presetsScroll { background: transparent; }
        #sectionHeader {
            color: #8a8a90; font-size: 11px; font-weight: bold;
            text-transform: uppercase; padding: 4px 2px 0 2px;
        }
        QToolButton#presetCard {
            background: #232327; color: #e8e8ea; border: 1px solid #38383d;
            border-radius: 8px; padding: 6px; font-size: 12px; text-align: center;
        }
        QToolButton#presetCard:hover { background: #2e2e34; border-color: #4a4a52; }
    )"));

    hide();
}

void PresetsPanel::setItems(const QVector<Item> &items)
{
    // Clear existing rows (keep the trailing stretch).
    while (m_listLayout->count() > 0) {
        QLayoutItem *it = m_listLayout->takeAt(0);
        if (QWidget *w = it->widget())
            w->deleteLater();
        delete it;
    }

    QString currentCategory;
    for (const Item &item : items) {
        if (item.category != currentCategory) {
            currentCategory = item.category;
            auto *header = new QLabel(currentCategory, m_list);
            header->setObjectName(QStringLiteral("sectionHeader"));
            m_listLayout->addWidget(header);
        }

        auto *card = new QToolButton(m_list);
        card->setObjectName(QStringLiteral("presetCard"));
        card->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        card->setText(item.name);
        card->setCursor(Qt::PointingHandCursor);
        card->setIconSize(QSize(kThumbW, kThumbH));
        if (!item.thumb.isNull())
            card->setIcon(QIcon(item.thumb));
        card->setFixedWidth(kPanelWidth - 32);
        const QString id = item.id;
        connect(card, &QToolButton::clicked, this, [this, id] { emit applyRequested(id); });

        // User presets get a right-click menu to rename or delete them; built-ins
        // are read-only.
        if (item.userEditable) {
            const QString name = item.name;
            card->setToolTip(QStringLiteral("Right-click to rename or delete"));
            card->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(card, &QWidget::customContextMenuRequested, this,
                    [this, card, id, name](const QPoint &pos) {
                        QMenu menu(this);
                        QAction *rename = menu.addAction(QStringLiteral("Rename…"));
                        QAction *remove = menu.addAction(QStringLiteral("Delete"));
                        QAction *chosen = menu.exec(card->mapToGlobal(pos));
                        if (chosen == rename) {
                            bool ok = false;
                            const QString next = QInputDialog::getText(
                                this, QStringLiteral("Rename preset"), QStringLiteral("Name:"),
                                QLineEdit::Normal, name, &ok);
                            if (ok && !next.trimmed().isEmpty())
                                emit renameRequested(id, next.trimmed());
                        } else if (chosen == remove) {
                            emit deleteRequested(id);
                        }
                    });
        }
        m_listLayout->addWidget(card);
    }

    m_listLayout->addStretch(1);
    adjustSize();
}

void PresetsPanel::reveal()
{
    adjustSize();
    show();
    raise();
}

int PresetsPanel::amount() const
{
    return m_amount->value();
}

void PresetsPanel::setAmount(int percent)
{
    const QSignalBlocker block(m_amount); // seed without emitting amountChanged
    m_amount->setValue(percent);
    m_amountValue->setText(QStringLiteral("%1%").arg(m_amount->value()));
}

void PresetsPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void PresetsPanel::mouseMoveEvent(QMouseEvent *event)
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

void PresetsPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}
