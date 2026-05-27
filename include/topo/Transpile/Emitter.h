#ifndef TOPO_TRANSPILE_EMITTER_H
#define TOPO_TRANSPILE_EMITTER_H

#include "topo/Transpile/TranspileModel.h"
#include <string>
#include <vector>

namespace topo::transpile {

struct EmitError {
    std::string construct;
    std::string reason;
    std::string location;
};

struct EmitResult {
    std::string code;
    std::vector<EmitError> errors;
};

class Emitter {
public:
    virtual ~Emitter() = default;
    virtual EmitResult emit(const TranspileModule& module) = 0;
};

} // namespace topo::transpile

#endif // TOPO_TRANSPILE_EMITTER_H
