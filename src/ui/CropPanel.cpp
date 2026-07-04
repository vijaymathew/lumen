#include "ui/CropPanel.h"

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
constexpr int kPanelWidth = 248;
constexpr double kStraightenScale = 10.0; // slider units per degree (0.1° steps)
constexpr int kStraightenRange = 450;     // ±45.0°

QString straightenText(double deg)
{
    return QStringLiteral("%1°").arg(deg, 0, 'f', 1);
}

struct AspectPreset {
    const char *label;
    double ratio; // width/height; 0 = free; -1 = original (resolved at runtime)
};
const AspectPreset kAspects[] = {
    {"Free", 0.0}, {"Original", -1.0}, {"1:1", 1.0},
    {"3:2", 3.0 / 2.0}, {"4:3", 4.0 / 3.0}, {"16:9", 16.0 / 9.0},
};
} // namespace

CropPanel::CropPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("cropPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Crop & Rotate"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(title);

    auto *aspectLabel = new QLabel(QStringLiteral("Aspect"), this);
    aspectLabel->setObjectName(QStringLiteral("rowName"));
    layout->addWidget(aspectLabel);
    auto *aspectRow1 = new QHBoxLayout;
    auto *aspectRow2 = new QHBoxLayout;
    aspectRow1->setContentsMargins(0, 0, 0, 0);
    aspectRow2->setContentsMargins(0, 0, 0, 0);
    aspectRow1->setSpacing(4);
    aspectRow2->setSpacing(4);
    int i = 0;
    for (const AspectPreset &a : kAspects) {
        auto *b = new QPushButton(QString::fromLatin1(a.label), this);
        b->setCheckable(true);
        connect(b, &QPushButton::clicked, this, [this, idx = i, ratio = a.ratio] {
            selectAspectButton(idx);
            emit aspectChanged(ratio < 0 ? m_originalAspect : ratio);
        });
        (i < 3 ? aspectRow1 : aspectRow2)->addWidget(b);
        m_aspectButtons.push_back(b);
        ++i;
    }
    m_aspectButtons[0]->setChecked(true); // Free
    layout->addLayout(aspectRow1);
    layout->addLayout(aspectRow2);

    auto *orientLabel = new QLabel(QStringLiteral("Orient"), this);
    orientLabel->setObjectName(QStringLiteral("rowName"));
    layout->addWidget(orientLabel);
    auto *rotRow = new QHBoxLayout;
    rotRow->setContentsMargins(0, 0, 0, 0);
    rotRow->setSpacing(4);
    auto *rotCCW = new QPushButton(QStringLiteral("⟲ 90°"), this);
    auto *rotCW = new QPushButton(QStringLiteral("⟳ 90°"), this);
    m_flipH = new QPushButton(QStringLiteral("Flip H"), this);
    m_flipV = new QPushButton(QStringLiteral("Flip V"), this);
    m_flipH->setCheckable(true);
    m_flipV->setCheckable(true);
    connect(rotCCW, &QPushButton::clicked, this, [this] { emit rotateRequested(-90); });
    connect(rotCW, &QPushButton::clicked, this, [this] { emit rotateRequested(90); });
    connect(m_flipH, &QPushButton::clicked, this, [this] { emit flipRequested(true); });
    connect(m_flipV, &QPushButton::clicked, this, [this] { emit flipRequested(false); });
    rotRow->addWidget(rotCCW);
    rotRow->addWidget(rotCW);
    rotRow->addWidget(m_flipH);
    rotRow->addWidget(m_flipV);
    layout->addLayout(rotRow);

    // Straighten: a fine tilt slider (level-the-horizon), with a live degree read-out.
    auto *straightenHeader = new QHBoxLayout;
    straightenHeader->setContentsMargins(0, 0, 0, 0);
    auto *straightenLabel = new QLabel(QStringLiteral("Straighten"), this);
    straightenLabel->setObjectName(QStringLiteral("rowName"));
    m_straightenValue = new QLabel(straightenText(0.0), this);
    m_straightenValue->setObjectName(QStringLiteral("rowValue"));
    m_straightenValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    straightenHeader->addWidget(straightenLabel);
    straightenHeader->addWidget(m_straightenValue);
    layout->addLayout(straightenHeader);

    m_straighten = new QSlider(Qt::Horizontal, this);
    m_straighten->setRange(-kStraightenRange, kStraightenRange);
    m_straighten->setSingleStep(1);  // 0.1°
    m_straighten->setPageStep(50);   // 5°
    m_straighten->setValue(0);
    connect(m_straighten, &QSlider::valueChanged, this, [this](int v) {
        const double deg = v / kStraightenScale;
        m_straightenValue->setText(straightenText(deg));
        emit straightenChanged(deg);
    });
    layout->addWidget(m_straighten);

    auto *reset = new QPushButton(QStringLiteral("Reset crop"), this);
    connect(reset, &QPushButton::clicked, this, [this] {
        selectAspectButton(0);
        emit resetRequested();
    });
    layout->addWidget(reset);

    setStyleSheet(QStringLiteral(R"(
        #cropPanel {
            background: #1c1c1f;
            border: 1px solid #38383d;
            border-radius: 10px;
        }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        #rowValue { color: #e8e8ea; font-size: 12px; }
        QSlider::groove:horizontal {
            height: 4px; background: #38383d; border-radius: 2px;
        }
        QSlider::handle:horizontal {
            background: #cfcbf0; border: 1px solid #7F77DD; width: 14px;
            margin: -6px 0; border-radius: 7px;
        }
        QSlider::handle:horizontal:hover { background: #ffffff; }
        QPushButton {
            background: #2a2a2e; color: #e8e8ea; border: 1px solid #38383d;
            border-radius: 6px; padding: 4px 8px; font-size: 12px;
        }
        QPushButton:hover { background: #34343a; }
        QPushButton:checked { background: #3a3550; border-color: #7F77DD; }
    )"));

    hide();
}

void CropPanel::selectAspectButton(int index)
{
    for (int i = 0; i < m_aspectButtons.size(); ++i)
        m_aspectButtons[i]->setChecked(i == index);
}

void CropPanel::reveal(const CropState &crop, double originalAspect)
{
    m_originalAspect = originalAspect > 0 ? originalAspect : 1.0;
    {
        const QSignalBlocker bh(m_flipH);
        const QSignalBlocker bv(m_flipV);
        const QSignalBlocker bs(m_straighten);
        m_flipH->setChecked(crop.flipH);
        m_flipV->setChecked(crop.flipV);
        const int v = static_cast<int>(std::lround(crop.straighten * kStraightenScale));
        m_straighten->setValue(std::clamp(v, -kStraightenRange, kStraightenRange));
        m_straightenValue->setText(straightenText(crop.straighten));
    }
    adjustSize();
    show();
    raise();
    setFocus(Qt::ShortcutFocusReason);
}

void CropPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void CropPanel::mouseMoveEvent(QMouseEvent *event)
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

void CropPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}

bool CropPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape || ke->key() == Qt::Key_Return
            || ke->key() == Qt::Key_Enter) {
            emit closed();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
