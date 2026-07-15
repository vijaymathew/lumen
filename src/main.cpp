#include "core/ImageBuffer.h"
#include "ui/AppIcon.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    // libvips must be initialised before any pipeline call. (Init lives in
    // ImageBuffer so vips/glib headers stay out of this Qt-heavy TU — see the
    // note in ImageBuffer.h.)
    if (!ImageBuffer::initLibrary(argv[0])) {
        qFatal("Failed to initialise libvips");
        return 1;
    }

    QApplication app(argc, argv);
    // Organisation + application name give QSettings a stable per-user store
    // (used to remember the file dialogs' last-used directories).
    QCoreApplication::setOrganizationName(QStringLiteral("Lumen"));
    QCoreApplication::setApplicationName(QStringLiteral("Lumen"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LUMEN_VERSION));
    // Window / taskbar icon (generated in code — see AppIcon).
    app.setWindowIcon(makeAppIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("An immersive, command-palette-driven photo editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("image"),
                                 QStringLiteral("Image file(s) to open on launch"),
                                 QStringLiteral("[image...]"));
    parser.process(app);

    // Scope the window so it (and the libvips-backed images its edit graph
    // caches) destruct BEFORE vips_shutdown() — unref'ing a VipsImage after the
    // library is gone dereferences freed GLib type tables and crashes.
    int rc = 0;
    {
        MainWindow window;
        window.show();

        // Images named on the command line are an explicit choice and win over
        // crash recovery; otherwise offer to restore work from a session that
        // didn't shut down cleanly. Each path opens as its own tab (the first
        // reuses the empty launch document); opens past the first are queued and
        // drained as each decode completes.
        const QStringList args = parser.positionalArguments();
        if (!args.isEmpty()) {
            for (const QString &path : args)
                window.openPath(path);
        } else {
            window.offerCrashRecovery();
        }

        rc = app.exec();
    }
    ImageBuffer::shutdownLibrary();
    return rc;
}
