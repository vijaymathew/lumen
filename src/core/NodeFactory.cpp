#include "core/NodeFactory.h"

#include "core/CurvesNode.h"
#include "core/EditNode.h"
#include "core/HealNode.h"
#include "core/LensCorrectionNode.h"
#include "core/LutNode.h"
#include "core/MonoNode.h"
#include "core/TuneNode.h"

#include <QString>

std::unique_ptr<EditNode> createNode(const QString &typeName)
{
    if (typeName == QLatin1String("tune"))
        return std::make_unique<TuneNode>();
    if (typeName == QLatin1String("curves"))
        return std::make_unique<CurvesNode>();
    if (typeName == QLatin1String("lut"))
        return std::make_unique<LutNode>();
    if (typeName == QLatin1String("mono"))
        return std::make_unique<MonoNode>();
    if (typeName == QLatin1String("heal"))
        return std::make_unique<HealNode>();
    if (typeName == QLatin1String("lens"))
        return std::make_unique<LensCorrectionNode>();
    return nullptr;
}
