#include "core/ContentCredentials.h"

#include "core/EditNode.h"
#include "core/NodeFactory.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#ifdef LUMEN_HAVE_C2PA
#include <c2pa.h>
#endif

namespace credentials {

namespace {

const QString kSoftwareName = QStringLiteral("Lumen");

QJsonObject softwareAgent(const QString &version)
{
    QJsonObject o;
    o[QStringLiteral("name")] = kSoftwareName;
    if (!version.isEmpty())
        o[QStringLiteral("version")] = version;
    return o;
}

// How each node type is reported: the C2PA action it maps to, and the label
// listed in that action's description.
struct NodeAction {
    const char *type;
    const char *action;
    const char *label;
};
const NodeAction kNodeActions[] = {
    {"lens", "c2pa.edited", "Lens & perspective correction"},
    {"heal", "c2pa.edited", "Healing brush (texture copied from elsewhere in the same photo)"},
    {"retouch", "c2pa.drawing", "Retouch brush (colour sampled from the same photo)"},
    {"denoise", "c2pa.filtered", "Denoise"},
    {"defringe", "c2pa.filtered", "Defringe"},
    {"sharpen", "c2pa.filtered", "Sharpen"},
    {"structure", "c2pa.filtered", "Structure"},
    {"dodgeburn", "c2pa.color_adjustments", "Dodge & burn"},
    {"tune", "c2pa.color_adjustments", "Tone & white balance"},
    {"colormixer", "c2pa.color_adjustments", "Colour mixer"},
    {"curves", "c2pa.color_adjustments", "Curves"},
    {"colorgrade", "c2pa.color_adjustments", "Colour grading"},
    {"lut", "c2pa.color_adjustments", "Look (3D LUT)"},
    {"mono", "c2pa.color_adjustments", "Monochrome"},
    {"grain", "c2pa.filtered", "Film grain"},
};

const NodeAction *lookupNode(const QString &type)
{
    for (const NodeAction &n : kNodeActions)
        if (type == QLatin1String(n.type))
            return &n;
    return nullptr;
}

// Collects actions in first-seen order, merging labels per (action, local).
class ActionList {
public:
    void add(const QString &action, const QString &label, bool local)
    {
        for (Entry &e : m_entries) {
            if (e.action == action && e.local == local) {
                if (!e.labels.contains(label))
                    e.labels << label;
                return;
            }
        }
        m_entries.push_back({action, local, {label}});
    }

