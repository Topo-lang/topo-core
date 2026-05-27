#ifndef TOPO_BASIC_BUILDTYPES_H
#define TOPO_BASIC_BUILDTYPES_H

#include "topo/Build/PassConfig.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace topo {

// --- Optimization and build mode enums (from PassPipeline.h) ---

enum class OptLevel { O0 = 0, O1 = 1, O2 = 2, O3 = 3 };
enum class BuildMode { Dev, Aggressive };
enum class OutputType { Exe, Shared, Static };

// --- Symbol obfuscation types (from SymbolObfuscator.h) ---

enum class ObfuscationMode { Normal, Salted };

struct ObfuscationResult {
    int renamedCount = 0;
    std::unordered_map<std::string, std::string> protectedMapping;
};

// --- Verification result (from Verifier.h) ---

struct VerifyResult {
    int publicMissing = 0;
    int blockMismatches = 0;
    int signatureMismatches = 0;
    int constMismatches = 0;
    int classMemberMissing = 0;
    int stageOrderViolations = 0;
    int pipelineEdgeMismatches = 0;
    int templateInstantiationMissing = 0;
    int constraintViolations = 0;
    int stageParallelViolations = 0;

    bool passed() const {
        return publicMissing == 0 && blockMismatches == 0 && signatureMismatches == 0 && classMemberMissing == 0 &&
               stageOrderViolations == 0 && pipelineEdgeMismatches == 0 && templateInstantiationMissing == 0 &&
               constraintViolations == 0 && stageParallelViolations == 0;
    }
};

// --- Visibility apply stats (from VisibilityApplier.h) ---

struct ApplyStats {
    int publicCount = 0;
    int protectedCount = 0;
    int privateCount = 0;
    int internalCount = 0;
};

} // namespace topo

#endif // TOPO_BASIC_BUILDTYPES_H
