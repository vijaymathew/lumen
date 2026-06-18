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
    m_sourceSmall = Image(); // invalidate the cached downsample
    m_sourceSmallDim = 0;
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
    current = applyCrop(current, m_crop); // final geometric stage
    return applyVignette(current, m_vignette); // creative vignette, post-crop
}

Image EditGraph::resultDownsampled(int maxDim)
{
    if (m_source.isNull())
        return Image();
    if (m_sourceSmall.isNull() || m_sourceSmallDim != maxDim) {
        m_sourceSmall = m_source.downscaled(maxDim); // materialised once, then reused
        m_sourceSmallDim = maxDim;
    }
    Image current = m_sourceSmall;
    for (auto &layer : m_layers)
        current = layer->compositeUncached(current);
    current = applyCrop(current, m_crop);
    return applyVignette(current, m_vignette);
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
    if (!m_crop.isIdentity())
        root[QStringLiteral("crop")] = m_crop.toJson();
    if (!m_vignette.isIdentity())
        root[QStringLiteral("vignette")] = m_vignette.toJson();
    return root;
}

void EditGraph::restoreState(const QJsonObject &state)
{
    const QJsonArray layers = state.value(QStringLiteral("layers")).toArray();
    if (layers.isEmpty())
        return; // malformed snapshot — leave the graph as-is

    // The Base layer (index 0) always exists and its node set is fixed, so
    // restore it in place by id — this keeps external pointers into Base nodes
    // (e.g. the heal node) valid. Non-Base layers can be added/removed, so
    // reconcile the list length and rebuild their node chains from the snapshot.
    m_layers.resize(1); // drop any current non-Base layers; rebuild below
    m_layers[0]->restoreState(layers[0].toObject());
    for (int i = 1; i < layers.size(); ++i) {
        auto layer = std::make_unique<Layer>(QStringLiteral("Layer"));
        layer->restoreStructure(layers[i].toObject());
        m_layers.push_back(std::move(layer));
    }

    m_activeLayer = state.value(QStringLiteral("active")).toInt(0);
    if (m_activeLayer < 0 || m_activeLayer >= static_cast<int>(m_layers.size()))
        m_activeLayer = 0;

    m_crop = state.contains(QStringLiteral("crop"))
                 ? CropState::fromJson(state.value(QStringLiteral("crop")).toObject())
                 : CropState{};
    m_vignette = state.contains(QStringLiteral("vignette"))
                     ? VignetteParams::fromJson(state.value(QStringLiteral("vignette")).toObject())
                     : VignetteParams{};
}

void EditGraph::loadProjectState(const QJsonObject &state)
{
    const QJsonArray layers = state.value(QStringLiteral("layers")).toArray();
    if (layers.isEmpty())
        return; // malformed manifest — leave the graph as-is

    // Base layer (index 0): restore by node type so it lands on this session's
    // existing Base nodes (their ids differ from the saved file). Non-Base layers
    // are rebuilt structurally from the manifest.
    m_layers.resize(1);
    m_layers[0]->restoreByType(layers[0].toObject());
    for (int i = 1; i < layers.size(); ++i) {
        auto layer = std::make_unique<Layer>(QStringLiteral("Layer"));
        layer->restoreStructure(layers[i].toObject());
        m_layers.push_back(std::move(layer));
    }

    m_activeLayer = state.value(QStringLiteral("active")).toInt(0);
    if (m_activeLayer < 0 || m_activeLayer >= static_cast<int>(m_layers.size()))
        m_activeLayer = 0;

    for (auto &layer : m_layers)
        layer->invalidateFrom(0); // recompute against the loaded source

    m_crop = state.contains(QStringLiteral("crop"))
                 ? CropState::fromJson(state.value(QStringLiteral("crop")).toObject())
                 : CropState{};
    m_vignette = state.contains(QStringLiteral("vignette"))
                     ? VignetteParams::fromJson(state.value(QStringLiteral("vignette")).toObject())
                     : VignetteParams{};
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
