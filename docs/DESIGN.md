# Lumen — Design Document

> A native, Linux-first photo editor inspired by Snapseed, built around an
> immersive fullscreen canvas and a command-palette-driven workflow.

**Status:** Draft / pre-implementation
**Last updated:** 2026-06-22
**Primary target:** Linux · **Also a v1 requirement:** macOS

---

## 1. Vision

Lumen is a non-destructive photo editor whose differentiator is its
**interaction model**, not its image-processing math. The processing side
(tone, curves, masking, healing, LUT looks) is a largely solved problem served
by mature libraries. The novel work — and therefore the project's focus and
risk — is the UX: an immersive, fullscreen editing experience driven by a fuzzy
command palette, with the photo as "the whole world."

The guiding principle:

> **Keyboard to navigate and command; pointer to manipulate.**

You reach for tools, switch modes, undo, and move around the app from the
keyboard. You *adjust values* — drag curve points, paint masks, move sliders —
with the mouse or trackpad, with the keyboard available as a precision/garnish
layer (nudge, numeric entry, brush resize).

This is a deliberate revision of an earlier "fully keyboard-driven" concept.
Forcing inherently spatial, continuous interactions (building a curve, color
masking) onto arrow keys was the weakest, least-proven part of the idea. We keep
keyboard-first where it genuinely shines and let the pointer own manipulation.

### Non-goals (for now)
- Not a full Photoshop/GIMP replacement (no layers-as-canvas, no painting).
- Not a digital-asset-manager / library (no catalog, ratings, collections).
- Not an ML-feature showcase (no portrait relighting, sky replacement, etc.).

---

## 2. Target user

Someone already comfortable in keyboard-driven environments (terminal, Vim,
VSCode) who wants a fast, focused, native Linux photo editor — not a
mouse-heavy, panel-cluttered tool. The same audience most likely to want a
Linux-first image editor in the first place.

---

## 3. Scope

### v1 features
- **Tone / exposure / curves** — brightness, contrast, highlights, shadows,
  white balance, saturation; per-channel and luminance curves.
- **Selective adjustments** — local edits driven by a mask (color-affinity,
  luminosity/tone range, and brush).
- **Healing brush** — content-aware removal via inpainting.
- **Filters / looks** — LUT-based (3D / HALD CLUT) film and stylistic looks.

### Explicitly deferred (post-v1)
- **Lens correction** (distortion, vignetting, TCA). Requires pairing LibRaw
  with **Lensfun** as a separate `LensCorrectionNode`. Not in the v1 feature set
  and not on the critical path. See §8.
- RAW is *designed for* from day one (LibRaw in the loader) but full RAW
  workflow polish is a later milestone.
- Perspective / crop-rotate beyond a basic crop.

**macOS is a v1 requirement** (must run from first release), not deferred. This
drives the rendering decision in §7.

---

## 4. Interaction model

### 4.1 Two layers of interaction

| Layer | Input | Examples |
|---|---|---|
| **Navigate & command** (keyboard-first) | Keyboard | `/` palette, tool switching, undo/redo, history, zoom/pan, commit/cancel, preview toggle |
| **Manipulate** (pointer-first) | Mouse / trackpad, keyboard as garnish | Drag curve points, paint masks, move sliders; arrows nudge, numbers type, `[`/`]` resize brush |

### 4.2 Command palette
- Triggered by `/` from Browse mode. Overlays the dimmed (not hidden) image.
- **Fuzzy subsequence matching** (fzf-style), not strict prefix: `crv` → Curves,
  `xpo` → Exposure.
- **Frequency/recency ranking** so common tools bubble up.
- Enter activates the highlighted tool; Esc closes.

### 4.3 Modal input system

A lightweight Vim-spirited mode machine is the backbone of the immersive feel —
it's what lets `/` mean "open palette" in Browse but be inert mid-stroke.

```
Mode::Browse        — default; image fullscreen, palette accessible
Mode::ToolActive    — a tool is open; its (thin) keymap is live
Mode::MaskEditing   — sub-mode for mask painting / range selection
Mode::CommandPalette — fuzzy search overlay open
```

Modes are dispatched through a central `InputController`. Because manipulation is
now pointer-first, **per-tool keymaps are intentionally thin** — mostly a shared
vocabulary (nudge selected value, commit, cancel, toggle preview) rather than a
rich per-tool keyboard contract. The earlier idea of every tool implementing a
heavy `IKeyboardOperable` interface is dropped in favor of standard pointer
handling plus this small shared keyboard set.

### 4.4 Per-tool interaction

- **Curves** — pointer-first. Click to add a point, drag to move, drag off to
  remove. Keyboard: Tab/Shift+Tab cycle points, arrows nudge ±1 (Shift = ±10),
  numeric entry for the selected point, Delete removes it.
- **Selective / mask tools**
  - *Luminosity / tone* — parametric range + feather; equally easy with slider
    drag or arrow keys. This is the one mask type that's keyboard-friendly.
  - *Color-affinity* — pointer-first. Tap a point to sample the color; drag to
    set range. (Keyboard color-masking was the weakest unproven idea and is cut.)
  - *Brush* — pointer-first (see §4.5).
