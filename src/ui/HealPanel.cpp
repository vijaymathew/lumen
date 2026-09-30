#include "ui/HealPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace {
constexpr int kPanelWidth = 248;
}

HealPanel::HealPanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("healPanel"), QStringLiteral("Heal & Retouch"), kPanelWidth, parent)
{
    // Mode switch: heal (remove) vs retouch (paint a picked colour).
    m_healModeButton = new QPushButton(QStringLiteral("Heal"), this);
    m_retouchModeButton = new QPushButton(QStringLiteral("Retouch"), this);
    m_healModeButton->setCheckable(true);
    m_retouchModeButton->setCheckable(true);
    m_healModeButton->setChecked(true);
    connect(m_healModeButton, &QPushButton::clicked, this, [this] {
        const bool changed = m_retouch;
        applyMode(false);
        if (changed)
            emit modeChanged(false);
    });
    connect(m_retouchModeButton, &QPushButton::clicked, this, [this] {
        const bool changed = !m_retouch;
        applyMode(true);
        if (changed)
            emit modeChanged(true);
    });
    auto *tabs = new QHBoxLayout;
    tabs->setContentsMargins(0, 0, 0, 0);
    tabs->addWidget(m_healModeButton);
    tabs->addWidget(m_retouchModeButton);
    tabs->addStretch(1);
    contentLayout()->addLayout(tabs);

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
    contentLayout()->addLayout(modeRow);

    m_size = addBrushRow(QStringLiteral("Size"), 30, &m_sizeValue);
    m_size->setToolTip(QStringLiteral("Hold S and scroll the wheel over the image"));
    m_hardness = addBrushRow(QStringLiteral("Hardness"), 50, &m_hardnessValue);
    m_hardness->setToolTip(QStringLiteral("Hold H and scroll the wheel over the image"));

    // Fill quality: Detailed (Criminisi exemplar) vs Fast (Telea diffusion).
    m_qualityButton = new QPushButton(QStringLiteral("Fill: Detailed"), this);
    connect(m_qualityButton, &QPushButton::clicked, this, [this] {
        m_highQuality = !m_highQuality;
        m_qualityButton->setText(m_highQuality ? QStringLiteral("Fill: Detailed")
                                               : QStringLiteral("Fill: Fast"));
        emit qualityChanged(m_highQuality);
    });
    auto *qualityRow = new QHBoxLayout;
    qualityRow->setContentsMargins(0, 0, 0, 0);
    qualityRow->addWidget(m_qualityButton);
    qualityRow->addStretch(1);
    contentLayout()->addLayout(qualityRow);

    // Retouch: pick a colour from the image, then paint it elsewhere.
    m_retouchRow = new QWidget(this);
    auto *pickRow = new QHBoxLayout(m_retouchRow);
    pickRow->setContentsMargins(0, 0, 0, 0);
    m_pickButton = new QPushButton(QStringLiteral("Pick colour"), m_retouchRow);
    m_pickButton->setToolTip(QStringLiteral("Click a spot in the image to sample its colour"));
    connect(m_pickButton, &QPushButton::clicked, this, &HealPanel::pickColourRequested);
    m_swatch = new QLabel(m_retouchRow);
    m_swatch->setFixedSize(28, 18);
    pickRow->addWidget(m_pickButton);
    pickRow->addWidget(m_swatch);
    pickRow->addStretch(1);
    contentLayout()->addWidget(m_retouchRow);

    m_opacityBox = new QWidget(this);
    auto *opacityLayout = new QVBoxLayout(m_opacityBox);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->setSpacing(contentLayout()->spacing());
    auto *opacityHeader = new QHBoxLayout;
    opacityHeader->setContentsMargins(0, 0, 0, 0);
    auto *opacityName = new QLabel(QStringLiteral("Opacity"), m_opacityBox);
    opacityName->setObjectName(QStringLiteral("rowName"));
    m_opacityValue = new QLabel(QStringLiteral("100"), m_opacityBox);
    m_opacityValue->setObjectName(QStringLiteral("rowValue"));
    m_opacityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    opacityHeader->addWidget(opacityName);
    opacityHeader->addStretch(1);
    opacityHeader->addWidget(m_opacityValue);
    m_opacity = new QSlider(Qt::Horizontal, m_opacityBox);
    m_opacity->setRange(1, 100);
    m_opacity->setValue(100);
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        m_opacityValue->setText(QString::number(v));
        emit opacityChanged(v);
    });
    opacityLayout->addLayout(opacityHeader);
    opacityLayout->addWidget(m_opacity);
    contentLayout()->addWidget(m_opacityBox);

    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("section"));
    m_hint->setWordWrap(true);
    contentLayout()->addWidget(m_hint);
    setRetouchColour(QColor());
    applyMode(false);

    appendStyleSheet(QStringLiteral(R"(
        #section { color: #c4c4c9; font-size: 12px; }
        QPushButton { padding: 2px 8px; font-size: 11px; }
    )"));
}

