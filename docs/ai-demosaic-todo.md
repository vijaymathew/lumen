# AI demosaicing — TODO / roadmap

Status of the neural-demosaicing feature and the work still open. See
`src/core/AiDemosaic.{h,cpp}` (inference + colour), `src/core/AiModelStore.*`
(model-path persistence), and the RAW panel (`src/ui/RawSettingsPanel.*`).

## Done (context)

- Opt-in build flag `LUMEN_AI_DEMOSAIC` (OFF by default); when off the code is a
  stub and the RAW loader falls back to AHD.
- ONNX Runtime discovery: CMake package **or** a prebuilt SDK via
  `ONNXRUNTIME_ROOT`. Shared lib bundled next to the binary on install.
- Release workflow builds flag-on, fetches CPU ORT 1.20.1, bundles it into the
  AppImage/DMG, and greps the AppImage to fail loudly if the lib is missing.
- RAW pipeline: `extractMosaic()` (black-subtract, WB-to-grey, RGGB) →
  `runAiDemosaic()` (RGGB packing, tiled inference, camera→sRGB + sRGB gamma).
- UI: hidden "AI" button when not built in; when built in, a "Choose AI model…"
  file picker (local `.onnx`, path remembered in QSettings) + an empty-state
  hint. `$LUMEN_DEMOSAIC_MODEL` overrides the picked model.
- Tests: `aimodelstore_test` (path persistence/override), `raw_ai_button_smoke`
  (button visibility per build), `rawoptions_test` (AiDemosaic value round-trip).

## Open — model & correctness

- [ ] **Ship/point at a real model.** Lumen ships none. Model must match the
      I/O contract: input float32 `[1,4,H/2,W/2]` RGGB-packed (dynamic H/W),
      output float32 `[1,3,H,W]` linear camera RGB. Either re-export a model
      (e.g. `torch.onnx.export` from demosaicnet, MIT) to this contract, or adapt
      `extractMosaic`/`runAiDemosaic` to a chosen model's actual I/O.
- [ ] **Validate model I/O with a clear error** instead of the current silent
      AHD fallback on shape mismatch — surface "output was X, expected Y" so a
      wrong model is obvious, not mysteriously inert.
- [ ] **Verify colour/gamma + tile seams** against AHD on real Bayer files. The
      `runAiDemosaic` colour math and tiling are untested end-to-end (no model in
      CI). Confirm no visible seams and correct colour.
- [ ] **Honour `autoBright` / `highlight`** in the AI path — it currently does a
      fixed normalisation and ignores those RAW-decode options.
- [ ] Test the hint-hidden / AI-selected states once a loadable model exists
      (only the empty-state is verified so far).

## Open — packaging & CI

- [ ] **Run the release workflow end-to-end** (`workflow_dispatch`) — the AI
      bundling has never actually executed. Confirm the AppImage and DMG bundle
      **and load** ORT at runtime.
- [ ] **macOS DMG smoke check** analogous to the AppImage `libonnxruntime` grep
      (mount the `.dmg`, look in `lumen.app/Contents/Frameworks`). macdeployqt
      `-libpath` bundling is currently unverified.
- [ ] **GPU execution providers.** CPU-only today. GPU EPs ship as separate
      `libonnxruntime_providers_*` libs that are `dlopen`'d at runtime, so the
      auto-bundlers miss them — needs explicit install rules + EP-registration
      code in `AiDemosaic.cpp`.
- [ ] **Windows** — no release job exists; AI packaging untouched.
- [ ] **Release size decision.** Every release now carries ORT (~tens of MB)
      even for users who never demosaic RAW. Consider a separate AI-enabled
      artifact variant (CI matrix axis) instead of flipping the default jobs.

## Open — future

- [ ] **Interactive preview.** AI is full-res/export-oriented; the live canvas
      uses the classic half-size demosaic. Formalise a two-tier strategy (fast
      classic for preview, AI for export) if AI preview is wanted.
- [ ] **X-Trans / non-Bayer.** AI is Bayer-only; X-Trans/Foveon fall back to
      LibRaw. A packed-X-Trans path + model would be a separate effort.
- [ ] **Model catalogue (reconsidered).** The in-app download catalogue was
      removed in favour of a file picker (hosting/licensing complexity). Revisit
      only if a redistribution-permissive, hosted model becomes available.
