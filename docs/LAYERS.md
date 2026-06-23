# Lumen — Layers & Mask System (design note)

> Design for the Layers feature (Phase 7) and the mask system it carries —
> including drawn/geometric masks (linear gradient, radial/elliptical) alongside
> the existing brush / luminosity / colour masks.

**Status:** Design — not yet implemented
**Last updated:** 2026-06-23
**Depends on / supersedes:** the single-chain `EditGraph` and `SelectiveNode`
(see DESIGN.md §5.1, PROGRESS.md Phase 7)

---

## 1. Goals

- Adjustments apply to the **currently active layer**. One layer (the Base)
  exists by default; the user can add / delete / reorder any number of layers,
  each with its own adjustments **and its own mask**.
- A layer's mask can be: **none** (full image), **brush**, **linear gradient**,
  **radial/elliptical**, **luminosity range**, or **colour affinity** — with an
  **invert** flag and per-mask **feather/opacity**. (Free-hand = the existing
  brush mask; gradient/radial are the new geometric masks.)
- Keep the project invariant: **GPU preview == libvips export** for everything.

### Non-goals (v1 of layers)
- Rich blend modes — start with **Normal + opacity** (multiply/screen/overlay
  later).
- Arbitrary mask *compositing* graphs. v1 allows **one base mask per layer**,
  optionally refined by brush add/subtract (covers the Lightroom
  "radial + brush" workflow) — not a free-form boolean mask tree.
- Group/nested layers.

---

## 2. Model

A **Project** is a source image plus an ordered list of layers:

```
Project
├── source              (decoded Image; heal/inpaint applies here, pre-stack)
└── layers[]            (bottom → top)
    ├── Layer 0 "Base"  (mask = None/full; cannot be deleted; global look)
    ├── Layer 1 …       (own adjustments + own mask + opacity)
    └── …
```

```cpp
struct Layer {
    QString id;            // stable uuid
    QString name;          // "Base", "Sky", "Subject", …
    bool    enabled = true;
    float   opacity = 1.0; // 0..1
    BlendMode blend = Normal;
    MaskSpec mask;         // None for the Base layer
    std::vector<std::unique_ptr<EditNode>> adjustments; // tune, curves, lut, mono…
};
```

Each layer owns an ordered list of **adjustment nodes** — the *existing*
`TuneNode` / `CurvesNode` / `LutNode` (and the future `MonoNode`). The Base layer
is just a layer whose mask selects everything. This unifies "global edit" and
"local edit": there is no special-case global path — the Base layer *is* the
global edit.

### Composition

Layers composite **sequentially**, each adjusting the running result, gated by
its mask × opacity:

```
result = source                       (after heal/inpaint, see §6)
for layer in layers (bottom → top), if enabled:
    adjusted = applyAdjustments(layer, result)   // the layer's node list
    m        = coverage(layer.mask) * layer.opacity
    result   = blend(result, adjusted, m, layer.blend)   // Normal: mix(result, adjusted, m)
```

`coverage(mask)` is a per-pixel value in [0,1]. For the Base layer it is 1
everywhere, so `result = applyAdjustments(Base, source)` — identical to today's
global chain.

### What happens to `SelectiveNode`

It **dissolves**: its *adjustment* (exposure/contrast/saturation) becomes a
`TuneNode` in a layer; its *mask* (luminosity/colour/brush + invert) becomes the
**layer's mask**. "Selective adjustment" in the UI becomes "add a masked layer."
This removes a whole node and folds masking into one place.

---

## 3. Mask system (`MaskSpec`)

A mask is a small, serializable spec that resolves to per-pixel coverage. Most
types are **parametric** — computed identically in the fragment shader and in
libvips, so they're resolution-independent, cheap, and preview==export for free
(exactly how the luminosity mask already works).

```cpp
struct MaskSpec {
    enum Type { None, Brush, LinearGradient, Radial, Luminosity, Colour };
    Type  type = None;
    bool  invert = false;
    float feather = 0.1;      // edge softness (meaning depends on type)

    // Linear gradient: a directed line; coverage ramps 0→1 across it.
    QPointF gradFrom, gradTo; // image-normalised endpoints

    // Radial / elliptical: centre, radii, rotation; coverage inside (or out).
    QPointF center; float radiusX, radiusY, angle; bool radialInside = true;

    // Luminosity range.
    float low = 0, high = 1;

    // Colour affinity.
    float targetR, targetG, targetB, colourRange;

    // Optional brush refinement layered on top of the above (add/subtract),
    // stored as a working-res bitmap (base64 PNG in the project, as today).
    MaskBuffer brushRefine;   // empty = none
};
```

### Coverage math (shared, shader + libvips)

- **Linear gradient** — project the pixel onto the `from→to` axis, normalise to
  the line's length, `smoothstep` with `feather`. (A "graduated filter".)
- **Radial/elliptical** — `d = length((p − center) / (radiusX, radiusY))` after
  un-rotating by `angle`; `smoothstep(1 − feather, 1, d)`, flipped by
  `radialInside`. A circle is `radiusX == radiusY`; a hard shape is
  `feather ≈ 0`.
- **Luminosity / Colour** — as today (`SelectiveMask`).
- **Brush** — sampled from the layer's mask texture (as today).
- Then `if (invert) coverage = 1 − coverage`, and finally compose the optional
  `brushRefine` (`max`/`min` for add/subtract).

Geometric + luminosity + colour are parametric → no texture needed (a few
uniforms). Only **brush / colour** need a per-layer mask texture.

---

