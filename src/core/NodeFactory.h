#pragma once

#include <memory>

class EditNode;
class QString;

// Creates a node from its typeName() (e.g. "tune", "curves", "lut", "mono",
// "heal"). Used by structural undo to rebuild a layer's node chain from a
// snapshot. Returns nullptr for an unknown type.
std::unique_ptr<EditNode> createNode(const QString &typeName);
