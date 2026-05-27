#ifndef TOPO_BUILD_PASSCATEGORYREGISTRY_H
#define TOPO_BUILD_PASSCATEGORYREGISTRY_H

#include "topo/Build/PassConfig.h"

#include <optional>
#include <string_view>

namespace topo {

/// Look up the FeatureCategory for a pass by name.
///
/// Registry entries MUST stay aligned with the project's authoritative
/// feature-taxonomy definition; a CI gate enforces it.
///
/// Returns std::nullopt for unregistered pass names so callers can distinguish
/// "unknown pass" from any valid category value.
std::optional<FeatureCategory> categoryOf(std::string_view passName);

} // namespace topo

#endif // TOPO_BUILD_PASSCATEGORYREGISTRY_H
