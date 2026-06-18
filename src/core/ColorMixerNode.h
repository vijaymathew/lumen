#pragma once

#include "core/EditNode.h"

// Per-color HSL "color mixer": eight hue bands (Red, Orange, Yellow, Green,
// Aqua, Blue, Purple, Magenta), each with a Hue shift, Saturation and Luminance
// adjustment — the classic Lightroom-style HSL / colour-mixer control. It is
// pointwise, so the GPU preview shader and the libvips apply() run identical
// math (see texture.frag, step 1.5 applyColorMix). All-zero = passthrough.
//
// Each amount is a slider unit in [-100,100], 0 = neutral. A pixel's effect is
// interpolated between the two adjacent band anchors by its hue (the same tent
// weighting as MonoNode's per-color mix) and the whole adjustment is blended in
// by the pixel's HSV saturation, so neutral greys are untouched.
struct ColorMixerValues {
    float hue[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // hue shift, -100..100
    float sat[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // saturation, -100..100
    float lum[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // luminance, -100..100

    friend bool operator==(const ColorMixerValues &, const ColorMixerValues &) = default;
};

class ColorMixerNode : public EditNode {
public:
    // Slider amounts are in [-100,100], 0 = neutral.
    static constexpr float kMinAmount = -100.0f;
    static constexpr float kMaxAmount = 100.0f;
    // Max hue rotation (degrees) at a full hue slider. Kept in sync with the
    // literal in texture.frag's applyColorMix (preview==export).
    static constexpr float kHueRangeDeg = 30.0f;
    // Luminance scale at a full luminance slider (v *= 1 + lum·kLumRange). Kept
    // in sync with texture.frag.
    static constexpr float kLumRange = 1.0f;

    ColorMixerNode();

    const ColorMixerValues &values() const { return m_values; }
    void setValues(const ColorMixerValues &values);

    Image apply(const Image &input) const override;
    void contributeToPreview(PreviewState &state) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

    // Per-color interpolated amount at `hueDeg`: tent interpolation between the
    // two adjacent band anchors (same centres/weights as MonoNode::bandShift and
    // texture.frag's mixBandInterp). `b` holds the 8 band values in [-1,1].
    static float bandInterp(const float b[8], float hueDeg);
    // RGB<->HSV, matching texture.frag exactly. h in [0,360), s/v in [0,1].
    static void rgbToHsv(float r, float g, float b, float &h, float &s, float &v);
    static void hsvToRgb(float h, float s, float v, float &r, float &g, float &b);

private:
    bool isNeutral() const;
    ColorMixerValues m_values;
};
