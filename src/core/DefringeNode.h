#pragma once

#include "core/EditNode.h"

// DefringeNode suppresses chromatic fringing (purple/green colour edges) at
// high-contrast boundaries — the axial-CA artifact that LensCorrectionNode's
// geometric TCA fix does NOT address. It works in L*a*b*: an edge weight from the
// L gradient gates a desaturation of the a/b chroma for pixels whose hue falls in
// the purple or green fringe bands.
//
// Like denoise/sharpen it is a *spatial* op (it samples neighbours for the edge
// gradient), so it is a Base node in the "baked" group and carries no GPU preview
// replica — the preview shows its libvips output baked into the base, which is by
// construction identical to the export. It sits after denoise and before sharpen
// (lens -> heal -> denoise -> defringe -> sharpen) so sharpening doesn't
// re-emphasise residual fringe.
class DefringeNode : public EditNode {
public:
    struct Values {
        bool enabled = false;
        float purple = 50.0f;    // 0..100: strength on the purple/violet band
        float green = 50.0f;     // 0..100: strength on the green band
        float threshold = 25.0f; // 0..100: how strong an edge must be to be treated

        bool operator==(const Values &o) const
        {
            return enabled == o.enabled && purple == o.purple && green == o.green
                && threshold == o.threshold;
        }
        bool operator!=(const Values &o) const { return !(*this == o); }
    };

    DefringeNode();

    const Values &values() const { return m_values; }
    void setValues(const Values &values);

    Image apply(const Image &input) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    Values m_values;
};
