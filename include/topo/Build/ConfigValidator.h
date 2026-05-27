#ifndef TOPO_BUILD_CONFIGVALIDATOR_H
#define TOPO_BUILD_CONFIGVALIDATOR_H

#include "topo/Build/BuildConfig.h"

#include <string>
#include <vector>

namespace topo {

enum class ConfigErrorLevel { Error, Warning };

struct ConfigError {
    ConfigErrorLevel level;
    std::string section; // e.g. "[adaptive]"
    std::string key;     // e.g. "enabled"
    std::string message; // human-readable description
};

struct ValidationResult {
    std::vector<ConfigError> errors;

    bool hasErrors() const {
        for (const auto& e : errors) {
            if (e.level == ConfigErrorLevel::Error) return true;
        }
        return false;
    }

    bool hasWarnings() const {
        for (const auto& e : errors) {
            if (e.level == ConfigErrorLevel::Warning) return true;
        }
        return false;
    }
};

/// Validate a parsed Topo.toml configuration.
/// Collects all errors and warnings rather than stopping at the first.
///
/// Full validation against BuildConfig — checks all sections.
ValidationResult validateConfig(const build::BuildConfig& cfg);

/// \deprecated Use the BuildConfig overload instead.
/// Legacy wrapper kept for backward compatibility.
ValidationResult validateConfig(bool adaptiveEnabled,
                                bool embedIR,
                                bool parallelInstrument,
                                const std::string& language = "",
                                const std::string& outputType = "",
                                const std::string& builderMode = "");

} // namespace topo

#endif // TOPO_BUILD_CONFIGVALIDATOR_H
