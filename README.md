# Lumen

Intuitive and powerful photo editor for Linux (and macOS).

Lumen's differentiator is its interaction model — a fullscreen canvas with a
`/`-triggered fuzzy command palette ("keyboard to navigate and command; pointer
to manipulate"), not the image-processing math. See [docs/DESIGN.md](docs/DESIGN.md)
for the full design.

> **Status: milestone 1 (skeleton).** Loads and displays an image through
> libvips on a Qt RHI canvas with zoom/pan, plus the command-palette shell.
> Editing tools (curves, selective, healing, looks) are not implemented yet.

## What works today

- Open an image (`Ctrl+O`, the palette, or a path on the command line). Decoding
  goes through **libvips**.
- Display on a **Qt RHI** canvas (Metal on macOS, OpenGL/Vulkan on Linux) — one
  textured quad, fit-to-window.
- **Zoom** (mouse wheel) and **pan** (left-drag).
- **Command palette** (`/`) with fuzzy subsequence matching over commands.
- Shell shortcuts: `Ctrl+O` open · `Ctrl+0` reset view · `F11` fullscreen ·
  `Ctrl+Q` quit.

## Building

### Requirements

- CMake ≥ 3.24
- A C++20 compiler
- **Qt 6.7+** (Core, Gui, Widgets, ShaderTools) — 6.7 is required for `QRhiWidget`
- **libvips** (with the C++ bindings, `vips-cpp`)

### Linux

Distro Qt is often older than 6.7. Either install Qt 6.7+ (e.g. via
[aqtinstall](https://github.com/miurahr/aqtinstall) or the Qt online installer)
or your distro's package if it is new enough. libvips comes from the system:

```bash
sudo apt-get install -y libvips-dev ninja-build   # Debian/Ubuntu

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
./build/lumen path/to/photo.jpg
```

If your Qt is in a non-standard location, point CMake at it:
`-DCMAKE_PREFIX_PATH=/path/to/Qt/6.7.2/gcc_64`.

### macOS

```bash
brew install vips ninja
# Qt 6.7+ via brew (`brew install qt`) or aqt/online installer.

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
./build/lumen.app/Contents/MacOS/lumen path/to/photo.jpg
```

CI ([.github/workflows/ci.yml](.github/workflows/ci.yml)) builds both Linux and
macOS on every push.

## Layout

```
src/
  core/   ImageBuffer        — libvips → RGBA8 QImage loader
  gpu/    CanvasWidget       — QRhiWidget textured-quad viewer (zoom/pan)
          shaders/           — GLSL compiled to .qsb via qt_add_shaders
  input/  InputController    — modal state machine (Browse/ToolActive/…)
          CommandPalette     — fuzzy "/" overlay
  ui/     MainWindow         — immersive shell, command routing
docs/     DESIGN.md          — full design document
mockups/                     — early UI mockups
```
