# Content Credentials

Lumen can embed signed [C2PA](https://c2pa.org) **Content Credentials** in the
images it exports. A credential records two things:

- **The original.** The photo you opened is recorded as the export's parent.
  If that photo already had credentials (for example from a C2PA camera), they
  are carried forward, so the history reaches back to the capture.
- **What Lumen did.** The edits are listed as standard C2PA actions:
  `c2pa.color_adjustments` (tone, curves, colour), `c2pa.filtered` (sharpen,
  denoise, grain…), `c2pa.edited` (healing, lens correction), `c2pa.cropped`,
  `c2pa.resized` and so on. Lumen's actions never carry a *digital source type*,
  which is the field C2PA uses to flag AI-generated or AI-composited content. A
  small assertion (`com.github.vijaymathew.lumen.declaration`) says the same in
  plain words. You can also choose to include the full edit recipe: every
  setting, masks included.

**What it proves.** The credential is a *signed statement*, by whoever holds
the signing key, of what was done in Lumen. Any change to the image afterwards
breaks the signature, so it is tamper-evident. It is not a cryptographic proof
that no AI was involved. If the original's own credentials declare AI-generated
content, the chain shows that, and Lumen tells you so after export.

## 1. Get a signing certificate

Lumen never creates or ships a key. You need a certificate chain and its
private key in PEM format. There are two ways to get them.

**A. From a certificate authority on the
[C2PA trust list](https://opensource.contentauthenticity.org/docs/conformance/trust-lists).**
Verifiers then show your name as a recognised signer. Follow the CA's
instructions and ask for a C2PA / document-signing certificate.

**B. Make your own** with the script in this repository (it needs OpenSSL):

```bash
tools/make-signing-cert.sh --name "Jane Doe"
# optional: --org "Jane Doe Photography" --out ~/keys/c2pa --days 730
```

This writes four files to `~/.lumen/signing/`. The private keys are readable
only by you.

| File | What it is |
|---|---|
| `signing-chain.pem` | your signing certificate (Lumen's *Certificate chain*) |
| `signing.key` | its private key (Lumen's *Private key*) |
| `root-ca.pem` | your personal root certificate: share it with people who want to check your images |
| `root-ca.key` | the root's private key: keep it safe, ideally offline. You only need it to issue a new signing certificate when the current one expires |

Credentials signed this way are valid and tamper-evident. Verifiers show the
signer as **unrecognised**, though, because your root isn't on the C2PA trust
list. Anyone who adds your `root-ca.pem` as a trust anchor sees them as
**trusted** (see [below](#checking-your-own-images-as-trusted)).

## 2. Set it up in Lumen

Press <kbd>/</kbd> and run **Content Credentials (signing certificate)…**:

- **Certificate chain** — `signing-chain.pem`
- **Private key** — `signing.key`
- **Algorithm** — ES256 for certificates made by the script
- **Timestamp server** — optional. Leave it empty to sign entirely offline.
  With an [RFC 3161](https://www.rfc-editor.org/rfc/rfc3161) server, signatures
  stay verifiable after your certificate expires. Each export then sends a hash
  of the manifest (never the image) to that server.

Press **Test signing**. It signs a tiny image and reports the name verifiers
will show. Lumen stores only the file paths, never the key itself.

## 3. Export with credentials

In the export dialog, tick **Attach Content Credentials**, and optionally
**Include full edit recipe**. The option is greyed out until a certificate is
set up, or if your build has no libc2pa (`./install.sh` fetches it). If signing
fails, the image is still exported, but without credentials, and Lumen warns you.

## 4. Verify the credentials in an image

### In a browser

- [Content Credentials Verify](https://verify.contentauthenticity.org/)
- [Adobe Content Authenticity Inspect](https://inspect.cr/)

Drop the image on the page. Both sites check against public trust lists, so an
image signed with a self-made certificate shows an unrecognised signer there.
These are websites: the image is sent to them. Use `c2patool` (below) for
images you'd rather keep private.

### With `c2patool` (offline)

Install C2PA Tool with `brew install c2patool`, or download a prebuilt binary
from the [c2pa-rs releases](https://github.com/contentauth/c2pa-rs/releases?q=c2patool).

```bash
c2patool photo.jpg              # the manifest report, as JSON
c2patool photo.jpg --info       # a short summary
c2patool photo.jpg --tree       # the provenance chain: this image and its ingredients
c2patool photo.jpg --detailed   # everything, including all validation results
```

In the report, look for:

- **`validation_state`**:
  - `Invalid` means the image or its manifest was changed after signing, or the
    signature is broken.
  - `Valid` means the image is intact and correctly signed, but the signer isn't
    on any trust list you supplied.
  - `Trusted` means it is valid *and* the signer chains to a trusted root.
- **`signature_info`**: who signed it (`common_name`, plus `issuer`, which is
  the certificate's organisation) and when.
- **The `c2pa.actions` assertion** in the active manifest:
  - one `c2pa.opened` action for the original;
  - one entry per kind of edit, with `softwareAgent.name` set to `Lumen` and a
    `description` naming the tools used.

  Search the whole report for `digitalSourceType`. Values such as
  `trainedAlgorithmicMedia` or `compositeWithTrainedAlgorithmicMedia` mean that
  some step in the chain declared AI content. Lumen never adds them.
- **`ingredients`**: the original (`relationship: parentOf`) and any
  credentials it carried.

### Checking your own images as trusted

Give your root certificate to `c2patool` as a trust anchor:

```bash
c2patool photo.jpg trust --trust_anchors ~/.lumen/signing/root-ca.pem
```

`validation_state` now reads `Trusted` for images you signed. Send
`root-ca.pem` (never a `.key` file) to editors, clients or anyone else who
wants to check your work the same way.

### If an image has no credentials

The image may have been exported without them, or it may have been re-saved by
software that doesn't support C2PA. Many social networks and messaging apps also
strip metadata on upload. Missing credentials don't mean an image was faked;
they mean there is no signed history to check. Share the original export file
when provenance matters.

## Renewing an expired certificate

Before your signing certificate expires (the script prints the date), issue a
new one from the same root:

```bash
tools/make-signing-cert.sh --name "Jane Doe" --renew
```

This replaces `signing-chain.pem` and `signing.key` in place, so Lumen picks
them up with no further setup. Your root, and the `root-ca.pem` you've shared,
stays the same. Use `--force` only to start over with a new root. You would then
have to re-distribute `root-ca.pem`, and keep the old one to check images
signed earlier.

Without a timestamp server, signatures are checked against the certificate's
validity period. Images signed with an expired certificate may then be flagged
by verifiers, even though they haven't been altered.
