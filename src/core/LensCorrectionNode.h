#pragma once

#include "core/EditNode.h"

#include <QString>

#include <array>

// LensCorrectionNode applies geometric corrections to the (base) image:
//
//  * Automatic lens correction from EXIF — distortion, transverse chromatic
//    aberration (TCA) and vignetting — using the Lensfun database. This is only
//    available when the build found Lensfun (LUMEN_HAVE_LENSFUN) AND a profile
//    matching the camera/lens was found; otherwise these are silently inert.
//  * Manual perspective — vertical/horizontal keystone, rotation and zoom — a
//    user-driven homography Lensfun does not provide. Always available.
//
// Everything is one backward-resample (vips_mapim) on the full-res image, so the
// preview source is simply re-rendered when a parameter changes (preview==export
// is preserved — the GPU never sees the geometry, only the corrected pixels).
//
// As a geometric/pixel-editing Base node it sits first in the chain, before the
// pointwise tone nodes, so heal and tone operate on the corrected image.
class LensCorrectionNode : public EditNode {
public:
    struct Params {
        // --- Lens identity (from EXIF; drives the Lensfun lookup, serialised) -
        QString cameraMaker;   // e.g. "Canon"
        QString cameraModel;   // e.g. "Canon EOS 5D Mark III"
        QString lensModel;     // e.g. "Canon EF 24-70mm f/2.8L II USM"
        float focalLength = 0; // mm  (0 = unknown)
        float aperture = 0;    // f-number (0 = unknown)
        float focusDistance = 0; // metres (0 = unknown → Lensfun assumes ∞)
        float cropFactor = 0;    // 0 = take the camera's crop from the DB

        // --- Automatic corrections (need a matched Lensfun profile) ----------
        bool distortion = true;
        float distortionAmount = 1.0f; // 0..1 strength (lerp to identity)
        bool tca = true;               // transverse chromatic aberration
        bool vignetting = true;
        float vignettingAmount = 1.0f; // 0..1 strength

        // --- Manual perspective (always available) ---------------------------
        float keystoneV = 0; // vertical keystone / pitch, degrees [-45,45]
        float keystoneH = 0; // horizontal keystone / yaw, degrees [-45,45]
        float rotate = 0;    // roll, degrees [-45,45]
        float scale = 1.0f;  // zoom about centre (1 = none)

        bool operator==(const Params &o) const;
        bool operator!=(const Params &o) const { return !(*this == o); }
    };

    static constexpr float kMaxKeystone = 45.0f; // degrees
    static constexpr float kMaxRotate = 45.0f;
    static constexpr float kMinScale = 0.25f;
    static constexpr float kMaxScale = 4.0f;

    LensCorrectionNode();

    const Params &params() const { return m_params; }
    void setParams(const Params &params);

    // True if a usable lens profile is present in the Lensfun database for the
    // current identity (false when Lensfun is unavailable or unmatched). Drives
    // the UI's "lens detected" affordance.
    bool lensMatched() const;

    // The matched profile's display name (e.g. for fixed-lens compacts whose
    // EXIF carries no lens string), or empty when nothing matched.
    QString matchedLensName() const;

    Image apply(const Image &input) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

    // Backward-map homography (destination pixel → source pixel) for the manual
    // perspective parameters, row-major 3x3. Pure + exposed for unit testing:
    // neutral params yield the identity. Coordinates are in pixels.
    static std::array<double, 9> perspectiveBackMap(int width, int height,
                                                     float keystoneV, float keystoneH,
                                                     float rotate, float scale);

private:
    bool perspectiveActive() const;
    bool autoActive() const; // a Lensfun correction is requested

    Params m_params;
    // Cached Lensfun lookup result for the current identity: -1 unknown, 0 no
    // match, 1 matched. Invalidated whenever the parameters change.
    mutable int m_matchCache = -1;
    mutable QString m_matchedName; // matched profile's display name (cached)
};
