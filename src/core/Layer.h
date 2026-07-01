#pragma once

#include "core/Image.h"
#include "core/Lut.h"
#include "core/Lut3D.h"
#include "core/MaskSpec.h"
#include "core/PreviewState.h"

#include <QJsonObject>
#include <QString>

#include <memory>
#include <vector>

class EditNode;

// A Layer is a group of adjustment nodes plus a mask, opacity and enabled flag
// (LAYERS.md §2). Its nodes form a chain (like the old EditGraph) — applied in
// order with per-node caching. composite() blends the layer's adjusted output
// onto a base image by `mask × opacity`.
//
// The Base layer (a None mask at full opacity) reduces to "apply the node chain"
// — i.e. exactly the old global pipeline.
class Layer {
public:
    explicit Layer(QString name);
    ~Layer();

    Layer(const Layer &) = delete;
    Layer &operator=(const Layer &) = delete;

    const QString &name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on) { m_enabled = on; }
    float opacity() const { return m_opacity; }
    void setOpacity(float o);
    const MaskSpec &mask() const { return m_mask; }
    void setMask(const MaskSpec &mask);

    // --- Node chain --------------------------------------------------------
    EditNode *addNode(std::unique_ptr<EditNode> node);
    bool removeNode(const QString &id);
    bool moveNode(int from, int to);
    int nodeCount() const { return static_cast<int>(m_nodes.size()); }
    EditNode *nodeAt(int index) const;
    EditNode *findNode(const QString &id) const;
    EditNode *nodeOfType(const QString &type) const; // first node of `type`, or null
    void invalidateFrom(int index); // mark node `index`+ dirty (input changed)

    // Applies the (enabled) node chain to `input`, reusing per-node cache.
    Image applyAdjustments(Image input);

    // Blends this layer's adjusted output onto `base` by mask × opacity. For a
    // None mask at full opacity this is just applyAdjustments(base).
    Image composite(Image base);

    // Like composite(), but runs the node chain WITHOUT touching the per-node
    // cache — for a one-off pass on a different-sized input (e.g. a downsampled
    // image for the live histogram) that must not disturb the interactive caches.
    Image compositeUncached(Image base) const;

    // --- GPU preview accumulation (single-pass; used for the Base layer) ----
    void contributeToPreview(PreviewState &state) const;
    void contributeToPreviewLut(ChannelLuts &luts) const;
    Lut3D look() const;

    // --- Serialisation -----------------------------------------------------
    QJsonObject saveState() const;
    // Restores layer properties and node parameters by matching node ids — does
    // NOT recreate nodes, so external pointers stay valid. Used for the Base
    // layer, whose node set is fixed for the app's lifetime.
    void restoreState(const QJsonObject &state);
    // Restores layer properties AND rebuilds the node chain from the snapshot
    // (recreating nodes via the node factory, preserving their ids). Used for
    // non-Base layers, which can be added/removed (structural undo). Any external
    // pointers into this layer's nodes are invalidated.
    void restoreStructure(const QJsonObject &state);
    // Restores layer properties and each saved node's state into the EXISTING
    // node of the same type (not by id). Used to load the Base layer from a
    // project saved in another session, where node ids differ but the Base has
    // exactly one node per type. Keeps external pointers valid.
    void restoreByType(const QJsonObject &state);

private:
    int indexOf(const QString &id) const;
    void restoreProperties(const QJsonObject &state); // name/enabled/opacity/mask
    Image applyAdjustmentsUncached(Image input) const; // node chain, no caching
    Image blend(Image base, Image adjusted) const;     // mask×opacity composite

    QString m_name;
    bool m_enabled = true;
    float m_opacity = 1.0f;
    MaskSpec m_mask; // None for the Base layer

    std::vector<std::unique_ptr<EditNode>> m_nodes;
    std::vector<Image> m_cache; // m_cache[i] = output of node i when clean
};