// Deliberately not FloatingToolPanel::addRow(): that installs an event filter
// that consumes Esc/Return/Enter to emit closed() — which HealPanel doesn't
// have. It closes via the normal keyPress bubbling to MainWindow instead
// (there's no per-tool closed() signal for it to intercept and eat), so its
// sliders must not swallow that key first.
QSlider *HealPanel::addBrushRow(const QString &name, int def, QLabel **valueOut)
{
    auto *header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    auto *nameLabel = new QLabel(name, this);
    nameLabel->setObjectName(QStringLiteral("rowName"));
    auto *valueLabel = new QLabel(QString::number(def), this);
    valueLabel->setObjectName(QStringLiteral("rowValue"));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(nameLabel);
    header->addStretch(1);
    header->addWidget(valueLabel);

    auto *slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(1, 100);
    slider->setValue(def);
    connect(slider, &QSlider::valueChanged, this, [this, valueLabel](int v) {
        valueLabel->setText(QString::number(v));
        emitSettings();
    });

    contentLayout()->addLayout(header);
    contentLayout()->addWidget(slider);
    *valueOut = valueLabel;
    return slider;
}

void HealPanel::applyMode(bool retouch)
{
    m_retouch = retouch;
    m_healModeButton->setChecked(!retouch);
    m_retouchModeButton->setChecked(retouch);
    m_qualityButton->setVisible(!retouch);
    m_retouchRow->setVisible(retouch);
    m_opacityBox->setVisible(retouch);
    m_hint->setText(retouch
        ? QStringLiteral("pick a colour, then paint it over another region · S / H + scroll "
                         "resize the brush · Ctrl+Z undoes a stroke")
        : QStringLiteral("paint over a blemish · S / H + scroll resize the brush · "
                         "Ctrl+Z undoes a stroke"));
    // A wrapped label's height depends on its width; reserve it explicitly so the
    // text isn't clipped when the panel resizes for the mode.
    m_hint->ensurePolished(); // pick up the stylesheet font before measuring
    m_hint->setMinimumHeight(m_hint->heightForWidth(kPanelWidth - 32));
    layout()->activate();
    adjustSize();
}

void HealPanel::setRetouchColour(const QColor &colour)
{
    m_swatch->setStyleSheet(
        colour.isValid()
            ? QStringLiteral("background: %1; border: 1px solid #55555c; border-radius: 3px;")
                  .arg(colour.name())
            : QStringLiteral("background: transparent; border: 1px dashed #55555c; "
                             "border-radius: 3px;"));
    m_swatch->setToolTip(colour.isValid() ? colour.name().toUpper()
                                          : QStringLiteral("No colour picked yet"));
}

void HealPanel::emitSettings()
{
    emit settingsChanged(m_size->value(), m_hardness->value(), m_add);
}

void HealPanel::reveal(int size, int hardness, bool add, bool highQuality, bool retouch,
                       const QColor &colour, int opacity)
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
    m_highQuality = highQuality;
    m_qualityButton->setText(highQuality ? QStringLiteral("Fill: Detailed")
                                         : QStringLiteral("Fill: Fast"));
    {
        const QSignalBlocker b(m_opacity);
        m_opacity->setValue(opacity);
        m_opacityValue->setText(QString::number(opacity));
    }
    setRetouchColour(colour);
    applyMode(retouch); // also adjusts the size
    show();
    raise();
    setFocus(Qt::ShortcutFocusReason);
}

void HealPanel::setBrushParams(int size, int hardness)
{
    const QSignalBlocker b1(m_size);
    const QSignalBlocker b2(m_hardness);
    m_size->setValue(size);
    m_hardness->setValue(hardness);
    m_sizeValue->setText(QString::number(size));
    m_hardnessValue->setText(QString::number(hardness));
}
