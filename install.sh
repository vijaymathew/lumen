#!/usr/bin/env bash
#
# One-shot build + install for Lumen.
#
#   ./install.sh                 # install deps, build, install to /usr/local
#   ./install.sh --prefix ~/.local
#   ./install.sh --no-deps       # skip dependency install (deps already present)
#   ./install.sh --no-install    # configure + build only, don't install
#   ./install.sh --qt-dir /path/to/Qt/6.7.2/gcc_64
#   ./install.sh --no-c2pa       # build without Content Credentials (libc2pa)
#
# It installs the imaging libraries + toolchain (apt or Homebrew), makes sure a
# Qt 6.7+ is available (using it if the system has one, otherwise fetching it
# with aqtinstall), fetches a pinned, checksum-verified libc2pa for Content
# Credentials, then configures, builds, and installs.

set -euo pipefail

# --- Defaults / arg parsing ------------------------------------------------

PREFIX="/usr/local"
BUILD_TYPE="RelWithDebInfo"
BUILD_DIR="build"
INSTALL_DEPS=1
DO_INSTALL=1
QT_DIR="${QT_DIR:-}"
QT_VERSION="6.7.2"
WITH_C2PA=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)      PREFIX="$2"; shift 2 ;;
        --prefix=*)    PREFIX="${1#*=}"; shift ;;
        --build-type)  BUILD_TYPE="$2"; shift 2 ;;
        --build-dir)   BUILD_DIR="$2"; shift 2 ;;
        --qt-dir)      QT_DIR="$2"; shift 2 ;;
        --qt-dir=*)    QT_DIR="${1#*=}"; shift ;;
        --no-deps)     INSTALL_DEPS=0; shift ;;
        --no-install)  DO_INSTALL=0; shift ;;
        --no-c2pa)     WITH_C2PA=0; shift ;;
        -h|--help)
            sed -n '3,15p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

log()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m==>\033[0m %s\n' "$*" >&2; }

# sudo only when we are not already root.
SUDO=""
if [[ "$(id -u)" -ne 0 ]]; then SUDO="sudo"; fi

OS="$(uname -s)"

# --- 1. System dependencies ------------------------------------------------

install_deps_linux() {
    # GUI/link libraries. Distro Qt pulls these in as package dependencies, but a
    # Qt fetched via aqtinstall does not — without them the Widgets/Gui app fails
    # to link (libGL) or run (the xcb platform plugin) on minimal systems.
    if command -v apt-get >/dev/null 2>&1; then
        log "Installing imaging libraries + toolchain via apt"
        # An unrelated broken third-party repo shouldn't abort the install — the
        # packages we need come from the main archives. Warn but keep going; a
        # genuinely unavailable package will still fail at the install step.
        $SUDO apt-get update || warn "apt-get update reported errors — continuing anyway"
        $SUDO apt-get install -y \
            build-essential cmake ninja-build pkg-config \
            libvips-dev libraw-dev liblensfun-dev liblensfun-data-v1 liblcms2-dev \
            libgl1-mesa-dev libglu1-mesa-dev libxkbcommon-dev \
            libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
            libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1
    elif command -v dnf >/dev/null 2>&1; then
        log "Installing imaging libraries + toolchain via dnf"
        # dnf refreshes repo metadata as part of `install`; skip_if_unavailable
        # lets it ignore a broken/unreachable repo instead of aborting.
        $SUDO dnf install -y --setopt=skip_if_unavailable=true \
            gcc-c++ cmake ninja-build pkgconf-pkg-config \
            vips-devel LibRaw-devel lensfun-devel lcms2-devel \
            mesa-libGL-devel mesa-libGLU-devel libxkbcommon-devel \
            xcb-util-cursor xcb-util-wm xcb-util-image xcb-util-keysyms xcb-util-renderutil
    elif command -v pacman >/dev/null 2>&1; then
        log "Installing imaging libraries + toolchain via pacman"
        # No `-y`: install from the existing sync DB so a stale or unreachable
        # repo can't abort the bootstrap (and to avoid the -Sy partial-upgrade
        # trap). Run `sudo pacman -Syu` yourself first if your DB is stale.
        $SUDO pacman -S --needed --noconfirm \
            base-devel cmake ninja pkgconf libvips libraw lensfun lcms2 \
            libglvnd libxkbcommon \
            xcb-util-cursor xcb-util-wm xcb-util-image xcb-util-keysyms xcb-util-renderutil
    else
        warn "No supported package manager (apt/dnf/pacman) found."
        warn "Install libvips, LibRaw, Lensfun, lcms2, cmake and ninja manually,"
        warn "then re-run with --no-deps."
        exit 1
    fi
}

install_deps_macos() {
    if ! command -v brew >/dev/null 2>&1; then
        warn "Homebrew not found — install it from https://brew.sh, then re-run."
        exit 1
    fi
    log "Installing imaging libraries + toolchain via Homebrew"
    brew install vips libraw lensfun little-cms2 cmake ninja
}

if [[ "$INSTALL_DEPS" -eq 1 ]]; then
    case "$OS" in
        Linux)  install_deps_linux ;;
        Darwin) install_deps_macos ;;
        *) warn "Unsupported OS: $OS"; exit 1 ;;
    esac
