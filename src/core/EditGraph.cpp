#include "core/EditGraph.h"

#include "core/EditNode.h"

#include <algorithm>

EditGraph::EditGraph() = default;
EditGraph::~EditGraph() = default;

void EditGraph::setSource(const Image &source)
{
    m_source = source;
    // The input to node 0 changed, so every node must recompute.
    invalidateFrom(0);
}

EditNode *EditGraph::addNode(std::unique_ptr<EditNode> node)
{
    EditNode *raw = node.get();
    m_nodes.push_back(std::move(node));
    m_cache.emplace_back(); // empty cache slot; the node starts dirty
    return raw;
}

bool EditGraph::removeNode(const QString &id)
{
    const int idx = indexOf(id);
    if (idx < 0)
        return false;
    m_nodes.erase(m_nodes.begin() + idx);
    m_cache.erase(m_cache.begin() + idx);
    // Everything from idx onward now receives a different input.
    invalidateFrom(idx);
    return true;
}

bool EditGraph::moveNode(int from, int to)
{
    const int n = static_cast<int>(m_nodes.size());
    if (from < 0 || from >= n || to < 0 || to >= n)
        return false;
    if (from == to)
        return true;

    auto node = std::move(m_nodes[from]);
    m_nodes.erase(m_nodes.begin() + from);
    m_nodes.insert(m_nodes.begin() + to, std::move(node));

    // The cache no longer lines up with node order; rebuild it empty and
    // invalidate from the earliest affected index.
    m_cache.assign(m_nodes.size(), Image());
    invalidateFrom(std::min(from, to));
    return true;
}

EditNode *EditGraph::nodeAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_nodes.size()))
        return nullptr;
    return m_nodes[index].get();
}

EditNode *EditGraph::findNode(const QString &id) const
{
    const int idx = indexOf(id);
    return idx < 0 ? nullptr : m_nodes[idx].get();
}

void EditGraph::invalidateFrom(int index)
{
    if (index < 0)
        index = 0;
    for (int i = index; i < static_cast<int>(m_nodes.size()); ++i) {
        m_nodes[i]->markDirty();
        if (i < static_cast<int>(m_cache.size()))
            m_cache[i] = Image();
    }
}

Image EditGraph::result()
{
    Image current = m_source;
    if (current.isNull())
        return current;

    // Once any node recomputes, its output changed, so every node after it must
    // recompute too even if its own dirty flag was clear.
    bool recomputeRest = false;

    for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i) {
        EditNode *node = m_nodes[i].get();
        const bool needsCompute =
            recomputeRest || node->isDirty() || m_cache[i].isNull();

        if (needsCompute) {
            current = node->isEnabled() ? node->apply(current) : current;
            m_cache[i] = current;
            node->clearDirty();
            recomputeRest = true;
        } else {
            current = m_cache[i];
        }
    }
    return current;
}

PreviewState EditGraph::previewState() const
{
    PreviewState state;
    for (const auto &node : m_nodes) {
        if (node->isEnabled())
            node->contributeToPreview(state);
    }
    return state;
}

int EditGraph::indexOf(const QString &id) const
{
    for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i) {
        if (m_nodes[i]->id() == id)
            return i;
    }
    return -1;
}
