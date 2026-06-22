#pragma once

#include "core/Image.h"
#include "core/PreviewState.h"

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

private:
    int indexOf(const QString &id) const;

    Image m_source;
    std::vector<std::unique_ptr<EditNode>> m_nodes;
    std::vector<Image> m_cache; // m_cache[i] = output of node i when clean
};
