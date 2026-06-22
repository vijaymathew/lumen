#include "core/EditNode.h"

#include <QUuid>

#include <utility>

EditNode::EditNode(QString typeName)
    : m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_typeName(std::move(typeName))
{
}

EditNode::~EditNode() = default;

void EditNode::setEnabled(bool enabled)
{
    if (m_enabled != enabled) {
        m_enabled = enabled;
        m_dirty = true; // toggling enable/disable changes this node's output
    }
}

QJsonObject EditNode::saveState() const
{
    QJsonObject state;
    state[QStringLiteral("enabled")] = m_enabled;
    return state;
}

void EditNode::restoreState(const QJsonObject &state)
{
    setEnabled(state.value(QStringLiteral("enabled")).toBool(true));
}
