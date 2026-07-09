# Changelog

All notable changes to Lumen are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **AI demosaicing (optional).** RAW files can be demosaiced with a neural
  network as an alternative to the classic LibRaw algorithms (Bayer sensors
  only). It is an opt-in build (`-DLUMEN_AI_DEMOSAIC=ON`, needs ONNX Runtime);
  the release AppImage/DMG bundle the runtime, so no separate install is
  required. **Lumen does not ship a model** — choose a local `.onnx` file via
  *RAW Defaults → Choose AI model…* (or set `LUMEN_DEMOSAIC_MODEL`). The **AI**
  demosaic option stays disabled until a model is selected, and the panel shows
  a hint pointing you to the picker.

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

[0.0.1]: https://github.com/vijaymathew/lumen/releases/tag/v0.0.1
