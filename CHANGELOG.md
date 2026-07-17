# Changelog

All notable changes to Lumen are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- **Lens corrections in the AppImage** — distortion, chromatic aberration, and
  the manual perspective controls silently did nothing in the AppImage: it
  bundles libvips 8.12, which predates the `background` option the resampler
  passed to `vips_mapim`, so every resample failed and left the image untouched.
  The option is now used only where libvips supports it, and a failed resample
  warns instead of passing the image through unchanged. (Vignetting was
  unaffected — it never went through the resampler.)
- **Lens corrections without lensfun-data installed** — the AppImage bundled
  liblensfun but not its profile database, so on a host without the system
  profiles nothing ever matched. The profiles now ship inside the AppImage.
- **Lens corrections on 3-band images** — the resampler's background was fixed
  at 4 values, which libvips rejects for a 3-band image, disabling every
  correction on one. It is now sized to the image.
- **AppImage appearance** — the AppImage ignored the desktop's fonts and colours
  and fell back to Qt's built-in defaults, so it looked unlike a locally built
  Lumen. It now ships Qt's GTK platform theme and takes glib from the host
  rather than bundling its own, which is what kept that theme from loading.

## [0.1.0] — 2026-07-16

This release makes Lumen a multi-image workspace and adds a way to see the
camera's own rendering next to your own.

### Added

- **Tabs** — open several photos and projects at once, each an independent
  document with its own edit graph, undo history, view, and background work.
  Switch with <kbd>Ctrl+Tab</kbd> / <kbd>Ctrl+Shift+Tab</kbd> and close with
  <kbd>Ctrl+W</kbd>; the tab strip appears once more than one is open.
- **Duplicate to a new tab** — branch the current photo (source plus its full
  edit) into a fresh, independent tab to explore a variation without disturbing
  the original.
- **Open the embedded camera JPEG** — from a RAW, load the camera's processed
  JPEG preview in a new tab, orientation-matched to the RAW, to compare it
  against your own rendering. Available from a RAW-only button on the view
  cluster.

### Changed

- The bottom-right view cluster is now a vertical stack, each toggle labelled
  with its keyboard shortcut (Histogram <kbd>G</kbd>, Clipping <kbd>J</kbd>,
  History <kbd>A</kbd>); the bottom hint bar no longer repeats them. The cluster
  is hidden until an image is open.
- Export now runs on a background thread, so the interface stays responsive while
  a full-resolution image is written.
- Healing brush interface refinements.
- Binary releases (AppImage and DMG) now report the exact version.

### Fixed

- Installer robustness fixes.
- Reduced interaction lag when several documents are open.

## [0.0.1] — 2026-07-04

First public release of Lumen — a fast, non-destructive RAW and photo editor for
Linux and macOS, built around an immersive canvas and a command-palette
workflow. RAW files are demosaiced at 16 bits and carried through a
floating-point working space; every edit is a re-orderable node in a
non-destructive edit graph, with the full-resolution result rendered by libvips
and the interactive preview on the GPU. Your original is never touched.

### Added

#### Tone & colour
- Tone controls — exposure, contrast, highlights, shadows, whites, blacks,
  saturation, and a saturation-aware vibrance.
- White balance — linear-light Kelvin/tint correction seeded from the RAW's
  as-shot values, with an eyedropper for neutral picks.
- Curves — per-channel and luma tone curves.
- Colour mixer — per-band HSL (hue / saturation / luminance) control.
- Colour grading — shadow / midtone / highlight colour wheels.
- Looks — apply 3D LUT (`.cube`) film and creative looks, with adjustable
  intensity.
- Monochrome — channel-weighted black & white with split toning.

#### Detail & repair
- Healing brush — content-aware inpainting to remove spots and distractions.
- Sharpen, Denoise, and Defringe (chromatic-aberration cleanup).

#### Geometry & lens
- Crop & rotate — aspect-ratio presets, 90° rotation, and horizontal/vertical
  flips.
- Lens & perspective — automatic distortion, TCA, and vignetting correction from
  EXIF via Lensfun, plus manual perspective correction.

#### Local adjustments
- Layers with independent adjustments, opacity, and masks.
- Masks — radial, linear-gradient, luminosity-range, colour-range, and free-hand
  brush, all non-destructive.

#### Creative
- Vignette and film grain for finishing.

#### Workflow
- Non-destructive edit graph with unlimited undo/redo and a step-through
  adjustment history.
- Presets — built-in looks plus user presets saved as reusable `.lumenpreset`
  files; copy/paste all settings between photos
  (<kbd>Ctrl+Shift+C</kbd> / <kbd>Ctrl+Shift+V</kbd>).
- Projects — `.lumen` files embed the original plus the full edit, with autosave
  and crash recovery.
- Live histogram and clipping warnings.
- Command palette (<kbd>/</kbd>) with fuzzy matching over every tool.
- Thumbnail browser in the open dialog, with real previews for every supported
  format, RAW included.

#### Formats & output
- Input — JPEG, PNG, TIFF, WebP, and camera RAW (Canon, Nikon, Sony, Panasonic,
  Fujifilm, and more) via LibRaw.
- Export — JPEG, PNG, TIFF, and WebP with control over quality, 8- or 16-bit
  depth, output resize (long-edge), and colour management (sRGB, Display P3, or
  Adobe RGB with the matching ICC profile embedded).

[0.1.0]: https://github.com/vijaymathew/lumen/releases/tag/v0.1.0
[0.0.1]: https://github.com/vijaymathew/lumen/releases/tag/v0.0.1
