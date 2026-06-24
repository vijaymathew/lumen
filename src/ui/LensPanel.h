#pragma once

#include "core/LensCorrectionNode.h" // Params

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

// LensPanel is the floating tool card for geometric correction: an automatic
// section (distortion / TCA / vignetting, driven from the EXIF-matched Lensfun
// profile) and a manual perspective section (vertical & horizontal keystone,
// rotation, zoom). The automatic controls are disabled when no lens profile was
// matched; perspective is always available. It mirrors MonoPanel — drives the
// live preview via paramsChanged() and closes on Esc/Enter.
//
// The panel only edits correction parameters; the lens *identity* (camera/lens
// model, focal, aperture…) is carried verbatim from the node it was revealed for.
class LensPanel : public QWidget {
    Q_OBJECT

public:
    explicit LensPanel(QWidget *parent = nullptr);

    // `detected` is the human-readable matched lens (or empty); `matched` gates
    // the automatic-correction controls.
    void reveal(const LensCorrectionNode::Params &params, bool matched,
                const QString &detected);

signals:
    void paramsChanged(const LensCorrectionNode::Params &params);
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QSlider *addRow(const QString &name, int min, int max, QLabel **valueOut);
    QPushButton *addToggle(const QString &text);
    void onChanged();
    void refreshLabels();
    LensCorrectionNode::Params currentParams() const;

    LensCorrectionNode::Params m_base; // identity carried verbatim
    bool m_matched = false;

    QLabel *m_detected = nullptr;
    QPushButton *m_distortion = nullptr;
    QSlider *m_distortionAmount = nullptr;
    QLabel *m_distortionValue = nullptr;
    QPushButton *m_tca = nullptr;
    QPushButton *m_vignetting = nullptr;
    QSlider *m_vignettingAmount = nullptr;
    QLabel *m_vignettingValue = nullptr;
    QSlider *m_keystoneV = nullptr;
    QLabel *m_keystoneVValue = nullptr;
    QSlider *m_keystoneH = nullptr;
    QLabel *m_keystoneHValue = nullptr;
    QSlider *m_rotate = nullptr;
    QLabel *m_rotateValue = nullptr;
    QSlider *m_scale = nullptr;
    QLabel *m_scaleValue = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;
};
