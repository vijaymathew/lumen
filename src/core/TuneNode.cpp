// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/TuneNode.h"

#include <algorithm>
#include <cmath>
#include <vector>

TuneNode::TuneNode()
    : EditNode(QStringLiteral("tune"))
{
}

void TuneNode::setExposure(float ev)
{
    ev = std::clamp(ev, kMinExposure, kMaxExposure);
    if (ev != m_exposure) {
        m_exposure = ev;
        invalidate();
    }
}

void TuneNode::contributeToPreview(PreviewState &state) const
{
    state.exposure += m_exposure; // EV stops sum
}

QJsonObject TuneNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    state[QStringLiteral("exposure")] = m_exposure;
    return state;
}

void TuneNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    setExposure(static_cast<float>(state.value(QStringLiteral("exposure")).toDouble()));
}

Image TuneNode::apply(const Image &input) const
{
    if (input.isNull() || m_exposure == 0.0f)
        return input;

    VipsImage *in = input.handle();
    const int bands = vips_image_get_bands(in);

    // 2^(ev/2.2): a linear-light exposure expressed as an encoded-space multiply
    // so it matches the preview shader exactly (see texture.frag).
    const double f = std::pow(2.0, static_cast<double>(m_exposure) / 2.2);

    std::vector<double> scale(bands, f);
    std::vector<double> offset(bands, 0.0);
    if (bands == 4)
        scale[3] = 1.0; // leave alpha untouched

    VipsImage *scaled = nullptr;
    if (vips_linear(in, &scaled, scale.data(), offset.data(), bands, nullptr))
        return input; // on failure, pass through unchanged

    // vips_linear promotes to float; cast back to 8-bit (saturating).
    VipsImage *casted = nullptr;
    if (vips_cast(scaled, &casted, VIPS_FORMAT_UCHAR, nullptr)) {
        g_object_unref(scaled);
        return input;
    }
    g_object_unref(scaled);
    return Image::adopt(casted);
}
