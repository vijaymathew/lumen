# Changelog

All notable changes to Lumen are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.4] — 2026-08-24

### Fixed

- **Layers panel row highlighting stopped working after a Qt QSS cascade
  quirk** — the active layer's highlight, and the Luminosity mask's
  Shadows/Midtones/Highlights preset buttons, are now styled directly on each
  widget instead of via a dynamic-property stylesheet rule, and the preset
  buttons re-sync their checked state whenever the mask range changes (they
  stay checked while fine-tuning the sliders afterwards, and uncheck once the
  range no longer matches any preset).
- **Crop panel buttons swallowed the Enter/Esc used to close the tool** — the
  aspect-ratio, rotate, flip, and reset buttons no longer take keyboard focus
  on click, so Enter/Esc reaches `MainWindow`'s tool-close handling instead of
  re-triggering the last-clicked button.

## [0.1.3] — 2026-08-15

### Added

- **Custom "Open image" dialog**, replacing the system file picker — folder
  navigation with a thumbnail grid by default, a Carousel view for larger
  single-image previews, multi-select (with Delete/Copy) in both views, and a
  metadata panel (ISO, focal length, camera/lens, dimensions, etc.) for the
  selected image.
- **Folder statistics** — a "Statistics…" view on the Open dialog showing a
  folder's focal-length distribution, colour-vs-monochrome split, and camera
  breakdown, computed across subfolders by default off (toggle to include
  them). Folder stats are cached for the life of the app and recomputed only
  when a folder's contents look stale (checked roughly every 5 minutes).

### Changed

- After deleting an image from the Open dialog, the selection now moves to
  the *next* image in the folder instead of the preceding one.

### Internal

- No user-visible behavior changes, but a substantial refactor of the C++
  codebase for maintainability (see `docs/cpp-code-review.md` for the full
  writeup):
  - Extracted a shared `FloatingToolPanel` base class, removing ~1,300 lines
    of near-identical boilerplate (stylesheet, drag-to-move, Escape/Enter
    handling, slider-row builder) duplicated across 19 of the 20 tool panels.
  - Collapsed MainWindow's ~20 duplicate tool open/close pairs onto two
    shared helpers and a table-driven `closeActiveTool()`.
  - Split `CanvasWidget::render()` into `updateGpuResources()` and
    `recordPasses()`, and extracted its crop/orientation coordinate math into
    a new, independently unit-tested `CropViewTransform.h`.
  - Fixed a const-correctness gap in `EditGraph` (`layer()`/`baseLayer()`/
    `activeLayer()` were `const`-qualified but returned mutable references),
    replaced raw `new`/`delete` for `lfModifier` in `LensCorrectionNode` with
    `std::unique_ptr`, and named a couple of previously-duplicated magic
    number constants.

## [0.1.2] — 2026-07-31

### Added

- **Image info panel** — a draggable card (<kbd>I</kbd>, or the view cluster)
  showing the open image's file path and dimensions, and, for RAW, the
  camera/lens identity and capture settings (focal length, aperture, shutter,
  ISO, capture time) read from EXIF.
- **Live-updating Adjustments panel** — the history list now follows edits as
  they land, so a newly-active adjustment (e.g. dialling in Monochrome) shows
  up without closing and reopening the panel.
- **Zoom read-out** — wheel-zooming flashes the current magnification (e.g.
  "125%") for a moment, then fades.
- **Export settings are now remembered globally**, not per-document — the
  Export dialog opens on whatever format, quality, bit depth, size limit, and
  colour space you used last, across images and across app restarts.
- Opening a RAW's embedded camera JPEG now decodes the *largest* preview the
  file carries rather than LibRaw's small default thumbnail, so the comparison
  opens at (or near) the RAW's own resolution.
- Applying a preset now shows real progress: the busy badge reads "Applying
  preset…" instead of a generic label, and dragging the preset's Amount slider
  labels the same way once it settles.

### Changed

