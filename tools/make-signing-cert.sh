#!/usr/bin/env bash
#
# Create a personal signing certificate for Lumen's Content Credentials (C2PA).
#
#   tools/make-signing-cert.sh --name "Jane Doe"
#   tools/make-signing-cert.sh --name "Jane Doe" --org "Jane Doe Photography" --out ~/keys/c2pa
#
# Options:
#   --name NAME   who signs: shown as the signer in Content Credentials (required)
#   --org ORG     organisation (optional)
#   --out DIR     where to write the files (default: ~/.lumen/signing)
#   --days N      signing certificate lifetime in days (default: 730)
#   --renew       issue a new signing certificate from the existing root in DIR
#                 (when the old one expires) — the root, and so the trust anchor
#                 you've shared, stays the same
#   --force       overwrite all existing files in DIR, root included
#
# It makes a two-level hierarchy, since C2PA rejects self-signed signing certs:
#
#   root-ca.pem / root-ca.key   your personal root CA (10 years). Keep the key
#                               offline if you can; it only issues signing certs.
#   signing-chain.pem           the signing certificate (EC P-256, ES256) —
#                               point Lumen's "Certificate chain" at this
#   signing.key                 its private key — Lumen's "Private key"
#
# Credentials signed this way are valid and tamper-evident, but verifiers show
# the signer as unrecognised: your root is not on the C2PA trust list. Anyone
# who has your root-ca.pem can check them as trusted (see
# docs/content-credentials.md). Everything happens locally; no key leaves this
# machine. Requires OpenSSL 1.1.1+ (or LibreSSL 3.x).

set -euo pipefail

NAME=""
ORG=""
OUT="${HOME}/.lumen/signing"
DAYS=730
FORCE=0
RENEW=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --name)   NAME="$2"; shift 2 ;;
        --name=*) NAME="${1#*=}"; shift ;;
        --org)    ORG="$2"; shift 2 ;;
        --org=*)  ORG="${1#*=}"; shift ;;
        --out)    OUT="$2"; shift 2 ;;
        --out=*)  OUT="${1#*=}"; shift ;;
        --days)   DAYS="$2"; shift 2 ;;
        --days=*) DAYS="${1#*=}"; shift ;;
        --force)  FORCE=1; shift ;;
        --renew)  RENEW=1; shift ;;
        -h|--help)
            sed -n '3,30p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "Unknown option: $1 (see --help)" >&2; exit 1 ;;
    esac
done

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

[[ -n "$NAME" ]] || die "--name is required (it is shown as the signer)"
[[ "$DAYS" =~ ^[0-9]+$ && "$DAYS" -gt 0 ]] || die "--days must be a positive number"
command -v openssl >/dev/null 2>&1 || die "openssl not found"

ROOT_KEY="$OUT/root-ca.key"
ROOT_CERT="$OUT/root-ca.pem"
SIGN_KEY="$OUT/signing.key"
SIGN_CHAIN="$OUT/signing-chain.pem"

if [[ "$RENEW" -eq 1 ]]; then
    [[ -f "$ROOT_KEY" && -f "$ROOT_CERT" ]] \
        || die "--renew needs the existing $ROOT_CERT and $ROOT_KEY"
    openssl x509 -in "$ROOT_CERT" -noout -checkend $((DAYS * 86400)) >/dev/null \
        || die "the root certificate expires within --days; create a new root with --force"
elif [[ "$FORCE" -eq 0 ]]; then
    for f in "$ROOT_KEY" "$ROOT_CERT" "$SIGN_KEY" "$SIGN_CHAIN"; do
        [[ -e "$f" ]] && die "$f already exists (use --renew for a new signing certificate, or --force to replace everything)"
    done
fi

# Keys are created owner-only from the start.
umask 077
mkdir -p "$OUT"
chmod 700 "$OUT"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Subject strings, with "/" and "\" escaped for openssl -subj.
esc() { printf '%s' "$1" | sed 's/[\\/]/\\&/g'; }
SUBJ_ORG=""
[[ -n "$ORG" ]] && SUBJ_ORG="/O=$(esc "$ORG")"

# Extensions: the root is a CA that only signs certificates; the leaf is an
# end-entity whose key signs documents. C2PA requires the leaf to carry
# digitalSignature (critical) and an allowed EKU — emailProtection is the one
# every OpenSSL knows by name.
cat > "$WORK/openssl.cnf" <<'EOF'
[req]
distinguished_name = dn
[dn]

[root_ext]
basicConstraints = critical, CA:TRUE, pathlen:0
keyUsage = critical, keyCertSign, cRLSign
subjectKeyIdentifier = hash

[leaf_ext]
basicConstraints = critical, CA:FALSE
keyUsage = critical, digitalSignature
extendedKeyUsage = emailProtection
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always
EOF

if [[ "$RENEW" -eq 1 ]]; then
    echo "==> Reusing root CA $ROOT_CERT"
    cp "$ROOT_KEY" "$WORK/root.key"
    cp "$ROOT_CERT" "$WORK/root.pem"
else
    echo "==> Creating root CA"
    openssl ecparam -name prime256v1 -genkey -noout -out "$WORK/root.key"
    openssl req -new -x509 -sha256 -days 3650 \
        -config "$WORK/openssl.cnf" -extensions root_ext \
        -key "$WORK/root.key" \
        -subj "/CN=$(esc "$NAME") Content Credentials Root${SUBJ_ORG}" \
        -out "$WORK/root.pem"
fi

echo "==> Creating signing certificate (${DAYS} days)"
openssl ecparam -name prime256v1 -genkey -noout -out "$WORK/leaf.ec.key"
# PKCS#8 is the private-key format libc2pa expects.
openssl pkcs8 -topk8 -nocrypt -in "$WORK/leaf.ec.key" -out "$WORK/leaf.key"
openssl req -new -sha256 -config "$WORK/openssl.cnf" \
    -key "$WORK/leaf.key" \
    -subj "/CN=$(esc "$NAME")${SUBJ_ORG}" \
    -out "$WORK/leaf.csr"
openssl x509 -req -sha256 -days "$DAYS" \
    -in "$WORK/leaf.csr" -CA "$WORK/root.pem" -CAkey "$WORK/root.key" \
    -set_serial "0x$(openssl rand -hex 16)" \
    -extfile "$WORK/openssl.cnf" -extensions leaf_ext \
    -out "$WORK/leaf.pem"

# Sanity check before anything is written to $OUT.
openssl verify -CAfile "$WORK/root.pem" "$WORK/leaf.pem" >/dev/null \
    || die "generated certificate does not verify against its root"

if [[ "$RENEW" -eq 0 ]]; then
    install -m 600 "$WORK/root.key" "$ROOT_KEY"
    install -m 644 "$WORK/root.pem" "$ROOT_CERT"
fi
install -m 600 "$WORK/leaf.key" "$SIGN_KEY"
# The chain is the signing cert plus any intermediates, without the root.
install -m 644 "$WORK/leaf.pem" "$SIGN_CHAIN"

cat <<EOF

Done. In Lumen, press / and run "Content Credentials (signing certificate)…":

    Certificate chain:  $SIGN_CHAIN
    Private key:        $SIGN_KEY
    Algorithm:          ES256

Then press "Test signing".

Keep $ROOT_KEY safe (ideally offline). It is only needed to issue a new
signing certificate when this one expires ($(openssl x509 -in "$SIGN_CHAIN" -noout -enddate | cut -d= -f2)).
Share $ROOT_CERT with anyone who wants to check your images as trusted.
EOF
