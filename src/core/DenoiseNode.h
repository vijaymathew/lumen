#pragma once

#include "core/EditNode.h"

// DenoiseNode reduces noise in L*a*b* space, treating luminance and colour noise
// separately (as real denoisers do):
//
//  * Chroma — a gaussian blur of the a/b channels. Colour noise (blotchy speckle)
//    is the visually ugly kind and the eye barely resolves chroma, so this is a
//    cheap, near-detail-free win.
//  * Luma — a self-guided guided filter on L (edge-preserving box-mean smoothing,
//    reusing core/GuidedFilter), which removes luminance grain while keeping
//    edges crisp.
//
// Like sharpen it is a Base node in the "baked" group; it sits BEFORE sharpen
// (lens -> heal -> denoise -> sharpen) so noise isn't amplified by sharpening.
class DenoiseNode : public EditNode {
public:
    struct Values {
        bool enabled = false;
        float luma = 50.0f;   // 0..100 luminance noise reduction
        float chroma = 50.0f; // 0..100 colour noise reduction

        bool operator==(const Values &o) const
        {
            return enabled == o.enabled && luma == o.luma && chroma == o.chroma;
        }
        bool operator!=(const Values &o) const { return !(*this == o); }
    };

    DenoiseNode();

    const Values &values() const { return m_values; }
    void setValues(const Values &values);

    Image apply(const Image &input) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    Values m_values;
};
