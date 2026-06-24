#pragma once

#include "core/EditNode.h"

// TuneNode applies basic global tone: exposure (EV stops), contrast, saturation,
// and basic white balance (temperature/tint). Highlights/shadows are future work.
//
// Exposure is a linear-light multiply of 2^ev. With a power-law gamma that is
// equivalent to multiplying the encoded (sRGB-ish) values by 2^(ev/2.2), which
// is what both the GPU preview shader and apply() do — so preview and export
// agree. See texture.frag.
class TuneNode : public EditNode {
public:
    static constexpr float kMinExposure = -5.0f; // EV stops
    static constexpr float kMaxExposure = 5.0f;
    // Contrast and saturation are slider units in [-100, 100], 0 = neutral.
    static constexpr float kMinAmount = -100.0f;
    static constexpr float kMaxAmount = 100.0f;

    TuneNode();

    float exposure() const { return m_exposure; } // EV stops
    void setExposure(float ev);

    float contrast() const { return m_contrast; } // -100..100
    void setContrast(float amount);

    float saturation() const { return m_saturation; } // -100..100
    void setSaturation(float amount);

    // Basic white balance as encoded-space channel gains. Temperature is a
    // warm(+)/cool(-) R↔B shift; tint is a magenta(+)/green(-) shift on G. Both
    // are slider units in [-100, 100], 0 = neutral. (A true linear-light/Kelvin
    // WB is part of the deferred scene-linear work — see docs.)
    float temperature() const { return m_temperature; } // -100..100
    void setTemperature(float amount);
    float tint() const { return m_tint; } // -100..100
    void setTint(float amount);

    // Maps temperature/tint to per-channel multipliers. Shared by apply() and the
    // GPU preview so the two agree. Pure + static for testing.
    static void wbGains(float temperature, float tint, float &r, float &g, float &b);

    Image apply(const Image &input) const override;
    void contributeToPreview(PreviewState &state) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    bool isNeutral() const;

    float m_exposure = 0.0f;
    float m_contrast = 0.0f;
    float m_saturation = 0.0f;
    float m_temperature = 0.0f;
    float m_tint = 0.0f;
};
