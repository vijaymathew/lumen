# Lumen — Implementation Progress

Living tracker for implementation, organised by the phases in
[DESIGN.md §10](DESIGN.md#10-suggested-build-order). Update this as work lands.

**Status legend:** ✅ done · 🟡 in progress · ⬜ not started · ⏸️ deferred

**Last updated:** 2026-06-22

---

## Phase 1 — Skeleton 🟡

> Qt window + RHI canvas, load/display an image via libvips → GPU texture,
> zoom/pan. Command-palette shell stood up early. Linux **and** macOS CI.

| Item | Status | Notes |
|---|---|---|
| CMake build + project layout | ✅ | `qt_add_shaders` → `.qsb`; aqt-Qt fallback for missing `Qt6GuiPrivate` |
| libvips image loader (`ImageBuffer`) | ✅ | → 8-bit sRGB + alpha → `QImage`; vips lifecycle isolated from Qt TUs |
| RHI canvas (`CanvasWidget`) | ✅ | `QRhiWidget` textured quad, fit-to-window |
| Zoom (wheel) + pan (drag) | ✅ | Zoom is viewport-centred for now (cursor-centred = Phase 2 TODO) |
| Command palette (`/`, fuzzy match) | ✅ | Subsequence match; recency/frequency ranking still TODO |
| Modal `InputController` | ✅ | Browse / CommandPalette exercised; other modes defined, unused |
| Immersive shell (`MainWindow`) | ✅ | Fullscreen, hint bar, command routing, shell shortcuts |
| Builds locally (Linux) | ✅ | Verified: links, `--version` runs |
| **Rendering visually verified** | ⬜ | Not yet confirmed on a real display |
| CI: Linux + macOS | 🟡 | Workflow written; not yet pushed / green |
| Commit + push to GitHub | ⬜ | Work currently uncommitted |

**To close Phase 1:** confirm an image renders on-screen with working zoom/pan,
push, and get CI green on both platforms.

---

## Phase 2 — Edit graph 🟡 (foundational, started)

> Non-destructive node pipeline: nodes, dirty/cache system, a simple exposure
> node end-to-end (preview + export).

| Item | Status | Notes |
|---|---|---|
| `EditNode` base + `EditGraph` | ⬜ | Core architecture decision (DESIGN §5.1) |
| Dirty-flag / cache invalidation | ⬜ | |
| `TuneNode` (exposure) — preview path | ⬜ | GLSL/RHI shader on downsampled image |
| `TuneNode` — libvips export path | ⬜ | Full-res |
| Global undo/redo over the graph | ⬜ | Distinct from per-tool session undo |
| Cursor-centred zoom | ⬜ | Carried over from Phase 1 |

---

## Phase 3 — Tone tools + curves editor ⬜

> Brightness/contrast/highlights/shadows/WB/saturation; pointer-first curve
> editor (DESIGN §4.4).

| Item | Status | Notes |
|---|---|---|
| Tone sliders (tool panel, bottom-docked) | ⬜ | |
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
