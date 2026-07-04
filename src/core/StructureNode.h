#pragma once

#include "core/EditNode.h"

// StructureNode is a local-contrast ("Clarity" / Nik "Structure") enhancer. It
// adds a midtone-weighted high-pass of the luminance back onto the image: a
// large-radius unsharp mask that boosts medium-scale texture and "punch" without
// the fine-edge halos of capture sharpening. Positive amounts add structure;
// negative amounts soften local contrast for a dreamy/glow look.
//
// Like heal, denoise and sharpen it is a Base node in the "baked" group (a
// neighbourhood op the GPU shader can't replicate), so it bakes into the preview
// base texture and re-renders on change. It runs after sharpen: capture detail
// first, then the broader structural contrast. Working on luminance keeps it
// achromatic — no saturation shift.
class StructureNode : public EditNode {
public:
    struct Values {
        bool enabled = false;
        float amount = 40.0f; // -100..100 (negative softens local contrast)
        float radius = 12.0f; // gaussian sigma in pixels (the contrast scale)

        bool operator==(const Values &o) const
        {
            return enabled == o.enabled && amount == o.amount && radius == o.radius;
        }
        bool operator!=(const Values &o) const { return !(*this == o); }
    };

    static constexpr float kMinAmount = -100.0f;
    static constexpr float kMaxAmount = 100.0f;
    static constexpr float kMinRadius = 2.0f;
    static constexpr float kMaxRadius = 50.0f;

    StructureNode();

    const Values &values() const { return m_values; }
    void setValues(const Values &values);

    Image apply(const Image &input) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    Values m_values;
};
