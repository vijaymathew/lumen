#pragma once

#include "core/EditNode.h"

// Monochrome conversion: a weighted B&W mix (per-channel + per-color) plus
// optional split toning (separate shadow/highlight tints). It is pointwise, so
// the GPU preview shader and the libvips apply() run identical math (see
// texture.frag, step 3.5). When `enabled` is false the node is a passthrough.
struct MonoValues {
    bool enabled = false; // convert to B&W (off = passthrough)
    // B&W mix weights — relative; the node normalises them to sum 1. Default is
    // Rec.709 luminance (a natural greyscale).
    float mixR = 0.2126f;
    float mixG = 0.7152f;
    float mixB = 0.0722f;
    // Per-color B&W mix: 8 hue bands at 0/45/.../315° (Red, Orange, Yellow,
    // Green, Aqua, Blue, Purple, Magenta) that brighten/darken pixels of that
    // colour in the grey conversion. Each in [-1,1]; 0 = no shift. The shift is
    // scaled by pixel chroma, so neutral greys are unaffected.
    float band[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    // Split toning: separate shadow + highlight tints, blended across the tonal
    // range by `balance`. hue in [0,360); sat in [0,1] (sat 0 = no tint, so it
    // doubles as the per-region amount). Defaults = no toning (sats 0).
    float shadowHue = 0.0f;
    float shadowSat = 0.0f;
    float highHue = 0.0f;
    float highSat = 0.0f;
    float balance = 0.0f; // [-1,1]; shifts the shadow→highlight crossover

    friend bool operator==(const MonoValues &, const MonoValues &) = default;
};

class MonoNode : public EditNode {
public:
    // Strength multiplier for the per-color mix shift. Kept in sync with the
    // literal in texture.frag step 3.5 (preview==export).
    static constexpr float kBandGain = 3.0f;

    MonoNode();

    const MonoValues &values() const { return m_values; }
    void setValues(const MonoValues &values);

    Image apply(const Image &input) const override;
    void contributeToPreview(PreviewState &state) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

    // Normalised B&W weights (sum 1); falls back to luma if the mix is degenerate.
    void normalizedWeights(float &r, float &g, float &b) const;
    // Tint colour for `hueDeg` at saturation `satV`, normalised so its luma is 1
    // (so toning preserves perceived brightness). Shared by apply() and
    // contributeToPreview().
    static void tintFromHue(float hueDeg, float satV, float &r, float &g, float &b);

    // Hue of an RGB colour in degrees [0,360) (standard 6-segment formula).
    static float hue6(float r, float g, float b);
    // Per-color mix weight at `hueDeg`: a tent-interpolated sum of the 8 band
    // values (centres at k·45°, circular). Result in [-1,1]. Identical math to
    // texture.frag's mono band block.
    static float bandShift(const float band[8], float hueDeg);

private:
    MonoValues m_values;
};