    QJsonArray toJson(const QString &version) const
    {
        QJsonArray out;
        for (const Entry &e : m_entries) {
            QJsonObject a;
            a[QStringLiteral("action")] = e.action;
            a[QStringLiteral("softwareAgent")] = softwareAgent(version);
            QString desc = e.labels.join(QStringLiteral(", "));
            if (e.local)
                desc += QStringLiteral(" (local adjustment through a layer mask)");
            a[QStringLiteral("description")] = desc;
            out.append(a);
        }
        return out;
    }

private:
    struct Entry {
        QString action;
        bool local;
        QStringList labels;
    };
    std::vector<Entry> m_entries;
};

// Structural JSON equality with a relative tolerance on numbers (states pass
// through float members, so exact double equality would be brittle).
bool jsonNear(const QJsonValue &a, const QJsonValue &b)
{
    if (a.isDouble() && b.isDouble()) {
        const double x = a.toDouble(), y = b.toDouble();
        return std::abs(x - y) <= 1e-6 * std::max(1.0, std::abs(x));
    }
    if (a.type() != b.type())
        return false;
    if (a.isObject()) {
        const QJsonObject oa = a.toObject(), ob = b.toObject();
        if (oa.size() != ob.size())
            return false;
        for (auto it = oa.begin(); it != oa.end(); ++it) {
            if (!ob.contains(it.key()) || !jsonNear(it.value(), ob.value(it.key())))
                return false;
        }
        return true;
    }
    if (a.isArray()) {
        const QJsonArray xa = a.toArray(), xb = b.toArray();
        if (xa.size() != xb.size())
            return false;
        for (qsizetype i = 0; i < xa.size(); ++i)
            if (!jsonNear(xa[i], xb[i]))
                return false;
        return true;
    }
    return a == b;
}

void removeKeys(QJsonObject &o, std::initializer_list<const char *> keys)
{
    for (const char *k : keys)
        o.remove(QLatin1String(k));
}

// Drops metadata that describes the camera/source rather than an edit.
QJsonObject editOnly(const QString &type, QJsonObject s, const QJsonObject &fresh)
{
    s.remove(QStringLiteral("enabled"));
    if (type == QLatin1String("tune")) {
        // White balance left at the as-shot value is the camera's rendering, not
        // an adjustment: treat it like the default temperature.
        const double asShot = s.value(QStringLiteral("asShotKelvin")).toDouble(0.0);
        if (asShot > 0.0
            && std::abs(s.value(QStringLiteral("kelvin")).toDouble() - asShot) < 0.5)
            s[QStringLiteral("kelvin")] = fresh.value(QStringLiteral("kelvin"));
        removeKeys(s, {"asShotKelvin", "camToRgb", "xyzToCam"});
    } else if (type == QLatin1String("lens")) {
        removeKeys(s, {"cameraMaker", "cameraModel", "lensModel", "focalLength", "aperture",
                       "focusDistance", "cropFactor"});
    }
    return s;
}

const QSet<QString> &generativeSourceTypes()
{
    // IPTC digital source types that mean a trained model generated or
    // composited content (https://cv.iptc.org/newscodes/digitalsourcetype/).
    static const QSet<QString> kTypes = {
        QStringLiteral("trainedAlgorithmicMedia"),
        QStringLiteral("compositeWithTrainedAlgorithmicMedia"),
        QStringLiteral("trainedAlgorithmicData"),
        QStringLiteral("compositeSynthetic"),
    };
    return kTypes;
}

bool scanForGenerative(const QJsonValue &v)
{
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (it.key() == QLatin1String("digitalSourceType") && it.value().isString()) {
                const QString t = it.value().toString().section(QLatin1Char('/'), -1);
                if (generativeSourceTypes().contains(t))
                    return true;
            }
            if (scanForGenerative(it.value()))
                return true;
        }
    } else if (v.isArray()) {
        for (const QJsonValue &e : v.toArray())
            if (scanForGenerative(e))
                return true;
    }
    return false;
}

// Replace absolute local paths (e.g. a LUT's source path) with file names, so
// the recipe doesn't publish the user's directory layout.
QJsonValue stripPaths(const QJsonValue &v)
{
    if (v.isObject()) {
        QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (it.key() == QLatin1String("lut") && it.value().isString())
                it.value() = QFileInfo(it.value().toString()).fileName();
            else
                it.value() = stripPaths(it.value());
        }
        return o;
    }
    if (v.isArray()) {
        QJsonArray a = v.toArray();
        for (qsizetype i = 0; i < a.size(); ++i)
            a[i] = stripPaths(a[i]);
        return a;
    }
    return v;
}

void setError(QString *error, const QString &msg)
{
    if (error)
        *error = msg;
}

} // namespace

QStringList supportedAlgorithms()
{
    return {QStringLiteral("es256"), QStringLiteral("es384"), QStringLiteral("es512"),
            QStringLiteral("ps256"), QStringLiteral("ps384"), QStringLiteral("ps512"),
            QStringLiteral("ed25519")};
}

bool isNeutralNode(const QString &type, const QJsonObject &state)
{
    if (type == QLatin1String("lut")) {
        // No look loaded, or loaded at zero strength, changes nothing.
        if (!state.contains(QStringLiteral("lutData")) && !state.contains(QStringLiteral("lut")))
            return true;
        if (std::abs(state.value(QStringLiteral("intensity")).toDouble(1.0)) < 1e-6)
            return true;
    }
    const std::unique_ptr<EditNode> fresh = createNode(type);
    if (!fresh)
        return false; // unknown type: assume it did something
    const QJsonObject freshState = fresh->saveState();
    return jsonNear(editOnly(type, state, freshState), editOnly(type, freshState, freshState));
}

