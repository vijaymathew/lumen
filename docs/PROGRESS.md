# Lumen — Implementation Progress

Living tracker for implementation, organised by the phases in
[DESIGN.md §10](DESIGN.md#10-suggested-build-order). Update this as work lands.

**Status legend:** ✅ done · 🟡 in progress · ⬜ not started · ⏸️ deferred

**Last updated:** 2026-06-24

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

## Phase 3 — Tone tools + curves editor ✅

> Brightness/contrast/highlights/shadows/WB/saturation; pointer-first curve
> editor (DESIGN §4.4).

| Item | Status | Notes |
|---|---|---|
| Tone sliders (floating tool panel) | ✅ | Exposure + contrast + saturation in `TuneNode`, driven by a 3-slider `TonePanel`; GPU preview & libvips export use identical math (verified to match on a real photo). Warmth/highlights/shadows can follow. (Panel floats per §4.6, not bottom-docked) |
| Curves editor — drag points, add/remove | ✅ | `Curve` (monotone-cubic → 256 LUT), `CurvesNode` (libvips `maplut`), GPU LUT texture in canvas (preview==export verified), pointer-first `CurvesPanel` (click add / drag move / drag-out or Del remove, arrow nudge, draggable card) |
| Per-channel + luminance curves | ✅ | Four curves (RGB master + R/G/B); effective LUT = channel ∘ master; 3-channel GPU LUT texture (RGBA) + 4-band libvips `maplut`; channel-selector tabs in the editor; preview==export verified on a real photo |

---

## Phase 4 — LUT looks ✅

> 3D LUT (HALD CLUT) loader + apply.

| Item | Status | Notes |
|---|---|---|
| HALD CLUT loader | ✅ | `Lut3D` parses a HALD CLUT (side n³ → cube edge n²) with a trilinear `sample()`; identity round-trip + file load + invalid-input unit-tested (`lut3d_test`) |
| `LutNode` (trilinear apply) | ✅ | libvips export (per-pixel trilinear) + GPU preview via a 32³ `sampler3D` (hardware trilinear); wired into the graph after curves; preview==export verified on a real photo (inverting CLUT). Look persisted by CLUT path |
| Look intensity slider | ✅ | `out = mix(input, lut(input), t)` in both export and shader (intensity rides in `PreviewState`); `LooksPanel` (Load…/Clear + intensity slider); blend unit-tested + preview==export verified |

---

## Phase 5 — Selective adjustments ✅

> Local edits via mask (color-affinity, luminosity/tone, brush). DESIGN §4.4–4.5.

| Item | Status | Notes |
|---|---|---|
| Mask infrastructure on `EditNode` | ✅ | Pointwise masked-tone path: mask gates `mix(input, tone(input), mask)`. Parametric masks fold into the shader chain; texture-based masks (brush/colour) will extend this |
| Luminosity/tone range mask (parametric) | ✅ | `SelectiveNode` + `SelectivePanel` (range low/high + feather, exposure/contrast/saturation); smoothstep mask; libvips export + shader preview, preview==export verified. **Show mask** toggle: off → red overlay → grayscale (preview-only) |
| Color-affinity mask (guided filter) | ✅ | Self-contained guided filter (He et al., integral-image box means — no OpenCV) refines a colour-distance mask; click-to-pick target on the canvas + Range slider; export computes the mask full-res in the node, preview uploads it as a mask texture (`sampler2D` binding 4); verified selecting the logo by colour |
| Brush mask (Add/Subtract, session undo) | ✅ | Soft brush stamped along the drag (size/hardness), Add/Subtract, Clear; per-stroke **session undo** (Ctrl+Z pops a stroke), commits as one graph node on close; reuses the mask-texture preview; export upscales the working-res mask; mask persisted as base64 PNG for global undo |
| `SelectiveNode` | ✅ | Masked exposure/contrast/saturation; in the graph after looks; unit-tested (`selective_test`) |

---

## Phase 6 — Healing brush ✅

> Content-aware removal via inpainting.

| Item | Status | Notes |
|---|---|---|
| `HealNode` (inpaint) | ✅ | Self-contained **Telea FMM** inpaint (no OpenCV, per the guided-filter precedent); paints a heal mask (white = remove), `apply()` upscales it + fills via libvips export path; unit-tested + verified on a real photo. Also fixed a latent colourspace bug: full-image rebuilds now route through `Image::fromInterleaved` (tags sRGB) so coloured pixels round-trip faithfully — also fixed in Selective/Lut nodes |
| Heal brush UI (paint mask, commit) | ✅ | `HealPanel` (Paint/Erase/Clear, size, hardness) + heal node **first** in the graph. Reuses the shared brush-paint + per-stroke session undo (generalised via a brush-target enum). Live: red overlay while stroking, inpaint result shown on stroke end (`refreshBaseImage`); commit on close = one global undo step; verified on-screen |
| Higher-quality fill (Criminisi exemplar) | ✅ | Self-contained Criminisi et al. exemplar inpainting (copies real patches along isophotes — texture-aware, no OpenCV). HealNode quality toggle (Detailed/Fast) in the panel; default Detailed; unit-tested + verified vs Telea on a real photo. **Heal preview runs off the UI thread** (`QtConcurrent` + `QFutureWatcher`, latest-wins) so Detailed never freezes the app; "Healing…" hint while it computes. Exact-match early-out speeds flat regions |

---

## Phase 7 — Layers, masks, monochrome ✅

> Decided sequencing (2026-06-23): do these **before** RAW. RAW lands later as an
> 8-bit loader; a 16-bit-linear precision upgrade is a separate future effort.
> **Design: [LAYERS.md](LAYERS.md)** (layer model + the full mask system).

| Item | Status | Notes |
|---|---|---|
| Mask inversion | ✅ | `SelectiveValues.invert` — complements the mask in both libvips export and the shader (`selInvert` uniform); **Invert** toggle in the panel; mask overlay reflects it; unit-tested |
| Layers (per-layer adjustments, add/delete) | ✅ | Done: `MaskSpec`/`evaluateMask`; layered `EditGraph` + libvips composite export; **multi-pass GPU preview** (ping-pong, per-layer pass); **Layers panel** (add/delete/select/visibility/opacity) + active-layer routing of Tone/Curves/Looks; **per-layer mask UI** (None/Gradient/Radial + Feather + Invert) with an **on-canvas `MaskGizmo`** (draggable gradient line / radial ellipse; follows zoom/pan; passes non-handle events through to the canvas); **`SelectiveNode` dissolved** — selective edits are now masked adjustment layers (Luminosity/Colour/Brush mask + the layer's `TuneNode`), the Selective panel retargeted to drive the active layer; the "show mask" overlay reflects the active layer's mask; preview path evaluates data-driven masks against the source; **structural undo of layer add/delete** — a `createNode` factory rebuilds non-Base layer node chains from the snapshot (ids preserved), while the Base layer restores in place to keep external node pointers valid; verified add→undo→redo round-trips in the UI + unit-tested (`layer_undo_test`); **Selective panel folded into the Layers panel (Option 2)** — all mask editing (None/Gradient/Radial/Luminosity/Colour/Brush + colour-pick + Show-mask overlay) now lives in the Layers panel; `SelectivePanel` removed; tone for a masked layer is the normal Tone tool; the `selective` command adds a masked layer and opens the Layers + Tone panels. See [LAYERS.md](LAYERS.md) |
| Drawn / geometric masks (gradient, radial) | ✅ | Linear-gradient + radial/elliptical via `MaskSpec`/`evaluateMask` (free-hand already = brush mask), parametric → shader + libvips. Per-layer mask controls in the Layers panel + on-canvas `MaskGizmo` for direct manipulation; feather + invert; verified on-screen (radial = feathered bright ellipse, gradient = left→right ramp matching the gizmo). [LAYERS.md](LAYERS.md) §3 |
| Monochrome (B&W mixer, toning) | ✅ | `MonoNode` (pointwise — same math in libvips `apply()` and the shader, step 3.5): a weighted B&W mixer (R/G/B, normalised) + hue-tinted toning (strength + hue). In the Base chain after the look and added to every adjustment layer (added on demand for layers that lack it); `MonoPanel` mirrors `TonePanel` (enable toggle + 5 sliders); unit-tested (`mono_test`) + verified on-screen (neutral grey + warm-sepia toning). **Grain** deferred — it's spatial/stochastic, so it doesn't fold into the pointwise `PreviewState`; needs its own pass |
| **Project save/load (`.lumen`)** | ✅ | Self-contained binary document (`core/Project`): `LUMENPRJ` magic + version + JSON manifest (`EditGraph::saveState`) + the **original source image embedded verbatim**. Save/Open commands + **Ctrl+S / Ctrl+Shift+O**; `.lumen` also opens from the CLI / file arg. Load decodes the embedded source (`Image::fromBytes`, materialised), restores the Base layer **by node type** (`EditGraph::loadProjectState` — cross-session ids differ) and non-Base layers structurally via the `createNode` factory. Round-trip unit-tested (`project_test`) + verified end-to-end on-screen (radial-masked +2 EV layer survives save→reopen). Future: thumbnails, autosave, zip container |

---

## Deferred / post-v1 ⏸️

| Item | Notes |
|---|---|
| Lens correction (`LensCorrectionNode`) | LibRaw + Lensfun. DESIGN §8 |
| Full RAW workflow polish | LibRaw decode designed for from day one |
| Perspective / advanced crop-rotate | |
| Built-in presets -- Kodachrome 64, Fuji Velvia, Ilford HP5 Plus (ISO 400), Ilford Delta 400 (ISO 400) and Ilford FP4 Plus (ISO 125) (maybe reuse existing LUTs?)

---

## Dependencies wired up

| Library | Status | Used for |
|---|---|---|
| Qt6 (Core/Gui/Widgets/ShaderTools) | ✅ | UI, RHI rendering |
| libvips | ✅ | Image decode / pipeline |
| OpenCV | ⬜ | Healing, guided-filter masks (Phase 5–6) |
| LibRaw | ⬜ | RAW decode |
| Lensfun | ⬜ | Lens correction (deferred) |
