# Lumen — Implementation Progress

Living tracker for implementation, organised by the phases in
[DESIGN.md §10](DESIGN.md#10-suggested-build-order). Update this as work lands.

**Status legend:** ✅ done · 🟡 in progress · ⬜ not started · ⏸️ deferred

**Last updated:** 2026-06-22

---

## Phase 1 — Skeleton ✅

> Qt window + RHI canvas, load/display an image via libvips → GPU texture,
> zoom/pan. Command-palette shell stood up early.

| Item | Status | Notes |
|---|---|---|
| CMake build + project layout | ✅ | `qt_add_shaders` → `.qsb`; aqt-Qt fallback for missing `Qt6GuiPrivate` |
| libvips image loader (`ImageBuffer`) | ✅ | → 8-bit sRGB + alpha → `QImage`; vips lifecycle isolated from Qt TUs |
| RHI canvas (`CanvasWidget`) | ✅ | `QRhiWidget` textured quad, fit-to-window |
| Zoom (wheel) + pan (drag) | ✅ | Zoom is viewport-centred for now (cursor-centred = Phase 2 TODO) |
| Command palette (`/`, fuzzy match) | ✅ | Subsequence match; recency/frequency ranking still TODO |
| Modal `InputController` | ✅ | Browse / CommandPalette exercised; other modes defined, unused |
| Immersive shell (`MainWindow`) | ✅ | Fullscreen, hint bar, command routing, shell shortcuts |
| Builds locally (Linux) | ✅ | Links; `--version` runs |
| **Rendering visually verified** | ✅ | Confirmed on a real X display — image renders on the RHI canvas with title/hint bar (2026-06-22) |
| CI workflow (Linux + macOS) | ✅ | Defined in `.github/workflows/ci.yml`; runs on push |

**Phase 1 is complete and visually verified.** The command-palette overlay is
wired in code but was not screenshot-tested (no `xdotool` available) — low risk,
plain Qt widgets.

> Git/commit/push and CI runs are handled outside this tracker; it records
> engineering deliverables only.

---

## Phase 2 — Edit graph ✅

> Non-destructive node pipeline: nodes, dirty/cache system, a simple exposure
> node end-to-end (preview + export).

| Item | Status | Notes |
|---|---|---|
| `EditNode` base + `EditGraph` | ✅ | `Image` (RAII libvips wrapper), abstract `EditNode`, ordered `EditGraph`; in headless `lumen_core` lib |
| Dirty-flag / cache invalidation | ✅ | Lazy eval + per-node cache + downstream dirty propagation; unit-tested (`edit_graph_test`) |
| `TuneNode` (exposure) — preview path | ✅ | `TuneNode` model + exposure uniform in fragment shader; modeless draggable right-side card; verified on-screen (+2 EV brightens) |
| `TuneNode` — libvips export path | ✅ | Export command walks `EditGraph.result()` at full res → `Image::saveToFile` (alpha stripped); round-trip unit-tested + verified on a real photo (export matches preview) |
| Wire graph into the display pipeline | ✅ | Preview driven by `EditGraph::previewState()` (walks nodes → `PreviewState` → fragment shader); GPU real-time path kept. Spatial/multi-pass nodes deferred until one exists |
| Global undo/redo over the graph | ✅ | Snapshot history of node state (JSON); commit per tool session, no-op coalescing, redo-tail truncation; Ctrl+Z/Ctrl+Shift+Z + palette; unit-tested + verified on-screen. Structural undo (add/remove/reorder) deferred until nodes are user-addable |
| Cursor-centred zoom | ✅ | Wheel zoom keeps the image point under the cursor fixed; view math extracted to `gpu/ZoomMath.h`, shared with the renderer, unit-tested (`zoom_test`) |

---

## Phase 3 — Tone tools + curves editor ⬜

> Brightness/contrast/highlights/shadows/WB/saturation; pointer-first curve
> editor (DESIGN §4.4).

| Item | Status | Notes |
|---|---|---|
| Tone sliders (floating tool panel) | ✅ | Exposure + contrast + saturation in `TuneNode`, driven by a 3-slider `TonePanel`; GPU preview & libvips export use identical math (verified to match on a real photo). Warmth/highlights/shadows can follow. (Panel floats per §4.6, not bottom-docked) |
| Curves editor — drag points, add/remove | ⬜ | Pointer-first; keyboard nudge as garnish |
| Per-channel + luminance curves | ⬜ | |

---

## Phase 4 — LUT looks ⬜

> 3D LUT (HALD CLUT) loader + apply.

| Item | Status | Notes |
|---|---|---|
| HALD CLUT loader | ⬜ | |
| `LutNode` (trilinear apply) | ⬜ | |
| Look intensity slider | ⬜ | |

---

## Phase 5 — Selective adjustments ⬜

> Local edits via mask (color-affinity, luminosity/tone, brush). DESIGN §4.4–4.5.

| Item | Status | Notes |
|---|---|---|
| Mask infrastructure on `EditNode` | ⬜ | |
| Luminosity/tone range mask (parametric) | ⬜ | Keyboard-friendly path |
| Color-affinity mask (guided filter) | ⬜ | OpenCV `ximgproc::guidedFilter` |
| Brush mask (Add/Subtract, session undo) | ⬜ | Accumulate → flatten on commit (DESIGN §4.5) |
| `SelectiveNode` | ⬜ | |

---

## Phase 6 — Healing brush ⬜

> Content-aware removal via inpainting.

| Item | Status | Notes |
|---|---|---|
| `HealNode` (`cv::inpaint`) | ⬜ | Telea / NS to start |
| Higher-quality fill (PatchMatch/xphoto) | ⬜ | Later refinement |

---

## Deferred / post-v1 ⏸️

| Item | Notes |
|---|---|
| Lens correction (`LensCorrectionNode`) | LibRaw + Lensfun. DESIGN §8 |
| Full RAW workflow polish | LibRaw decode designed for from day one |
| Perspective / advanced crop-rotate | |

---

## Dependencies wired up

| Library | Status | Used for |
|---|---|---|
| Qt6 (Core/Gui/Widgets/ShaderTools) | ✅ | UI, RHI rendering |
| libvips | ✅ | Image decode / pipeline |
| OpenCV | ⬜ | Healing, guided-filter masks (Phase 5–6) |
| LibRaw | ⬜ | RAW decode |
| Lensfun | ⬜ | Lens correction (deferred) |