QJsonArray actionsFor(const QJsonObject &graphState, bool resized, const QString &softwareVersion)
{
    ActionList list;

    const QJsonArray layers = graphState.value(QStringLiteral("layers")).toArray();
    for (qsizetype li = 0; li < layers.size(); ++li) {
        const QJsonObject layer = layers[li].toObject();
        const bool local = li > 0; // non-Base layers apply through a mask
        if (local
            && (!layer.value(QStringLiteral("enabled")).toBool(true)
                || layer.value(QStringLiteral("opacity")).toDouble(1.0) <= 0.0))
            continue;
        for (const QJsonValue &nv : layer.value(QStringLiteral("nodes")).toArray()) {
            const QJsonObject node = nv.toObject();
            const QString type = node.value(QStringLiteral("type")).toString();
            const QJsonObject state = node.value(QStringLiteral("state")).toObject();
            if (!state.value(QStringLiteral("enabled")).toBool(true) || isNeutralNode(type, state))
                continue;
            if (const NodeAction *na = lookupNode(type))
                list.add(QString::fromLatin1(na->action), QString::fromUtf8(na->label), local);
            else // a node type this table doesn't know yet: still report it
                list.add(QStringLiteral("c2pa.edited"), type, local);
        }
    }

    // Geometry and finishing live on the graph itself (present only when not
    // identity — see EditGraph::saveState).
    const QJsonObject crop = graphState.value(QStringLiteral("crop")).toObject();
    if (!crop.isEmpty() && crop.value(QStringLiteral("enabled")).toBool(true)) {
        const double x = crop.value(QStringLiteral("x")).toDouble(0.0);
        const double y = crop.value(QStringLiteral("y")).toDouble(0.0);
        const double w = crop.value(QStringLiteral("w")).toDouble(1.0);
        const double h = crop.value(QStringLiteral("h")).toDouble(1.0);
        if (x > 1e-6 || y > 1e-6 || w < 1.0 - 1e-6 || h < 1.0 - 1e-6)
            list.add(QStringLiteral("c2pa.cropped"), QStringLiteral("Crop"), false);
        if (crop.value(QStringLiteral("rotation")).toInt(0) != 0
            || crop.value(QStringLiteral("flipH")).toBool(false)
            || crop.value(QStringLiteral("flipV")).toBool(false))
            list.add(QStringLiteral("c2pa.orientation"), QStringLiteral("Rotate / flip"), false);
        if (std::abs(crop.value(QStringLiteral("straighten")).toDouble(0.0)) > 1e-6)
            list.add(QStringLiteral("c2pa.edited"), QStringLiteral("Straighten"), false);
    }
    const QJsonObject vignette = graphState.value(QStringLiteral("vignette")).toObject();
    if (vignette.value(QStringLiteral("enabled")).toBool(false)
        && std::abs(vignette.value(QStringLiteral("amount")).toDouble(0.0)) >= 1e-4)
        list.add(QStringLiteral("c2pa.filtered"), QStringLiteral("Vignette"), false);

    if (resized)
        list.add(QStringLiteral("c2pa.resized"), QStringLiteral("Downscaled for export"), false);

    return list.toJson(softwareVersion);
}

QJsonObject recipeFor(const Provenance &p)
{
    QJsonObject exportOpts;
    exportOpts[QStringLiteral("quality")] = p.opts.quality;
    exportOpts[QStringLiteral("bits")] = p.opts.bits;
    exportOpts[QStringLiteral("longEdge")] = p.opts.longEdge;
    exportOpts[QStringLiteral("colorSpace")] = static_cast<int>(p.opts.colorSpace);

    QJsonObject r;
    r[QStringLiteral("lumenVersion")] = p.softwareVersion;
    r[QStringLiteral("source")] = p.sourceName;
    r[QStringLiteral("export")] = exportOpts;
    r[QStringLiteral("graph")] = stripPaths(p.graphState).toObject();
    return r;
}

