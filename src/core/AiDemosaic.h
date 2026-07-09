#pragma once

#include "core/RawLoader.h" // raw::ColorProfile

#include <QString>

#include <cstdint>
#include <vector>

// AI (neural) demosaicing. Kept in its own translation unit so the optional
// ONNX Runtime dependency is isolated: everything here compiles to a graceful
// "unavailable" stub when the app is built without LUMEN_AI_DEMOSAIC, and the
// RAW loader falls back to a classic LibRaw demosaic. The interface deals only
// in plain data (no LibRaw types), so RawLoader owns the sensor-specific
// extraction and this module owns inference + colour.
namespace raw {

// A demosaic-ready view of the sensor: one photosite per output pixel,
// black-subtracted, normalised, and white-balanced to grey — but NOT yet
// interpolated or colour-transformed. `bayer` is false for X-Trans / Foveon /
// no-CFA sources, which the neural model does not handle (caller falls back).
struct MosaicInput {
    std::vector<float> mono;  // width*height, 0..1 (black→0, sensor white→1)
    std::vector<uint8_t> cfa; // width*height, per-pixel colour: 0=R 1=G 2=B
    int width = 0;
    int height = 0;
    bool bayer = false;
};

// True when AI demosaicing is compiled into this build (LUMEN_AI_DEMOSAIC),
// regardless of whether a model has been downloaded yet. Gates the model-manager
// UI entry point — the user can open it to fetch a model even before one exists.
bool aiDemosaicSupported();

// True only when AI demosaicing is both supported AND a usable model is present
// and loads. Gates the "AI" demosaic option itself, so it's never offered when it
// would silently fall back to a classic demosaic.
bool aiDemosaicAvailable();

// Runs the neural demosaicker on `in` and applies the camera→sRGB colour
// transform (derived from `color`), returning interleaved RGBA in Lumen's
// 0..255 float working scale — the same format processToImage produces. Returns
// an empty vector (and sets *error) on any failure or when unavailable, so the
// caller can fall back to a classic demosaic. Bayer input only.
std::vector<float> runAiDemosaic(const MosaicInput &in, const ColorProfile &color,
                                 QString *error);

} // namespace raw
