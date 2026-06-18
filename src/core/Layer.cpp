// vips before any Qt-tainted header (see Image.cpp for why).
#include <vips/vips.h>

#include "core/Layer.h"

#include "core/EditNode.h"
#include "core/NodeFactory.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

Layer::Layer(QString name) : m_name(std::move(name)) {}
Layer::~Layer() = default;

void Layer::setOpacity(float o)
{
    m_opacity = std::clamp(o, 0.0f, 1.0f);
}

void Layer::setMask(const MaskSpec &mask)
{
    m_mask = mask;
}

EditNode *Layer::addNode(std::unique_ptr<EditNode> node)
{
    EditNode *raw = node.get();
    m_nodes.push_back(std::move(node));
    m_cache.emplace_back(); // empty cache slot; the node starts dirty
    return raw;
}

bool Layer::removeNode(const QString &id)
{
    const int idx = indexOf(id);
    if (idx < 0)
        return false;
    m_nodes.erase(m_nodes.begin() + idx);
    m_cache.erase(m_cache.begin() + idx);
    invalidateFrom(idx);
    return true;
}

bool Layer::moveNode(int from, int to)
{
    const int n = static_cast<int>(m_nodes.size());
    if (from < 0 || from >= n || to < 0 || to >= n)
        return false;
    if (from == to)
        return true;
    auto node = std::move(m_nodes[from]);
    m_nodes.erase(m_nodes.begin() + from);
    m_nodes.insert(m_nodes.begin() + to, std::move(node));
    m_cache.assign(m_nodes.size(), Image());
    invalidateFrom(std::min(from, to));
    return true;
}

EditNode *Layer::nodeAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_nodes.size()))
        return nullptr;
    return m_nodes[index].get();
}

EditNode *Layer::findNode(const QString &id) const
{
    const int idx = indexOf(id);
    return idx < 0 ? nullptr : m_nodes[idx].get();
}

EditNode *Layer::nodeOfType(const QString &type) const
{
    for (const auto &n : m_nodes)
        if (n->typeName() == type)
            return n.get();
    return nullptr;
}

int Layer::indexOf(const QString &id) const
{
    for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i)
        if (m_nodes[i]->id() == id)
            return i;
    return -1;
}

void Layer::invalidateFrom(int index)
{
    if (index < 0)
        index = 0;
    for (int i = index; i < static_cast<int>(m_nodes.size()); ++i) {
        m_nodes[i]->markDirty();
        if (i < static_cast<int>(m_cache.size()))
            m_cache[i] = Image();
    }
}

Image Layer::applyAdjustments(Image input)
{
    Image current = std::move(input);
    if (current.isNull())
        return current;

    // Once any node recomputes, every node after it must recompute too.
    bool recomputeRest = false;
    for (int i = 0; i < static_cast<int>(m_nodes.size()); ++i) {
        EditNode *node = m_nodes[i].get();
        const bool needsCompute =
            recomputeRest || node->isDirty() || m_cache[i].isNull();
        if (needsCompute) {
            current = node->isEnabled() ? node->apply(current) : current;
            m_cache[i] = current;
            node->clearDirty();
            recomputeRest = true;
        } else {
            current = m_cache[i];
        }
    }
    return current;
}

Image Layer::applyAdjustmentsUncached(Image input) const
{
    Image current = std::move(input);
    if (current.isNull())
        return current;
    for (const auto &node : m_nodes)
        if (node->isEnabled())
            current = node->apply(current);
    return current;
}

Image Layer::composite(Image base)
{
    if (base.isNull() || !m_enabled)
        return base;
    Image adjusted = applyAdjustments(base);
    return blend(std::move(base), std::move(adjusted));
}

Image Layer::compositeUncached(Image base) const
{
    if (base.isNull() || !m_enabled)
        return base;
    Image adjusted = applyAdjustmentsUncached(base);
    return blend(std::move(base), std::move(adjusted));
}