QJsonObject manifestDefinition(const Provenance &p, const QString &outputName,
                               bool ingredientInDefinition)
{
    QJsonObject def;
    def[QStringLiteral("claim_generator_info")] = QJsonArray{softwareAgent(p.softwareVersion)};
    def[QStringLiteral("title")] = outputName;
    if (const QString mime = mimeForName(outputName); !mime.isEmpty())
        def[QStringLiteral("format")] = mime;

    QJsonArray assertions;
    const QJsonArray actions = actionsFor(p.graphState, p.resized, p.softwareVersion);
    if (!actions.isEmpty()) {
        QJsonObject data;
        data[QStringLiteral("actions")] = actions;
        QJsonObject a;
        a[QStringLiteral("label")] = QStringLiteral("c2pa.actions.v2");
        a[QStringLiteral("data")] = data;
        assertions.append(a);
    }

    {
        QJsonObject data;
        data[QStringLiteral("generativeAI")] = false;
        data[QStringLiteral("statement")] = QStringLiteral(
            "Lumen applied only deterministic, non-machine-learning adjustments chosen by the "
            "user. It has no generative fill, AI upscaling or neural enhancement. This covers "
            "Lumen's own edits only; the source image's provenance is recorded by its "
            "ingredient.");
        QJsonObject a;
        a[QStringLiteral("label")] = QLatin1String(kDeclarationLabel);
        a[QStringLiteral("data")] = data;
        assertions.append(a);
    }

    if (p.includeRecipe) {
        QJsonObject a;
        a[QStringLiteral("label")] = QLatin1String(kRecipeLabel);
        a[QStringLiteral("data")] = recipeFor(p);
        assertions.append(a);
    }
    def[QStringLiteral("assertions")] = assertions;

    if (ingredientInDefinition) {
        QJsonObject ing;
        ing[QStringLiteral("title")] = p.sourceName;
        if (const QString mime = mimeForName(p.sourceName); !mime.isEmpty())
            ing[QStringLiteral("format")] = mime;
        ing[QStringLiteral("relationship")] = QStringLiteral("parentOf");
        def[QStringLiteral("ingredients")] = QJsonArray{ing};
    }
    return def;
}

bool reportShowsGenerativeAI(const QJsonObject &readerReport)
{
    return scanForGenerative(readerReport);
}

QString mimeForName(const QString &fileName)
{
    const QString ext = QFileInfo(fileName).suffix().toLower();
    static const std::pair<const char *, const char *> kMimes[] = {
        {"jpg", "image/jpeg"},   {"jpeg", "image/jpeg"},        {"png", "image/png"},
        {"tif", "image/tiff"},   {"tiff", "image/tiff"},        {"webp", "image/webp"},
        {"dng", "image/x-adobe-dng"}, {"heic", "image/heic"},   {"heif", "image/heif"},
        {"avif", "image/avif"},  {"gif", "image/gif"},          {"svg", "image/svg+xml"},
    };
    for (const auto &[e, m] : kMimes)
        if (ext == QLatin1String(e))
            return QString::fromLatin1(m);
    return {};
}

#ifdef LUMEN_HAVE_C2PA

