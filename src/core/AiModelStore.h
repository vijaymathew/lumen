#pragma once

#include <QString>

// Persistence for the user's chosen AI-demosaic model. The model is a local
// ONNX file the user points Lumen at (via the file picker in RawSettingsPanel);
// its path is remembered in QSettings so inference (AiDemosaic) loads it on the
// next decode and across sessions. Pure Core — no networking, no ONNX Runtime —
// so it compiles and is unit-testable regardless of the LUMEN_AI_DEMOSAIC flag.
namespace raw {

// Resolved path to the model inference should load: the $LUMEN_DEMOSAIC_MODEL
// override if set and present, else the user-selected model if it still exists,
// else empty (availability then degrades gracefully to a classic demosaic).
QString aiActiveModelPath();

// Remembers the user's chosen model path (empty clears the selection).
void setAiActiveModelPath(const QString &path);

} // namespace raw
