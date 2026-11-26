#pragma once

#include "core/Image.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// C2PA Content Credentials for exported images.
//
// After libvips has encoded an export, sign() embeds a C2PA manifest that
// records where the picture came from (the original as a "parentOf"
// ingredient, carrying any credentials it already had — e.g. from a C2PA
// camera) and what Lumen did to it, as standard C2PA actions
// (c2pa.color_adjustments, c2pa.cropped, c2pa.filtered, …). Lumen's actions
// never carry a digitalSourceType, the field C2PA uses to flag trained /
// algorithmic (AI) media, and a small declaration assertion states the same in
// words. Optionally the full edit recipe (the edit-graph snapshot) is attached.
//
// This is an attestation by whoever holds the signing key, not a proof: the
// user brings their own certificate (Lumen never generates or ships a key).
//
// libc2pa is an optional dependency. Without it (LUMEN_HAVE_C2PA undefined)
// available() is false and sign() fails with an explanatory error; the pure
// JSON helpers (actionsFor, manifestDefinition, …) work in every build so they
// can be unit-tested anywhere.
namespace credentials {

// Assertion labels Lumen adds besides the standard c2pa.actions.
inline constexpr const char *kDeclarationLabel = "com.github.vijaymathew.lumen.declaration";
inline constexpr const char *kRecipeLabel = "com.github.vijaymathew.lumen.recipe";

// The user's signing identity. Only file paths are stored (in QSettings);
// the key itself is read from disk at signing time and never copied.
struct SignerConfig {
    QString certChainPath;            // PEM: signing cert first, then intermediates
    QString privateKeyPath;           // PEM private key matching the signing cert
    QString alg = QStringLiteral("es256"); // es256|es384|es512|ps256|ps384|ps512|ed25519
    QString tsaUrl;                   // optional RFC 3161 timestamp server; empty = offline

    bool isConfigured() const { return !certChainPath.isEmpty() && !privateKeyPath.isEmpty(); }
};

// Signing algorithms accepted by SignerConfig::alg, in display order.
QStringList supportedAlgorithms();

// Everything the manifest describes about one export.
struct Provenance {
    QByteArray sourceBytes;      // the original encoded source (Document::sourceBytes)
    QString sourceName;          // its file name (no directory)
    QJsonObject graphState;      // EditGraph::saveState() of the exported edit
    Image::ExportOptions opts;   // how it was encoded
    bool resized = false;        // opts.longEdge actually downscaled the image
    bool includeRecipe = false;  // attach the edit-graph snapshot as an assertion
    QString softwareVersion;     // Lumen's version, for claim_generator_info / softwareAgent
};

// Whether this build can sign (links libc2pa).
bool available();
// The linked libc2pa version, or an empty string when unavailable.
QString libraryVersion();

// --- Pure JSON (no libc2pa needed) ----------------------------------------

// The C2PA actions describing the edits in `graphState`, one per kind of
// change (e.g. a single c2pa.color_adjustments listing "Tone, Curves").
// Disabled and neutral (default-valued) nodes are skipped. The c2pa.opened
// action is not included: the library adds it, tied to the parent ingredient.
QJsonArray actionsFor(const QJsonObject &graphState, bool resized,
                      const QString &softwareVersion);

// True when a node's saved state is equivalent to a freshly constructed node of
// that type — i.e. it changes nothing. Camera metadata a node carries (as-shot
// white balance, colour matrices, lens EXIF) is not an edit and is ignored.
bool isNeutralNode(const QString &type, const QJsonObject &state);

// The manifest definition handed to the C2PA builder for an export named
// `outputName`. When `ingredientInDefinition` is set, the parent ingredient is
// described here (title/format only) instead of being read from the source
// bytes — the fallback for source formats libc2pa can't parse (most RAWs).
QJsonObject manifestDefinition(const Provenance &p, const QString &outputName,
                               bool ingredientInDefinition = false);

// The recipe stored in the optional recipe assertion: the graph snapshot with
// local file paths reduced to file names, plus the export options.
QJsonObject recipeFor(const Provenance &p);

// True when any manifest in a C2PA report (Reader JSON) declares a generative
// or trained-algorithmic digitalSourceType anywhere in the chain.
bool reportShowsGenerativeAI(const QJsonObject &readerReport);

// MIME type for a file name's extension ("" when unknown).
QString mimeForName(const QString &fileName);

// --- libc2pa -----------------------------------------------------------------

// Embed a signed manifest describing `p` into the encoded image at
// `encodedPath`, writing the result to `destPath` (which must differ). Returns
// false with `error` set on failure; `destPath` is then removed.
bool sign(const QString &encodedPath, const QString &destPath, const Provenance &p,
          const SignerConfig &signer, QString *error = nullptr);

// The C2PA report (Reader JSON) of an encoded image, or an empty object when it
// has no manifest / can't be read / C2PA is unavailable.
QJsonObject readReport(const QByteArray &bytes, const QString &fileName);

// What an image's own credentials say, for warning before an export.
struct SourceInfo {
    bool hasManifest = false;
    bool generativeAI = false; // the chain declares AI-generated / AI-composited content
};
SourceInfo inspectSource(const QByteArray &bytes, const QString &fileName);

// Sign a tiny image with `signer` and read it back. On success, `summary`
// names the signer as verifiers will show it (common name / issuer).
bool testSigner(const SignerConfig &signer, QString *summary, QString *error);

} // namespace credentials