namespace {

QString takeC2paError()
{
    char *e = c2pa_error();
    QString msg = e ? QString::fromUtf8(e) : QStringLiteral("unknown C2PA error");
    if (e)
        c2pa_free(e);
    return msg;
}

struct C2paFree {
    void operator()(const void *p) const
    {
        if (p)
            c2pa_free(p);
    }
};
template <typename T>
using C2paPtr = std::unique_ptr<T, C2paFree>;

// C2PA streams backed by a QIODevice (a QFile or a QBuffer).
QIODevice *dev(StreamContext *c) { return reinterpret_cast<QIODevice *>(c); }

intptr_t streamRead(StreamContext *c, uint8_t *data, intptr_t len)
{
    return static_cast<intptr_t>(dev(c)->read(reinterpret_cast<char *>(data), len));
}

intptr_t streamSeek(StreamContext *c, intptr_t offset, C2paSeekMode mode)
{
    QIODevice *d = dev(c);
    qint64 pos = offset;
    if (mode == Current)
        pos += d->pos();
    else if (mode == End)
        pos += d->size();
    if (pos < 0 || !d->seek(pos))
        return -1;
    return static_cast<intptr_t>(pos);
}

intptr_t streamWrite(StreamContext *c, const uint8_t *data, intptr_t len)
{
    return static_cast<intptr_t>(dev(c)->write(reinterpret_cast<const char *>(data), len));
}

intptr_t streamFlush(StreamContext *c)
{
    if (auto *f = qobject_cast<QFileDevice *>(dev(c)))
        return f->flush() ? 0 : -1;
    return 0;
}

struct StreamRelease {
    void operator()(C2paStream *s) const
    {
        if (s)
            c2pa_release_stream(s);
    }
};
using StreamPtr = std::unique_ptr<C2paStream, StreamRelease>;

StreamPtr makeStream(QIODevice *d)
{
    return StreamPtr(c2pa_create_stream(reinterpret_cast<StreamContext *>(d), streamRead,
                                        streamSeek, streamWrite, streamFlush));
}

bool mimeListed(const char *const *list, uintptr_t count, const QString &mime)
{
    bool found = false;
    for (uintptr_t i = 0; i < count && !found; ++i)
        found = mime == QLatin1String(list[i]);
    c2pa_free_string_array(list, count);
    return found;
}

bool readerSupports(const QString &mime)
{
    uintptr_t n = 0;
    const char *const *list = c2pa_reader_supported_mime_types(&n);
    return list && mimeListed(list, n, mime);
}

// Lumen's signing context: no thumbnails at all. The claim thumbnail would be
// harmless, but an ingredient thumbnail is a preview of the *unedited* original
// — it would leak whatever the user cropped or healed out.
C2paPtr<C2paContext> makeContext(QString *error)
{
    C2paPtr<C2paSettings> settings(c2pa_settings_new());
    if (!settings
        || c2pa_settings_set_value(settings.get(), "builder.thumbnail.enabled", "false") < 0) {
        setError(error, takeC2paError());
        return nullptr;
    }
    C2paContextBuilder *cb = c2pa_context_builder_new();
    if (!cb || c2pa_context_builder_set_settings(cb, settings.get()) < 0) {
        setError(error, takeC2paError());
        if (cb)
            c2pa_free(cb);
        return nullptr;
    }
    C2paPtr<C2paContext> ctx(c2pa_context_builder_build(cb)); // consumes cb
    if (!ctx)
        setError(error, takeC2paError());
    return ctx;
}

bool readFile(const QString &path, QByteArray *out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot read %1: %2").arg(path, f.errorString()));
        return false;
    }
    *out = f.readAll();
    return true;
}

} // namespace

bool available()
{
    return true;
}

QString libraryVersion()
{
    char *v = c2pa_version();
    const QString s = v ? QString::fromUtf8(v) : QString();
    if (v)
        c2pa_free(v);
    return s;
}

