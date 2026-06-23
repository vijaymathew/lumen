#pragma once

#include "core/Image.h"
#include "core/Layer.h"
#include "core/Lut.h"
#include "core/Lut3D.h"
#include "core/PreviewState.h"

#include <QJsonObject>
#include <QString>

#include <memory>
#include <vector>

class EditNode;

// EditGraph is the non-destructive pipeline: a source image plus an ordered list
// of composited Layers (LAYERS.md). result() composites the layers via libvips;
// a Base layer (full-coverage mask) always exists and holds the global edit, so
// a single-layer project reduces to the old linear node chain.
//
// Node-level operations (addNode, etc.) delegate to the *active* layer (the Base
// layer by default), so existing callers and the GPU preview are unchanged while
// the multi-pass preview is built (step 3).
//
// Ownership: the graph owns its layers and their nodes. Accessors are non-owning.
class EditGraph {
public:
    EditGraph();
    ~EditGraph();

    EditGraph(const EditGraph &) = delete;
    EditGraph &operator=(const EditGraph &) = delete;

    // Sets the input image. Invalidates the pipeline.
    void setSource(const Image &source);
    const Image &source() const { return m_source; }

    // --- Layers ------------------------------------------------------------
    int layerCount() const { return static_cast<int>(m_layers.size()); }
    Layer &layer(int index) const { return *m_layers[index]; }
    Layer &baseLayer() const { return *m_layers.front(); }
    Layer &activeLayer() const { return *m_layers[m_activeLayer]; }
    int activeLayerIndex() const { return m_activeLayer; }
    void setActiveLayer(int index);
    Layer &addLayer(const QString &name = QStringLiteral("Layer"));
    bool removeLayer(int index); // index 0 (Base) cannot be removed

    // --- Node ops (delegate to the active layer) ---------------------------
    EditNode *addNode(std::unique_ptr<EditNode> node);
    bool removeNode(const QString &id);
    int nodeCount() const { return activeLayer().nodeCount(); }
    EditNode *nodeAt(int index) const { return activeLayer().nodeAt(index); }

    // Composites the layers at full resolution (libvips export path). Returns
    // the source if there are no edits, or a null Image if no source is set.
    Image result();

    // GPU preview parameters from the Base layer (single-pass; the multi-pass
    // chain over all layers is step 3).
    PreviewState previewState() const;
    ChannelLuts previewLut() const;
    Lut3D previewLook() const;

    // --- Undo/redo (snapshots of the whole layer list) ---------------------
    void resetHistory();
    void commit();
    bool canUndo() const;
    bool canRedo() const;
    bool undo();
    bool redo();

    QJsonObject saveState() const;
    void restoreState(const QJsonObject &state);

private:
    Image m_source;
    std::vector<std::unique_ptr<Layer>> m_layers; // [0] = Base, always present
    int m_activeLayer = 0;

    std::vector<QJsonObject> m_history;
    int m_historyIndex = -1;
};
