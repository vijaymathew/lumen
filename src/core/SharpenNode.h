#pragma once

#include "core/EditNode.h"

// SharpenNode is an unsharp-mask sharpener built on vips_sharpen (which works in
// L*a*b*, so only luminance is sharpened — no colour fringing). It is a Base
// node in the "baked" group (after heal, before the pointwise tone nodes): like
// heal and lens it is a neighbourhood op the GPU shader can't replicate, so it
// bakes into the preview base and re-renders on change. Capture-style sharpening
// (before tone) is the consequence of that ordering.
class SharpenNode : public EditNode {
public:
    struct Values {
        bool enabled = false;
        float amount = 50.0f; // 0..100 (maps to the unsharp slope)
        float radius = 1.0f;  // gaussian sigma in pixels

        bool operator==(const Values &o) const
        {
            return enabled == o.enabled && amount == o.amount && radius == o.radius;
        }
        bool operator!=(const Values &o) const { return !(*this == o); }
    };

    static constexpr float kMinRadius = 0.3f;
    static constexpr float kMaxRadius = 4.0f;

    SharpenNode();

    const Values &values() const { return m_values; }
    void setValues(const Values &values);

    Image apply(const Image &input) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    Values m_values;
};
