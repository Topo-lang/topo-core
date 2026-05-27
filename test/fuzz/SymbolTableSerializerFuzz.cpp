// Fuzz target for the SymbolTable JSON deserializer.
// Feeds arbitrary byte sequences as a JSON document and exercises
// deserializeSymbolTable() — malformed JSON should fail gracefully
// (return false or throw) without crashes or UBSan triggers.

#include "topo/Build/SymbolTableJson.h"
#include "topo/Sema/SymbolTable.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string input(reinterpret_cast<const char*>(data), size);

    // nlohmann::json::parse throws on malformed input; wrap so the fuzzer
    // only catches real crashes, not expected parse failures.
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(input);
    } catch (const nlohmann::json::exception&) {
        return 0;
    }

    try {
        topo::SymbolTable symbols;
        (void)topo::deserializeSymbolTable(j, symbols);
    } catch (const nlohmann::json::exception&) {
        // Type/key errors from ill-formed (but parseable) JSON are expected.
    } catch (const std::exception&) {
        // Any other std::exception is also tolerated — the contract is
        // "no crash / no sanitizer trip".
    }

    return 0;
}
