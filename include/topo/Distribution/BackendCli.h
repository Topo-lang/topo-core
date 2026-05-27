#ifndef TOPO_DISTRIBUTION_BACKENDCLI_H
#define TOPO_DISTRIBUTION_BACKENDCLI_H

/// `topo backend` CLI — the command surface that lists, installs, removes,
/// updates, and selects backends, plus the offline `pack` path.
///
/// This is a topo-core capability and is invoked as a subcommand of the
/// `topo` tool (`topo backend <subcommand> ...`). It manages the local
/// `~/.topo/backends/` cache and never builds anything itself.

#include <string>
#include <vector>

namespace topo::dist {

/// The topo-core release this build identifies as, for `core_compat`
/// validation. SemVer; sourced from the project version (CLAUDE.md "v4.0").
inline constexpr const char* kTopoCoreVersion = "4.0.0";

/// Run `topo backend <args...>`. `args` excludes the leading `topo` and
/// `backend` tokens — i.e. args[0] is the subcommand. Returns the process
/// exit code per the spec §2 exit-code table.
int runBackendCli(const std::vector<std::string>& args);

/// Print the `topo backend` usage block to stderr.
void printBackendUsage();

} // namespace topo::dist

#endif // TOPO_DISTRIBUTION_BACKENDCLI_H
