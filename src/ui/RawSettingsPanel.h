#pragma once

#include "core/RawLoader.h" // raw::RawDecodeOptions, raw::RawLensDefaults

#include <QVector>
#include <QWidget>

class QButtonGroup;
class QLabel;
class QPushButton;
class QSlider;

// RawSettingsPanel is the floating tool card for the automatic-RAW configuration:
// LibRaw decode-time options (auto-brightness + threshold, highlight handling,
// white-balance source, demosaic algorithm) plus the default state of the Lensfun
// auto-corrections. Each change is the new global default; for the open RAW the
// decode knobs trigger a re-decode and the lens toggles update its lens node.
// Closes on Esc/Enter.
class RawSettingsPanel : public QWidget {
    Q_OBJECT

public:
    explicit RawSettingsPanel(QWidget *parent = nullptr);

    void reveal(const raw::RawDecodeOptions &opts, const raw::RawLensDefaults &lens);

signals:
    void valuesChanged(const raw::RawDecodeOptions &opts, const raw::RawLensDefaults &lens);
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    // Builds a labelled row of mutually-exclusive buttons; `values[i]` is the
    // logical value carried by button i. Returns the group (id == index).
    QButtonGroup *addButtonRow(const QString &name, const QStringList &labels);
    // Reflects the given values into the controls without emitting valuesChanged.
    void setControls(const raw::RawDecodeOptions &opts, const raw::RawLensDefaults &lens);
    // Opens a file picker to select a local .onnx model, then re-evaluates.
    void chooseAiModel();
    // Reflects AI support in the "AI" button: hidden when not built in, visible +
    // clickable (with an explanatory tooltip) when it is. Also updates the model
    // button's label to show the current selection.
    void refreshAiAvailability();
    // User picked "AI" with no usable model: open the file picker, then either
    // decode with AI (if they chose a valid one) or revert to the prior algorithm.
    void handleAiWithoutModel();
    void onChanged();
    void refreshLabels();
    raw::RawDecodeOptions currentOptions() const;
    raw::RawLensDefaults currentLens() const;

    QPushButton *m_autoBright = nullptr;
    QSlider *m_threshold = nullptr; // auto_bright_thr * 1000 (1..100 → 0.001..0.1)
    QLabel *m_thresholdValue = nullptr;
    QButtonGroup *m_highlight = nullptr; // ids: 0 clip, 1 blend(→2), 2 reconstruct(→3)
    QButtonGroup *m_wb = nullptr;        // ids match RawDecodeOptions::Wb
    QButtonGroup *m_demosaic = nullptr;  // ids match user_qual (0..4), 5 = AI
    int m_prevDemosaicId = 3;            // last non-AI choice, to revert to (AHD)
    QPushButton *m_aiModelButton = nullptr; // "Choose AI model…" (only when built in)
    QLabel *m_aiHint = nullptr;             // empty-state hint: AI needs a model file
    QPushButton *m_lensDistortion = nullptr;
    QPushButton *m_lensTca = nullptr;
    QPushButton *m_lensVignetting = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
