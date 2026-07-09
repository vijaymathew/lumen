#include "core/AiModelStore.h"

#include <QFileInfo>
#include <QSettings>

#include <cstdlib>

namespace raw {

QString aiActiveModelPath()
{
    // Developer/testing override: an explicit model path wins over the setting.
    if (const char *env = std::getenv("LUMEN_DEMOSAIC_MODEL")) {
        const QString p = QString::fromLocal8Bit(env);
        if (!p.isEmpty() && QFileInfo::exists(p))
            return p;
    }
    const QString p = QSettings().value(QStringLiteral("aiDemosaic/modelPath")).toString();
    // A remembered model may have since been moved or deleted — treat that as
    // "no model" so the AI option disables itself rather than failing to load.
    if (!p.isEmpty() && QFileInfo::exists(p))
        return p;
    return QString();
}

void setAiActiveModelPath(const QString &path)
{
    QSettings().setValue(QStringLiteral("aiDemosaic/modelPath"), path);
}

} // namespace raw
