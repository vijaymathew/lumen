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

    MainWindow window;
    window.show();

    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty())
        window.openPath(args.first());

    const int rc = app.exec();
    ImageBuffer::shutdownLibrary();
    return rc;
}
