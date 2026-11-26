#include "ui/ContentCredentialsDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {
const QString kCertKey = QStringLiteral("credentials/certChainPath");
const QString kKeyKey = QStringLiteral("credentials/privateKeyPath");
const QString kAlgKey = QStringLiteral("credentials/alg");
const QString kTsaKey = QStringLiteral("credentials/tsaUrl");
} // namespace

credentials::SignerConfig ContentCredentialsDialog::loadSigner()
{
    QSettings s;
    credentials::SignerConfig c;
    c.certChainPath = s.value(kCertKey).toString();
    c.privateKeyPath = s.value(kKeyKey).toString();
    c.alg = s.value(kAlgKey, c.alg).toString();
    c.tsaUrl = s.value(kTsaKey).toString();
    return c;
}

void ContentCredentialsDialog::saveSigner(const credentials::SignerConfig &c)
{
    QSettings s;
    s.setValue(kCertKey, c.certChainPath);
    s.setValue(kKeyKey, c.privateKeyPath);
    s.setValue(kAlgKey, c.alg);
    s.setValue(kTsaKey, c.tsaUrl);
}

ContentCredentialsDialog::ContentCredentialsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Content Credentials"));
    setMinimumWidth(520);

    auto *intro = new QLabel(this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    intro->setOpenExternalLinks(true);
    intro->setText(QStringLiteral(
        "Exports can carry signed <b>C2PA Content Credentials</b>: a tamper-evident record "
        "of the original photo and of the edits Lumen made, none of them generative AI.<br><br>"
        "Credentials are signed with <i>your</i> certificate. A certificate from a CA on the "
        "C2PA trust list shows your name to verifiers. Any other certificate still produces "
        "valid, tamper-evident credentials, but verifiers will show the signer as unrecognised."));

    const credentials::SignerConfig cfg = loadSigner();

    m_cert = new QLineEdit(cfg.certChainPath, this);
    m_cert->setPlaceholderText(QStringLiteral("PEM file: signing certificate, then intermediates"));
    auto *certBrowse = new QPushButton(QStringLiteral("Browse…"), this);
    auto *certRow = new QHBoxLayout;
    certRow->setContentsMargins(0, 0, 0, 0);
    certRow->addWidget(m_cert, 1);
    certRow->addWidget(certBrowse);

    m_key = new QLineEdit(cfg.privateKeyPath, this);
    m_key->setPlaceholderText(QStringLiteral("PEM file: the certificate's private key"));
    auto *keyBrowse = new QPushButton(QStringLiteral("Browse…"), this);
    auto *keyRow = new QHBoxLayout;
    keyRow->setContentsMargins(0, 0, 0, 0);
    keyRow->addWidget(m_key, 1);
    keyRow->addWidget(keyBrowse);

    m_keyWarning = new QLabel(this);
    m_keyWarning->setWordWrap(true);
    m_keyWarning->setStyleSheet(QStringLiteral("color: #d08a2e;"));

    m_alg = new QComboBox(this);
    for (const QString &a : credentials::supportedAlgorithms())
        m_alg->addItem(a.toUpper(), a);
    if (const int idx = m_alg->findData(cfg.alg.toLower()); idx >= 0)
        m_alg->setCurrentIndex(idx);

    m_tsa = new QLineEdit(cfg.tsaUrl, this);
    m_tsa->setPlaceholderText(QStringLiteral("Optional. Leave empty to sign offline."));
    m_tsa->setToolTip(QStringLiteral(
        "An RFC 3161 timestamp server keeps the signature verifiable after your certificate "
        "expires. Each export then sends a hash of the manifest (not the image) to that server."));

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Certificate chain"), certRow);
    form->addRow(QStringLiteral("Private key"), keyRow);
    form->addRow(QString(), m_keyWarning);
    form->addRow(QStringLiteral("Algorithm"), m_alg);
    form->addRow(QStringLiteral("Timestamp server"), m_tsa);

    m_test = new QPushButton(QStringLiteral("Test signing"), this);
    m_testResult = new QLabel(this);
    m_testResult->setWordWrap(true);
    m_testResult->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *testRow = new QHBoxLayout;
    testRow->setContentsMargins(0, 0, 0, 0);
    testRow->addWidget(m_test, 0, Qt::AlignTop);
    testRow->addWidget(m_testResult, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel
                                             | QDialogButtonBox::Reset,
                                         this);
    buttons->button(QDialogButtonBox::Reset)->setText(QStringLiteral("Clear"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        saveSigner(current());
        accept();
    });
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this] {
        m_cert->clear();
        m_key->clear();
        m_tsa->clear();
        m_testResult->clear();
    });

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addSpacing(8);
    layout->addLayout(form);
    layout->addLayout(testRow);
    layout->addStretch(1);
    layout->addWidget(buttons);

    connect(certBrowse, &QPushButton::clicked, this,
            [this] { browse(m_cert, QStringLiteral("Certificate chain (PEM)")); });
    connect(keyBrowse, &QPushButton::clicked, this,
            [this] { browse(m_key, QStringLiteral("Private key (PEM)")); });
    connect(m_key, &QLineEdit::textChanged, this, &ContentCredentialsDialog::updateKeyWarning);
    connect(m_test, &QPushButton::clicked, this, &ContentCredentialsDialog::runTest);
    updateKeyWarning();

    if (!credentials::available()) {
        for (QWidget *w : {static_cast<QWidget *>(m_cert), static_cast<QWidget *>(certBrowse),
                           static_cast<QWidget *>(m_key), static_cast<QWidget *>(keyBrowse),
                           static_cast<QWidget *>(m_alg), static_cast<QWidget *>(m_tsa),
                           static_cast<QWidget *>(m_test)})
            w->setEnabled(false);
        buttons->button(QDialogButtonBox::Save)->setEnabled(false);
        m_testResult->setText(QStringLiteral(
            "This build of Lumen was compiled without libc2pa, so it can't sign. "
            "Rebuild with install.sh to enable Content Credentials."));
    }
}