bool sign(const QString &encodedPath, const QString &destPath, const Provenance &p,
          const SignerConfig &signer, QString *error)
{
    if (!signer.isConfigured()) {
        setError(error, QStringLiteral("No signing certificate is configured"));
        return false;
    }
    const QString outMime = mimeForName(destPath);
    if (outMime.isEmpty()) {
        setError(error, QStringLiteral("Content Credentials don't support this file type"));
        return false;
    }

    QByteArray cert, key;
    if (!readFile(signer.certChainPath, &cert, error)
        || !readFile(signer.privateKeyPath, &key, error))
        return false;

    const C2paPtr<C2paContext> ctx = makeContext(error);
    if (!ctx)
        return false;

    const QString outputName = QFileInfo(destPath).fileName();
    // Build the manifest. Preferably the parent ingredient is read from the
    // source bytes (hashing them and carrying over any credentials the source
    // already has); if libc2pa can't parse that format, it's described by
    // title/format only.
    auto build = [&](bool ingredientInDefinition) -> C2paPtr<C2paBuilder> {
        const QByteArray def =
            QJsonDocument(manifestDefinition(p, outputName, ingredientInDefinition))
                .toJson(QJsonDocument::Compact);
        C2paBuilder *raw = c2pa_builder_from_context(ctx.get());
        if (raw)
            raw = c2pa_builder_with_definition(raw, def.constData()); // consumes raw
        C2paPtr<C2paBuilder> b(raw);
        if (!b) {
            setError(error, takeC2paError());
            return nullptr;
        }
        if (c2pa_builder_set_intent(b.get(), Edit, Empty) < 0) {
            setError(error, takeC2paError());
            return nullptr;
        }
        if (!ingredientInDefinition) {
            QJsonObject ing;
            ing[QStringLiteral("title")] = p.sourceName;
            ing[QStringLiteral("relationship")] = QStringLiteral("parentOf");
            const QByteArray ingJson = QJsonDocument(ing).toJson(QJsonDocument::Compact);
            const QByteArray mime = mimeForName(p.sourceName).toUtf8();
            QBuffer buf;
            buf.setData(p.sourceBytes);
            buf.open(QIODevice::ReadOnly);
            const StreamPtr s = makeStream(&buf);
            if (!s
                || c2pa_builder_add_ingredient_from_stream(b.get(), ingJson.constData(),
                                                           mime.constData(), s.get())
                       < 0) {
                setError(error, takeC2paError());
                return nullptr;
            }
        }
        return b;
    };

    const QString srcMime = mimeForName(p.sourceName);
    C2paPtr<C2paBuilder> builder;
    if (!p.sourceBytes.isEmpty() && !srcMime.isEmpty() && readerSupports(srcMime))
        builder = build(false);
    if (!builder)
        builder = build(true);
    if (!builder)
        return false;

    const QByteArray alg = signer.alg.toLower().toUtf8();
    const QByteArray tsa = signer.tsaUrl.trimmed().toUtf8();
    const C2paSignerInfo info{alg.constData(), cert.constData(), key.constData(),
                              tsa.isEmpty() ? nullptr : tsa.constData()};
    const C2paPtr<C2paSigner> c2paSigner(c2pa_signer_from_info(&info));
    if (!c2paSigner) {
        setError(error, QStringLiteral("Signing certificate rejected: %1").arg(takeC2paError()));
        return false;
    }

    QFile src(encodedPath);
    if (!src.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot read %1: %2").arg(encodedPath, src.errorString()));
        return false;
    }
    QFile dst(destPath);
    if (!dst.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        setError(error, QStringLiteral("Cannot write %1: %2").arg(destPath, dst.errorString()));
        return false;
    }

    int64_t rc = -1;
    {
        const StreamPtr in = makeStream(&src);
        const StreamPtr out = makeStream(&dst);
        // The manifest-bytes out-parameter is documented as optional, but
        // libc2pa 0.91 rejects NULL ("NullParameter: manifest_bytes_ptr").
        const unsigned char *manifestBytes = nullptr;
        if (in && out)
            rc = c2pa_builder_sign(builder.get(), outMime.toUtf8().constData(), in.get(),
                                   out.get(), c2paSigner.get(), &manifestBytes);
        if (manifestBytes)
            c2pa_free(manifestBytes);
    }
    const bool flushed = dst.flush();
    dst.close();
    if (rc < 0 || !flushed) {
        setError(error, rc < 0 ? takeC2paError() : dst.errorString());
        QFile::remove(destPath);
        return false;
    }
    return true;
}

QJsonObject readReport(const QByteArray &bytes, const QString &fileName)
{
    const QString mime = mimeForName(fileName);
    if (bytes.isEmpty() || mime.isEmpty() || !readerSupports(mime))
        return {};
    const C2paPtr<C2paContext> ctx(c2pa_context_new());
    if (!ctx)
        return {};
    QBuffer buf;
    buf.setData(bytes);
    buf.open(QIODevice::ReadOnly);
    const StreamPtr s = makeStream(&buf);
    C2paReader *raw = c2pa_reader_from_context(ctx.get());
    if (raw && s)
        raw = c2pa_reader_with_stream(raw, mime.toUtf8().constData(), s.get()); // consumes
    const C2paPtr<C2paReader> reader(raw);
    if (!reader) {
        takeC2paError(); // "no manifest" is the common case — not an error to report
        return {};
    }
    C2paPtr<char> json(c2pa_reader_json(reader.get()));
    if (!json)
        return {};
    return QJsonDocument::fromJson(QByteArray(json.get())).object();
}

