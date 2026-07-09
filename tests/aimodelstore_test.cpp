// Unit test for raw::AiModelStore: the user-selected model path persists via
// QSettings, resolves only when the file still exists, and the
// $LUMEN_DEMOSAIC_MODEL override takes precedence. Uses a temp settings root so
// it never touches the real user store. No networking or ONNX Runtime involved.

#include "core/AiModelStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);     \
            return 1;                                                          \
        }                                                                      \
    } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LumenTest"));
    QCoreApplication::setApplicationName(QStringLiteral("aimodelstore_test"));
    QStandardPaths::setTestModeEnabled(true); // redirect QSettings to a temp store

    using namespace raw;

    // Test-mode settings persist between runs — start clean, and make sure no
    // override leaks in from the environment.
    QSettings().clear();
    qunsetenv("LUMEN_DEMOSAIC_MODEL");

    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString modelFile = QDir(tmp.path()).filePath(QStringLiteral("model.onnx"));

    // 1. Clean slate: nothing selected → nothing resolves.
    CHECK(aiActiveModelPath().isEmpty());

    // 2. Selecting a path that doesn't exist yet does not resolve (guards against
    //    pointing inference at a missing file).
    setAiActiveModelPath(modelFile);
    CHECK(aiActiveModelPath().isEmpty());

    // 3. Once the file exists, the selection resolves and persists.
    {
        QFile f(modelFile);
        CHECK(f.open(QIODevice::WriteOnly));
        f.write("onnx");
        f.close();
    }
    CHECK(aiActiveModelPath() == modelFile);

    // 4. Clearing the selection resolves to empty again.
    setAiActiveModelPath(QString());
    CHECK(aiActiveModelPath().isEmpty());

    // 5. The environment override wins over the stored selection.
    {
        const QString other = QDir(tmp.path()).filePath(QStringLiteral("override.onnx"));
        QFile f(other);
        CHECK(f.open(QIODevice::WriteOnly));
        f.write("onnx");
        f.close();
        setAiActiveModelPath(modelFile);         // stored selection
        qputenv("LUMEN_DEMOSAIC_MODEL", other.toLocal8Bit());
        CHECK(aiActiveModelPath() == other);     // override takes precedence
        qunsetenv("LUMEN_DEMOSAIC_MODEL");
        CHECK(aiActiveModelPath() == modelFile); // falls back to the selection
    }

    std::printf("aimodelstore_test OK\n");
    return 0;
}
