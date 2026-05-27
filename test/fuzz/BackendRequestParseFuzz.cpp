// Fuzz target for the BackendRequest JSON deserializer.
// Feeds arbitrary byte sequences as a JSON document and exercises
// deserializeBackendRequest() — malformed input should fail gracefully
// (return false or throw) without crashes or UBSan triggers.

#include "topo/Build/BackendProtocol.h"

#include <cstdint>
#include <exception>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string input(reinterpret_cast<const char*>(data), size);

    try {
        topo::build::BackendRequest req;
        (void)topo::build::deserializeBackendRequest(input, req);
    } catch (const std::exception&) {
        // Parse/type errors from malformed JSON are expected.
        // The contract is "no crash / no sanitizer trip".
    }

    return 0;
}
