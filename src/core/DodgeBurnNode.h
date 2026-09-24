#pragma once

#include "core/EditNode.h"
#include "core/SelectiveMask.h"

#include <cstdint>

// DodgeBurnNode lightens (dodge) or darkens (burn) painted regions, like the
// darkroom technique. The user paints two coverage masks — one for dodge, one for
// burn — and the node applies exposure = strength * (dodge - burn), weighted by
// the pixel's luminance so the effect can be confined to the shadows, midtones or
// highlights. Strength and range are node-wide, so they stay adjustable after
// painting. Like HealNode it is a "baked" pass (masks are painted bitmaps, not a
// GPU uniform): the preview re-bakes it on stroke end, export runs apply().
class DodgeBurnNode : public EditNode {
public:
    enum class Range { Shadows, Midtones, Highlights };

    static constexpr int kMinExposure = 1;
    static constexpr int kMaxExposure = 100;
    static constexpr int kDefaultExposure = 30;
    // EV applied where a mask is fully covered at exposure == kMaxExposure.
    static constexpr float kMaxEv = 2.0f;

    DodgeBurnNode();

    // Coverage masks at working resolution (white = affected). Upscaled in apply().
    const MaskBuffer &dodgeMask() const { return m_dodge; }
    const MaskBuffer &burnMask() const { return m_burn; }
    void setDodgeMask(const MaskBuffer &mask);
    void setBurnMask(const MaskBuffer &mask);

    int exposure() const { return m_exposure; } // [kMinExposure, kMaxExposure]
    void setExposure(int exposure);
    Range range() const { return m_range; }
    void setRange(Range range);

    // True when applying would change pixels (some mask painted).
    bool hasEffect() const { return !m_dodge.isEmpty() || !m_burn.isEmpty(); }

    // Luminance weight of `range` at luma L in [0,1] (peak 1).
    static float rangeWeight(Range range, float luma);

    // Applies the effect in place to interleaved 8-bit pixels (`bands` >= 3,
    // extra bands untouched). Masks are resampled to w x h. Exposed for tests.
    void applyToPixels(uint8_t *px, int w, int h, int bands) const;

    Image apply(const Image &input) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    MaskBuffer m_dodge;
    MaskBuffer m_burn;
    int m_exposure = kDefaultExposure;
    Range m_range = Range::Midtones;
};
