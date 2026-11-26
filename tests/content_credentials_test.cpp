// Content Credentials (C2PA): how edits map to C2PA actions, what the manifest
// definition carries, and — when this build links libc2pa — that a signed
// export reads back with the expected provenance.

#include "core/ContentCredentials.h"
#include "core/CropState.h"
#include "core/EditGraph.h"
#include "core/HealNode.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/Layer.h"
#include "core/LensCorrectionNode.h"
#include "core/LutNode.h"
#include "core/SharpenNode.h"
#include "core/TuneNode.h"
#include "core/Vignette.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>
#include <memory>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

namespace {

const QString kVersion = QStringLiteral("9.9.9-test");

// The action object for `name`, or an empty object.
QJsonObject findAction(const QJsonArray &actions, const QString &name)
{
    for (const QJsonValue &v : actions)
        if (v.toObject().value(QStringLiteral("action")).toString() == name)
            return v.toObject();
    return {};
}

bool anyDigitalSourceType(const QJsonArray &actions)
{
    for (const QJsonValue &v : actions)
        if (v.toObject().contains(QStringLiteral("digitalSourceType")))
            return true;
    return false;
}

QJsonObject findAssertion(const QJsonObject &def, const char *label)
{
    for (const QJsonValue &v : def.value(QStringLiteral("assertions")).toArray())
        if (v.toObject().value(QStringLiteral("label")).toString() == QLatin1String(label))
            return v.toObject();
    return {};
}

// A graph shaped like the app's Base layer: every node present, all neutral.
void fillGraph(EditGraph &g)
{
    g.setSource(Image::black(32, 24));
    g.addNode(std::make_unique<LensCorrectionNode>());
    g.addNode(std::make_unique<HealNode>());
    g.addNode(std::make_unique<SharpenNode>());
    g.addNode(std::make_unique<TuneNode>());
    g.addNode(std::make_unique<LutNode>());
}

template <typename T>
T *nodeOf(EditGraph &g, const char *type)
{
    for (int i = 0; i < g.activeLayer().nodeCount(); ++i)
        if (g.nodeAt(i)->typeName() == QLatin1String(type))
            return static_cast<T *>(g.nodeAt(i));
    return nullptr;
}

int pureTests()
{
    // A pristine graph changes nothing, so it reports no actions (the library
    // itself adds c2pa.opened for the parent ingredient).
    {
        EditGraph g;
        fillGraph(g);
        CHECK(credentials::actionsFor(g.saveState(), false, kVersion).isEmpty());
        // ...unless the export was downscaled.
        const QJsonArray a = credentials::actionsFor(g.saveState(), true, kVersion);
        CHECK(a.size() == 1);
        CHECK(!findAction(a, QStringLiteral("c2pa.resized")).isEmpty());
    }

    // Exposure → one c2pa.color_adjustments naming the tone node, with Lumen as
    // the software agent and no digitalSourceType anywhere.
    {
        EditGraph g;
        fillGraph(g);
        nodeOf<TuneNode>(g, "tune")->setExposure(0.7f);
        const QJsonArray a = credentials::actionsFor(g.saveState(), false, kVersion);
        CHECK(a.size() == 1);
        const QJsonObject ca = findAction(a, QStringLiteral("c2pa.color_adjustments"));
        CHECK(!ca.isEmpty());
        CHECK(ca.value(QStringLiteral("description")).toString().contains(QStringLiteral("Tone")));
        const QJsonObject agent = ca.value(QStringLiteral("softwareAgent")).toObject();
        CHECK(agent.value(QStringLiteral("name")).toString() == QStringLiteral("Lumen"));
        CHECK(agent.value(QStringLiteral("version")).toString() == kVersion);
        CHECK(!anyDigitalSourceType(a));

        // Disabled, the same edit is not reported.
        nodeOf<TuneNode>(g, "tune")->setEnabled(false);
        CHECK(credentials::actionsFor(g.saveState(), false, kVersion).isEmpty());
    }

    // Healing → c2pa.edited; sharpening → c2pa.filtered; crop and rotation →
    // c2pa.cropped + c2pa.orientation; vignette → c2pa.filtered (merged).
    {
        EditGraph g;
        fillGraph(g);
        MaskBuffer m;
        m.width = 4;
        m.height = 4;
        m.data.assign(16, 0.0f);
        m.data[5] = 1.0f;
        nodeOf<HealNode>(g, "heal")->setHealMask(m);
        nodeOf<SharpenNode>(g, "sharpen")->restoreState(
            QJsonObject{{QStringLiteral("sharpenEnabled"), true}, {QStringLiteral("amount"), 0.8}});
        CropState c;
        c.rect = QRectF(0.1, 0.1, 0.5, 0.5);
        c.rotation = 90;
        g.setCrop(c);
        VignetteParams v;
        v.enabled = true;
        v.amount = -0.5f;
        g.setVignette(v);

        const QJsonArray a = credentials::actionsFor(g.saveState(), false, kVersion);
        CHECK(!findAction(a, QStringLiteral("c2pa.edited")).isEmpty());
        CHECK(findAction(a, QStringLiteral("c2pa.edited"))
                  .value(QStringLiteral("description"))
                  .toString()
                  .contains(QStringLiteral("Healing")));
        const QString filtered = findAction(a, QStringLiteral("c2pa.filtered"))
                                     .value(QStringLiteral("description"))
                                     .toString();
        CHECK(filtered.contains(QStringLiteral("Sharpen")));
        CHECK(filtered.contains(QStringLiteral("Vignette")));
        CHECK(!findAction(a, QStringLiteral("c2pa.cropped")).isEmpty());
        CHECK(!findAction(a, QStringLiteral("c2pa.orientation")).isEmpty());
        CHECK(!anyDigitalSourceType(a));
    }

    // Edits on a masked layer are reported as local adjustments; a hidden layer
    // is not reported at all.
    {
        EditGraph g;
        fillGraph(g);
        Layer &layer = g.addLayer();
        auto tune = std::make_unique<TuneNode>();
        tune->setContrast(0.4f);
        layer.addNode(std::move(tune));
        QJsonArray a = credentials::actionsFor(g.saveState(), false, kVersion);
        CHECK(a.size() == 1);
        CHECK(findAction(a, QStringLiteral("c2pa.color_adjustments"))
                  .value(QStringLiteral("description"))
                  .toString()
                  .contains(QStringLiteral("local adjustment")));
        layer.setEnabled(false);
        CHECK(credentials::actionsFor(g.saveState(), false, kVersion).isEmpty());
    }

    // Camera metadata is not an edit: as-shot white balance with a colour
    // profile, or lens EXIF alone, stays neutral.
    {
        TuneNode fresh;
        QJsonObject s = fresh.saveState();
        s[QStringLiteral("asShotKelvin")] = 5200.0;
        s[QStringLiteral("kelvin")] = 5200.0;
        s[QStringLiteral("camToRgb")] = QJsonArray{1, 0, 0, 0, 1, 0, 0, 0, 1};
        CHECK(credentials::isNeutralNode(QStringLiteral("tune"), s));
        s[QStringLiteral("kelvin")] = 6100.0; // moved off as-shot: a real WB edit
        CHECK(!credentials::isNeutralNode(QStringLiteral("tune"), s));

        LensCorrectionNode lens;
        QJsonObject ls = lens.saveState();
        ls[QStringLiteral("cameraMaker")] = QStringLiteral("Fujifilm");
        ls[QStringLiteral("lensModel")] = QStringLiteral("XF23mmF2");
        ls[QStringLiteral("focalLength")] = 23.0;
        CHECK(credentials::isNeutralNode(QStringLiteral("lens"), ls));

        // An unknown node type is assumed to have done something.
        CHECK(!credentials::isNeutralNode(QStringLiteral("not-a-node"), QJsonObject{}));
    }

    // Manifest definition: always the declaration, the recipe only on opt-in,
    // and the recipe never carries local directory paths.
    {
        EditGraph g;
        fillGraph(g);
        nodeOf<TuneNode>(g, "tune")->setExposure(0.3f);
        QJsonObject gs = g.saveState();
        // Simulate a loaded LUT referenced by an absolute path.
        QJsonArray layers = gs.value(QStringLiteral("layers")).toArray();
        QJsonObject base = layers[0].toObject();
        QJsonArray nodes = base.value(QStringLiteral("nodes")).toArray();
        for (qsizetype i = 0; i < nodes.size(); ++i) {
            QJsonObject n = nodes[i].toObject();
            if (n.value(QStringLiteral("type")).toString() == QLatin1String("lut")) {
                QJsonObject st = n.value(QStringLiteral("state")).toObject();
                st[QStringLiteral("lut")] = QStringLiteral("/home/someone/looks/Film.cube");
                n[QStringLiteral("state")] = st;
                nodes[i] = n;
            }
        }
        base[QStringLiteral("nodes")] = nodes;
        layers[0] = base;
        gs[QStringLiteral("layers")] = layers;

        credentials::Provenance p;
        p.sourceName = QStringLiteral("DSCF0001.RAF");
        p.graphState = gs;
        p.softwareVersion = kVersion;

        QJsonObject def = credentials::manifestDefinition(p, QStringLiteral("out.jpg"));
        CHECK(def.value(QStringLiteral("format")).toString() == QStringLiteral("image/jpeg"));
        CHECK(!findAssertion(def, credentials::kDeclarationLabel).isEmpty());
        CHECK(findAssertion(def, credentials::kRecipeLabel).isEmpty());
        CHECK(!def.contains(QStringLiteral("ingredients")));

        p.includeRecipe = true;
        def = credentials::manifestDefinition(p, QStringLiteral("out.jpg"), true);
        const QJsonObject recipe = findAssertion(def, credentials::kRecipeLabel);
        CHECK(!recipe.isEmpty());
        const QByteArray recipeText = QJsonDocument(recipe).toJson(QJsonDocument::Compact);
        CHECK(recipeText.contains("Film.cube"));
        CHECK(!recipeText.contains("/home/someone"));
        // Fallback ingredient (unparseable RAW): described by title only.
        const QJsonArray ings = def.value(QStringLiteral("ingredients")).toArray();
        CHECK(ings.size() == 1);
        CHECK(ings[0].toObject().value(QStringLiteral("relationship")).toString()
              == QStringLiteral("parentOf"));
        CHECK(ings[0].toObject().value(QStringLiteral("title")).toString() == p.sourceName);
    }

    // Detecting generative AI anywhere in a source's credentials.
    {
        const QJsonObject clean = QJsonDocument::fromJson(R"({"manifests":{"m":{"assertions":[
            {"label":"c2pa.actions.v2","data":{"actions":[{"action":"c2pa.created",
            "digitalSourceType":"http://cv.iptc.org/newscodes/digitalsourcetype/digitalCapture"}]}}]}}})")
                                      .object();
        const QJsonObject genAI = QJsonDocument::fromJson(R"({"manifests":{"m":{"assertions":[
            {"label":"c2pa.actions.v2","data":{"actions":[{"action":"c2pa.edited",
            "digitalSourceType":"http://cv.iptc.org/newscodes/digitalsourcetype/compositeWithTrainedAlgorithmicMedia"}]}}]}}})")
                                      .object();
        CHECK(!credentials::reportShowsGenerativeAI(clean));
        CHECK(credentials::reportShowsGenerativeAI(genAI));
    }

    CHECK(credentials::mimeForName(QStringLiteral("a.TIF")) == QStringLiteral("image/tiff"));
    CHECK(credentials::mimeForName(QStringLiteral("a.cr3")).isEmpty());
    return 0;
}

