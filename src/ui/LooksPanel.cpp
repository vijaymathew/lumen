#include "ui/LooksPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr int kPanelWidth = 280;
}

LooksPanel::LooksPanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("looksPanel"), QStringLiteral("Looks"), kPanelWidth,
                        parent)
{
    m_name = new QLabel(this);
    m_name->setObjectName(QStringLiteral("lookName"));
    m_name->setWordWrap(false);

    auto *load = new QPushButton(QStringLiteral("Load…"), this);
    auto *clear = new QPushButton(QStringLiteral("Clear"), this);
    connect(load, &QPushButton::clicked, this, &LooksPanel::loadRequested);
    connect(clear, &QPushButton::clicked, this, &LooksPanel::clearRequested);

    auto *fileRow = new QHBoxLayout;
    fileRow->setContentsMargins(0, 0, 0, 0);
    fileRow->addWidget(m_name, 1);
    fileRow->addWidget(load);
    fileRow->addWidget(clear);
    contentLayout()->addLayout(fileRow);

    auto *intLabel = new QLabel(QStringLiteral("Intensity"), this);
    intLabel->setObjectName(QStringLiteral("rowName"));
    m_intensityValue = new QLabel(this);
    m_intensityValue->setObjectName(QStringLiteral("rowValue"));
    m_intensityValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *intHeader = new QHBoxLayout;
    intHeader->setContentsMargins(0, 0, 0, 0);
    intHeader->addWidget(intLabel);
    intHeader->addStretch(1);
    intHeader->addWidget(m_intensityValue);
    contentLayout()->addLayout(intHeader);

    // Hand-built (not FloatingToolPanel::addRow()): this panel has no closed()
    // signal, it closes via the normal keyPress bubbling to MainWindow, so its
    // slider must not install an event filter that would eat Esc/Enter first.
    m_intensity = new QSlider(Qt::Horizontal, this);
    m_intensity->setRange(0, 100);
    connect(m_intensity, &QSlider::valueChanged, this, &LooksPanel::onSliderChanged);
    contentLayout()->addWidget(m_intensity);

    appendStyleSheet(QStringLiteral("#lookName { color: #b4b4b8; font-size: 12px; }"));

    setLookName(QString());
}

void LooksPanel::setLookName(const QString &name)
{
    m_name->setText(name.isEmpty() ? QStringLiteral("No look loaded") : name);
}

void LooksPanel::reveal(const QString &lookName, double intensity)
{
    setLookName(lookName);
    {
        const QSignalBlocker block(m_intensity);
        m_intensity->setValue(static_cast<int>(std::lround(intensity * 100.0)));
        onSliderChanged(m_intensity->value()); // refresh label without emitting
    }
    adjustSize();
    show();
    raise();
    m_intensity->setFocus(Qt::ShortcutFocusReason);
}

void LooksPanel::onSliderChanged(int value)
{
    m_intensityValue->setText(QStringLiteral("%1%").arg(value));
    if (!m_intensity->signalsBlocked())
        emit intensityChanged(value / 100.0);
}