Image Layer::blend(Image base, Image adjusted) const
{
    // Base-layer fast path: a None mask (with no exclusive zone) at full opacity
    // is just the adjustment.
    if (m_mask.type == MaskSpec::None && m_mask.zones.empty() && m_opacity >= 0.999f)
        return adjusted;
    if (m_opacity <= 0.001f)
        return base; // layer contributes nothing

    // Blend base and adjusted per pixel by coverage × opacity, at float precision
    // (alpha is taken from the base).
    VipsImage *bf = nullptr, *af = nullptr;
    if (vips_cast(base.handle(), &bf, VIPS_FORMAT_FLOAT, nullptr))
        return adjusted;
    if (vips_cast(adjusted.handle(), &af, VIPS_FORMAT_FLOAT, nullptr)) {
        g_object_unref(bf);
        return adjusted;
    }
    void *bbuf = vips_image_write_to_memory(bf, nullptr);
    void *abuf = vips_image_write_to_memory(af, nullptr);
    const int w = bf->Xsize, h = bf->Ysize, bands = bf->Bands;
    const bool sizeOk = af->Xsize == w && af->Ysize == h;
    g_object_unref(bf);
    g_object_unref(af);
    if (!bbuf || !abuf || !sizeOk) {
        if (bbuf) g_free(bbuf);
        if (abuf) g_free(abuf);
        return adjusted;
    }

    auto *bp = static_cast<float *>(bbuf);
    const auto *ap = static_cast<const float *>(abuf);

    // Luminosity/Colour masks read the base pixels; evaluateMask wants 8-bit, so
    // give it an 8-bit copy of the base (geometric masks ignore it).
    std::vector<uint8_t> base8;
    if (m_mask.type == MaskSpec::Luminosity || m_mask.type == MaskSpec::Colour) {
        base8.resize(static_cast<size_t>(w) * h * bands);
        for (size_t i = 0; i < base8.size(); ++i)
            base8[i] = static_cast<uint8_t>(std::clamp(std::lround(bp[i]), 0L, 255L));
    }
    const MaskBuffer cov =
        evaluateMask(m_mask, w, h, base8.empty() ? nullptr : base8.data(), bands);

    const long long n = static_cast<long long>(w) * h;
    for (long long i = 0; i < n; ++i) {
        const float c = (cov.data.empty() ? 0.0f : cov.data[i]) * m_opacity;
        float *b = bp + i * bands;
        const float *a = ap + i * bands;
        for (int ch = 0; ch < 3; ++ch)
            b[ch] = b[ch] * (1.0f - c) + a[ch] * c;
        // alpha (band 3+) left as the base's
    }

    Image out = Image::fromInterleavedFloat(bp, w, h, bands);
    g_free(bbuf);
    g_free(abuf);
    return out.isNull() ? adjusted : out;
}

void Layer::contributeToPreview(PreviewState &state) const
{
    for (const auto &node : m_nodes)
        if (node->isEnabled())
            node->contributeToPreview(state);
}

void Layer::contributeToPreviewLut(ChannelLuts &luts) const
{
    for (const auto &node : m_nodes)
        if (node->isEnabled())
            node->contributeToPreviewLut(luts);
}

Lut3D Layer::look() const
{
    Lut3D look;
    for (const auto &node : m_nodes) {
        if (!node->isEnabled())
            continue;
        const Lut3D *l = node->lookLut();
        if (l && l->isValid())
            look = *l; // last enabled look wins
    }
    return look;
}

QJsonObject Layer::saveState() const
{
    QJsonObject o;
    o[QStringLiteral("name")] = m_name;
    o[QStringLiteral("enabled")] = m_enabled;
    o[QStringLiteral("opacity")] = m_opacity;
    o[QStringLiteral("mask")] = m_mask.toJson();
    QJsonArray nodes;
    for (const auto &node : m_nodes) {
        QJsonObject e;
        e[QStringLiteral("id")] = node->id();
        e[QStringLiteral("type")] = node->typeName();
        e[QStringLiteral("state")] = node->saveState();
        nodes.append(e);
    }
    o[QStringLiteral("nodes")] = nodes;
    return o;
}

void Layer::restoreProperties(const QJsonObject &o)
{
    if (o.contains(QStringLiteral("name")))
        m_name = o.value(QStringLiteral("name")).toString();
    m_enabled = o.value(QStringLiteral("enabled")).toBool(true);
    m_opacity = static_cast<float>(o.value(QStringLiteral("opacity")).toDouble(1.0));
    m_mask = MaskSpec::fromJson(o.value(QStringLiteral("mask")).toObject());
}

void Layer::restoreState(const QJsonObject &o)
{
    restoreProperties(o);
    // Restore parameters into existing nodes by id (no recreation).
    for (const QJsonValue &v : o.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject e = v.toObject();
        if (EditNode *node = findNode(e.value(QStringLiteral("id")).toString()))
            node->restoreState(e.value(QStringLiteral("state")).toObject());
    }
}

void Layer::restoreByType(const QJsonObject &o)
{
    restoreProperties(o);
    for (const QJsonValue &v : o.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject e = v.toObject();
        if (EditNode *node = nodeOfType(e.value(QStringLiteral("type")).toString()))
            node->restoreState(e.value(QStringLiteral("state")).toObject());
    }
}

void Layer::restoreStructure(const QJsonObject &o)
{
    restoreProperties(o);
    // Rebuild the node chain from the snapshot, recreating each node via the
    // factory and preserving its id (so later id-based restores still match).
    m_nodes.clear();
    m_cache.clear();
    for (const QJsonValue &v : o.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject e = v.toObject();
        std::unique_ptr<EditNode> node =
            createNode(e.value(QStringLiteral("type")).toString());
        if (!node)
            continue; // unknown type — skip rather than corrupt the chain
        node->setId(e.value(QStringLiteral("id")).toString());
        node->restoreState(e.value(QStringLiteral("state")).toObject());
        m_nodes.push_back(std::move(node));
        m_cache.emplace_back(); // fresh node starts dirty (null cache)
    }
}
