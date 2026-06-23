#include "ui/LayersPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kPanelWidth = 220;
}

LayersPanel::LayersPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("layersPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Layers"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    auto *rows = new QWidget(this);
    m_rowsLayout = new QVBoxLayout(rows);
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);
    m_rowsLayout->setSpacing(3);

    auto *add = new QPushButton(QStringLiteral("＋ Add"), this);
    m_deleteButton = new QPushButton(QStringLiteral("🗑 Delete"), this);
    connect(add, &QPushButton::clicked, this, &LayersPanel::addRequested);
    connect(m_deleteButton, &QPushButton::clicked, this, &LayersPanel::deleteRequested);
    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->addWidget(add);
    buttons->addWidget(m_deleteButton);

    auto *opacityLabel = new QLabel(QStringLiteral("Opacity"), this);
    opacityLabel->setObjectName(QStringLiteral("rowName"));
    m_opacityValue = new QLabel(this);
    m_opacityValue->setObjectName(QStringLiteral("rowValue"));
    m_opacityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *opacityHeader = new QHBoxLayout;
    opacityHeader->setContentsMargins(0, 0, 0, 0);
    opacityHeader->addWidget(opacityLabel);
    opacityHeader->addStretch(1);
    opacityHeader->addWidget(m_opacityValue);
    m_opacity = new QSlider(Qt::Horizontal, this);
    m_opacity->setRange(0, 100);
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        m_opacityValue->setText(QStringLiteral("%1%").arg(v));
        emit opacityChanged(v);
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addWidget(rows);
    layout->addLayout(buttons);
    layout->addLayout(opacityHeader);
    layout->addWidget(m_opacity);

    setStyleSheet(QStringLiteral(R"(
        #layersPanel { background: #1c1c1f; border: 1px solid #38383d; border-radius: 10px; }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        #rowValue { color: #d6d6d9; font-size: 12px; }
        QPushButton {
            background: #2a2a2e; color: #e8e8ea; border: 1px solid #38383d;
            border-radius: 6px; padding: 3px 8px; font-size: 12px; text-align: left;
        }
        QPushButton:hover { background: #34343a; }
        QPushButton[active="true"] { background: #3a3550; border-color: #7F77DD; }
    )"));

    hide();
}

void LayersPanel::setLayers(const QVector<Row> &rows, int active, int activeOpacity)
{
    qDeleteAll(m_rowWidgets);
    m_rowWidgets.clear();

    // Show top→bottom with the topmost layer first (visually like Photoshop).
    for (int i = rows.size() - 1; i >= 0; --i) {
        const Row &row = rows[i];
        auto *rowWidget = new QWidget;
        auto *h = new QHBoxLayout(rowWidget);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);

        auto *vis = new QPushButton(row.enabled ? QStringLiteral("●") : QStringLiteral("○"));
        vis->setFixedWidth(24);
        connect(vis, &QPushButton::clicked, this,
                [this, i, en = row.enabled] { emit visibilityToggled(i, !en); });

        auto *name = new QPushButton(row.name);
        name->setProperty("active", i == active);
        connect(name, &QPushButton::clicked, this, [this, i] { emit layerSelected(i); });

        h->addWidget(vis);
        h->addWidget(name, 1);
        m_rowsLayout->addWidget(rowWidget);
        m_rowWidgets.push_back(rowWidget);
    }

    m_deleteButton->setEnabled(active > 0); // the Base layer can't be deleted
    {
        const QSignalBlocker block(m_opacity);
        m_opacity->setValue(activeOpacity);
        m_opacityValue->setText(QStringLiteral("%1%").arg(activeOpacity));
    }
    m_opacity->setEnabled(active > 0); // Base layer is always full opacity
    adjustSize();
}

void LayersPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void LayersPanel::mouseMoveEvent(QMouseEvent *event)
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

void LayersPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}
