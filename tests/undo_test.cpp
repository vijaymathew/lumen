// Unit test for global undo/redo over the edit graph: snapshot history,
// commit (with no-op coalescing), undo/redo navigation, and redo-tail
// truncation on a new commit. Pure parameter state — no libvips needed.

#include "core/EditGraph.h"
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

int main()
{
    EditGraph graph;
    auto *tune = static_cast<TuneNode *>(graph.addNode(std::make_unique<TuneNode>()));

    graph.resetHistory(); // baseline: exposure 0
    CHECK(!graph.canUndo());
    CHECK(!graph.canRedo());

    tune->setExposure(1.0f);
    graph.commit();
    CHECK(graph.canUndo());
    CHECK(graph.previewState().exposure == 1.0f);

    tune->setExposure(2.0f);
    graph.commit();
    CHECK(graph.previewState().exposure == 2.0f);

    // Committing with no change is a no-op (no extra undo step).
    graph.commit();
    CHECK(graph.undo());
    CHECK(graph.previewState().exposure == 1.0f);

    CHECK(graph.undo());
    CHECK(graph.previewState().exposure == 0.0f);
    CHECK(!graph.canUndo());
    CHECK(!graph.undo());

    // Redo back forward.
    CHECK(graph.redo());
    CHECK(graph.previewState().exposure == 1.0f);
    CHECK(graph.canRedo());

    // A new commit truncates the redo tail (the old 2.0 step is gone).
    tune->setExposure(3.0f);
    graph.commit();
    CHECK(!graph.canRedo());
    CHECK(graph.previewState().exposure == 3.0f);

    CHECK(graph.undo());
    CHECK(graph.previewState().exposure == 1.0f);

    std::puts("undo_test: OK");
    return 0;
}
