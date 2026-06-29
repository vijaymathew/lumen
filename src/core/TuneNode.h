#pragma once

#include "core/EditNode.h"

// TuneNode applies basic global tone: exposure (EV stops), contrast, saturation,
// and white balance. Highlights/shadows are future work.
//
// Exposure is a linear-light multiply of 2^ev. With a power-law gamma that is
// equivalent to multiplying the encoded (sRGB-ish) values by 2^(ev/2.2), which
// is what both the GPU preview shader and apply() do — so preview and export
// agree. See texture.frag.
//
// White balance (WB v2) is a true linear-light operation: a Kelvin/Planckian
// illuminant + green/magenta tint become a 3x3 matrix (see core/WhiteBalance)
// applied in linear light (encoded → ^2.2 → W·rgb → ^(1/2.2)). With a RAW camera
// profile (setCameraProfile) the matrix is the camera-accurate equivalent of
// re-doing WB in sensor space; without one it degrades to the sRGB model. The
// same matrix drives apply() and the GPU preview, so they agree.
class TuneNode : public EditNode {
public:
    static constexpr float kMinExposure = -5.0f; // EV stops
    static constexpr float kMaxExposure = 5.0f;
    // Contrast, saturation and tint are slider units in [-100, 100], 0 = neutral.
    static constexpr float kMinAmount = -100.0f;
    static constexpr float kMaxAmount = 100.0f;
    // White-balance colour temperature range (Kelvin).
    static constexpr float kMinKelvin = 2000.0f;
    static constexpr float kMaxKelvin = 12000.0f;
    static constexpr float kDefaultKelvin = 6500.0f;

    TuneNode();

    float exposure() const { return m_exposure; } // EV stops
    void setExposure(float ev);

    float contrast() const { return m_contrast; } // -100..100
    void setContrast(float amount);

    float saturation() const { return m_saturation; } // -100..100
    void setSaturation(float amount);

    // White balance: absolute colour temperature in Kelvin + green(−)/magenta(+)
    // tint. The neutral point is the as-shot temperature (asShotKelvin), at which
    // the WB matrix is the identity (image unchanged).
    float kelvin() const { return m_kelvin; }
    void setKelvin(float kelvin);
    float tint() const { return m_tint; } // -100..100
    void setTint(float amount);
    float asShotKelvin() const { return m_asShotKelvin; }

    // Installs the camera colour profile (from RAW decode) used for camera-
    // accurate WB and re-estimates the as-shot temperature. When `seedKelvin` is
    // true (opening a fresh RAW) the slider is moved to the as-shot temperature;
    // when false (restoring a saved project) the current kelvin is kept. All
    // matrices are row-major 3x3.
    void setCameraProfile(const double camToRgb[9], const double xyzToCam[9],
                          const double asShotMul[3], bool seedKelvin = true);

    // Computes the linear-light WB matrix (row-major 3x3) for the current state.
    void whiteBalanceMatrix(double outW[9]) const;

    Image apply(const Image &input) const override;
    void contributeToPreview(PreviewState &state) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    bool isNeutral() const;
    bool wbIsIdentity() const;
    Image applyWhiteBalance(const Image &input, const double W[9]) const;

    float m_exposure = 0.0f;
    float m_contrast = 0.0f;
    float m_saturation = 0.0f;

    // White balance.
    float m_kelvin = kDefaultKelvin;
    float m_tint = 0.0f;
    float m_asShotKelvin = kDefaultKelvin;
    bool m_hasProfile = false;          // true once a RAW camera profile is set
    double m_camToRgb[9];               // camera → linear sRGB (identity if none)
    double m_xyzToCam[9];               // CIE XYZ → camera (sRGB matrix if none)
};
