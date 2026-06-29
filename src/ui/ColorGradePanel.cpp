#include "ui/ColorGradePanel.h"

#include "ui/ColorWheel.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int kMasterScale = 100; // slider units per master unit ([-100,100] → [-1,1])
} // namespace

ColorGradePanel::ColorGradePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("gradePanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(QStringLiteral("Color grade"), this);
    title->setObjectName(QStringLiteral("toolTitle"));
    m_enable = new QCheckBox(QStringLiteral("Enable"), this);
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(m_enable);
    layout->addLayout(header);

    auto *wheels = new QHBoxLayout;
    wheels->setContentsMargins(0, 0, 0, 0);
    wheels->setSpacing(14);
    layout->addLayout(wheels);

    const auto addColumn = [&](const QString &name) -> Wheel {
        auto *col = new QVBoxLayout;
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(6);
        auto *label = new QLabel(name, this);
        label->setObjectName(QStringLiteral("rowName"));
        label->setAlignment(Qt::AlignHCenter);
        auto *wheel = new ColorWheel(this);
        auto *master = new QSlider(Qt::Horizontal, this);
        master->setRange(-kMasterScale, kMasterScale);
        master->installEventFilter(this);
        wheel->installEventFilter(this);
        connect(wheel, &ColorWheel::changed, this, [this](float, float) { onChanged(); });
        connect(master, &QSlider::valueChanged, this, [this] { onChanged(); });
        col->addWidget(label);
        col->addWidget(wheel, 0, Qt::AlignHCenter);
        col->addWidget(master);
        wheels->addLayout(col);
        return {wheel, master};
    };
    m_lift = addColumn(QStringLiteral("Lift"));
    m_gamma = addColumn(QStringLiteral("Gamma"));
    m_gain = addColumn(QStringLiteral("Gain"));

    connect(m_enable, &QCheckBox::toggled, this, [this] { onChanged(); });

    setStyleSheet(QStringLiteral(R"(
        #gradePanel {
            background: #1c1c1f;
            border: 1px solid #38383d;
            border-radius: 10px;
        }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        QCheckBox { color: #b4b4b8; font-size: 12px; }
    )"));

    hide();
}

ColorGradeValues ColorGradePanel::currentValues() const
{
    ColorGradeValues v;
    v.enabled = m_enable->isChecked();
    v.liftX = m_lift.wheel->x();
    v.liftY = m_lift.wheel->y();
    v.liftMaster = static_cast<float>(m_lift.master->value()) / kMasterScale;
    v.gammaX = m_gamma.wheel->x();
    v.gammaY = m_gamma.wheel->y();
    v.gammaMaster = static_cast<float>(m_gamma.master->value()) / kMasterScale;
    v.gainX = m_gain.wheel->x();
    v.gainY = m_gain.wheel->y();
    v.gainMaster = static_cast<float>(m_gain.master->value()) / kMasterScale;
    return v;
}

void ColorGradePanel::onChanged()
{
    emit valuesChanged(currentValues());
}

void ColorGradePanel::reveal(const ColorGradeValues &v)
{
    const QSignalBlocker b0(m_enable);
    const QSignalBlocker b1(m_lift.master);
    const QSignalBlocker b2(m_gamma.master);
    const QSignalBlocker b3(m_gain.master);
    m_enable->setChecked(v.enabled);
    m_lift.wheel->setValue(v.liftX, v.liftY);
    m_gamma.wheel->setValue(v.gammaX, v.gammaY);
    m_gain.wheel->setValue(v.gainX, v.gainY);
    m_lift.master->setValue(static_cast<int>(std::lround(v.liftMaster * kMasterScale)));
    m_gamma.master->setValue(static_cast<int>(std::lround(v.gammaMaster * kMasterScale)));
    m_gain.master->setValue(static_cast<int>(std::lround(v.gainMaster * kMasterScale)));

    adjustSize();
    show();
    raise();
    setFocus(Qt::ShortcutFocusReason);
}

void ColorGradePanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void ColorGradePanel::mouseMoveEvent(QMouseEvent *event)
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

void ColorGradePanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}

bool ColorGradePanel::eventFilter(QObject *watched, QEvent *event)
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