else
    log "Skipping dependency install (--no-deps)"
fi

# --- 2. Qt 6.7+ ------------------------------------------------------------
# QRhiWidget needs Qt 6.7+, newer than most distro packages. Use an existing
# suitable Qt if we can find one; otherwise fetch it with aqtinstall (the same
# tool CI uses), including the qtshadertools module qt_add_shaders needs.

qt_prefix_from_qmake() {
    local qmake
    for qmake in qmake6 qmake; do
        if command -v "$qmake" >/dev/null 2>&1; then
            local ver; ver="$("$qmake" -query QT_VERSION 2>/dev/null || true)"
            if [[ "$ver" =~ ^6\.([0-9]+) ]] && (( ${BASH_REMATCH[1]} >= 7 )); then
                "$qmake" -query QT_INSTALL_PREFIX
                return 0
            fi
        fi
    done
    return 1
}

fetch_qt_with_aqt() {
    # aqtinstall needs Python 3 + pip. If they're missing, fetching Qt can't
    # proceed — fail early with a clear pointer to the --qt-dir escape hatch
    # rather than a cryptic error from deep inside pip.
    # NOTE: all log/warn calls use >&2 so stdout stays clean for the caller
    # that captures this function's output via $(...).
    if ! command -v python3 >/dev/null 2>&1 || ! python3 -m pip --version >/dev/null 2>&1; then
        warn "No Qt 6.7+ found on this system, and Python 3 + pip (needed to fetch" >&2
        warn "Qt automatically via aqtinstall) are not available." >&2
        warn "" >&2
        warn "Do one of the following, then re-run:" >&2
        warn "  * install python3 and pip (e.g. 'apt-get install python3-pip'); or" >&2
        warn "  * install Qt 6.7+ yourself and pass it in:" >&2
        warn "        ./install.sh --qt-dir /path/to/Qt/${QT_VERSION}/gcc_64" >&2
        exit 1
    fi

    local host arch
    case "$OS" in
        Linux)  host="linux";  arch="linux_gcc_64" ;;
        Darwin) host="mac";    arch="clang_64" ;;
    esac
    # aqt renamed the Linux arch id; fall back for older aqt.
    local dest="$ROOT/.qt"
    if [[ ! -d "$dest/$QT_VERSION" ]]; then
        log "Fetching Qt $QT_VERSION via aqtinstall (one-time, into .qt/)" >&2
        # On Python 3.12+ (Ubuntu 24.04+) pip blocks --user installs for
        # "externally managed" environments; try --break-system-packages as a
        # fallback before giving up.
        python3 -m pip install --user --upgrade aqtinstall >/dev/null 2>&1 \
            || python3 -m pip install --user --upgrade --break-system-packages aqtinstall >/dev/null 2>&1 \
            || { warn "Failed to install aqtinstall via pip. Install it manually:" >&2
                 warn "  pip3 install --user aqtinstall" >&2
                 warn "then re-run, or use --qt-dir to point at an existing Qt 6.7+ install." >&2
                 exit 1; }
        python3 -m aqt install-qt "$host" desktop "$QT_VERSION" "$arch" \
            -m qtshadertools -O "$dest" \
            || python3 -m aqt install-qt "$host" desktop "$QT_VERSION" \
                   -m qtshadertools -O "$dest"
    fi
    # The arch subdir name is whatever aqt unpacked under <version>/.
    local sub
    sub="$(find "$dest/$QT_VERSION" -maxdepth 1 -mindepth 1 -type d | head -n1)"
    echo "$sub"
}

if [[ -z "$QT_DIR" ]]; then
    if QT_DIR="$(qt_prefix_from_qmake)"; then
        log "Using system Qt at $QT_DIR"
    else
        QT_DIR="$(fetch_qt_with_aqt)"
        log "Using fetched Qt at $QT_DIR"
    fi
else
    log "Using Qt at $QT_DIR (--qt-dir)"
fi

# --- 3. libc2pa (Content Credentials) -------------------------------------
# The C2PA SDK's C library isn't packaged by distros or Homebrew, so fetch the
# prebuilt release from github.com/contentauth/c2pa-rs — pinned to one version
# and verified against these SHA-256 sums — into .c2pa/. It's optional: on any
# problem, warn and build without Content Credentials.

