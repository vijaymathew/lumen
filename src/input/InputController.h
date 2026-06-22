#pragma once

#include <QObject>

// InputController owns the app's modal state — the backbone of the immersive,
// command-driven feel (see docs/DESIGN.md §4.3). It is deliberately lightweight:
// for milestone 1 only Browse and CommandPalette are exercised, but the full set
// of modes is defined so tools can slot in without reworking the state machine.
//
// Per the keyboard tone-down, per-tool keymaps are thin; this class tracks
// *which* mode is active and lets the rest of the UI react, rather than owning a
// large per-tool key-dispatch table.
class InputController : public QObject {
    Q_OBJECT

public:
    enum class Mode {
        Browse,         // default: image fullscreen, palette accessible
        ToolActive,     // a tool is open, its keymap is live
        MaskEditing,    // sub-mode for mask painting / range selection
        CommandPalette, // fuzzy search overlay is open
    };
    Q_ENUM(Mode)

    explicit InputController(QObject *parent = nullptr);

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

signals:
    void modeChanged(Mode mode);

private:
    Mode m_mode = Mode::Browse;
};
