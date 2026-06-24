#include "ui/LayersPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kPanelWidth = 240;

// Mask-type values mirror MaskSpec::Type.
constexpr int kNone = MaskSpec::None;
constexpr int kBrush = MaskSpec::Brush;
constexpr int kGradient = MaskSpec::LinearGradient;
constexpr int kRadial = MaskSpec::Radial;
constexpr int kLum = MaskSpec::Luminosity;
constexpr int kColour = MaskSpec::Colour;
} // namespace

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

    // ---- Mask section (active layer) -------------------------------------
    m_maskSection = new QWidget(this);
    auto *maskLayout = new QVBoxLayout(m_maskSection);
    maskLayout->setContentsMargins(0, 0, 0, 0);
    maskLayout->setSpacing(6);

    auto *maskTitle = new QLabel(QStringLiteral("Mask"), m_maskSection);
    maskTitle->setObjectName(QStringLiteral("rowName"));
    m_showButton = new QPushButton(QStringLiteral("Show: Off"), m_maskSection);
    connect(m_showButton, &QPushButton::clicked, this, [this] {
        m_showMode = (m_showMode + 1) % 3;
        static const char *labels[] = {"Show: Off", "Show: Red", "Show: Gray"};
        m_showButton->setText(QString::fromLatin1(labels[m_showMode]));
        emit maskShowChanged(m_showMode);
    });
    auto *maskTitleRow = new QHBoxLayout;
    maskTitleRow->setContentsMargins(0, 0, 0, 0);
    maskTitleRow->addWidget(maskTitle);
    maskTitleRow->addStretch(1);
    maskTitleRow->addWidget(m_showButton);

    // Type buttons in two rows of three.
    const struct { const char *label; int type; } kTypes[] = {
        {"None", kNone}, {"Gradient", kGradient}, {"Radial", kRadial},
        {"Lum", kLum}, {"Colour", kColour}, {"Brush", kBrush}};
    auto *typeRow1 = new QHBoxLayout;
    auto *typeRow2 = new QHBoxLayout;
    typeRow1->setContentsMargins(0, 0, 0, 0);
    typeRow2->setContentsMargins(0, 0, 0, 0);
    typeRow1->setSpacing(4);
    typeRow2->setSpacing(4);
    int idx = 0;
    for (const auto &t : kTypes) {
        auto *b = new QPushButton(QString::fromLatin1(t.label), m_maskSection);
        b->setCheckable(true);
        b->setProperty("maskType", t.type);
        connect(b, &QPushButton::clicked, this,
                [this, type = t.type] { emit maskTypeChanged(type); });
        (idx++ < 3 ? typeRow1 : typeRow2)->addWidget(b);
        m_maskTypeButtons.push_back(b);
    }

    // Luminosity sub-section.
    m_lumSection = new QWidget(m_maskSection);
    auto *lumLayout = new QVBoxLayout(m_lumSection);
    lumLayout->setContentsMargins(0, 0, 0, 0);
    lumLayout->setSpacing(6);
    m_low = addSlider(lumLayout, QStringLiteral("Range low"), 0, 100, &m_lowValue);
    m_high = addSlider(lumLayout, QStringLiteral("Range high"), 0, 100, &m_highValue);
    const auto emitRange = [this](int) {
        m_lowValue->setText(QString::number(m_low->value()));
        m_highValue->setText(QString::number(m_high->value()));
        emit maskRangeChanged(m_low->value(), m_high->value());
    };
    connect(m_low, &QSlider::valueChanged, this, emitRange);
    connect(m_high, &QSlider::valueChanged, this, emitRange);

    // Colour sub-section.
    m_colorSection = new QWidget(m_maskSection);
    auto *colorLayout = new QVBoxLayout(m_colorSection);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    colorLayout->setSpacing(6);
    auto *pick = new QPushButton(QStringLiteral("Pick colour"), m_colorSection);
    connect(pick, &QPushButton::clicked, this, &LayersPanel::maskPickColorRequested);
    m_swatch = new QLabel(m_colorSection);
    m_swatch->setObjectName(QStringLiteral("swatch"));
    m_swatch->setFixedSize(22, 22);
    auto *pickRow = new QHBoxLayout;
    pickRow->setContentsMargins(0, 0, 0, 0);
    pickRow->addWidget(pick);
    pickRow->addWidget(m_swatch);
    pickRow->addStretch(1);
    colorLayout->addLayout(pickRow);
    m_range = addSlider(colorLayout, QStringLiteral("Range"), 2, 100, &m_rangeValue);
    connect(m_range, &QSlider::valueChanged, this, [this](int v) {
        m_rangeValue->setText(QString::number(v));
        emit maskColorRangeChanged(v);
    });

    // Brush sub-section.
    m_brushSection = new QWidget(m_maskSection);
    auto *brushLayout = new QVBoxLayout(m_brushSection);
    brushLayout->setContentsMargins(0, 0, 0, 0);
    brushLayout->setSpacing(6);
    m_addButton = new QPushButton(QStringLiteral("Add"), m_brushSection);
    m_subButton = new QPushButton(QStringLiteral("Subtract"), m_brushSection);
    m_addButton->setCheckable(true);
    m_subButton->setCheckable(true);
    auto *clear = new QPushButton(QStringLiteral("Clear"), m_brushSection);
    connect(m_addButton, &QPushButton::clicked, this, [this] {
        m_brushAdd = true;
        m_addButton->setChecked(true);
        m_subButton->setChecked(false);
        emitBrush();
    });
    connect(m_subButton, &QPushButton::clicked, this, [this] {
        m_brushAdd = false;
        m_addButton->setChecked(false);
        m_subButton->setChecked(true);
        emitBrush();
    });
    connect(clear, &QPushButton::clicked, this, &LayersPanel::brushClearRequested);
    auto *brushModeRow = new QHBoxLayout;
    brushModeRow->setContentsMargins(0, 0, 0, 0);
    brushModeRow->addWidget(m_addButton);
    brushModeRow->addWidget(m_subButton);
    brushModeRow->addStretch(1);
    brushModeRow->addWidget(clear);
    brushLayout->addLayout(brushModeRow);
    m_brushSize = addSlider(brushLayout, QStringLiteral("Size"), 1, 100, &m_brushSizeValue);
    m_brushHardness = addSlider(brushLayout, QStringLiteral("Hardness"), 1, 100,
                                &m_brushHardnessValue);
    const auto emitBrushSlider = [this](int) {
        m_brushSizeValue->setText(QString::number(m_brushSize->value()));
        m_brushHardnessValue->setText(QString::number(m_brushHardness->value()));
        emitBrush();
    };
    connect(m_brushSize, &QSlider::valueChanged, this, emitBrushSlider);
    connect(m_brushHardness, &QSlider::valueChanged, this, emitBrushSlider);

    // Feather (geometric / luminosity) + Invert.
    m_featherRow = new QWidget(m_maskSection);
    auto *featherLayout = new QVBoxLayout(m_featherRow);
    featherLayout->setContentsMargins(0, 0, 0, 0);
    featherLayout->setSpacing(6);
    m_feather = addSlider(featherLayout, QStringLiteral("Feather"), 0, 100, &m_featherValue);
    connect(m_feather, &QSlider::valueChanged, this, [this](int v) {
        m_featherValue->setText(QStringLiteral("%1%").arg(v));
        emit maskFeatherChanged(v);
    });

    m_invertButton = new QPushButton(QStringLiteral("Invert mask"), m_maskSection);
    m_invertButton->setCheckable(true);
    connect(m_invertButton, &QPushButton::toggled, this,
            [this](bool on) { emit maskInvertChanged(on); });

    maskLayout->addLayout(maskTitleRow);
    maskLayout->addLayout(typeRow1);
    maskLayout->addLayout(typeRow2);
    maskLayout->addWidget(m_lumSection);
    maskLayout->addWidget(m_colorSection);
    maskLayout->addWidget(m_brushSection);
    maskLayout->addWidget(m_featherRow);
    maskLayout->addWidget(m_invertButton);

    auto *layout = new QVBoxLayout(this);
    // Keep the panel sized to its content so sub-sections shown/hidden with the
    // mask type never get compressed (which would clip slider labels).
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(8);
    layout->addWidget(title);
    layout->addWidget(rows);
    layout->addLayout(buttons);
    layout->addLayout(opacityHeader);
    layout->addWidget(m_opacity);
    layout->addWidget(m_maskSection);

    setStyleSheet(QStringLiteral(R"(
        #layersPanel { background: #1c1c1f; border: 1px solid #38383d; border-radius: 10px; }
        #toolTitle { color: #e8e8ea; font-size: 13px; }
        #rowName { color: #b4b4b8; font-size: 12px; }
        #rowValue { color: #d6d6d9; font-size: 12px; }
        #swatch { border: 1px solid #38383d; border-radius: 4px; background: #000; }
        QPushButton {
            background: #2a2a2e; color: #e8e8ea; border: 1px solid #38383d;
            border-radius: 6px; padding: 3px 6px; font-size: 11px;
        }
        QPushButton:hover { background: #34343a; }
        QPushButton[active="true"] { background: #3a3550; border-color: #7F77DD; }
        QPushButton:checked { background: #3a3550; border-color: #7F77DD; }
        QPushButton:disabled { color: #6a6a6e; }
    )"));

    m_addButton->setChecked(true);
    hide();
}

QSlider *LayersPanel::addSlider(QVBoxLayout *layout, const QString &name, int min,
                                int max, QLabel **valueOut)
{
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 4, 0, 0); // breathing room above each row
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
    layout->addLayout(header);
    layout->addWidget(slider);
    *valueOut = valueLabel;
    return slider;
}

void LayersPanel::emitBrush()
{
    emit brushSettingsChanged(m_brushSize->value(), m_brushHardness->value(), m_brushAdd);
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

void LayersPanel::setMaskState(const MaskSpec &mask, bool isBaseActive, int brushSize,
                               int brushHardness, bool brushAdd, int showMode)
{
    // The Base layer has no mask of its own (everything composites onto it).
    m_maskSection->setVisible(!isBaseActive);
    if (isBaseActive) {
        adjustSize();
        return;
    }

    const int type = static_cast<int>(mask.type);
    for (auto *b : m_maskTypeButtons) {
        const QSignalBlocker block(b);
        b->setChecked(b->property("maskType").toInt() == type);
    }

    m_lumSection->setVisible(type == kLum);
    m_colorSection->setVisible(type == kColour);
    m_brushSection->setVisible(type == kBrush);
    // Feather applies to gradient/radial/luminosity (colour uses Range; brush
    // uses Hardness; None has no mask).
    m_featherRow->setVisible(type == kGradient || type == kRadial || type == kLum);
    m_invertButton->setVisible(type != kNone);

    {
        const QSignalBlocker bl(m_low), bh(m_high);
        m_low->setValue(static_cast<int>(std::lround(mask.low * 100)));
        m_high->setValue(static_cast<int>(std::lround(mask.high * 100)));
        m_lowValue->setText(QString::number(m_low->value()));
        m_highValue->setText(QString::number(m_high->value()));
    }
    {
        const QSignalBlocker b(m_range);
        m_range->setValue(static_cast<int>(std::lround(mask.colorRange * 100)));
        m_rangeValue->setText(QString::number(m_range->value()));
    }
    setTargetColor(QColor::fromRgbF(mask.targetR, mask.targetG, mask.targetB));
    {
        const QSignalBlocker b(m_feather);
        m_feather->setValue(static_cast<int>(std::lround(mask.feather * 100)));
        m_featherValue->setText(QStringLiteral("%1%").arg(m_feather->value()));
    }
    {
        const QSignalBlocker b(m_invertButton);
        m_invertButton->setChecked(mask.invert);
    }
    setBrushParams(brushSize, brushHardness);
    m_brushAdd = brushAdd;
    m_addButton->setChecked(brushAdd);
    m_subButton->setChecked(!brushAdd);

    m_showMode = showMode;
    static const char *labels[] = {"Show: Off", "Show: Red", "Show: Gray"};
    m_showButton->setText(QString::fromLatin1(labels[std::clamp(showMode, 0, 2)]));
}

void LayersPanel::setTargetColor(const QColor &color)
{
    m_swatch->setStyleSheet(QStringLiteral("background: %1; border: 1px solid #38383d;"
                                           "border-radius: 4px;")
                                .arg(color.name()));
}

void LayersPanel::setBrushParams(int size, int hardness)
{
    const QSignalBlocker b1(m_brushSize);
    const QSignalBlocker b2(m_brushHardness);
    m_brushSize->setValue(size);
    m_brushHardness->setValue(hardness);
    m_brushSizeValue->setText(QString::number(size));
    m_brushHardnessValue->setText(QString::number(hardness));
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
