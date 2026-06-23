#include "ui/HealPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kPanelWidth = 248;
}

HealPanel::HealPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("healPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Heal"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    m_addButton = new QPushButton(QStringLiteral("Paint"), this);
    m_subButton = new QPushButton(QStringLiteral("Erase"), this);
    m_addButton->setCheckable(true);
    m_subButton->setCheckable(true);
    auto *clear = new QPushButton(QStringLiteral("Clear"), this);
    connect(m_addButton, &QPushButton::clicked, this, [this] {
        m_add = true;
        m_addButton->setChecked(true);
        m_subButton->setChecked(false);
        emitSettings();
    });
    connect(m_subButton, &QPushButton::clicked, this, [this] {
        m_add = false;
        m_addButton->setChecked(false);
        m_subButton->setChecked(true);
        emitSettings();
    });
    connect(clear, &QPushButton::clicked, this, &HealPanel::clearRequested);

    auto *modeRow = new QHBoxLayout;
    modeRow->setContentsMargins(0, 0, 0, 0);
    modeRow->addWidget(m_addButton);
    modeRow->addWidget(m_subButton);
    modeRow->addStretch(1);
    modeRow->addWidget(clear);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addLayout(modeRow);
    m_size = addRow(QStringLiteral("Size"), 30, &m_sizeValue);
    m_hardness = addRow(QStringLiteral("Hardness"), 50, &m_hardnessValue);

    auto *hint = new QLabel(QStringLiteral("paint over a blemish · Ctrl+Z undoes a stroke"),
                            this);
    hint->setObjectName(QStringLiteral("section"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    setStyleSheet(QStringLiteral(R"(
        #healPanel {
            background: #1c1c1f; border: 1px solid #38383d; border-radius: 10px;
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
        QPushButton:checked { background: #3a3550; border-color: #7F77DD; }
    )"));

    hide();
}

QSlider *HealPanel::addRow(const QString &name, int def, QLabel **valueOut)
{
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    auto *nameLabel = new QLabel(name);
    nameLabel->setObjectName(QStringLiteral("rowName"));
    auto *valueLabel = new QLabel(QString::number(def));
    valueLabel->setObjectName(QStringLiteral("rowValue"));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(nameLabel);
    header->addStretch(1);
    header->addWidget(valueLabel);

    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(1, 100);
    slider->setValue(def);
    connect(slider, &QSlider::valueChanged, this, [this, valueLabel](int v) {
        valueLabel->setText(QString::number(v));
        emitSettings();
    });

    auto *l = static_cast<QVBoxLayout *>(layout());
    l->addLayout(header);
    l->addWidget(slider);
    *valueOut = valueLabel;
    return slider;
}

void HealPanel::emitSettings()
{
    emit settingsChanged(m_size->value(), m_hardness->value(), m_add);
}

void HealPanel::reveal(int size, int hardness, bool add)
{
    {
        const QSignalBlocker b1(m_size);
        const QSignalBlocker b2(m_hardness);
        m_size->setValue(size);
        m_hardness->setValue(hardness);
        m_sizeValue->setText(QString::number(size));
        m_hardnessValue->setText(QString::number(hardness));
    }
    m_add = add;
    m_addButton->setChecked(add);
    m_subButton->setChecked(!add);
    adjustSize();
    show();
    raise();
    setFocus(Qt::ShortcutFocusReason);
}

void HealPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void HealPanel::mouseMoveEvent(QMouseEvent *event)
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

void HealPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}
