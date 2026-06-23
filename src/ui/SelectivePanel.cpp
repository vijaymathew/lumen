#include "ui/SelectivePanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kExposureScale = 100;
constexpr int kPanelWidth = 248;

QLabel *makeSection(QWidget *parent, const QString &name)
{
    auto *label = new QLabel(name, parent);
    label->setObjectName(QStringLiteral("section"));
    return label;
}
} // namespace

SelectivePanel::SelectivePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("selectivePanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedWidth(kPanelWidth);

    auto *title = new QLabel(QStringLiteral("Selective"), this);
    title->setObjectName(QStringLiteral("toolTitle"));
    m_maskButton = new QPushButton(QStringLiteral("Mask: Off"), this);
    connect(m_maskButton, &QPushButton::clicked, this, &SelectivePanel::cycleMaskView);
    m_invertButton = new QPushButton(QStringLiteral("Invert"), this);
    m_invertButton->setCheckable(true);
    connect(m_invertButton, &QPushButton::toggled, this,
            [this] { emit valuesChanged(currentValues()); });

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    titleRow->addWidget(m_invertButton);
    titleRow->addWidget(m_maskButton);

    // Mask-mode selector.
    m_lumaButton = new QPushButton(QStringLiteral("Lum"), this);
    m_colorButton = new QPushButton(QStringLiteral("Colour"), this);
    m_brushButton = new QPushButton(QStringLiteral("Brush"), this);
    m_lumaButton->setCheckable(true);
    m_colorButton->setCheckable(true);
    m_brushButton->setCheckable(true);
    connect(m_lumaButton, &QPushButton::clicked, this, [this] { setMaskMode(0, true); });
    connect(m_colorButton, &QPushButton::clicked, this, [this] { setMaskMode(1, true); });
    connect(m_brushButton, &QPushButton::clicked, this, [this] { setMaskMode(2, true); });
    auto *modeRow = new QHBoxLayout;
    modeRow->setContentsMargins(0, 0, 0, 0);
    modeRow->addWidget(m_lumaButton);
    modeRow->addWidget(m_colorButton);
    modeRow->addWidget(m_brushButton);

    // Luminosity section.
    m_lumaSection = new QWidget(this);
    auto *lumaLayout = new QVBoxLayout(m_lumaSection);
    lumaLayout->setContentsMargins(0, 0, 0, 0);
    lumaLayout->setSpacing(8);
    lumaLayout->addWidget(makeSection(m_lumaSection, QStringLiteral("Mask (luminosity)")));
    m_low = addRow(lumaLayout, QStringLiteral("Range low"), 0, 100, &m_lowValue);
    m_high = addRow(lumaLayout, QStringLiteral("Range high"), 0, 100, &m_highValue);
    m_feather = addRow(lumaLayout, QStringLiteral("Feather"), 0, 100, &m_featherValue);

    // Colour section.
    m_colorSection = new QWidget(this);
    auto *colorLayout = new QVBoxLayout(m_colorSection);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    colorLayout->setSpacing(8);
    colorLayout->addWidget(makeSection(m_colorSection, QStringLiteral("Mask (colour)")));
    auto *pick = new QPushButton(QStringLiteral("Pick colour"), m_colorSection);
    connect(pick, &QPushButton::clicked, this, &SelectivePanel::pickColorRequested);
    m_swatch = new QLabel(m_colorSection);
    m_swatch->setObjectName(QStringLiteral("swatch"));
    m_swatch->setFixedSize(22, 22);
    auto *pickRow = new QHBoxLayout;
    pickRow->setContentsMargins(0, 0, 0, 0);
    pickRow->addWidget(pick);
    pickRow->addWidget(m_swatch);
    pickRow->addStretch(1);
    colorLayout->addLayout(pickRow);
    m_range = addRow(colorLayout, QStringLiteral("Range"), 2, 100, &m_rangeValue);

    // Brush section.
    m_brushSection = new QWidget(this);
    auto *brushLayout = new QVBoxLayout(m_brushSection);
    brushLayout->setContentsMargins(0, 0, 0, 0);
    brushLayout->setSpacing(8);
    brushLayout->addWidget(makeSection(m_brushSection, QStringLiteral("Brush")));
    m_addButton = new QPushButton(QStringLiteral("Add"), m_brushSection);
    m_subButton = new QPushButton(QStringLiteral("Subtract"), m_brushSection);
    m_addButton->setCheckable(true);
    m_subButton->setCheckable(true);
    auto *clear = new QPushButton(QStringLiteral("Clear"), m_brushSection);
    connect(m_addButton, &QPushButton::clicked, this, [this] {
        m_brushAdd = true;
        m_addButton->setChecked(true);
        m_subButton->setChecked(false);
        emit brushSettingsChanged(m_brushSize->value(), m_brushHardness->value(), m_brushAdd);
    });
    connect(m_subButton, &QPushButton::clicked, this, [this] {
        m_brushAdd = false;
        m_addButton->setChecked(false);
        m_subButton->setChecked(true);
        emit brushSettingsChanged(m_brushSize->value(), m_brushHardness->value(), m_brushAdd);
    });
    connect(clear, &QPushButton::clicked, this, &SelectivePanel::brushClearRequested);
    auto *modeBrushRow = new QHBoxLayout;
    modeBrushRow->setContentsMargins(0, 0, 0, 0);
    modeBrushRow->addWidget(m_addButton);
    modeBrushRow->addWidget(m_subButton);
    modeBrushRow->addStretch(1);
    modeBrushRow->addWidget(clear);
    brushLayout->addLayout(modeBrushRow);

    const auto brushRow = [this, brushLayout](const QString &name, int def, QLabel **valueOut) {
        auto *header = new QHBoxLayout;
        header->setContentsMargins(0, 0, 0, 0);
        auto *nameLabel = new QLabel(name);
        nameLabel->setObjectName(QStringLiteral("rowName"));
        auto *valueLabel = new QLabel();
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
            emit brushSettingsChanged(m_brushSize->value(), m_brushHardness->value(), m_brushAdd);
        });
        brushLayout->addLayout(header);
        brushLayout->addWidget(slider);
        *valueOut = valueLabel;
        return slider;
    };
    m_brushSize = brushRow(QStringLiteral("Size"), 30, &m_brushSizeValue);
    m_brushHardness = brushRow(QStringLiteral("Hardness"), 50, &m_brushHardnessValue);

    // Adjust section (always visible).
    auto *adjust = new QWidget(this);
    auto *adjustLayout = new QVBoxLayout(adjust);
    adjustLayout->setContentsMargins(0, 0, 0, 0);
    adjustLayout->setSpacing(8);
    adjustLayout->addWidget(makeSection(adjust, QStringLiteral("Adjust")));
    m_exposure = addRow(adjustLayout, QStringLiteral("Exposure"),
                        static_cast<int>(SelectiveNode::kMinExposure * kExposureScale),
                        static_cast<int>(SelectiveNode::kMaxExposure * kExposureScale),
                        &m_exposureValue);
    m_contrast = addRow(adjustLayout, QStringLiteral("Contrast"),
                        static_cast<int>(SelectiveNode::kMinAmount),
                        static_cast<int>(SelectiveNode::kMaxAmount), &m_contrastValue);
    m_saturation = addRow(adjustLayout, QStringLiteral("Saturation"),
                          static_cast<int>(SelectiveNode::kMinAmount),
                          static_cast<int>(SelectiveNode::kMaxAmount), &m_saturationValue);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(8);
    layout->addLayout(titleRow);
    layout->addLayout(modeRow);
    layout->addWidget(m_lumaSection);
    layout->addWidget(m_colorSection);
    layout->addWidget(m_brushSection);
    layout->addWidget(adjust);

    setStyleSheet(QStringLiteral(R"(
        #selectivePanel {
            background: #1c1c1f; border: 1px solid #38383d; border-radius: 10px;
        }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #section { color: #8a8a90; font-size: 11px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        #rowValue { color: #d6d6d9; font-size: 12px; }
        #swatch { border: 1px solid #38383d; border-radius: 4px; background: #000; }
        QPushButton {
            background: #2a2a2e; color: #e8e8ea; border: 1px solid #38383d;
            border-radius: 6px; padding: 2px 8px; font-size: 11px;
        }
        QPushButton:hover { background: #34343a; }
        QPushButton:checked { background: #3a3550; border-color: #7F77DD; }
    )"));

    setMaskMode(0, false);
    hide();
}

QSlider *SelectivePanel::addRow(QVBoxLayout *layout, const QString &name, int min,
                                int max, QLabel **valueOut)
{
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    auto *nameLabel = new QLabel(name);
    nameLabel->setObjectName(QStringLiteral("rowName"));
    auto *valueLabel = new QLabel();
    valueLabel->setObjectName(QStringLiteral("rowValue"));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(nameLabel);
    header->addStretch(1);
    header->addWidget(valueLabel);

    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    connect(slider, &QSlider::valueChanged, this, &SelectivePanel::onChanged);

    layout->addLayout(header);
    layout->addWidget(slider);
    *valueOut = valueLabel;
    return slider;
}

void SelectivePanel::setMaskMode(int mode, bool emitChange)
{
    m_maskMode = mode;
    m_lumaButton->setChecked(mode == 0);
    m_colorButton->setChecked(mode == 1);
    m_brushButton->setChecked(mode == 2);
    m_lumaSection->setVisible(mode == 0);
    m_colorSection->setVisible(mode == 1);
    m_brushSection->setVisible(mode == 2);
    adjustSize();
    if (emitChange) {
        emit valuesChanged(currentValues());
        if (mode == 2)
            emit brushSettingsChanged(m_brushSize->value(), m_brushHardness->value(), m_brushAdd);
    }
}

void SelectivePanel::setTargetColor(const QColor &color)
{
    m_target = color;
    m_swatch->setStyleSheet(QStringLiteral("background: %1; border: 1px solid #38383d;"
                                           "border-radius: 4px;")
                                .arg(color.name()));
}

SelectiveValues SelectivePanel::currentValues() const
{
    SelectiveValues v;
    v.maskMode = m_maskMode;
    v.low = static_cast<float>(m_low->value()) / 100.0f;
    v.high = static_cast<float>(m_high->value()) / 100.0f;
    v.feather = static_cast<float>(m_feather->value()) / 100.0f;
    v.targetR = static_cast<float>(m_target.redF());
    v.targetG = static_cast<float>(m_target.greenF());
    v.targetB = static_cast<float>(m_target.blueF());
    v.colorRange = static_cast<float>(m_range->value()) / 100.0f;
    v.exposure = static_cast<float>(m_exposure->value()) / kExposureScale;
    v.contrast = static_cast<float>(m_contrast->value());
    v.saturation = static_cast<float>(m_saturation->value());
    v.invert = m_invertButton->isChecked();
    return v;
}

void SelectivePanel::refreshLabels()
{
    m_lowValue->setText(QString::number(m_low->value()));
    m_highValue->setText(QString::number(m_high->value()));
    m_featherValue->setText(QString::number(m_feather->value()));
    m_rangeValue->setText(QString::number(m_range->value()));
    const double ev = m_exposure->value() / double(kExposureScale);
    m_exposureValue->setText(QStringLiteral("%1%2 EV")
                                 .arg(ev >= 0 ? QStringLiteral("+") : QString())
                                 .arg(ev, 0, 'f', 2));
    const auto signedInt = [](int a) {
        return QStringLiteral("%1%2").arg(a > 0 ? QStringLiteral("+") : QString()).arg(a);
    };
    m_contrastValue->setText(signedInt(m_contrast->value()));
    m_saturationValue->setText(signedInt(m_saturation->value()));
}

void SelectivePanel::reveal(const SelectiveValues &v)
{
    QSlider *sliders[] = {m_low, m_high, m_feather, m_range, m_exposure, m_contrast, m_saturation};
    for (QSlider *s : sliders)
        s->blockSignals(true);
    m_low->setValue(static_cast<int>(std::lround(v.low * 100.0f)));
    m_high->setValue(static_cast<int>(std::lround(v.high * 100.0f)));
    m_feather->setValue(static_cast<int>(std::lround(v.feather * 100.0f)));
    m_range->setValue(static_cast<int>(std::lround(v.colorRange * 100.0f)));
    m_exposure->setValue(static_cast<int>(std::lround(v.exposure * kExposureScale)));
    m_contrast->setValue(static_cast<int>(std::lround(v.contrast)));
    m_saturation->setValue(static_cast<int>(std::lround(v.saturation)));
    for (QSlider *s : sliders)
        s->blockSignals(false);

    setTargetColor(QColor::fromRgbF(v.targetR, v.targetG, v.targetB));
    {
        const QSignalBlocker b(m_invertButton);
        m_invertButton->setChecked(v.invert);
    }
    m_brushAdd = true;
    m_addButton->setChecked(true);
    m_subButton->setChecked(false);
    m_brushSizeValue->setText(QString::number(m_brushSize->value()));
    m_brushHardnessValue->setText(QString::number(m_brushHardness->value()));
    setMaskMode(v.maskMode, false);
    refreshLabels();

    m_maskViewMode = 0; // reset overlay
    m_maskButton->setText(QStringLiteral("Mask: Off"));

    adjustSize();
    show();
    raise();
    m_exposure->setFocus(Qt::ShortcutFocusReason);
}

void SelectivePanel::onChanged()
{
    refreshLabels();
    emit valuesChanged(currentValues());
}

void SelectivePanel::cycleMaskView()
{
    static const char *labels[] = {"Mask: Off", "Mask: Red", "Mask: Gray"};
    m_maskViewMode = (m_maskViewMode + 1) % 3;
    m_maskButton->setText(QString::fromLatin1(labels[m_maskViewMode]));
    emit maskViewChanged(m_maskViewMode);
}

void SelectivePanel::setBrushParams(int size, int hardness)
{
    const QSignalBlocker b1(m_brushSize);
    const QSignalBlocker b2(m_brushHardness);
    m_brushSize->setValue(size);
    m_brushHardness->setValue(hardness);
    m_brushSizeValue->setText(QString::number(size));
    m_brushHardnessValue->setText(QString::number(hardness));
}

void SelectivePanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void SelectivePanel::mouseMoveEvent(QMouseEvent *event)
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

void SelectivePanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}