- **Sliders** (tone, brush size/hardness/opacity, look intensity) — drag, with
  arrow-nudge and direct numeric entry as backup.

### 4.5 Brush masking

- Strokes paint into a **session accumulator** while the tool is open.
- **Add** mode (default) unions strokes; **Subtract** removes. `Alt+drag` is the
  momentary subtract toggle (Photoshop/Lightroom muscle memory); a dedicated key
  flips persistently.
- **Local, session-level undo:** Ctrl+Z pops the *last stroke*, not the whole
  mask. This needs its own small history stack, separate from the global
  `EditGraph` history.
- On **commit** (Enter / close), the accumulated mask flattens and becomes a
  **single** node in the edit graph — so app-level undo backs out the whole
  masking session in one step.
- `[` / `]` resize the brush. A "Preview mask" toggle shows the mask shape vs.
  the photo.
- **Storage:** while the session is open, keep strokes as a growable list of
  vector paths / a layered bitmap so popping the last stroke is cheap. **Flatten
  to a single mask buffer only at commit time.**

### 4.6 Discoverability
- A persistent, dismissible **hint bar** shows the keys available in the current
  mode (which-key.nvim spirit). Doubles as the visual legend for the "drawer."
- Overlays **dim** the image rather than hide it, so you keep judging the edit
  live. Dimming opacity is a **tuning value** (not a fixed design constant):
  too dark loses edit context, too light hurts overlay legibility. Start with a
  sensible default and tune by feel during development; keep it a single named
  constant (or a hidden setting) so it's trivial to adjust.
- **Tool panels dock consistently at the bottom** so users build spatial memory.
  The palette appears near the top. (Consistency over the mixed mockup layout.)

---

## 5. Architecture

### 5.1 Non-destructive edit graph (core decision)

Every edit is a **node**; the final image is the result of walking the graph.
This is what makes Snapseed-style "stacks" work. **Commit to this early —
retrofitting it is painful.**

```
Image Source (JPEG / PNG / RAW)
        │
   [Edit Graph]  — ordered, revisitable nodes
   ┌────┼─────────┬───────────┐
  Tune  Curves  Selective    Heal   …
        │
   Compositor
   ┌────┴───────────┐
Preview (GL,        Export (libvips,
 downsampled)        full-res)
```

Each node holds:
- **Parameters** (e.g. exposure +0.3, contrast +10, curve control points)
- An optional **mask** (radial, color, luminosity, or brush; none = global)
- A **dirty flag** for cache invalidation

Two render paths from the same graph:
- **Preview** — downsampled image through GPU shaders for real-time feedback.
- **Export** — full graph through libvips at full resolution.

### 5.2 Layers

```
┌──────────────────────────────────────────┐
│  UI Layer — Qt6                           │
│  MainWindow · CommandPalette · ToolPanels │
│  InputController (modal state machine)    │
├──────────────────────────────────────────┤
│  Edit Graph — non-destructive node pipeline│
├──────────────────────────────────────────┤
│  Processing — libvips (tone/LUT/export)   │
│              OpenCV (heal, guided filter) │
│              LibRaw (decode)              │
├──────────────────────────────────────────┤
│  GPU Preview — Qt RHI (Metal/OpenGL/Vulkan)│
│  (downsampled real-time shader pipeline)  │
└──────────────────────────────────────────┘
```

### 5.3 Proposed project structure

```
lumen/
├── src/
│   ├── core/
│   │   ├── EditGraph.{h,cpp}      # node pipeline + history
│   │   ├── EditNode.{h,cpp}       # base node
│   │   ├── ImageBuffer.{h,cpp}    # libvips wrapper
│   │   └── nodes/
│   │       ├── TuneNode.cpp
│   │       ├── CurvesNode.cpp
│   │       ├── SelectiveNode.cpp
│   │       ├── HealNode.cpp
│   │       └── LutNode.cpp
│   ├── gpu/
│   │   ├── PreviewRenderer.{h,cpp}  # Qt RHI-based renderer
│   │   └── shaders/                 # shaders → compiled to .qsb per backend
│   ├── input/
│   │   ├── InputController.{h,cpp}  # modal state machine
│   │   └── CommandPalette.{h,cpp}   # fuzzy matcher + overlay
│   ├── ui/
│   │   ├── MainWindow.{h,cpp}
│   │   ├── Canvas.{h,cpp}           # zoom/pan + overlay host
│   │   ├── ToolPanel.{h,cpp}
│   │   ├── CurveEditor.{h,cpp}
│   │   └── BrushOverlay.{h,cpp}
│   └── main.cpp
├── docs/
│   └── DESIGN.md
├── mockups/
├── CMakeLists.txt
└── vcpkg.json
```

---

## 6. Technology choices

