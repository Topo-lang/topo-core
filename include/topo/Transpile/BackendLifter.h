#ifndef TOPO_TRANSPILE_BACKENDLIFTER_H
#define TOPO_TRANSPILE_BACKENDLIFTER_H

#include "topo/Transpile/TranspileModel.h"
#include "topo/Sema/SymbolTable.h"
#include <string>

namespace topo::transpile {

enum class DecompileLevel { Direct, Structured, Idiomatic };

class BackendLifter {
public:
    virtual ~BackendLifter() = default;
    virtual TranspileModule lift(const std::string& artifactPath,
                                 const SymbolTable& metadata,
                                 DecompileLevel level) = 0;
};

} // namespace topo::transpile

#endif // TOPO_TRANSPILE_BACKENDLIFTER_H
