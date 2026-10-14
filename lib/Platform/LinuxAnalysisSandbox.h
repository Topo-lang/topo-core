#ifndef TOPO_PLATFORM_LINUXANALYSISSANDBOX_H
#define TOPO_PLATFORM_LINUXANALYSISSANDBOX_H

#include <string>

namespace topo::platform::detail {

// A preflight only: the launcher must still install both policies before it
// emits the startup marker. No target runs after an installation failure.
int linuxAnalysisLandlockAbi(std::string& diagnostic);
bool establishLinuxAnalysisSandbox(int argc, char** argv, int& targetIndex, std::string& diagnostic);

} // namespace topo::platform::detail
#endif