- Default keybindings: the clipping-warnings toggle moved from <kbd>J</kbd> to
  <kbd>C</kbd>, and the Adjustments (history) panel from <kbd>A</kbd> to
  <kbd>Y</kbd>. The bottom-right view cluster's labels reflect the new keys.
- The selective-mask overlay is now green instead of red when shown (Layers
  panel "Show" button, and the on-canvas overlay itself).
- Export now suggests the source's own file name (e.g. `photo.jpg`) instead of
  appending "-edited" (`photo-edited.jpg`).

### Fixed

- **Applying a preset, or any edit downstream of lens correction, could freeze
  the UI for seconds** — every such change re-ran the full-resolution lens
  warp even when neither the source pixels nor the lens node's own params had
  moved since. It's now skipped unless something has actually changed.
  Re-rendering the Presets browser's thumbnails after a preset is applied
  (a full redraw of every built-in preset, since the change shifts every
  cached one) is also deferred a tick, so the busy badge and the freshly
  applied preview get to paint first.
- **Thumbnails in the Open dialog rendered bottom of the folder first** — Qt
  asks the proxy model for decorations in whatever order it measures rows
  internally, which walks the last rows of a folder before the first to size
  the scrollbar. Thumbnail requests are now queued and served nearest-the-
  viewport first, so the files actually on screen render before the ones
  scrolled past.

## [0.1.1] — 2026-07-17

A fix-only release. The Lens & Perspective sliders were unusable and lens
correction could silently do nothing; both now work. The pre-built downloads are
gone — see below.

### Removed

- **The Linux AppImage and the macOS .dmg.** Lumen now installs from source on
  both platforms with `./install.sh`. Every Linux-only defect in this release
  traces to the AppImage bundling the build machine's imaging libraries: pinned
  to Ubuntu 22.04, it shipped a libvips too old for the lens resampler, so
  v0.1.0's corrections did nothing at all on machines whose own libvips was
  perfectly fine. Neither bundle could really be tested — the libraries they ship
  are not the ones CI builds against, and the .dmg had no one to test it on — so
  they rotted unseen. Building against your own libraries is what keeps the lens
  profiles current; a runtime-based bundle (Flatpak) is the way back to a shipped
  binary. Both platforms are still built and tested in CI.

### Fixed

- **Lens corrections silently did nothing on libvips 8.12** — distortion,
  chromatic aberration, and the manual perspective controls all resample through
  `vips_mapim`, which only grew its `background` option in 8.13. Older libvips
  failed the call outright and left the image untouched, which is what made the
  v0.1.0 AppImage's lens corrections inert, and would equally affect a build on
  Ubuntu 22.04. The option is now used only where libvips supports it, and a
  failed resample warns instead of passing the image through unchanged.
  (Vignetting was unaffected — it never went through the resampler.)
- **Lens corrections on 3-band images** — the resampler's background was fixed
  at 4 values, which libvips rejects for a 3-band image, disabling every
  correction on one. It is now sized to the image.
- **Lens & Perspective sliders froze the app** — every slider tick re-ran the
  full-res warp and its display conversion on the UI thread (about three seconds
  on a 20MP frame), so a drag queued up seconds of blocking work and the image
  looked like it was ignoring the sliders. The warp now coalesces while dragging
  and runs off the UI thread, with the busy badge the other heavy panels already
  show.

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

[0.1.4]: https://github.com/vijaymathew/lumen/releases/tag/v0.1.4
[0.1.3]: https://github.com/vijaymathew/lumen/releases/tag/v0.1.3
[0.1.2]: https://github.com/vijaymathew/lumen/releases/tag/v0.1.2
[0.1.1]: https://github.com/vijaymathew/lumen/releases/tag/v0.1.1
[0.1.0]: https://github.com/vijaymathew/lumen/releases/tag/v0.1.0
[0.0.1]: https://github.com/vijaymathew/lumen/releases/tag/v0.0.1
