#pragma once

#include "core/Image.h"

#include <QString>

// EditNode is the base class for one non-destructive edit in the pipeline
// (DESIGN.md §5.1). A node holds its parameters (in subclasses), an enabled
// flag, and a dirty flag for cache invalidation. The graph walks nodes in order,
// each transforming the image it receives.
//
// Masks (radial/colour/luminosity/brush) will attach here in Phase 5; they are
// not modelled yet. Nodes are intentionally NOT QObjects — they're lightweight,
// copyable-by-pointer pipeline elements owned by the EditGraph.
class EditNode {
public:
    explicit EditNode(QString typeName);
    virtual ~EditNode();

    EditNode(const EditNode &) = delete;
    EditNode &operator=(const EditNode &) = delete;

    // Stable unique identifier (assigned at construction).
    const QString &id() const { return m_id; }
    // e.g. "tune", "curves", "lut".
    const QString &typeName() const { return m_typeName; }

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    // A dirty node (or one that has never been computed) must be re-applied by
    // the graph; a clean one can be served from cache.
    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; }

    // Applies this node's operation to `input`, returning a new image. Must be a
    // pure function of (input, parameters) so the graph can cache the result.
    // Concrete nodes (TuneNode, etc.) arrive in Phase 2.3+.
    virtual Image apply(const Image &input) const = 0;

protected:
    // Subclasses call this from parameter setters to invalidate cached output.
    void invalidate() { m_dirty = true; }

private:
    QString m_id;
    QString m_typeName;
    bool m_enabled = true;
    bool m_dirty = true; // never computed yet
};