#ifdef LUMEN_HAVE_C2PA

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QJsonObject activeManifest(const QJsonObject &report)
{
    return report.value(QStringLiteral("manifests"))
        .toObject()
        .value(report.value(QStringLiteral("active_manifest")).toString())
        .toObject();
}

QJsonArray manifestActions(const QJsonObject &manifest)
{
    for (const QJsonValue &v : manifest.value(QStringLiteral("assertions")).toArray()) {
        const QJsonObject a = v.toObject();
        if (a.value(QStringLiteral("label")).toString().startsWith(QStringLiteral("c2pa.actions")))
            return a.value(QStringLiteral("data")).toObject().value(QStringLiteral("actions")).toArray();
    }
    return {};
}

bool hasAssertion(const QJsonObject &manifest, const char *label)
{
    for (const QJsonValue &v : manifest.value(QStringLiteral("assertions")).toArray())
        if (v.toObject().value(QStringLiteral("label")).toString() == QLatin1String(label))
            return true;
    return false;
}

int c2paTests()
{
    const QString certs = QStringLiteral(LUMEN_TEST_C2PA_CERTS);
    credentials::SignerConfig signer;
    signer.certChainPath = certs + QStringLiteral("/es256.pub");
    signer.privateKeyPath = certs + QStringLiteral("/es256.pem");
    signer.alg = QStringLiteral("es256");

    QTemporaryDir dir;
    CHECK(dir.isValid());

    // The source: a plain JPEG with no credentials.
    const QString srcPath = dir.filePath(QStringLiteral("source.jpg"));
    CHECK(Image::black(32, 24).saveToFile(srcPath));
    const QByteArray srcBytes = readAll(srcPath);
    CHECK(!credentials::inspectSource(srcBytes, srcPath).hasManifest);

    EditGraph g;
    fillGraph(g);
    nodeOf<TuneNode>(g, "tune")->setExposure(0.5f);
    const Image rendered = g.result();
    CHECK(!rendered.isNull());

    credentials::Provenance p;
    p.sourceBytes = srcBytes;
    p.sourceName = QStringLiteral("source.jpg");
    p.graphState = g.saveState();
    p.softwareVersion = kVersion;

    for (const char *ext : {"jpg", "png", "tiff", "webp"}) {
        const QString encoded = dir.filePath(QStringLiteral("encoded.%1").arg(QLatin1String(ext)));
        const QString out = dir.filePath(QStringLiteral("out.%1").arg(QLatin1String(ext)));
        CHECK(rendered.saveToFile(encoded));
        p.includeRecipe = QLatin1String(ext) == QLatin1String("png");
        QString error;
        if (!credentials::sign(encoded, out, p, signer, &error)) {
            std::fprintf(stderr, "sign(%s) failed: %s\n", ext, qPrintable(error));
            return 1;
        }
        // Still a readable image of the same size.
        const Image back = Image::fromFile(out);
        CHECK(!back.isNull() && back.width() == 32 && back.height() == 24);

        const QJsonObject report = credentials::readReport(readAll(out), out);
        const QJsonObject m = activeManifest(report);
        CHECK(!m.isEmpty());
        const QJsonArray actions = manifestActions(m);
        CHECK(!findAction(actions, QStringLiteral("c2pa.opened")).isEmpty());
        CHECK(!findAction(actions, QStringLiteral("c2pa.color_adjustments")).isEmpty());
        CHECK(!anyDigitalSourceType(actions) || !credentials::reportShowsGenerativeAI(report));
        CHECK(hasAssertion(m, credentials::kDeclarationLabel));
        CHECK(hasAssertion(m, credentials::kRecipeLabel) == p.includeRecipe);
        // Exactly one parent ingredient, and no thumbnails anywhere: an
        // ingredient thumbnail would leak the unedited original.
        const QJsonArray ings = m.value(QStringLiteral("ingredients")).toArray();
        CHECK(ings.size() == 1);
        CHECK(ings[0].toObject().value(QStringLiteral("relationship")).toString()
              == QStringLiteral("parentOf"));
        CHECK(!ings[0].toObject().contains(QStringLiteral("thumbnail")));
        CHECK(!m.contains(QStringLiteral("thumbnail")));
        CHECK(credentials::inspectSource(readAll(out), out).hasManifest);
    }

    // Chaining: re-editing a signed export keeps its credentials as the parent's.
    {
        const QString signedSrc = dir.filePath(QStringLiteral("out.jpg"));
        credentials::Provenance p2 = p;
        p2.sourceBytes = readAll(signedSrc);
        p2.sourceName = QStringLiteral("out.jpg");
        const QString encoded = dir.filePath(QStringLiteral("encoded2.jpg"));
        const QString out = dir.filePath(QStringLiteral("out2.jpg"));
        CHECK(rendered.saveToFile(encoded));
        QString error;
        CHECK(credentials::sign(encoded, out, p2, signer, &error));
        const QJsonObject report = credentials::readReport(readAll(out), out);
        CHECK(report.value(QStringLiteral("manifests")).toObject().size() == 2);
        CHECK(!credentials::reportShowsGenerativeAI(report));
    }

    // A RAW-ish source libc2pa can't parse still yields a parent ingredient.
    {
        credentials::Provenance p3 = p;
        p3.sourceBytes = QByteArray("not really a raw file");
        p3.sourceName = QStringLiteral("DSCF0001.RAF");
        const QString encoded = dir.filePath(QStringLiteral("encoded3.jpg"));
        const QString out = dir.filePath(QStringLiteral("out3.jpg"));
        CHECK(rendered.saveToFile(encoded));
        QString error;
        if (!credentials::sign(encoded, out, p3, signer, &error)) {
            std::fprintf(stderr, "sign(raw source) failed: %s\n", qPrintable(error));
            return 1;
        }
        const QJsonObject m = activeManifest(credentials::readReport(readAll(out), out));
        CHECK(m.value(QStringLiteral("ingredients")).toArray().size() == 1);
    }

    // A wrong key fails cleanly and leaves no half-written file behind.
    {
        credentials::SignerConfig bad = signer;
        bad.privateKeyPath = dir.filePath(QStringLiteral("missing.pem"));
        const QString encoded = dir.filePath(QStringLiteral("encoded4.jpg"));
        const QString out = dir.filePath(QStringLiteral("out4.jpg"));
        CHECK(rendered.saveToFile(encoded));
        QString error;
        CHECK(!credentials::sign(encoded, out, p, bad, &error));
        CHECK(!error.isEmpty());
        CHECK(!QFile::exists(out));
    }

    // The settings dialog's Test button: the test cert signs, and is reported
    // as not on the trust list.
    {
        QString summary, error;
        if (!credentials::testSigner(signer, &summary, &error)) {
            std::fprintf(stderr, "testSigner failed: %s\n", qPrintable(error));
            return 1;
        }
        CHECK(summary.contains(QStringLiteral("C2PA Signer")));
        CHECK(summary.contains(QStringLiteral("not on the C2PA trust list")));
    }
    return 0;
}

#endif // LUMEN_HAVE_C2PA

} // namespace

int main(int /*argc*/, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "FAIL: libvips init\n");
        return 1;
    }
    if (const int rc = pureTests())
        return rc;
#ifdef LUMEN_HAVE_C2PA
    CHECK(credentials::available());
    if (const int rc = c2paTests())
        return rc;
    std::printf("content_credentials_test: OK (libc2pa %s)\n",
                qPrintable(credentials::libraryVersion()));
#else
    CHECK(!credentials::available());
    QString error;
    CHECK(!credentials::sign(QStringLiteral("a.jpg"), QStringLiteral("b.jpg"), {}, {}, &error));
    std::printf("content_credentials_test: OK (pure tests; built without libc2pa)\n");
#endif
    return 0;
}
