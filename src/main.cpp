#include "core/ImageBuffer.h"
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
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("An immersive, command-palette-driven photo editor"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("image"),
                                 QStringLiteral("Image file to open on launch"));
    parser.process(app);

    // Scope the window so it (and the libvips-backed images its edit graph
    // caches) destruct BEFORE vips_shutdown() — unref'ing a VipsImage after the
    // library is gone dereferences freed GLib type tables and crashes.
    int rc = 0;
    {
        MainWindow window;
        window.show();

        // An image named on the command line is an explicit choice and wins over
        // crash recovery; otherwise offer to restore work from a session that
        // didn't shut down cleanly.
        const QStringList args = parser.positionalArguments();
        if (!args.isEmpty())
            window.openPath(args.first());
        else
            window.offerCrashRecovery();

        rc = app.exec();
    }
    ImageBuffer::shutdownLibrary();
    return rc;
}
