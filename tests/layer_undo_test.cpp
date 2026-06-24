// Unit test for STRUCTURAL undo/redo: adding and deleting layers round-trips
// through the snapshot history — the layer count, the rebuilt layer's node
// parameters, its mask, node ids, and the active index are all restored. Pure
// parameter state (no libvips).

#include "core/EditGraph.h"
#include "core/Layer.h"
#include "core/TuneNode.h"

#include <cstdio>
#include <memory>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static TuneNode *tuneOf(EditGraph &g, int layer)
{
    return static_cast<TuneNode *>(g.layer(layer).nodeOfType(QStringLiteral("tune")));
}

int main()
{
    EditGraph graph;
    // Base layer carries a tune node; keep a raw pointer to prove the Base layer
    // is NOT rebuilt by restore (this pointer must stay valid across undo/redo).
    auto *baseTune = static_cast<TuneNode *>(graph.addNode(std::make_unique<TuneNode>()));
    baseTune->setExposure(0.5f);
    graph.resetHistory();
    CHECK(graph.layerCount() == 1);

    // Add an adjustment layer with a tune node + a radial mask; commit.
    Layer &layer = graph.addLayer(QStringLiteral("Spot"));
    auto *t1 = static_cast<TuneNode *>(graph.addNode(std::make_unique<TuneNode>()));
    t1->setExposure(2.0f);
    MaskSpec m;
    m.type = MaskSpec::Radial;
    m.radiusX = m.radiusY = 0.2f;
    layer.setMask(m);
    const QString nodeId = t1->id();
    graph.commit();
    CHECK(graph.layerCount() == 2);
    CHECK(graph.activeLayerIndex() == 1);

    // Undo the add → back to just the Base layer; the Base pointer is still valid.
    CHECK(graph.undo());
    CHECK(graph.layerCount() == 1);
    CHECK(graph.activeLayerIndex() == 0);
    CHECK(baseTune->exposure() == 0.5f); // Base survived (not rebuilt)
    baseTune->setExposure(0.5f);         // still safe to dereference

    // Redo → the layer comes back, rebuilt from the snapshot with its params,
    // mask, and node id intact.
    CHECK(graph.redo());
    CHECK(graph.layerCount() == 2);
    CHECK(graph.activeLayerIndex() == 1);
    TuneNode *t1b = tuneOf(graph, 1);
    CHECK(t1b != nullptr);
    CHECK(t1b->exposure() == 2.0f);
    CHECK(t1b->id() == nodeId); // id preserved across recreation
    CHECK(graph.layer(1).mask().type == MaskSpec::Radial);

    // Delete the layer and commit, then undo the delete → it returns.
    CHECK(graph.removeLayer(1));
    graph.commit();
    CHECK(graph.layerCount() == 1);
    CHECK(graph.undo());
    CHECK(graph.layerCount() == 2);
    CHECK(tuneOf(graph, 1) != nullptr);
    CHECK(tuneOf(graph, 1)->exposure() == 2.0f);
    CHECK(tuneOf(graph, 1)->id() == nodeId);

    // Editing a parameter on the rebuilt layer still commits/undoes correctly.
    tuneOf(graph, 1)->setExposure(-1.0f);
    graph.commit();
    CHECK(graph.undo());
    CHECK(tuneOf(graph, 1)->exposure() == 2.0f);

    std::puts("layer_undo_test: OK");
    return 0;
}