C2PA_VERSION="0.91.9"
c2pa_asset_and_sum() {
    case "$OS/$(uname -m)" in
        Linux/x86_64)
            echo "x86_64-unknown-linux-gnu 7e1d8ef8305a54064d35abb2ebba312b441761ea0b2a5c15f54ab2d5fd1be013" ;;
        Linux/aarch64|Linux/arm64)
            echo "aarch64-unknown-linux-gnu 3599b2399aeb4e4639bb6350578b2a8e0c21fdc860af8c672ed30e13d9e65d13" ;;
        Darwin/*)
            echo "universal-apple-darwin f11727130599983e7331a152f60dcbdd3c76db9d8f23ea8647ef464e3f4a2685" ;;
        *) return 1 ;;
    esac
}

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
    else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

fetch_c2pa() {
    local triple sum
    read -r triple sum < <(c2pa_asset_and_sum) \
        || { warn "No prebuilt libc2pa for $OS/$(uname -m) — building without Content Credentials" >&2; return 1; }
    local dest="$ROOT/.c2pa/$C2PA_VERSION"
    if [[ ! -f "$dest/include/c2pa.h" ]]; then
        for tool in curl unzip; do
            command -v "$tool" >/dev/null 2>&1 \
                || { warn "'$tool' not found — building without Content Credentials" >&2; return 1; }
        done
        local name="c2pa-v${C2PA_VERSION}-${triple}.zip"
        local url="https://github.com/contentauth/c2pa-rs/releases/download/c2pa-v${C2PA_VERSION}/${name}"
        local tmp; tmp="$(mktemp -d)"
        log "Fetching libc2pa $C2PA_VERSION (one-time, into .c2pa/)" >&2
        if ! curl -fsSL "$url" -o "$tmp/$name"; then
            warn "Download of $url failed — building without Content Credentials" >&2
            rm -rf "$tmp"; return 1
        fi
        if [[ "$(sha256_of "$tmp/$name")" != "$sum" ]]; then
            warn "Checksum mismatch for $name — refusing to use it; building without Content Credentials" >&2
            rm -rf "$tmp"; return 1
        fi
        rm -rf "$dest" && mkdir -p "$dest"
        unzip -q "$tmp/$name" -d "$dest"
        # Keep just the header and the shared library (the archive also carries
        # a Rust .rlib, a dep file and, on macOS, zipped debug symbols).
        rm -f "$dest"/lib/*.rlib "$dest"/lib/*.d "$dest"/lib/*.dSYM.zip
        rm -rf "$tmp"
    fi
    echo "$dest"
}

C2PA_ROOT=""
if [[ "$WITH_C2PA" -eq 1 ]]; then
    if C2PA_ROOT="$(fetch_c2pa)"; then
        log "Using libc2pa at $C2PA_ROOT"
    else
        C2PA_ROOT=""
    fi
else
    log "Skipping libc2pa (--no-c2pa): exports will carry no Content Credentials"
fi

# --- 4. Configure, build, install -----------------------------------------

# Homebrew's vips.pc requires libarchive, which Homebrew keeps keg-only (its
# .pc isn't on pkg-config's default path). Without it pkg-config can't resolve
# vips' flags and CMake ends up with no vips include dir, failing later on
# 'vips/vips.h' not found.
if [[ "$OS" == "Darwin" ]] && command -v brew >/dev/null 2>&1; then
    for keg in libarchive; do
        kegpc="$(brew --prefix "$keg" 2>/dev/null)/lib/pkgconfig"
        [[ -d "$kegpc" ]] && export PKG_CONFIG_PATH="$kegpc${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    done
fi

log "Configuring ($BUILD_TYPE) → $BUILD_DIR"
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_PREFIX_PATH="$QT_DIR" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DC2PA_ROOT="$C2PA_ROOT"

log "Building"
cmake --build "$BUILD_DIR" --parallel

if [[ "$DO_INSTALL" -eq 0 ]]; then
    log "Build complete (--no-install; skipping install)."
    exit 0
fi

log "Installing to $PREFIX"
# Writing under /usr (or another root-owned prefix) needs sudo.
if [[ -w "$PREFIX" || ! -e "$PREFIX" && -w "$(dirname "$PREFIX")" ]]; then
    cmake --install "$BUILD_DIR"
else
    $SUDO cmake --install "$BUILD_DIR"
fi

# If the app was linked against a Qt outside the system library path, the
# installed binary hard-codes that Qt's location in its RPATH (see CMakeLists) —
# moving or deleting that directory will break lumen. Flag it so it's not a
# surprise later. A system Qt under /usr or /lib is on the default loader path
# and needs no warning.
case "$QT_DIR" in
    /usr|/usr/*|/lib|/lib/*) : ;;
    *)
        warn ""
        warn "Note: lumen was linked against a Qt in a non-system location:"
        warn "    $QT_DIR"
        warn "The installed binary references that path at runtime, so keep that"
        warn "directory in place. Installing a system Qt 6.7+ and rebuilding will"
        warn "drop the dependency on it."
        ;;
esac

log "Done. Launch with: lumen path/to/photo.raf"
