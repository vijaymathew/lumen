// GUI smoke check for the Layers panel: renders the real LayersPanel with a
// few rows and grabs it to a PNG. Not a CTest assertion — it needs a display
// and is run by hand to eyeball the panel:
//
//   DISPLAY=:0 ./build/layers_gui_smoke <out.png>
#include "core/MaskSpec.h"
#include "ui/LayersPanel.h"

#include <QApplication>
#include <QTimer>

#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    const QString outPath =
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("/tmp/layers_panel.png");

    auto *panel = new LayersPanel;
    QVector<LayersPanel::Row> rows;
    rows.push_back({QStringLiteral("Base"), true});
    rows.push_back({QStringLiteral("Sky Burn"), true});
    rows.push_back({QStringLiteral("Skin Dodge"), true});
    rows.push_back({QStringLiteral("Vignette"), false});
    panel->setLayers(rows, 2, 80); // "Skin Dodge" active
    panel->setMaskState(MaskSpec{}, false, 40, 80, true, 0);
    panel->move(20, 20);
    panel->show();
    panel->raise();
    panel->adjustSize();

    QTimer::singleShot(300, [panel, outPath] {
        const bool ok = panel->grab().save(outPath);
        std::printf("%s -> %s\n", ok ? "OK" : "SAVE FAILED", qPrintable(outPath));
        QCoreApplication::quit();
    });
    return app.exec();
}
