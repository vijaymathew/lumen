#pragma once

#include "core/Curve.h"
#include "core/EditNode.h"

// CurvesNode applies a tone curve (master RGB) as a 256-entry LUT. The same LUT
// drives the GPU preview (texture) and the libvips export (vips_maplut), so they
// match. Per-channel curves are a later addition.
class CurvesNode : public EditNode {
public:
    CurvesNode();

    const Curve &curve() const { return m_curve; }
    void setCurve(const Curve &curve);

    Image apply(const Image &input) const override;
    void contributeToPreviewLut(std::array<uint8_t, 256> &lut) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    Curve m_curve;
};
