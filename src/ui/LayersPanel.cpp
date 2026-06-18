#include "ui/LayersPanel.h"

#include <QBoxLayout>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
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

    // One-tap tonal presets: set the luminosity range (plus a generous feather so
    // the band doesn't posterise) to Shadows / Midtones / Highlights. The sliders
    // stay live for fine-tuning afterwards.
    const auto applyLumPreset = [this](int low, int high, int feather) {
        {
            const QSignalBlocker bl(m_low), bh(m_high);
            m_low->setValue(low);
            m_high->setValue(high);
        }
        m_lowValue->setText(QString::number(low));
        m_highValue->setText(QString::number(high));
        emit maskRangeChanged(low, high);
        m_feather->setValue(feather); // emits maskFeatherChanged + syncs its label
    };
    struct LumPreset { const char *label; int low, high, feather; };
    auto *presetRow = new QHBoxLayout;
    presetRow->setContentsMargins(0, 0, 0, 0);
    for (const LumPreset &p : {LumPreset{"Shadows", 0, 35, 40},
                               LumPreset{"Midtones", 25, 75, 40},
                               LumPreset{"Highlights", 65, 100, 40}}) {
        auto *b = new QPushButton(QString::fromLatin1(p.label), m_lumSection);
        connect(b, &QPushButton::clicked, this,
                [applyLumPreset, p] { applyLumPreset(p.low, p.high, p.feather); });
        presetRow->addWidget(b);
    }
    lumLayout->insertLayout(0, presetRow); // above the Range low/high sliders

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

    // ---- Exclusive-zone sub-section --------------------------------------
    // Shapes that gate where the mask may act (outside = excluded). Always
    // available for a non-Base layer, independent of the mask type.
    m_zoneSection = new QWidget(m_maskSection);
    auto *zoneLayout = new QVBoxLayout(m_zoneSection);
    zoneLayout->setContentsMargins(0, 0, 0, 0);
    zoneLayout->setSpacing(6);

    auto *zoneTitleRow = new QHBoxLayout;
    zoneTitleRow->setContentsMargins(0, 0, 0, 0);
    auto *zoneTitle = new QLabel(QStringLiteral("Zone"), m_zoneSection);
    zoneTitle->setObjectName(QStringLiteral("rowName"));
    m_zoneCount = new QLabel(QString(), m_zoneSection);
    m_zoneCount->setObjectName(QStringLiteral("rowValue"));
    // Toggle the on-canvas overlay geometry (zone shapes + mask gizmo). Checked =
    // shown. Hidden is view-only; the mask effect still renders.
    m_overlayShowButton = new QPushButton(QStringLiteral("Show"), m_zoneSection);
    m_overlayShowButton->setCheckable(true);
    m_overlayShowButton->setChecked(true);
    m_overlayShowButton->setToolTip(QStringLiteral("Show/hide the overlay shapes (H)"));
    connect(m_overlayShowButton, &QPushButton::toggled, this,
            &LayersPanel::overlayVisibilityChanged);
    zoneTitleRow->addWidget(zoneTitle);
    zoneTitleRow->addStretch(1);
    zoneTitleRow->addWidget(m_overlayShowButton);
    zoneTitleRow->addWidget(m_zoneCount);
    zoneLayout->addLayout(zoneTitleRow);

    // Tool buttons (mutually exclusive). Index doubles as the tool id.
    const struct { const char *label; } kTools[] = {
        {"Select"}, {"Rect"}, {"Oval"}, {"Circle"}, {"Free"}};
    auto *zoneToolRow1 = new QHBoxLayout;
    auto *zoneToolRow2 = new QHBoxLayout;
    zoneToolRow1->setContentsMargins(0, 0, 0, 0);
    zoneToolRow2->setContentsMargins(0, 0, 0, 0);
    zoneToolRow1->setSpacing(4);
    zoneToolRow2->setSpacing(4);
    int zt = 0;
    for (const auto &t : kTools) {
        auto *b = new QPushButton(QString::fromLatin1(t.label), m_zoneSection);
        b->setCheckable(true);
        connect(b, &QPushButton::clicked, this, [this, tool = zt] {
            for (int i = 0; i < m_zoneToolButtons.size(); ++i)
                m_zoneToolButtons[i]->setChecked(i == tool);
            emit zoneToolChanged(tool);
        });
        (zt < 3 ? zoneToolRow1 : zoneToolRow2)->addWidget(b);
        m_zoneToolButtons.push_back(b);
        ++zt;
    }
    m_zoneToolButtons[0]->setChecked(true); // Select by default
    zoneLayout->addLayout(zoneToolRow1);
    zoneLayout->addLayout(zoneToolRow2);

    // Add / Subtract mode for newly drawn shapes + Clear.
    m_zoneAddButton = new QPushButton(QStringLiteral("Add"), m_zoneSection);
    m_zoneSubButton = new QPushButton(QStringLiteral("Subtract"), m_zoneSection);
    m_zoneAddButton->setCheckable(true);
    m_zoneSubButton->setCheckable(true);
    m_zoneAddButton->setChecked(true);
    auto *zoneClear = new QPushButton(QStringLiteral("Clear"), m_zoneSection);
    connect(m_zoneAddButton, &QPushButton::clicked, this, [this] {
        m_zoneSubtract = false;
        m_zoneAddButton->setChecked(true);
        m_zoneSubButton->setChecked(false);
        emit zoneModeChanged(false);
    });
    connect(m_zoneSubButton, &QPushButton::clicked, this, [this] {
        m_zoneSubtract = true;
        m_zoneAddButton->setChecked(false);
        m_zoneSubButton->setChecked(true);
        emit zoneModeChanged(true);
    });
    connect(zoneClear, &QPushButton::clicked, this, &LayersPanel::zoneClearRequested);
    auto *zoneModeRow = new QHBoxLayout;
    zoneModeRow->setContentsMargins(0, 0, 0, 0);
    zoneModeRow->addWidget(m_zoneAddButton);
    zoneModeRow->addWidget(m_zoneSubButton);
    zoneModeRow->addStretch(1);
    zoneModeRow->addWidget(zoneClear);
    zoneLayout->addLayout(zoneModeRow);

    m_zoneFeather = addSlider(zoneLayout, QStringLiteral("Zone feather"), 0, 100,
                              &m_zoneFeatherValue);
    connect(m_zoneFeather, &QSlider::valueChanged, this, [this](int v) {
        m_zoneFeatherValue->setText(QStringLiteral("%1%").arg(v));
        emit zoneFeatherChanged(v);
    });

    maskLayout->addLayout(maskTitleRow);
    maskLayout->addLayout(typeRow1);
    maskLayout->addLayout(typeRow2);
    maskLayout->addWidget(m_lumSection);
    maskLayout->addWidget(m_colorSection);
    maskLayout->addWidget(m_brushSection);
    maskLayout->addWidget(m_featherRow);
    maskLayout->addWidget(m_invertButton);
    maskLayout->addWidget(m_zoneSection);

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
        // Non-Base layers can be renamed by double-clicking the name (handled in
        // eventFilter, which reads the stashed index).
        if (i > 0) {
            name->setProperty("layerIndex", i);
            name->setToolTip(QStringLiteral("Double-click to rename"));
            name->installEventFilter(this);
        }

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

    // Zone sub-section: shape count + feather; reset the tool to Select so a
    // draw tool never stays armed across a layer/mask switch.
    {
        const int n = static_cast<int>(mask.zones.size());
        m_zoneCount->setText(n == 0 ? QStringLiteral("none")
                                    : QStringLiteral("%1 shape%2").arg(n).arg(n == 1 ? "" : "s"));
        const QSignalBlocker b(m_zoneFeather);
        m_zoneFeather->setValue(static_cast<int>(std::lround(mask.zoneFeather * 100)));
        m_zoneFeatherValue->setText(QStringLiteral("%1%").arg(m_zoneFeather->value()));
        for (int i = 0; i < m_zoneToolButtons.size(); ++i)
            m_zoneToolButtons[i]->setChecked(i == 0);
    }

    m_showMode = showMode;
    static const char *labels[] = {"Show: Off", "Show: Red", "Show: Gray"};
    m_showButton->setText(QString::fromLatin1(labels[std::clamp(showMode, 0, 2)]));
}

