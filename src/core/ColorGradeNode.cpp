// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/ColorGradeNode.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
// Wheel response constants. Master maps a [-1,1] slider; chroma is the unit-disc
// radius. Tuned for useful but not extreme ranges.
constexpr double kGainMaster = 0.5, kGainChroma = 0.5;   // slope = 1 ± ...
constexpr double kLiftMaster = 0.2, kLiftChroma = 0.2;   // offset = 0 ± ...
constexpr double kGammaMaster = 0.5, kGammaChroma = 0.5; // power = 1/(1 ± ...)

// Per-channel hue axes on the colour wheel (degrees): red up, then green, blue.
constexpr double kAxisDeg[3] = {90.0, 210.0, 330.0};

// A wheel puck (x,y) → luma-neutral per-channel chroma push (sum ≈ 0).
void chromaPush(float x, float y, double out[3])
{
    const double r = std::min(1.0, std::hypot(static_cast<double>(x), static_cast<double>(y)));
    const double hue = std::atan2(static_cast<double>(y), static_cast<double>(x));
    double mean = 0.0;
    for (int c = 0; c < 3; ++c) {
        out[c] = r * std::cos(hue - kAxisDeg[c] * M_PI / 180.0);
        mean += out[c];
    }
    mean /= 3.0;
    for (int c = 0; c < 3; ++c)
        out[c] -= mean;
}
} // namespace

ColorGradeNode::ColorGradeNode()
    : EditNode(QStringLiteral("colorgrade"))
{
}

void ColorGradeNode::setValues(const ColorGradeValues &values)
{
    ColorGradeValues v = values;
    const auto clampUnit = [](float &f) { f = std::clamp(f, -1.0f, 1.0f); };
    clampUnit(v.liftX);  clampUnit(v.liftY);  clampUnit(v.liftMaster);
    clampUnit(v.gammaX); clampUnit(v.gammaY); clampUnit(v.gammaMaster);
    clampUnit(v.gainX);  clampUnit(v.gainY);  clampUnit(v.gainMaster);
    if (v != m_values) {
        m_values = v;
        invalidate();
    }
}

void ColorGradeNode::resolveSOP(const ColorGradeValues &v, double slope[3],
                                double offset[3], double power[3])
{
    double cl[3], cg[3], cga[3];
    chromaPush(v.liftX, v.liftY, cl);
    chromaPush(v.gainX, v.gainY, cg);
    chromaPush(v.gammaX, v.gammaY, cga);
    for (int c = 0; c < 3; ++c) {
        slope[c] = 1.0 + v.gainMaster * kGainMaster + cg[c] * kGainChroma;
        offset[c] = v.liftMaster * kLiftMaster + cl[c] * kLiftChroma;
        double den = 1.0 + v.gammaMaster * kGammaMaster + cga[c] * kGammaChroma;
        den = std::clamp(den, 0.1, 10.0);
        power[c] = 1.0 / den;
    }
}

bool ColorGradeNode::isActive() const
{
    if (!m_values.enabled)
        return false;
    const ColorGradeValues neutral; // all-zero controls
    ColorGradeValues v = m_values;
    v.enabled = false; // compare controls only
    return !(v == neutral);
}

void ColorGradeNode::contributeToPreview(PreviewState &state) const
{
    if (!isActive())
        return; // passthrough → leave gradeEnabled at its 0 default
    double slope[3], offset[3], power[3];
    resolveSOP(m_values, slope, offset, power);
    state.gradeEnabled = 1.0f;
    state.gradeSlope0 = static_cast<float>(slope[0]);
    state.gradeSlope1 = static_cast<float>(slope[1]);
    state.gradeSlope2 = static_cast<float>(slope[2]);
    state.gradeOffset0 = static_cast<float>(offset[0]);
    state.gradeOffset1 = static_cast<float>(offset[1]);
    state.gradeOffset2 = static_cast<float>(offset[2]);
    state.gradePower0 = static_cast<float>(power[0]);
    state.gradePower1 = static_cast<float>(power[1]);
    state.gradePower2 = static_cast<float>(power[2]);
}

Image ColorGradeNode::apply(const Image &input) const
{
    if (input.isNull() || !isActive())
        return input;

    double slope[3], offset[3], power[3];
    resolveSOP(m_values, slope, offset, power);

    VipsImage *f = nullptr;
    if (vips_cast(input.handle(), &f, VIPS_FORMAT_FLOAT, nullptr))
        return input;
    void *buf = vips_image_write_to_memory(f, nullptr);
    const int w = f->Xsize;
    const int h = f->Ysize;
    const int bands = f->Bands;
    g_object_unref(f);
    if (!buf)
        return input;

    auto *px = static_cast<float *>(buf);
    const long long n = static_cast<long long>(w) * h;
    const int colorBands = std::min(bands, 3);
    for (long long i = 0; i < n; ++i) {
        float *p = px + i * bands;
        for (int c = 0; c < colorBands; ++c) {
            double x = p[c] / 255.0;                  // encoded → [0,1]
            x = x * slope[c] + offset[c];             // slope · v + offset
            x = std::pow(std::max(0.0, x), power[c]); // ^power (clamp ≥0 for pow)
            p[c] = static_cast<float>(x * 255.0);
        }
        // alpha (and any extra band) left untouched
    }

    Image result = Image::fromInterleavedFloat(px, w, h, bands);
    g_free(buf);
    return result.isNull() ? input : result;
}

QJsonObject ColorGradeNode::saveState() const
{
    QJsonObject state = EditNode::saveState();
    state[QStringLiteral("gradeEnabled")] = m_values.enabled;
    state[QStringLiteral("liftX")] = m_values.liftX;
    state[QStringLiteral("liftY")] = m_values.liftY;
    state[QStringLiteral("liftMaster")] = m_values.liftMaster;
    state[QStringLiteral("gammaX")] = m_values.gammaX;
    state[QStringLiteral("gammaY")] = m_values.gammaY;
    state[QStringLiteral("gammaMaster")] = m_values.gammaMaster;
    state[QStringLiteral("gainX")] = m_values.gainX;
    state[QStringLiteral("gainY")] = m_values.gainY;
    state[QStringLiteral("gainMaster")] = m_values.gainMaster;
    return state;
}

void ColorGradeNode::restoreState(const QJsonObject &state)
{
    EditNode::restoreState(state);
    ColorGradeValues v;
    const auto f = [&](const char *k) {
        return static_cast<float>(state.value(QLatin1String(k)).toDouble(0.0));
    };
    v.enabled = state.value(QStringLiteral("gradeEnabled")).toBool(false);
    v.liftX = f("liftX");   v.liftY = f("liftY");   v.liftMaster = f("liftMaster");
    v.gammaX = f("gammaX"); v.gammaY = f("gammaY"); v.gammaMaster = f("gammaMaster");
    v.gainX = f("gainX");   v.gainY = f("gainY");   v.gainMaster = f("gainMaster");
    setValues(v);
}