credentials::SignerConfig ContentCredentialsDialog::current() const
{
    credentials::SignerConfig c;
    c.certChainPath = m_cert->text().trimmed();
    c.privateKeyPath = m_key->text().trimmed();
    c.alg = m_alg->currentData().toString();
    c.tsaUrl = m_tsa->text().trimmed();
    return c;
}

void ContentCredentialsDialog::browse(QLineEdit *target, const QString &title)
{
    const QString start = target->text().isEmpty() ? QString() : QFileInfo(target->text()).path();
    const QString path = QFileDialog::getOpenFileName(
        this, title, start, QStringLiteral("PEM files (*.pem *.crt *.cer *.pub *.key);;All files (*)"));
    if (!path.isEmpty())
        target->setText(path);
}

void ContentCredentialsDialog::updateKeyWarning()
{
    // A signing key others on this machine can read lets them sign as you.
    const QFileInfo fi(m_key->text().trimmed());
    const bool exposed = fi.exists()
        && (fi.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther));
#ifdef Q_OS_WIN
    const bool show = false; // POSIX permission bits don't apply
#else
    const bool show = exposed;
#endif
    m_keyWarning->setVisible(show);
    if (show)
        m_keyWarning->setText(QStringLiteral(
            "Other users on this computer can read this key. Consider: chmod 600 \"%1\"")
                                  .arg(fi.absoluteFilePath()));
}

void ContentCredentialsDialog::runTest()
{
    const credentials::SignerConfig cfg = current();
    if (!cfg.isConfigured()) {
        m_testResult->setText(QStringLiteral("Choose a certificate chain and a private key first."));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString summary, error;
    const bool ok = credentials::testSigner(cfg, &summary, &error);
    QApplication::restoreOverrideCursor();
    m_testResult->setText(ok ? QStringLiteral("✓ %1").arg(summary)
                             : QStringLiteral("✗ %1").arg(error));
}
