// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/SharpenNode.h"

#include <algorithm>

SharpenNode::SharpenNode()
    : EditNode(QStringLiteral("sharpen"))
{
}

void SharpenNode::setValues(const Values &values)
{
    Values v = values;
    v.amount = std::clamp(v.amount, 0.0f, 100.0f);
    v.radius = std::clamp(v.radius, kMinRadius, kMaxRadius);
    if (v != m_values) {
        m_values = v;
        invalidate();
    }
}

Image SharpenNode::apply(const Image &input) const
{
    if (input.isNull() || !m_values.enabled || m_values.amount <= 0.0f)
        return input;
    if (vips_image_get_bands(input.handle()) < 3)
        return input; // nothing chromatic to sharpen

    // vips_sharpen does an unsharp mask on the L* channel: sigma is the blur
    // radius; m2 is the slope applied to "jaggy" (edge) pixels — our amount. m1
    // (flat-area slope) stays 0 so smooth regions / noise aren't amplified.
    const double m2 = static_cast<double>(m_values.amount) / 100.0 * 4.0;
    VipsImage *sharp = nullptr;
    if (vips_sharpen(input.handle(), &sharp, "sigma", static_cast<double>(m_values.radius),
                     "m1", 0.0, "m2", m2, nullptr))
        return input;

    // vips_sharpen round-trips through LabS and hands back an 8-bit sRGB image;
    // promote back to the float working format (sRGB-tagged) so the pipeline
    // invariant holds.
    VipsImage *asFloat = nullptr;
    if (vips_cast(sharp, &asFloat, VIPS_FORMAT_FLOAT, nullptr)) {
        g_object_unref(sharp);
        return input;
    }
    g_object_unref(sharp);

    VipsImage *tagged = nullptr;
    if (vips_copy(asFloat, &tagged, "interpretation", VIPS_INTERPRETATION_sRGB, nullptr)) {
        g_object_unref(asFloat);
        return input;
    }
    g_object_unref(asFloat);
    return Image::adopt(tagged);
}

QJsonObject SharpenNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    state[QStringLiteral("sharpenEnabled")] = m_values.enabled;
    state[QStringLiteral("amount")] = m_values.amount;
    state[QStringLiteral("radius")] = m_values.radius;
    return state;
}

void SharpenNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    Values v;
    v.enabled = state.value(QStringLiteral("sharpenEnabled")).toBool(false);
    v.amount = static_cast<float>(state.value(QStringLiteral("amount")).toDouble(50.0));
    v.radius = static_cast<float>(state.value(QStringLiteral("radius")).toDouble(1.0));
    setValues(v);
}
