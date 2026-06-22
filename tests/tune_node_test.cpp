// Unit test for TuneNode (Phase 2.3): exposure parameter, clamping, dirty
// behaviour, and that apply() produces a valid image.

#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/TuneNode.h"

#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    TuneNode node;
    CHECK(node.exposure() == 0.0f);
    CHECK(node.typeName() == QLatin1String("tune"));

    node.clearDirty();
    node.setExposure(1.5f);
    CHECK(node.exposure() == 1.5f);
    CHECK(node.isDirty()); // changing a parameter invalidates the cache

    // Setting the same value again does not re-dirty.
    node.clearDirty();
    node.setExposure(1.5f);
    CHECK(!node.isDirty());

    // Out-of-range values clamp.
    node.setExposure(100.0f);
    CHECK(node.exposure() == TuneNode::kMaxExposure);
    node.setExposure(-100.0f);
    CHECK(node.exposure() == TuneNode::kMinExposure);

    Image src = Image::black(4, 4);
    CHECK(!src.isNull());

    // ev == 0 is an identity passthrough.
    TuneNode zero;
    Image out0 = zero.apply(src);
    CHECK(out0.width() == 4 && out0.height() == 4);

    // ev != 0 returns a valid image of the same size.
    Image out1 = node.apply(src);
    CHECK(!out1.isNull());
    CHECK(out1.width() == 4 && out1.height() == 4);

    ImageBuffer::shutdownLibrary();
    std::puts("tune_node_test: OK");
    return 0;
}
