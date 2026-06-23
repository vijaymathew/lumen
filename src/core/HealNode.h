#pragma once

#include "core/EditNode.h"
#include "core/SelectiveMask.h"

// HealNode removes blemishes/objects: the user paints a mask over the region to
// remove, and apply() fills it content-aware via Telea inpainting (libvips
// export path). Healing is inherently spatial and there is no cheap GPU preview
// of the inpaint, so the preview shows the unhealed image and the committed
// result appears after apply() — see MainWindow for how the preview is staged.
class HealNode : public EditNode {
public:
    HealNode();

    // Heal mask at working resolution (white = remove). Upscaled in apply().
    const MaskBuffer &healMask() const { return m_mask; }
    void setHealMask(const MaskBuffer &mask);

    Image apply(const Image &input) const override;

    QJsonObject saveState() const override;
    void restoreState(const QJsonObject &state) override;

private:
    MaskBuffer m_mask;
};