void LayersPanel::setZoneCount(int n)
{
    if (!m_zoneCount)
        return;
    m_zoneCount->setText(n == 0 ? QStringLiteral("none")
                                : QStringLiteral("%1 shape%2").arg(n).arg(n == 1 ? "" : "s"));
}

void LayersPanel::resetZoneTool()
{
    for (int i = 0; i < m_zoneToolButtons.size(); ++i)
        m_zoneToolButtons[i]->setChecked(i == 0);
}

void LayersPanel::setOverlayVisible(bool visible)
{
    // Reflect the state (e.g. toggled by the H shortcut) without re-emitting.
    QSignalBlocker block(m_overlayShowButton);
    m_overlayShowButton->setChecked(visible);
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

bool LayersPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (auto *btn = qobject_cast<QPushButton *>(watched)) {
            const QVariant idx = btn->property("layerIndex");
            if (idx.isValid()) {
                beginRename(btn, idx.toInt());
                return true; // consume; don't also treat as a click
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LayersPanel::beginRename(QPushButton *nameButton, int index)
{
    auto *row = nameButton->parentWidget();
    auto *lay = row ? qobject_cast<QBoxLayout *>(row->layout()) : nullptr;
    if (!lay)
        return;

    auto *edit = new QLineEdit(nameButton->text(), row);
    edit->setObjectName(QStringLiteral("layerNameEdit"));
    lay->replaceWidget(nameButton, edit);
    lay->setStretchFactor(edit, 1);
    nameButton->hide();
    edit->selectAll();
    edit->setFocus(Qt::MouseFocusReason);

    // editingFinished fires on Enter and on focus-out; guard the double-fire.
    connect(edit, &QLineEdit::editingFinished, this, [this, edit, index] {
        if (edit->property("done").toBool())
            return;
        edit->setProperty("done", true);
        emit renameRequested(index, edit->text().trimmed());
        // MainWindow responds with refreshLayersPanel(), which rebuilds the rows
        // and disposes of this temporary editor.
    });
}
