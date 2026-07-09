// Headless smoke test for the RAW panel's "AI" demosaic button discoverability
// rule: in a build without AI support the button is hidden (no dead, unexplained
// control); in a build with AI it is visible and clickable (never a greyed-out
// dead-end). Runs on the offscreen platform so it needs no display.

#include "core/AiDemosaic.h"
#include "ui/RawSettingsPanel.h"

#include <QApplication>
#include <QPushButton>

#include <cstdio>

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    RawSettingsPanel panel;
    panel.reveal(raw::RawDecodeOptions{}, raw::RawLensDefaults{}); // realise state

    // Optional: dump a screenshot for visual inspection of the segmented rows.
    if (const QByteArray png = qgetenv("LUMEN_SMOKE_PNG"); !png.isEmpty())
        panel.grab().save(QString::fromLocal8Bit(png));

    QPushButton *ai = nullptr;
    for (QPushButton *b : panel.findChildren<QPushButton *>())
        if (b->text() == QStringLiteral("AI")) {
            ai = b;
            break;
        }
    if (!ai) {
        std::fprintf(stderr, "FAIL: AI demosaic button not found\n");
        return 1;
    }

    const bool supported = raw::aiDemosaicSupported();
    if (supported) {
        if (ai->isHidden()) {
            std::fprintf(stderr, "FAIL: AI button hidden in an AI-enabled build\n");
            return 1;
        }
        if (!ai->isEnabled()) {
            std::fprintf(stderr, "FAIL: AI button disabled (dead-end) in an AI build\n");
            return 1;
        }
    } else {
        if (!ai->isHidden()) {
            std::fprintf(stderr, "FAIL: AI button shown in a build without AI support\n");
            return 1;
        }
    }

    std::printf("raw_ai_button_smoke OK (aiDemosaicSupported=%d)\n", supported);
    return 0;
}
