#pragma once

#include <QFileDialog>
#include <QString>

// A non-native "Open image" dialog that browses like an image library: every
// supported format — camera RAW (e.g. Panasonic .rw2) included — is shown as a
// real thumbnail in an icon grid, where the OS's native dialog only thumbnails
// the formats it recognises. Thumbnails come from ThumbnailProxyModel, which
// renders them off the UI thread and caches them so browsing stays responsive.
class ImageOpenDialog : public QFileDialog {
    Q_OBJECT

public:
    ImageOpenDialog(QWidget *parent, const QString &caption, const QString &dir,
                    const QString &filter);

    // Runs the dialog modally; returns the chosen path, or an empty string if the
    // user cancelled. Drop-in replacement for QFileDialog::getOpenFileName.
    static QString getOpenFileName(QWidget *parent, const QString &caption,
                                   const QString &dir, const QString &filter);

protected:
    // Enforces a normal size the first time the dialog is shown, overriding any
    // oversized geometry a window manager may have remembered from a prior open.
    void showEvent(QShowEvent *event) override;

private:
    bool m_sizedOnce = false;
};
