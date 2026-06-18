#include "ui/RawSettingsPanel.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>

namespace {
constexpr int kPanelWidth = 268;
// Highlight button index → LibRaw `highlight` value (clip / blend / reconstruct).
constexpr std::array<int, 3> kHighlightValues = {0, 2, 3};

int highlightIndexOf(int value)
{
    for (int i = 0; i < static_cast<int>(kHighlightValues.size()); ++i)
        if (kHighlightValues[i] == value)
            return i;
    return 0;
}
} // namespace

RawSettingsPanel::RawSettingsPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("rawPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("RAW Defaults"), this);
    title->setObjectName(QStringLiteral("toolTitle"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(title);

    m_autoBright = new QPushButton(QStringLiteral("Auto-brightness"), this);
    m_autoBright->setCheckable(true);
    connect(m_autoBright, &QPushButton::toggled, this, &RawSettingsPanel::onChanged);
    layout->addWidget(m_autoBright);

    {
        auto *header = new QHBoxLayout;
        header->setContentsMargins(0, 0, 0, 0);
        auto *nameLabel = new QLabel(QStringLiteral("Clip threshold"), this);
        nameLabel->setObjectName(QStringLiteral("rowName"));
        m_thresholdValue = new QLabel(this);
        m_thresholdValue->setObjectName(QStringLiteral("rowValue"));
        m_thresholdValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        header->addWidget(nameLabel);
        header->addStretch(1);
        header->addWidget(m_thresholdValue);
        layout->addLayout(header);
        m_threshold = new QSlider(Qt::Horizontal, this);
        m_threshold->setRange(1, 100); // value/1000 → 0.001..0.1
        m_threshold->installEventFilter(this);
        connect(m_threshold, &QSlider::valueChanged, this, &RawSettingsPanel::onChanged);
        layout->addWidget(m_threshold);
    }

    m_highlight = addButtonRow(QStringLiteral("Highlights"),
                               {QStringLiteral("Clip"), QStringLiteral("Blend"),
                                QStringLiteral("Rebuild")});
    m_wb = addButtonRow(QStringLiteral("White balance"),
                        {QStringLiteral("Camera"), QStringLiteral("Auto"),
                         QStringLiteral("None")});
    m_demosaic = addButtonRow(QStringLiteral("Demosaic"),
                              {QStringLiteral("Lin"), QStringLiteral("VNG"),
                               QStringLiteral("PPG"), QStringLiteral("AHD"),
                               QStringLiteral("DCB")});

    auto *lensLabel = new QLabel(QStringLiteral("Lens corrections"), this);
    lensLabel->setObjectName(QStringLiteral("rowName"));
    layout->addWidget(lensLabel);
    auto *lensRow = new QHBoxLayout;
    lensRow->setContentsMargins(0, 0, 0, 0);
    lensRow->setSpacing(4);
    m_lensDistortion = new QPushButton(QStringLiteral("Distort"), this);
    m_lensTca = new QPushButton(QStringLiteral("TCA"), this);
    m_lensVignetting = new QPushButton(QStringLiteral("Vignette"), this);
    for (QPushButton *b : {m_lensDistortion, m_lensTca, m_lensVignetting}) {
        b->setCheckable(true);
        connect(b, &QPushButton::toggled, this, &RawSettingsPanel::onChanged);
        lensRow->addWidget(b);
    }
    layout->addLayout(lensRow);

    auto *reset = new QPushButton(QStringLiteral("Reset to defaults"), this);
    connect(reset, &QPushButton::clicked, this, [this] {
        setControls(raw::RawDecodeOptions{}, raw::RawLensDefaults{}); // historical baseline
        onChanged(); // re-decode the open RAW + restore default lens corrections
    });
    layout->addWidget(reset);

    setStyleSheet(QStringLiteral(R"(
        #rawPanel {
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
    )"));

    hide();
}

QButtonGroup *RawSettingsPanel::addButtonRow(const QString &name, const QStringList &labels)
{
    auto *nameLabel = new QLabel(name, this);
    nameLabel->setObjectName(QStringLiteral("rowName"));
    auto *layout = static_cast<QVBoxLayout *>(this->layout());
    layout->addWidget(nameLabel);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    for (int i = 0; i < labels.size(); ++i) {
        auto *b = new QPushButton(labels[i], this);
        b->setCheckable(true);
        group->addButton(b, i);
        row->addWidget(b);
    }
    layout->addLayout(row);
    connect(group, &QButtonGroup::idClicked, this, [this](int) { onChanged(); });
    return group;
}

raw::RawDecodeOptions RawSettingsPanel::currentOptions() const
{
    raw::RawDecodeOptions o;
    o.autoBright = m_autoBright->isChecked();
    o.autoBrightThreshold = static_cast<float>(m_threshold->value()) / 1000.0f;
    o.highlight = kHighlightValues[std::clamp(m_highlight->checkedId(), 0, 2)];
    o.wb = std::max(0, m_wb->checkedId());
    o.demosaic = std::max(0, m_demosaic->checkedId());
    return o;
}

raw::RawLensDefaults RawSettingsPanel::currentLens() const
{
    raw::RawLensDefaults l;
    l.distortion = m_lensDistortion->isChecked();
    l.tca = m_lensTca->isChecked();
    l.vignetting = m_lensVignetting->isChecked();
    return l;
}

void RawSettingsPanel::refreshLabels()
{
    const float thr = static_cast<float>(m_threshold->value()) / 1000.0f;
    m_thresholdValue->setText(QStringLiteral("%1%").arg(thr * 100.0f, 0, 'f', 1));
    m_threshold->setEnabled(m_autoBright->isChecked());
}

void RawSettingsPanel::setControls(const raw::RawDecodeOptions &opts,
                                   const raw::RawLensDefaults &lens)
{
    const QSignalBlocker b0(m_autoBright);
    const QSignalBlocker b1(m_threshold);
    const QSignalBlocker b2(m_lensDistortion);
    const QSignalBlocker b3(m_lensTca);
    const QSignalBlocker b4(m_lensVignetting);
    const QSignalBlocker b5(m_highlight);
    const QSignalBlocker b6(m_wb);
    const QSignalBlocker b7(m_demosaic);
    m_autoBright->setChecked(opts.autoBright);
    m_threshold->setValue(std::clamp(static_cast<int>(std::lround(opts.autoBrightThreshold * 1000.0f)),
                                     1, 100));
    if (auto *b = m_highlight->button(highlightIndexOf(opts.highlight)))
        b->setChecked(true);
    if (auto *b = m_wb->button(std::clamp(opts.wb, 0, 2)))
        b->setChecked(true);
    if (auto *b = m_demosaic->button(std::clamp(opts.demosaic, 0, 4)))
        b->setChecked(true);
    m_lensDistortion->setChecked(lens.distortion);
    m_lensTca->setChecked(lens.tca);
    m_lensVignetting->setChecked(lens.vignetting);
    refreshLabels();
}

void RawSettingsPanel::reveal(const raw::RawDecodeOptions &opts, const raw::RawLensDefaults &lens)
{
    setControls(opts, lens);
    adjustSize();
    show();
    raise();
    m_autoBright->setFocus(Qt::ShortcutFocusReason);
}

void RawSettingsPanel::onChanged()
{
    refreshLabels();
    emit valuesChanged(currentOptions(), currentLens());
}

void RawSettingsPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void RawSettingsPanel::mouseMoveEvent(QMouseEvent *event)
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

void RawSettingsPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}

bool RawSettingsPanel::eventFilter(QObject *watched, QEvent *event)
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
