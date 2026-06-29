#pragma once

#include "core/EditNode.h"

// GrainNode adds film grain: monochrome (luminance) value-noise keyed to image
// pixel coordinates, added to the final image. It is a Base-layer node placed
// LAST (after mono), so it applies over the finished tone/colour. Unlike the
// other pointwise nodes the value depends on position, but it is still
// replicated in the GPU preview (texture.frag, via gl_FragCoord) so preview and
// export match in character. Because GPU↔CPU float math isn't bit-identical and
// the signal is noise, the match is statistical (size/intensity/look), not
// pixel-exact — see docs/MONOCHROME.md.
class GrainNode : public EditNode {
public:
    struct Values {
        bool enabled = false;
        float amount = 25.0f; // 0..100 (grain intensity)
        float size = 2.0f;    // grain cell size in pixels

        bool operator==(const Values &o) const
        {
            return enabled == o.enabled && amount == o.amount && size == o.size;
        }
        bool operator!=(const Values &o) const { return !(*this == o); }
    };

    static constexpr float kMinSize = 1.0f;
    static constexpr float kMaxSize = 8.0f;
    // Max grain excursion in [0,1] at amount 100; kept in sync with texture.frag.
    static constexpr float kStrength = 0.18f;
    static constexpr float kSeed = 11.0f; // fixed → deterministic, stable grain

    GrainNode();

    const Values &values() const { return m_values; }
    void setValues(const Values &values);

    Image apply(const Image &input) const override;
    void contributeToPreview(PreviewState &state) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

    // Smooth 2D value noise in [0,1] at (x,y) — the C++ twin of texture.frag's
    // valueNoise (bilinear smoothstep interpolation of a hashed integer lattice).
    static float valueNoise(float x, float y);

private:
    Values m_values;
};
