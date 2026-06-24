#pragma once

#include "core/EditNode.h"

// Monochrome conversion: a weighted B&W mix (per-channel weights) plus optional
// toning (a hue-tinted blend). It is pointwise, so the GPU preview shader and
// the libvips apply() run identical math (see texture.frag, step 3.5). When
// `enabled` is false the node is a passthrough.
struct MonoValues {
    bool enabled = false; // convert to B&W (off = passthrough)
    // B&W mix weights — relative; the node normalises them to sum 1. Default is
    // Rec.709 luminance (a natural greyscale).
    float mixR = 0.2126f;
    float mixG = 0.7152f;
    float mixB = 0.0722f;
    // Toning: blend the grey toward a hue-tinted version.
    float toneStrength = 0.0f; // [0,1]
    float toneHue = 32.0f;     // degrees [0,360); default warm/sepia

    friend bool operator==(const MonoValues &, const MonoValues &) = default;
};

class MonoNode : public EditNode {
public:
    static constexpr float kToneSaturation = 0.5f; // fixed tint saturation

    MonoNode();

    const MonoValues &values() const { return m_values; }
    void setValues(const MonoValues &values);

    Image apply(const Image &input) const override;
    void contributeToPreview(PreviewState &state) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

    // Normalised B&W weights (sum 1); falls back to luma if the mix is degenerate.
    void normalizedWeights(float &r, float &g, float &b) const;
    // Tint colour for `hueDeg`, normalised so its luma is 1 (so toning preserves
    // perceived brightness). Shared by apply() and contributeToPreview().
    static void tintFromHue(float hueDeg, float &r, float &g, float &b);

private:
    MonoValues m_values;
};
