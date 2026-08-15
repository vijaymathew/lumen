// Unit test for gpu/CropViewTransform.h: the pure crop/orientation math split
// out of CanvasWidget in the Phase C refactor. No prior direct test coverage
// existed for this matrix math; it was previously only reachable by driving
// the GPU renderer.

#include "gpu/CropViewTransform.h"

#include <cmath>
#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static bool near(QPointF a, QPointF b, double eps = 1e-4)
{
    return std::abs(a.x() - b.x()) < eps && std::abs(a.y() - b.y()) < eps;
}

static bool near(QSizeF a, QSizeF b, double eps = 1e-4)
{
    return std::abs(a.width() - b.width()) < eps && std::abs(a.height() - b.height()) < eps;
}

int main()
{
    const QSize tex(400, 300);

    // ViewMode::None: identity transform, full texture size regardless of crop.
    {
        CropState crop;
        crop.rect = QRectF(0.1, 0.1, 0.5, 0.5);
        crop.rotation = 90;
        CHECK(near(cropview::effectiveImageSize(tex, crop, cropview::ViewMode::None),
                   QSizeF(400, 300)));
        QMatrix4x4 x = cropview::texXform(tex, crop, cropview::ViewMode::None);
        CHECK(x.isIdentity());
    }

    // ViewMode::Applied, no rotation: effective size is the crop rect scaled
    // into the (unrotated) texture size, and the transform maps the output
    // unit quad onto that same rect.
    {
        CropState crop;
        crop.rect = QRectF(0.25, 0.25, 0.5, 0.5);
        const QSizeF eff = cropview::effectiveImageSize(tex, crop, cropview::ViewMode::Applied);
        CHECK(near(eff, QSizeF(200, 150)));
        const QMatrix4x4 x = cropview::texXform(tex, crop, cropview::ViewMode::Applied);
        CHECK(near(x.map(QPointF(0, 0)), QPointF(0.25, 0.25)));
        CHECK(near(x.map(QPointF(1, 1)), QPointF(0.75, 0.75)));
    }

    // 90-degree rotation swaps the effective width/height, and Editing mode
    // ignores the crop rect (full oriented frame).
    {
        CropState crop;
        crop.rotation = 90;
        crop.rect = QRectF(0.1, 0.1, 0.2, 0.2); // ignored in Editing mode
        CHECK(near(cropview::effectiveImageSize(tex, crop, cropview::ViewMode::Editing),
                   QSizeF(300, 400)));
    }

    // Flip round-trips: flipping H then mapping (0,0) should land near (1,0)
    // in source space (undo-flip semantics), and applying texXform twice
    // (forward then its inverse) returns the original point.
    {
        CropState crop;
        crop.flipH = true;
        const QMatrix4x4 x = cropview::texXform(tex, crop, cropview::ViewMode::Applied);
        CHECK(near(x.map(QPointF(0, 0)), QPointF(1, 0)));
        CHECK(near(x.map(QPointF(1, 0)), QPointF(0, 0)));
        bool invertible = false;
        const QMatrix4x4 inv = x.inverted(&invertible);
        CHECK(invertible);
        CHECK(near(inv.map(x.map(QPointF(0.3, 0.7))), QPointF(0.3, 0.7)));
    }

    // MaskEdit round-trip: sourceNormFromOriented/orientedNormFromSource must
    // be true inverses of each other for a rotated+flipped state, and both
    // must be no-ops outside MaskEdit mode.
    {
        CropState crop;
        crop.rotation = 270;
        crop.flipV = true;
        const QMatrix4x4 x = cropview::texXform(tex, crop, cropview::ViewMode::MaskEdit);
        const QPointF oriented(0.2, 0.9);
        const QPointF source = cropview::sourceNormFromOriented(oriented, x, cropview::ViewMode::MaskEdit);
        const QPointF roundTrip = cropview::orientedNormFromSource(source, x, cropview::ViewMode::MaskEdit);
        CHECK(near(roundTrip, oriented));

        // Outside MaskEdit, both are identity regardless of the transform passed in.
        CHECK(near(cropview::sourceNormFromOriented(oriented, x, cropview::ViewMode::Applied), oriented));
        CHECK(near(cropview::orientedNormFromSource(oriented, x, cropview::ViewMode::None), oriented));
    }

    // Straighten only applies in Applied/Editing, not None/MaskEdit; a near-zero
    // angle should leave the transform close to its no-straighten counterpart.
    {
        CropState crop;
        crop.straighten = 5.0;
        const QMatrix4x4 xApplied = cropview::texXform(tex, crop, cropview::ViewMode::Applied);
        const QMatrix4x4 xMaskEdit = cropview::texXform(tex, crop, cropview::ViewMode::MaskEdit);
        // MaskEdit ignores straighten entirely -> identity (no rotation/flip/crop-offset).
        CHECK(xMaskEdit.isIdentity());
        // Applied does not, so it must differ from identity.
        CHECK(!xApplied.isIdentity());
    }

    std::puts("cropviewtransform_test: OK");
    return 0;
}