## 4. Architecture changes

### 4.1 Core: `EditGraph` → layered project

- Replace the single node list with the `layers[]` model above. Keep the
  existing **dirty-flag / cache** idea, but per layer: cache each layer's output
  image; a change in layer *k* invalidates *k* and everything above it.
- `result()` (libvips export) walks the layers, compositing per §2 with masks
  evaluated full-res (parametric masks computed per-pixel; brush upscaled).
- Snapshot **undo** already serialises node state — extend it to the whole layer
  list (order, masks, adjustments). Brush masks persist as base64 PNG (codec
  exists).

### 4.2 GPU preview: the deferred multi-pass chain

This is the real new machinery (DESIGN §5.1 explicitly deferred it). Today the
shader does the whole chain in one pass. Layers need **multi-pass ping-pong**:

```
texA ← source (healed base texture)
for each enabled layer:
    pass: read texA + (layer mask: uniforms and/or mask texture)
          → applyAdjustments + masked blend → write texB
    swap(texA, texB)
present texA
```

- Each layer = one render-to-texture pass into an offscreen RGBA target; two
  targets are reused (ping-pong).
- Per-layer **caching**: keep each layer's output texture; on edit, re-run only
  from the changed layer upward. Mirrors the core dirty/cache model on the GPU.
- The per-layer adjustment shader is essentially today's `texture.frag` body
  (tune + curves LUT + 3D LUT + mono…), now parameterised per layer and writing
  to a target instead of the screen. Mask coverage is computed in the same pass.
- Mask **overlay** (red / grayscale, already built) extends to every mask type
  by visualising `coverage`.

### 4.3 Export

`Image EditGraph::result()` composites the layers via libvips: for each layer,
`adjusted = applyAdjustments(input)`, `coverage = maskImage(spec)`,
`out = input*(1−c·opacity) + adjusted·(c·opacity)`. Mask images are generated
parametrically (a `vips` expression or a small CPU fill) so export matches the
shader.

---

## 5. UI

### 5.1 Layers panel (new)
A docked/floating list, bottom→top: each row shows name, a visibility toggle,
opacity, and a mask-type chip; the **active layer** is highlighted. Controls:
**＋ add layer**, **🗑 delete**, drag to reorder, double-click to rename. The
existing **Tone / Curves / Looks / Mono** tool panels edit the **active
layer's** corresponding adjustment (routing changes from "the node" to
"active-layer.node").

### 5.2 Mask creation & on-canvas gizmos
Choosing a layer's mask type arms an on-canvas interaction (reusing the
overlay-over-RHI pattern from the brush ring):
- **Gradient** — drag a line; show the line + two feather guides; endpoints and
  the midline are draggable; ⇧ constrains angle.
- **Radial** — drag to place/size an ellipse; handles for radius (x/y), a
  rotation handle, centre drag; toggle inside/outside.
- **Brush** — the existing brush (size/hardness via `s`/`h`+wheel, add/subtract,
  session undo) — also used to *refine* a gradient/radial mask.
- **Luminosity / Colour** — existing parametric controls / colour pick.
The mask overlay (red / grayscale toggle) confirms coverage for any type.

---

## 6. Where heal fits

Heal/inpaint **edits source pixels** (it's not a blend), so it stays a
pre-stack operation on the base image: `source' = heal(source)`, then the layer
stack runs on `source'`. Conceptually heal is "fix the original," layers are
"interpret the original." (Implementation-wise it can be modelled as a special
Base-layer pre-pass; it must run before the cached layer textures.)

---

## 7. Migration from today

1. Wrap the current chain as **Base layer** holding `TuneNode + CurvesNode +
   LutNode`; heal stays the pre-stack op (§6).
2. Build the layer container + per-layer cache in core; route the existing tool
   panels to the active layer.
3. Build the **multi-pass GPU preview** (the big step) — start with Base only
   (one pass, == today), then N layers.
4. Fold `SelectiveNode` into "masked layer": its tone params → a `TuneNode`, its
   mask → the layer mask. Keep project-file back-compat by translating old
   selective state into a layer on load.
5. Add the geometric masks (gradient, radial) — parametric, so just coverage
   math (shader + libvips) + the gizmos.

---

## 8. Suggested build order

1. **Mask abstraction** — `MaskSpec` + a `coverage()` evaluator (CPU/libvips) and
   the shader snippet; unit-test the gradient/radial/luminosity math. (No UI yet;
   verifiable like the existing mask tests.)
2. **Layer container + libvips compositing** (`result()` over layers) + undo/
   serialisation. Export works with layers before the GPU preview does.
3. **Multi-pass GPU preview** (ping-pong + per-layer cache).
4. **Layers panel UI** + active-layer routing; dissolve `SelectiveNode`.
5. **Mask gizmos** (gradient line, radial ellipse) + overlay for all types.
6. **Monochrome** as a layer adjustment (`MonoNode`) — slots in once layers exist.

Each step is independently verifiable (export first, then preview), matching how
the rest of the project was built.

---

## 9. Open decisions (recommended defaults in **bold**)

- **Layer adjustment scope:** a layer holds a small ordered set of the existing
  adjustment nodes (**tune + curves + lut + mono**), not an arbitrary graph.
- **Blend modes:** **Normal + opacity** now; multiply/screen/overlay later.
- **Mask compositing:** **one base mask type per layer + optional brush
  refine**, not a free-form boolean tree.
- **Geometric mask source of truth:** **parametric** (uniforms + libvips math),
  not baked to a texture — keeps it crisp at any zoom and preview==export.
