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

    // Loads a HALD CLUT from `path`. Returns false (and sets *error) on failure.
    bool loadHald(const QString &path, QString *error = nullptr);
    // Sets a LUT directly (not persisted across undo — used for tests).
    void setLut(const Lut3D &lut);
    void clear();

    const Lut3D &lut() const { return m_lut; }

    Image apply(const Image &input) const override;
    const Lut3D *lookLut() const override { return &m_lut; }

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    Lut3D m_lut;
    QString m_sourcePath; // for persistence; empty for directly-set LUTs
};
