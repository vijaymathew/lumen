#pragma once

#include "core/ContentCredentials.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

// Configures the signing identity used for Content Credentials: the user's own
// certificate chain and private key (Lumen never generates or ships a key),
// the signature algorithm, and an optional timestamp server. Only the file
// paths are persisted (QSettings "credentials/*"); key material stays in the
// user's files and is read at signing time.
class ContentCredentialsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ContentCredentialsDialog(QWidget *parent = nullptr);

    // The persisted signer configuration (empty paths when none is set up).
    static credentials::SignerConfig loadSigner();
    static void saveSigner(const credentials::SignerConfig &cfg);

private:
    credentials::SignerConfig current() const;
    void browse(QLineEdit *target, const QString &title);
    void updateKeyWarning();
    void runTest();

    QLineEdit *m_cert = nullptr;
    QLineEdit *m_key = nullptr;
    QComboBox *m_alg = nullptr;
    QLineEdit *m_tsa = nullptr;
    QLabel *m_keyWarning = nullptr;
    QLabel *m_testResult = nullptr;
    QPushButton *m_test = nullptr;
};
