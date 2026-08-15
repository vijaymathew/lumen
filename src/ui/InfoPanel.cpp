#include "ui/InfoPanel.h"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace {
constexpr int kPanelWidth = 320;
}

InfoPanel::InfoPanel(QWidget *parent)
    : FloatingToolPanel(QStringLiteral("infoPanel"), QStringLiteral("Image info"), kPanelWidth,
                        parent)
{
    m_empty = new QLabel(QStringLiteral("No image open"), this);
    m_empty->setObjectName(QStringLiteral("rowName"));

    m_grid = new QGridLayout;
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(14);
    m_grid->setVerticalSpacing(6);
    m_grid->setColumnStretch(1, 1); // let the value column take the slack

    contentLayout()->addWidget(m_empty);
    contentLayout()->addLayout(m_grid);

    appendStyleSheet(QStringLiteral(R"(
        #infoKey { color: #8a8a90; font-size: 12px; }
        #infoValue { color: #e2e2e5; font-size: 12px; }
    )"));
}

void InfoPanel::clearRows()
{
    for (QWidget *w : m_cells)
        w->deleteLater();
    m_cells.clear();
}

void InfoPanel::setRows(const QVector<Row> &rows)
{
    clearRows();
    m_empty->setVisible(rows.isEmpty());

    for (int i = 0; i < rows.size(); ++i) {
        auto *key = new QLabel(rows[i].label, this);
        key->setObjectName(QStringLiteral("infoKey"));
        key->setAlignment(Qt::AlignTop | Qt::AlignLeft);

        auto *val = new QLabel(rows[i].value, this);
        val->setObjectName(QStringLiteral("infoValue"));
        val->setWordWrap(true); // long paths wrap rather than widen the card
        val->setTextInteractionFlags(Qt::TextSelectableByMouse);
        val->setAlignment(Qt::AlignTop | Qt::AlignLeft);

        m_grid->addWidget(key, i, 0);
        m_grid->addWidget(val, i, 1);
        key->show();
        val->show();
        m_cells.push_back(key);
        m_cells.push_back(val);
    }
    adjustSize();
}

void InfoPanel::reveal()
{
    show();
    raise();
}
