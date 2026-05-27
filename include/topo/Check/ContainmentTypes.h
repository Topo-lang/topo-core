#ifndef TOPO_CHECK_CONTAINMENTTYPES_H
#define TOPO_CHECK_CONTAINMENTTYPES_H

#include "topo/Check/CapabilityCatalog.h"

#include <optional>
#include <string>

namespace topo::check {

struct HostImport {
    std::string normalizedPath;  // e.g. "sys/socket.h", "fstream"
    std::string file;            // source file containing the include
    int line = 0;
    UnsafeLevel unsafeLevel = UnsafeLevel::Safe;
};

struct DetectedCallSite {
    std::string callerQualifiedName;  // "ns::MyClass::render"
    std::string calleePattern;        // "fopen", "socket", etc.
    std::optional<CapabilityKind> capability;
    UnsafeLevel unsafeLevel = UnsafeLevel::Safe;
    std::string file;
    int line = 0;
};

} // namespace topo::check

#endif // TOPO_CHECK_CONTAINMENTTYPES_H
