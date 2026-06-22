#pragma once

#include "core/Curve.h"
#include "core/EditNode.h"

// The four editable tone curves. `master` is the RGB/luminance curve applied to
// all channels; `red`/`green`/`blue` are per-channel. The effective LUT for a
// channel is channelCurve ∘ master (master applied first).
struct ChannelCurves {
    Curve master;
    Curve red;
    Curve green;
    Curve blue;
};

// CurvesNode applies master + per-channel tone curves as three effective LUTs.
// The same LUTs drive the GPU preview (texture) and the libvips export
// (vips_maplut), so they match.
class CurvesNode : public EditNode {
public:
    CurvesNode();

    const ChannelCurves &curves() const { return m_curves; }
    void setCurves(const ChannelCurves &curves);

    Image apply(const Image &input) const override;
    void contributeToPreviewLut(ChannelLuts &luts) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    bool isIdentity() const;
    // Effective LUT for rgb channel index 0/1/2 (channelCurve ∘ master).
    std::array<uint8_t, 256> effectiveLut(int channel) const;

    ChannelCurves m_curves;
};
