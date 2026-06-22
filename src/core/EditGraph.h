#pragma once

#include "core/Image.h"
#include "core/Lut.h"
#include "core/Lut3D.h"
#include "core/PreviewState.h"

#include <QJsonArray>
#include <QString>

#include <memory>
#include <vector>

class EditNode;

// EditGraph is the non-destructive pipeline: a source image plus an ordered list
// of EditNodes. result() walks the nodes, transforming the image step by step.
//
// Evaluation is lazy and cached: each node's output is cached, and only dirty
// nodes (and everything downstream of them) are recomputed. This is the core of
// the dirty-flag / cache-invalidation system (DESIGN.md §5.1).
//
// Ownership: the graph owns its nodes. Accessors return non-owning pointers.
class EditGraph {
public:
    EditGraph();
    ~EditGraph();

    EditGraph(const EditGraph &) = delete;
    EditGraph &operator=(const EditGraph &) = delete;

    // Sets the input image. Invalidates all node caches.
    void setSource(const Image &source);
    const Image &source() const { return m_source; }

    // Appends a node and returns a non-owning pointer to it.
    EditNode *addNode(std::unique_ptr<EditNode> node);

    // Removes the node with `id`. Returns true if it existed.
    bool removeNode(const QString &id);

    // Moves the node at `from` to index `to`. Returns false on bad indices.
    bool moveNode(int from, int to);

    int nodeCount() const { return static_cast<int>(m_nodes.size()); }
    EditNode *nodeAt(int index) const;
    EditNode *findNode(const QString &id) const;

    // Marks the node at `index` and every node after it dirty (their cached
    // outputs are dropped). Call after a structural change at `index`.
    void invalidateFrom(int index);

    // Walks the graph and returns the final image, recomputing only what is
    // dirty and reusing cache otherwise. Returns the source if there are no
    // nodes, or a null Image if no source is set. (Full-res libvips export path.)
    Image result();

    // Accumulates the GPU preview parameters by walking enabled nodes. This is
    // the GPU (display) path counterpart to result(). Cheap — call it on any
    // edit and push the result to the canvas.
    PreviewState previewState() const;

    // Composes the per-channel tone-curve LUTs of all enabled curve nodes
    // (identity if none). The canvas applies these after the PreviewState ops.
    ChannelLuts previewLut() const;

    // The effective 3D LUT "look" of the enabled look nodes (the last one wins;
    // composing multiple looks is out of scope). Invalid if there is none.
    Lut3D previewLook() const;

    // --- Undo/redo ---------------------------------------------------------
    // History is a stack of graph snapshots. Call commit() to record the current
    // state as a new undo step (a no-op if nothing changed since the last). The
    // typical pattern: commit() when a tool's editing session ends.

    // Clears history and records the current state as the baseline. Call when
    // loading a new image so each image has its own undo timeline.
    void resetHistory();

    void commit();
    bool canUndo() const;
    bool canRedo() const;
    bool undo(); // restores the previous snapshot; returns false if none
    bool redo(); // restores the next snapshot; returns false if none

    // Serialises / restores all node parameters (also reusable for project I/O).
    QJsonArray saveState() const;
    void restoreState(const QJsonArray &state);

private:
    int indexOf(const QString &id) const;

    Image m_source;
    std::vector<std::unique_ptr<EditNode>> m_nodes;
    std::vector<Image> m_cache; // m_cache[i] = output of node i when clean

    std::vector<QJsonArray> m_history;
    int m_historyIndex = -1;
};
