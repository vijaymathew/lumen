#pragma once

#include "core/EditNode.h"
#include "core/SelectiveMask.h"

#include <cstdint>
#include <vector>

// RetouchNode paints a flat colour over parts of the image — pick a colour from
// one region, touch it over another (blemish, stain, colour patch). The paint is
// a list of patches: each has a colour and a coverage mask, composited in order,
// so several colours can coexist. Opacity is node-wide and stays adjustable after
// painting. Like HealNode/DodgeBurnNode it is a "baked" pass (painted bitmaps, not
// a GPU uniform): the preview re-bakes on stroke end, export runs apply().
class RetouchNode : public EditNode {
public:
    struct Patch {
        uint8_t r = 0, g = 0, b = 0;
        MaskBuffer mask; // working resolution (white = painted), upscaled in apply()
    };

    static constexpr int kMinOpacity = 1;
    static constexpr int kMaxOpacity = 100;

    RetouchNode();

    const std::vector<Patch> &patches() const { return m_patches; }
    void setPatches(const std::vector<Patch> &patches);
    // Replaces patch `index` (or appends when index == patches().size()). An empty
    // mask removes the patch instead, so unpainted sessions leave no trace.
    void setPatch(size_t index, uint8_t r, uint8_t g, uint8_t b, const MaskBuffer &mask);

    int opacity() const { return m_opacity; } // [kMinOpacity, kMaxOpacity] percent
    void setOpacity(int opacity);

    // True when applying would change pixels (some patch painted).
    bool hasEffect() const { return !m_patches.empty(); }

    // Composites the patches in place over interleaved 8-bit pixels (`bands` >= 3,
    // extra bands untouched). Masks are resampled to w x h. Exposed for tests.
    void applyToPixels(uint8_t *px, int w, int h, int bands) const;

    Image apply(const Image &input) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    std::vector<Patch> m_patches;
    int m_opacity = kMaxOpacity;
};
