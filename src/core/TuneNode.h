#pragma once

#include "core/EditNode.h"

// TuneNode applies basic tonal adjustments. Phase 2.3 implements exposure (in
// EV stops); contrast / highlights / shadows / white balance follow in Phase 3.
//
// Exposure is a linear-light multiply of 2^ev. With a power-law gamma that is
// equivalent to multiplying the encoded (sRGB-ish) values by 2^(ev/2.2), which
// is what both the GPU preview shader and apply() do — so preview and export
// agree. See texture.frag.
class TuneNode : public EditNode {
public:
    static constexpr float kMinExposure = -5.0f; // EV stops
    static constexpr float kMaxExposure = 5.0f;

    TuneNode();

    float exposure() const { return m_exposure; } // EV stops
    void setExposure(float ev);

    Image apply(const Image &input) const override;

private:
    float m_exposure = 0.0f;
};
