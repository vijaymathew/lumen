#include "core/EditGraph.h"

#include "core/EditNode.h"

#include <QJsonArray>

#include <algorithm>

EditGraph::EditGraph()
{
    // The Base layer always exists: full-coverage mask, holds the global edit.
    m_layers.push_back(std::make_unique<Layer>(QStringLiteral("Base")));
}

EditGraph::~EditGraph() = default;

void EditGraph::setSource(const Image &source)
{
    m_source = source;
    // The pipeline input changed: every layer must recompute from node 0.
    for (auto &layer : m_layers)
        layer->invalidateFrom(0);
}

void EditGraph::setActiveLayer(int index)
{
    if (index >= 0 && index < static_cast<int>(m_layers.size()))
        m_activeLayer = index;
}

Layer &EditGraph::addLayer(const QString &name)
{
    m_layers.push_back(std::make_unique<Layer>(name));
    m_activeLayer = static_cast<int>(m_layers.size()) - 1;
    return *m_layers.back();
}

bool EditGraph::removeLayer(int index)
{
    if (index <= 0 || index >= static_cast<int>(m_layers.size()))
        return false; // index 0 is the Base layer
    m_layers.erase(m_layers.begin() + index);
    if (m_activeLayer >= static_cast<int>(m_layers.size()))
        m_activeLayer = static_cast<int>(m_layers.size()) - 1;
    return true;
}

EditNode *EditGraph::addNode(std::unique_ptr<EditNode> node)
{
    return activeLayer().addNode(std::move(node));
}

bool EditGraph::removeNode(const QString &id)
{
    return activeLayer().removeNode(id);
}

Image EditGraph::result()
{
    Image current = m_source;
    if (current.isNull())
        return current;
    for (auto &layer : m_layers)
        current = layer->composite(current);
    return current;
}

PreviewState EditGraph::previewState() const
{
    PreviewState state;
    baseLayer().contributeToPreview(state);
    return state;
}

ChannelLuts EditGraph::previewLut() const
{
    ChannelLuts luts = identityChannelLuts();
    baseLayer().contributeToPreviewLut(luts);
    return luts;
}

Lut3D EditGraph::previewLook() const
{
    return baseLayer().look();
}

QJsonObject EditGraph::saveState() const
{
    QJsonObject root;
    QJsonArray layers;
    for (const auto &layer : m_layers)
        layers.append(layer->saveState());
    root[QStringLiteral("layers")] = layers;
    root[QStringLiteral("active")] = m_activeLayer;
    return root;
}

void EditGraph::restoreState(const QJsonObject &state)
{
    // Structure is stable in this step (only the Base layer is created by the
    // app), so restore properties/params into the existing layers by index —
    // keeping external node pointers valid. Structural undo arrives later.
    const QJsonArray layers = state.value(QStringLiteral("layers")).toArray();
    for (int i = 0; i < static_cast<int>(m_layers.size()) && i < layers.size(); ++i)
        m_layers[i]->restoreState(layers[i].toObject());
    m_activeLayer = state.value(QStringLiteral("active")).toInt(0);
    if (m_activeLayer < 0 || m_activeLayer >= static_cast<int>(m_layers.size()))
        m_activeLayer = 0;
}

void EditGraph::resetHistory()
{
    m_history.clear();
    m_history.push_back(saveState());
    m_historyIndex = 0;
}

void EditGraph::commit()
{
    QJsonObject state = saveState();
    if (m_historyIndex >= 0 && m_history[m_historyIndex] == state)
        return; // nothing changed since the current snapshot

    if (m_historyIndex + 1 < static_cast<int>(m_history.size()))
        m_history.erase(m_history.begin() + m_historyIndex + 1, m_history.end());
    m_history.push_back(std::move(state));
    m_historyIndex = static_cast<int>(m_history.size()) - 1;
}

bool EditGraph::canUndo() const
{
    return m_historyIndex > 0;
}

bool EditGraph::canRedo() const
{
    return m_historyIndex >= 0
        && m_historyIndex + 1 < static_cast<int>(m_history.size());
}

bool EditGraph::undo()
{
    if (!canUndo())
        return false;
    --m_historyIndex;
    restoreState(m_history[m_historyIndex]);
    return true;
}

bool EditGraph::redo()
{
    if (!canRedo())
        return false;
    ++m_historyIndex;
    restoreState(m_history[m_historyIndex]);
    return true;
}
