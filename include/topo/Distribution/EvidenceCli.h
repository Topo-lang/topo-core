#ifndef TOPO_DISTRIBUTION_EVIDENCECLI_H
#define TOPO_DISTRIBUTION_EVIDENCECLI_H
#include <string>
#include <vector>
namespace topo::dist {
// Content inventory and pinned offline signature verification. Neither
// operation runs or changes the artifact being inspected.
int runEvidenceCli(const std::vector<std::string>& args);
}
#endif
