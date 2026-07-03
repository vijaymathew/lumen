// GUI smoke check for the Presets browser: builds the real PresetsPanel with the
// real thumbnail pipeline (EditGraph → built-in presets → resultDownsampled →
// QPixmap) and renders it to a PNG. Not a CTest assertion — it needs a display
// and is run by hand to eyeball the panel:
//
//   DISPLAY=:0 ./build/presets_gui_smoke <image> <out.png>
//
// Verifies end-to-end that thumbnails generate and the panel lays out (headers,
// cards, images) without touching MainWindow.

#include "core/BuiltinPresets.h"
#include "core/ColorGradeNode.h"
#include "core/CurvesNode.h"
#include "core/EditGraph.h"
#include "core/GrainNode.h"
#include "core/Image.h"
#include "core/ImageBuffer.h"
#include "core/MonoNode.h"
#include "core/Preset.h"
#include "core/TuneNode.h"
#include "ui/PresetsPanel.h"

#include <QApplication>
#include <QTimer>

#include <cstdio>
#include <memory>

int main(int argc, char **argv)
{
    if (!ImageBuffer::initLibrary(argv[0])) {
        std::fprintf(stderr, "vips init failed\n");
        return 1;
    }
    QApplication app(argc, argv);

    const QString imgPath =
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("docs/screenshot.png");
    const QString outPath =
        argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("/tmp/panel.png");

    QString error;
    Image src = Image::fromFile(imgPath, &error);
    if (src.isNull()) {
        std::fprintf(stderr, "load failed: %s\n", qPrintable(error));
        return 1;
    }

    // Mirror MainWindow's Base creative chain so thumbnails reflect the real look.
    EditGraph g;
    g.setSource(src);
    g.addNode(std::make_unique<TuneNode>());
    g.addNode(std::make_unique<CurvesNode>());
    g.addNode(std::make_unique<ColorGradeNode>());
    g.addNode(std::make_unique<MonoNode>());
    g.addNode(std::make_unique<GrainNode>());

    QVector<PresetsPanel::Item> items;
    for (const preset::Builtin &b : preset::library()) {
        preset::applyToGraph(b.data, g);
        const QImage thumb = g.resultDownsampled(200).toQImage();
        items.push_back({b.id, b.name, b.category, QPixmap::fromImage(thumb),
                         b.id.startsWith(QStringLiteral("user:"))});
    }

    auto *panel = new PresetsPanel;
    panel->setItems(items);
    panel->reveal();
    panel->adjustSize();

    // Give the layout a beat to settle, grab the widget, save, quit.
    const int count = items.size();
    QTimer::singleShot(600, [panel, outPath, count] {
        const bool ok = panel->grab().save(outPath);
        std::printf("%s: %d preset cards -> %s\n", ok ? "OK" : "SAVE FAILED", count,
                    qPrintable(outPath));
        QCoreApplication::quit();
    });
    return app.exec();
}