| Concern | Choice | Reason |
|---|---|---|
| UI | **Qt6 (C++)** | Best-in-class cross-platform native, strong OpenGL integration |
| Image pipeline | **libvips** | Non-destructive, lazy, fast, handles huge files, C API |
| CV algorithms | **OpenCV** | Healing inpaint (`cv::inpaint`, `xphoto`), guided filter for masks |
| GPU preview | **Qt RHI** (Metal on macOS, OpenGL/Vulkan on Linux) | One shader codebase, real-time preview, macOS-safe (no deprecated GL) |
| RAW decode | **LibRaw** | Decode + metadata; design for it from day one |
| Lens correction (later) | **Lensfun** | Geometric correction; pairs with LibRaw metadata |
| Build | **CMake + vcpkg (or Conan)** | All deps package cleanly |

### Feature → implementation notes
- **Tone / curves** — GLSL shaders for preview; libvips (`vips_linear`, tone
  curve LUT) for export. The curve editor UI (point interaction) is the bulk of
  the work here.
- **Selective** — color-affinity mask ≈ guided/bilateral selection
  (`cv::ximgproc::guidedFilter`), refined with brush strokes; mask feeds the
  node's processing.
- **Healing** — `cv::inpaint` (Telea / Navier-Stokes) as a solid start; PatchMatch
  / `xphoto::inpaint` for larger or higher-quality fills. Mark region → inpaint
  on mouse-up.
- **Looks** — 3D LUTs (HALD CLUT standard) applied via trilinear interpolation;
  libvips can apply directly. Source free CLUTs or generate from presets.

---

## 7. Platform notes

- **Linux is the primary, tested target.**
- **macOS is a v1 requirement** — Lumen must run on macOS from first release.
- **Consequence (decided):** OpenGL is deprecated on macOS, so we do **not**
  target raw OpenGL directly. The preview renderer is built on **Qt RHI** (Qt's
  rendering hardware interface), which targets **Metal on macOS** and
  OpenGL/Vulkan on Linux from a single shader/codebase. Shaders are authored once
  and compiled per-backend via Qt's shader tooling (`.qsb`).
  - `PreviewRenderer` is written against RHI, not `QOpenGLWidget`'s GL calls
    directly. (We may still host it in a `QRhiWidget` / equivalent.)
  - This is a day-one decision, not a retrofit: the GPU path is the hardest thing
    to port later, so it starts portable.
- **CI / testing:** both Linux and macOS builds run in CI from milestone 1; "must
  run on macOS" is meaningless without a macOS build being exercised regularly.

---

## 8. Deferred: lens correction

LibRaw is **purely a raw decoder** (demosaic, white balance, color conversion,
metadata). It does *not* do geometric correction. For distortion / vignetting /
TCA, the standard pairing is **LibRaw + Lensfun** (as darktable does):

- Look up a Lensfun profile by lens model + focal length + aperture + focus
  distance (using the metadata LibRaw extracted), then apply correction as a
  separate `LensCorrectionNode`.
- **Wrinkle:** some makers (Sony, Nikon Z, Olympus) embed correction params in
  the raw rather than baking them in — LibRaw exposes the tags, applying them is
  on us. Others bake vignetting correction in-camera, so a Lensfun profile can
  *double-correct*. A known friction point; handle carefully when we get here.

Deferred to a post-v1 milestone.

---

## 9. Risks & open questions

1. **The immersive/palette premise is the novel part — but now low-risk.** With
   manipulation moved to pointer-first (§1, §4), the previously risky pieces
   (keyboard curve editing, keyboard color-masking) are gone. What remains novel
   is the command palette + immersive shell — and command palettes are
   well-trodden (VSCode/Sublime), just new in an image editor. **Decision: no
   separate throwaway prototype.** It's cheap enough to validate directly in the
   real Qt app at milestone 1; if the palette/immersive flow feels off, iterate
   there. (This is a direct consequence of the keyboard tone-down.)
2. **Responsive zoom/pan + brush at full res** is the hidden hard part — more so
   than any individual filter. Nail the canvas interaction early; do not back the
   canvas with a `QLabel`/pixmap — use the RHI-backed canvas with a custom
   coordinate transform.
3. **macOS via RHI** — see §7. Decided (RHI/Metal from day one), but it's the
   first thing CI must exercise; an untested macOS build will rot fast.
4. **Color-affinity mask quality** — guided-filter selection may need iteration to
   match Snapseed's feel.

---

## 10. Suggested build order

1. **Weeks 1–2** — Skeleton: Qt window + RHI canvas, load/display image via
   libvips → GPU texture, zoom/pan. Stand up Linux **and** macOS CI here.
   Drop in the command palette + immersive shell early and validate the feel
   directly (no separate prototype — see §9.1).
2. **Weeks 3–4** — Edit graph: nodes, dirty/cache system, a simple exposure node.
3. **Weeks 5–7** — Tone tools + curves editor (pointer-first).
4. **Weeks 8–10** — LUT looks + HALD CLUT loader.
5. **Weeks 11–14** — Selective adjustments (mask + brush refinement).
6. **Weeks 15–18** — Healing brush.

Healing is last: most self-contained, and its brush UX slots into the overlay
infrastructure built for selective adjustments.

---

## 11. Naming

Project name: **Lumen** (SI unit of luminous flux) — clean, technical without
being obscure, immediately legible. (`Latent` was the evocative alternative.)
```
