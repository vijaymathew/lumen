#!/usr/bin/env bash
#
# One-shot build + install for Lumen.
#
#   ./install.sh                 # install deps, build, install to /usr/local
#   ./install.sh --prefix ~/.local
#   ./install.sh --no-deps       # skip dependency install (deps already present)
#   ./install.sh --no-install    # configure + build only, don't install
#   ./install.sh --qt-dir /path/to/Qt/6.7.2/gcc_64
#
# It installs the imaging libraries + toolchain (apt or Homebrew), makes sure a
# Qt 6.7+ is available (using it if the system has one, otherwise fetching it
# with aqtinstall), then configures, builds, and installs.

set -euo pipefail

# --- Defaults / arg parsing ------------------------------------------------

PREFIX="/usr/local"
BUILD_TYPE="RelWithDebInfo"
BUILD_DIR="build"
INSTALL_DEPS=1
DO_INSTALL=1
QT_DIR="${QT_DIR:-}"
QT_VERSION="6.7.2"

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
        -h|--help)
            sed -n '3,13p' "$0" | sed 's/^# \{0,1\}//'
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
    if ! command -v python3 >/dev/null 2>&1 || ! python3 -m pip --version >/dev/null 2>&1; then
        warn "No Qt 6.7+ found on this system, and Python 3 + pip (needed to fetch"
        warn "Qt automatically via aqtinstall) are not available."
        warn ""
        warn "Do one of the following, then re-run:"
        warn "  * install python3 and pip (e.g. 'apt-get install python3-pip'); or"
        warn "  * install Qt 6.7+ yourself and pass it in:"
        warn "        ./install.sh --qt-dir /path/to/Qt/${QT_VERSION}/gcc_64"
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
        log "Fetching Qt $QT_VERSION via aqtinstall (one-time, into .qt/)"
        python3 -m pip install --user --upgrade aqtinstall >/dev/null
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

# --- 3. Configure, build, install -----------------------------------------

log "Configuring ($BUILD_TYPE) → $BUILD_DIR"
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_PREFIX_PATH="$QT_DIR" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"

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
        warn "directory in place. For a relocatable, dependency-bundled build,"
        warn "use the AppImage (Linux) or .dmg (macOS) from the project Releases."
        ;;
esac

log "Done. Launch with: lumen path/to/photo.raf"