#else // !LUMEN_HAVE_C2PA

bool available()
{
    return false;
}

QString libraryVersion()
{
    return {};
}

bool sign(const QString &, const QString &, const Provenance &, const SignerConfig &,
          QString *error)
{
    setError(error, QStringLiteral("Content Credentials are not available in this build"));
    return false;
}

QJsonObject readReport(const QByteArray &, const QString &)
{
    return {};
}

#endif // LUMEN_HAVE_C2PA

SourceInfo inspectSource(const QByteArray &bytes, const QString &fileName)
{
    SourceInfo info;
    const QJsonObject report = readReport(bytes, fileName);
    info.hasManifest = !report.value(QStringLiteral("manifests")).toObject().isEmpty();
    info.generativeAI = info.hasManifest && reportShowsGenerativeAI(report);
    return info;
}

bool testSigner(const SignerConfig &signer, QString *summary, QString *error)
{
    if (!available()) {
        setError(error, QStringLiteral("Content Credentials are not available in this build"));
        return false;
    }
    QTemporaryDir dir;
    if (!dir.isValid()) {
        setError(error, QStringLiteral("Cannot create a temporary folder"));
        return false;
    }
    const QString plain = dir.filePath(QStringLiteral("test.jpg"));
    const QString signedPath = dir.filePath(QStringLiteral("signed.jpg"));
    if (!Image::black(16, 16).saveToFile(plain, error))
        return false;

    Provenance p;
    p.sourceName = QStringLiteral("test.jpg");
    if (!sign(plain, signedPath, p, signer, error))
        return false;

    QFile f(signedPath);
    if (!f.open(QIODevice::ReadOnly)) {
        setError(error, f.errorString());
        return false;
    }
    const QJsonObject report = readReport(f.readAll(), signedPath);
    const QString active = report.value(QStringLiteral("active_manifest")).toString();
    const QJsonObject manifest =
        report.value(QStringLiteral("manifests")).toObject().value(active).toObject();
    if (manifest.isEmpty()) {
        setError(error, QStringLiteral("Signed, but the manifest could not be read back"));
        return false;
    }

    // Any validation failure other than "signer not on the trust list" means
    // the credentials are broken (bad chain, wrong key, unsupported cert...).
    QStringList failures;
    bool untrusted = false;
    const QJsonObject results = report.value(QStringLiteral("validation_results")).toObject();
    for (const QJsonValue &v : results.value(QStringLiteral("activeManifest"))
                                   .toObject()
                                   .value(QStringLiteral("failure"))
                                   .toArray()) {
        const QString code = v.toObject().value(QStringLiteral("code")).toString();
        if (code == QLatin1String("signingCredential.untrusted"))
            untrusted = true;
        else
            failures << code;
    }
    if (!failures.isEmpty()) {
        setError(error, QStringLiteral("Verification failed: %1").arg(failures.join(QStringLiteral(", "))));
        return false;
    }

    if (summary) {
        const QJsonObject sig = manifest.value(QStringLiteral("signature_info")).toObject();
        const QString cn = sig.value(QStringLiteral("common_name")).toString();
        const QString issuer = sig.value(QStringLiteral("issuer")).toString();
        *summary = QStringLiteral("Signs as \"%1\"%2.")
                       .arg(cn.isEmpty() ? issuer : cn,
                            issuer.isEmpty() || issuer == cn
                                ? QString()
                                : QStringLiteral(", issued by %1").arg(issuer));
        if (untrusted
            || report.value(QStringLiteral("validation_state")).toString()
                   != QLatin1String("Trusted"))
            *summary += QStringLiteral(
                "\nThis certificate is not on the C2PA trust list, so verifiers will show the "
                "signer as unrecognised. The credentials are still tamper-evident.");
    }
    return true;
}

} // namespace credentials
