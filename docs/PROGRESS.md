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
| LUT loader (HALD + `.cube`) | ✅ | `Lut3D` parses a HALD CLUT (side n³ → cube edge n²) **and Adobe/Resolve `.cube` 3D LUTs** (`fromCubeFile`: `LUT_3D_SIZE` + n³ red-fastest triples; `LutNode::loadLut` dispatches by extension; trilinear `sample()`; identity round-trip + HALD/`.cube` file load + invalid-input unit-tested (`lut3d_test`). **v2: float cube** — the internal cube is now `float` (no 8-bit quantisation), output values are unclamped (HDR looks survive), and `DOMAIN_MIN/MAX` / `LUT_3D_INPUT_RANGE` are honoured (inputs remapped from the LUT domain to [0,1] at sample time). Precision + HDR + non-default-DOMAIN unit-tested. CPU export is full-float; the GPU preview still resamples into a 32³ RGBA8 `sampler3D` (display-accurate; a float GPU LUT texture stays with the scene-linear/float-preview stage) |
| `LutNode` (trilinear apply) | ✅ | libvips export (per-pixel trilinear) + GPU preview via a 32³ `sampler3D` (hardware trilinear); wired into the graph after curves; preview==export verified on a real photo (inverting CLUT). Look persisted by CLUT path. **v2: float GPU LUT** — the look texture is now `RGBA16F` half-float (`resampleCube` → `qfloat16`, unclamped), so the preview carries the float cube's precision (no 8-bit LUT banding) while keeping hardware trilinear filtering; applies to the Base and per-layer cubes. (Out-of-range >1 look outputs still clamp where the result is written to the RGBA8 offscreen ping-pong buffers — full HDR-through-preview waits on the scene-linear/float-offscreen stage) |
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

## Post-v1 🟡

| Item | Status | Notes |
|---|---|---|
| RAW decode | ✅ | `core/RawLoader` decodes camera RAW via **LibRaw** (unpack → `dcraw_process` → **16-bit** sRGB, camera WB) → float `Image`, dropping into the same pipeline as a JPEG. Routed by extension in `openPath` + the open dialog (filter includes UPPERCASE globs, shared list with `isRawPath`); `.lumen` re-decodes the embedded RAW bytes on load. `raw_test` covers classification + graceful failure. **Verified on a real 25 MB Canon `.CR2`**: decodes + renders correctly, RAW→edit→`.lumen`→reopen round-trips |
| High-precision pipeline (float) | 🟡 | **Stage 1–2 done:** the working `Image` is now **float** (sRGB-encoded, unclamped) end-to-end — `fromFile`/`fromBytes`/`fromInterleavedFloat`, and every node (Tune/Curves/Lut/Mono/Heal/composite) processes at float (no inter-node 8-bit banding); `CurvesNode` interpolates its 256-LUT to match the GPU. RAW decodes at **16-bit → float**. **16-bit PNG/TIFF export** (ExportDialog depth selector; `saveToFile(..., bits)`); verified genuine `ushort rgb16` on a full-res CR2. `preview == export` preserved (8-bit display). Verified: 16/16 tests, visual parity on the CR2. **Remaining (Stage 3+):** true scene-linear processing (exposure/WB/blend in linear; currently encoded-space with the `/2.2` exposure approximation), extended RAW **highlight-recovery headroom** (LibRaw still normalises highlights to white), and a **float GPU preview** so recovery is live on-canvas (today the preview source is 8-bit, display-accurate) |
| Basic white balance | ✅ | Temperature (warm↔cool) + Tint (magenta↔green) folded into `TuneNode` as encoded-space per-channel gains (`wbGains`, applied before exposure); in both `apply()` (folded into the exposure/contrast `vips_linear`) and the GPU shader (`PreviewState` +3 floats `wbR/G/B`, ubuf in vert+frag, UBO 160→176) so **preview==export**; two sliders in `TonePanel`. `tune_node_test` covers gains/preview/apply. A true linear-light/Kelvin WB stays with the deferred scene-linear stage |
| Lens correction + perspective (`LensCorrectionNode`) | ✅ | First Base node (geometry baked into the preview base, before heal/tone). **Automatic** distortion / TCA / vignetting from EXIF via **Lensfun** (optional dep — `LUMEN_HAVE_LENSFUN`; perspective-only without it). `RawLoader` extracts camera/lens identity; matched by camera/lens with a **fixed-lens-compact fallback** (camera-mount lookup). **Manual perspective**: vertical/horizontal keystone + rotate + zoom via a centre-pinned homography (`vips_mapim` backward-resample). `LensPanel` tool; per-correction toggles + amounts. Re-renders the corrected source on change (cached so heal dabs don't re-warp), **preview == export** preserved. `lens_node_test` covers the homography + serialise; **verified on a real CR2** (Canon G7 X Mark II compact: auto-corrected, mean Δ≈12) |
| Built-in presets (film looks) | ⬜ | Decided approach: **parametric recipes applied as a layer** (Tune/Curves/Mono node bundles; B&W films → Mono mixer). Velvia, Kodachrome 64, HP5, Delta 400, FP4. No new deps |
| Interactive crop / rotate UI | ⬜ | On-canvas crop rectangle + free-rotate handles. The perspective/zoom homography already exists in `LensCorrectionNode`; this is the framing/gesture layer on top |
| Sharpen (`SharpenNode`) | ✅ | Unsharp mask via `vips_sharpen` (L* only — no colour fringing); Amount + Radius. A Base node in the "baked" group (after heal, before tone) → bakes into the preview base off the UI thread (debounced), so it's capture-style sharpening. Output cast back to float to keep the pipeline invariant. `SharpenPanel` tool. `sharpen_test` (flat unchanged, edge overshoot, serialise); verified at full-res on a 20 MP CR2 |
| Histogram | ✅ | `core/Histogram` (`computeHistogram` → 256-bin RGB from a downsampled, display-clamped copy via `vips_hist_find`); `HistogramWidget` additive RGB overlay (bottom-left), toggled from the palette. Consumes the full composite (`m_graph.result()`) computed **off the UI thread** (debounced, latest-wins), so big RAWs don't hitch. `histogram_test` covers bin placement |
| Denoise (`DenoiseNode`) | ✅ | L*a*b*-space noise reduction: **Color** = gaussian blur of a/b; **Luminance** = self-guided `guidedFilter` on L (edge-preserving, reuses the mask filter — no new deps). Base node in the "baked" group, placed **before** sharpen (lens→heal→denoise→sharpen) so noise isn't amplified; bakes off the UI thread (debounced). `DenoisePanel` tool. `denoise_test` (luma noise reduced, edge preserved, serialise); ~1.8 s full-res on a 20 MP CR2. v1 quality (good chroma; NL-means/AI would need OpenCV) |
| Show highlight/shadow clipping on the image

---

## Dependencies wired up

| Library | Status | Used for |
|---|---|---|
| Qt6 (Core/Gui/Widgets/ShaderTools) | ✅ | UI, RHI rendering |
| libvips | ✅ | Image decode / pipeline |
| OpenCV | ⬜ | Not needed — healing / guided-filter masks were written self-contained |
| LibRaw | ✅ | RAW decode (`core/RawLoader`); pkg-config `libraw` 0.21 |
| Lensfun | ✅ (optional) | Automatic lens correction (`core/LensCorrectionNode`); pkg-config `lensfun` 0.3.4. **Optional** — gated by `LUMEN_HAVE_LENSFUN`; without it the node is perspective-only |
