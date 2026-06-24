#pragma once

#include "core/EditNode.h"
#include "core/Lut3D.h"

#include <QString>

// LutNode applies a 3D LUT "look" (loaded from a HALD CLUT) with trilinear
// interpolation. The same Lut3D drives the libvips export (per-pixel trilinear)
// and the GPU preview (a 3D texture sampled by the canvas), so they match.
//
// The look-intensity blend and the file picker come in Phase 4.3.
class LutNode : public EditNode {
public:
    LutNode();

    // Loads a 3D LUT from `path`, picking the parser by extension: ".cube" for
    // an Adobe/Resolve text LUT, otherwise a HALD CLUT image. Returns false (and
    // sets *error) on failure.
    bool loadLut(const QString &path, QString *error = nullptr);
    // Loads a HALD CLUT from `path`. Returns false (and sets *error) on failure.
    bool loadHald(const QString &path, QString *error = nullptr);
    // Sets a LUT directly (not persisted across undo — used for tests).
    void setLut(const Lut3D &lut);
    void clear();

    const Lut3D &lut() const { return m_lut; }
    const QString &sourcePath() const { return m_sourcePath; }

    float intensity() const { return m_intensity; } // [0,1]
    void setIntensity(float intensity);

    Image apply(const Image &input) const override;
    const Lut3D *lookLut() const override { return &m_lut; }
    void contributeToPreview(PreviewState &state) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    Lut3D m_lut;
    QString m_sourcePath;     // for persistence; empty for directly-set LUTs
    float m_intensity = 1.0f; // look blend [0,1]
};
