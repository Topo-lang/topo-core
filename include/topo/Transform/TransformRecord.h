#pragma once

#include "topo/Basic/SourceLocation.h"
#include <string>
#include <vector>

namespace topo::transform {

struct TransformRecord {
    SourceLocation location;
    std::string originalText;
    std::string canonicalText;
    enum Kind { KeywordAlias, OperatorAlias, StructuralRewrite } kind;
};

} // namespace topo::transform
