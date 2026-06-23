#pragma once

#include "core/EditNode.h"
#include "core/SelectiveMask.h"

// Parameters for a selective (luminosity-masked) tone adjustment. The mask
// selects pixels whose luminance falls in [low, high], feathered by `feather`
// on each side; the adjustment (exposure/contrast/saturation) is blended in by
// the mask weight.
struct SelectiveValues {
    // Mask: 0 = luminosity range (parametric), 1 = colour affinity, 2 = brush.
    int maskMode = 0;
    // Luminosity-range mask.
    float low = 0.0f; // luminance range [0,1]
    float high = 1.0f;
    float feather = 0.1f; // edge softness [0,1]
    // Colour-affinity mask.
    float targetR = 0.0f; // sampled target colour [0,1]
    float targetG = 0.0f;
    float targetB = 0.0f;
    float colorRange = 0.3f; // colour-distance tolerance [0,1]
    // Adjustment.
    float exposure = 0.0f;   // EV stops
    float contrast = 0.0f;   // -100..100
    float saturation = 0.0f; // -100..100
    bool invert = false;     // adjust everywhere the mask is NOT

    friend bool operator==(const SelectiveValues &, const SelectiveValues &) = default;
};

// SelectiveNode applies a tone adjustment only where a luminosity-range mask is.
// The mask is pointwise (a function of each pixel's luminance), so the same math
// runs in the libvips export and the GPU preview shader.
class SelectiveNode : public EditNode {
public:
    static constexpr float kMinExposure = -5.0f;
    static constexpr float kMaxExposure = 5.0f;
    static constexpr float kMinAmount = -100.0f;
    static constexpr float kMaxAmount = 100.0f;

    SelectiveNode();

    const SelectiveValues &values() const { return m_values; }
    void setValues(const SelectiveValues &values);

    // The painted brush mask (used when maskMode == 2). Stored at working
    // resolution; apply() upscales it to the image.
    const MaskBuffer &brushMask() const { return m_brushMask; }
    void setBrushMask(const MaskBuffer &mask);

    Image apply(const Image &input) const override;
    void contributeToPreview(PreviewState &state) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    bool isNeutral() const;

    SelectiveValues m_values;
    MaskBuffer m_brushMask; // painted mask for brush mode
};
