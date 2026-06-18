// Unit test for the Phase 2.1 edit graph: verifies lazy evaluation, per-node
// caching, dirty propagation, and enable/disable passthrough.
//
// Uses a hand-rolled CHECK macro rather than assert(), because the default
// RelWithDebInfo build defines NDEBUG, which would compile assert() away.

#include "core/EditGraph.h"
#include "core/EditNode.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"

#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

namespace {

// Identity node that counts how many times apply() runs, so we can observe
// exactly when the graph recomputes vs. serves from cache.
class CountingNode : public EditNode {
public:
    CountingNode() : EditNode(QStringLiteral("counting")) {}

    Image apply(const Image &input) const override
    {
        ++applyCount;
        return input; // identity
    }

    mutable int applyCount = 0;
};

} // namespace

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    Image src = Image::black(8, 8);
    CHECK(!src.isNull());
    CHECK(src.width() == 8 && src.height() == 8);

    EditGraph graph;
    graph.setSource(src);

    // Empty graph returns the source.
    CHECK(!graph.result().isNull());

    auto *node = static_cast<CountingNode *>(
        graph.addNode(std::make_unique<CountingNode>()));

    // First evaluation computes the node once.
    CHECK(!graph.result().isNull());
    CHECK(node->applyCount == 1);

    // Second evaluation is fully cached — no recompute.
    graph.result();
    CHECK(node->applyCount == 1);

    // Marking dirty forces exactly one recompute.
    node->markDirty();
    graph.result();
    CHECK(node->applyCount == 2);

    // A second node downstream; recompute counts are independent.
    auto *node2 = static_cast<CountingNode *>(
        graph.addNode(std::make_unique<CountingNode>()));
    graph.result();
    CHECK(node->applyCount == 2);  // node1 was clean/cached
    CHECK(node2->applyCount == 1); // node2 computed once

    // Dirtying the FIRST node must recompute both (downstream propagation).
    node->markDirty();
    graph.result();
    CHECK(node->applyCount == 3);
    CHECK(node2->applyCount == 2);

    // Disabling node1 is a passthrough but still re-runs the pipeline once.
    node->setEnabled(false);
    Image out = graph.result();
    CHECK(out.width() == src.width());
    CHECK(node->applyCount == 3); // disabled: apply() not called

    // Removing a node invalidates from its position.
    const int before = node2->applyCount;
    graph.removeNode(node->id());
    graph.result();
    CHECK(node2->applyCount == before + 1);
    CHECK(graph.nodeCount() == 1);

    ImageBuffer::shutdownLibrary();
    std::puts("edit_graph_test: OK");
    return 0;
}
