#include "core/NodeFactory.h"

#include "core/ColorGradeNode.h"
#include "core/ColorMixerNode.h"
#include "core/CurvesNode.h"
#include "core/DefringeNode.h"
#include "core/DenoiseNode.h"
#include "core/EditNode.h"
#include "core/GrainNode.h"
#include "core/HealNode.h"
#include "core/LensCorrectionNode.h"
#include "core/LutNode.h"
#include "core/MonoNode.h"
#include "core/SharpenNode.h"
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
    if (typeName == QLatin1String("colorgrade"))
        return std::make_unique<ColorGradeNode>();
    if (typeName == QLatin1String("colormixer"))
        return std::make_unique<ColorMixerNode>();
    if (typeName == QLatin1String("heal"))
        return std::make_unique<HealNode>();
    if (typeName == QLatin1String("lens"))
        return std::make_unique<LensCorrectionNode>();
    if (typeName == QLatin1String("sharpen"))
        return std::make_unique<SharpenNode>();
    if (typeName == QLatin1String("denoise"))
        return std::make_unique<DenoiseNode>();
    if (typeName == QLatin1String("defringe"))
        return std::make_unique<DefringeNode>();
    if (typeName == QLatin1String("grain"))
        return std::make_unique<GrainNode>();
    return nullptr;
}
