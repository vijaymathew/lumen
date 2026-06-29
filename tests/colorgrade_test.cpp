// Unit test for ColorGradeNode (Lift/Gamma/Gain = ASC-CDL Slope/Offset/Power):
// neutral identity, the three master controls, a chroma push, the resolved
// preview state, and save/restore.

#include "core/ColorGradeNode.h"
#include "core/EditGraph.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/PreviewState.h"

#include <QColor>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

// A flat w×h patch of a single grey level (RGBA8, opaque).
static Image greyPatch(int level)
{
    std::vector<uint8_t> px(4 * 4 * 4, 255);
    for (int i = 0; i < 4 * 4; ++i)
        px[i * 4 + 0] = px[i * 4 + 1] = px[i * 4 + 2] = static_cast<uint8_t>(level);
    return Image::fromInterleaved(px.data(), 4, 4, 4);
}

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }

    // Neutral resolveSOP = identity (slope 1, offset 0, power 1).
    {
        ColorGradeValues v;
        double s[3], o[3], p[3];
        ColorGradeNode::resolveSOP(v, s, o, p);
        for (int c = 0; c < 3; ++c)
            CHECK(s[c] == 1.0 && o[c] == 0.0 && p[c] == 1.0);
    }

    // Disabled / neutral node is a passthrough.
    {
        ColorGradeNode n;
        Image grey = greyPatch(128);
        const QColor c = n.apply(grey).toQImage().pixelColor(0, 0);
        CHECK(c.red() == 128 && c.green() == 128 && c.blue() == 128);
    }

    // Gain master brightens; Lift master raises shadows; Gamma master lifts mids.
    {
        ColorGradeNode gain;
        ColorGradeValues vg;
        vg.enabled = true;
        vg.gainMaster = 1.0f;
        gain.setValues(vg);
        CHECK(gain.apply(greyPatch(128)).toQImage().pixelColor(0, 0).red() > 128);

        ColorGradeNode lift;
        ColorGradeValues vl;
        vl.enabled = true;
        vl.liftMaster = 1.0f;
        lift.setValues(vl);
        CHECK(lift.apply(greyPatch(30)).toQImage().pixelColor(0, 0).red() > 30);

        ColorGradeNode gamma;
        ColorGradeValues vm;
        vm.enabled = true;
        vm.gammaMaster = 1.0f; // power < 1 → mids brighten
        gamma.setValues(vm);
        CHECK(gamma.apply(greyPatch(128)).toQImage().pixelColor(0, 0).red() > 128);
    }

    // A red-ward Gain puck raises the red slope above green/blue.
    {
        ColorGradeValues v;
        v.enabled = true;
        v.gainX = 0.0f;
        v.gainY = 1.0f; // toward the red axis (90°)
        double s[3], o[3], p[3];
        ColorGradeNode::resolveSOP(v, s, o, p);
        CHECK(s[0] > s[1] && s[0] > s[2]);
        // On a grey patch this tints red up relative to blue.
        ColorGradeNode n;
        n.setValues(v);
        const QColor c = n.apply(greyPatch(128)).toQImage().pixelColor(0, 0);
        CHECK(c.red() > c.blue());
    }

    // Preview state carries the resolved SOP (enabled), and is untouched when off.
    {
        EditGraph g;
        auto *n = static_cast<ColorGradeNode *>(
            g.addNode(std::make_unique<ColorGradeNode>()));
        CHECK(g.previewState().gradeEnabled == 0.0f); // neutral
        ColorGradeValues v;
        v.enabled = true;
        v.gainMaster = 1.0f;
        n->setValues(v);
        const PreviewState ps = g.previewState();
        CHECK(ps.gradeEnabled == 1.0f);
        CHECK(ps.gradeSlope0 > 1.0f);
    }

    // Save / restore round-trip.
    {
        ColorGradeNode a;
        ColorGradeValues v;
        v.enabled = true;
        v.liftX = 0.3f;
        v.gammaMaster = -0.4f;
        v.gainY = 0.6f;
        a.setValues(v);
        ColorGradeNode b;
        b.restoreState(a.saveState());
        CHECK(b.values() == a.values());
    }

    ImageBuffer::shutdownLibrary();
    std::puts("colorgrade_test: OK");
    return 0;
}
