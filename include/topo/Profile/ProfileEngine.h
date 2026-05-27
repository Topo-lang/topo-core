#ifndef TOPO_PROFILE_PROFILEENGINE_H
#define TOPO_PROFILE_PROFILEENGINE_H

// ProfileEngine — zero-LLVM core of the `topo-prof` performance CLI.
//
// The subcommand orchestration (analyze / profile / hints),
// samples.json parsing, runtime_avg_ns aggregation, hints comparison logic
// and pipeline stage→node listing live here in topo-core with NO LLVM
// dependency. The single LLVM-bound data source — static TTI cost
// estimation from `build/*.ll` IR — is abstracted behind the
// `TTIProvider` seam. The LLVM backend (`topo-llvm/lib/Profile/Llvm`)
// supplies a concrete provider; the core remains fully functional
// (analyze / hints / non-TTI profile) when no provider is wired.

#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>

namespace topo {
namespace profile {

// Abstract seam for static TTI cost estimation. The core declares it;
// the LLVM backend implements it. `projectDir` is the directory holding
// `Topo.toml` (the IR is read from `<projectDir>/build/*.ll`).
//
// Contract (byte-identical to the legacy inline `buildTTIMap`):
//   - returns a qualified/demangled-name → TTI-cost map
//   - on any failure (missing Topo.toml, parse error, no `.ll` files,
//     no target) it writes the SAME warning text to `err` as before and
//     returns an empty map; an empty map disables the TTI comparison.
class TTIProvider {
public:
    virtual ~TTIProvider() = default;
    virtual std::map<std::string, uint64_t> buildTTIMap(const std::string& projectDir,
                                                        std::ostream& err) = 0;
};

// Subcommand entry points. Signatures and behaviour (stdout/stderr text,
// JSON shape, exit codes) are identical to the pre-refactor topo-prof.
// `tti` may be null — when null, the profile TTI-comparison path is
// skipped exactly as if `--project` were not supplied.

int runAnalyze(const std::string& projectDir, const std::string& focus,
                const std::string& format, std::ostream& out, std::ostream& err);

int runProfile(const std::string& samplesPath, const std::string& projectDir,
                const std::string& binaryPath, const std::string& format,
                const std::string& outputPath, TTIProvider* tti,
                std::ostream& out, std::ostream& err);

int runHints(const std::string& samplesPath, const std::string& projectDir,
              const std::string& format, const std::string& outputPath,
              std::ostream& out, std::ostream& err);

} // namespace profile
} // namespace topo

#endif // TOPO_PROFILE_PROFILEENGINE_H
