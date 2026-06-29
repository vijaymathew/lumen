#pragma once

#include "core/EditNode.h"

// ColorGradeNode — creative colour grading via Lift / Gamma / Gain colour wheels
// (the ASC-CDL Slope/Offset/Power model). Per channel, in the encoded working
// space (display-referred, like the tone curves — NOT scene-linear; this is the
// deliberate difference from white balance):
//
//     out = ( v · slope + offset ) ^ power      // gain=slope, lift=offset, gamma=power
//
// Each wheel contributes a luma-neutral RGB chroma push (its 2D puck) plus a
// master (uniform luma) term. It is pointwise, so the GPU preview shader and the
// libvips apply() run identical math (see texture.frag). When `enabled` is false
// (or every control is neutral) the node is a passthrough.
struct ColorGradeValues {
    bool enabled = false;
    // Each wheel: puck (x,y) in the unit disc (chroma) + master (luma). Neutral 0.
    float liftX = 0.0f, liftY = 0.0f, liftMaster = 0.0f;     // shadows  → offset
    float gammaX = 0.0f, gammaY = 0.0f, gammaMaster = 0.0f;  // midtones → power
    float gainX = 0.0f, gainY = 0.0f, gainMaster = 0.0f;     // highlights → slope

    friend bool operator==(const ColorGradeValues &, const ColorGradeValues &) = default;
};

class ColorGradeNode : public EditNode {
public:
    ColorGradeNode();

    const ColorGradeValues &values() const { return m_values; }
    void setValues(const ColorGradeValues &values);

    // Resolves the three wheels into per-channel Slope/Offset/Power. The single
    // source of truth shared by apply() and the GPU preview (matches texture.frag).
    static void resolveSOP(const ColorGradeValues &v, double slope[3],
                           double offset[3], double power[3]);

    Image apply(const Image &input) const override;
    void contributeToPreview(PreviewState &state) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    bool isActive() const; // enabled and not fully neutral

    ColorGradeValues m_values;
};
