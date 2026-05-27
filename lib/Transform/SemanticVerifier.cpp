#include "topo/Transform/SemanticVerifier.h"
#include <algorithm>
#include <set>

namespace topo::transform {

VerificationResult SemanticVerifier::verify(const SymbolTable& original, const SymbolTable& transformed) {
    VerificationResult result;
    compareFunctions(original, transformed, result.differences);
    compareClasses(original, transformed, result.differences);
    compareLogicBlocks(original, transformed, result.differences);
    compareConstraints(original, transformed, result.differences);
    result.equivalent = result.differences.empty();
    return result;
}

void SemanticVerifier::compareFunctions(const SymbolTable& a, const SymbolTable& b, std::vector<std::string>& diffs) {
    const auto& aFuncs = a.functions();
    const auto& bFuncs = b.functions();

    if (aFuncs.size() != bFuncs.size()) {
        diffs.push_back("function count mismatch: original has " + std::to_string(aFuncs.size()) +
                        ", transformed has " + std::to_string(bFuncs.size()));
    }

    // Collect sorted name sets
    std::set<std::string> aNames, bNames;
    for (const auto& [name, _] : aFuncs)
        aNames.insert(name);
    for (const auto& [name, _] : bFuncs)
        bNames.insert(name);

    // Find names in original but missing from transformed
    for (const auto& name : aNames) {
        if (bNames.find(name) == bNames.end()) {
            diffs.push_back("function '" + name + "' missing in transformed output");
        }
    }
    // Find names in transformed but missing from original
    for (const auto& name : bNames) {
        if (aNames.find(name) == aNames.end()) {
            diffs.push_back("function '" + name + "' unexpected in transformed output");
        }
    }

    // For matched names, compare structural properties
    for (const auto& name : aNames) {
        auto itB = bFuncs.find(name);
        if (itB == bFuncs.end()) continue;

        const auto& fa = aFuncs.at(name);
        const auto& fb = itB->second;

        if (fa.params.size() != fb.params.size()) {
            diffs.push_back("function '" + name + "' parameter count mismatch: " + std::to_string(fa.params.size()) +
                            " vs " + std::to_string(fb.params.size()));
        }

        std::string aRetStr = fa.returnType.toString();
        std::string bRetStr = fb.returnType.toString();
        if (aRetStr != bRetStr) {
            diffs.push_back("function '" + name + "' return type mismatch: '" + aRetStr + "' vs '" + bRetStr + "'");
        }
    }
}

void SemanticVerifier::compareClasses(const SymbolTable& a, const SymbolTable& b, std::vector<std::string>& diffs) {
    const auto& aClasses = a.classSymbols();
    const auto& bClasses = b.classSymbols();

    if (aClasses.size() != bClasses.size()) {
        diffs.push_back("class count mismatch: original has " + std::to_string(aClasses.size()) + ", transformed has " +
                        std::to_string(bClasses.size()));
    }

    std::set<std::string> aNames, bNames;
    for (const auto& [name, _] : aClasses)
        aNames.insert(name);
    for (const auto& [name, _] : bClasses)
        bNames.insert(name);

    for (const auto& name : aNames) {
        if (bNames.find(name) == bNames.end()) {
            diffs.push_back("class '" + name + "' missing in transformed output");
        }
    }
    for (const auto& name : bNames) {
        if (aNames.find(name) == aNames.end()) {
            diffs.push_back("class '" + name + "' unexpected in transformed output");
        }
    }

    for (const auto& name : aNames) {
        auto itB = bClasses.find(name);
        if (itB == bClasses.end()) continue;

        const auto& ca = aClasses.at(name);
        const auto& cb = itB->second;

        if (ca.memberFunctions.size() != cb.memberFunctions.size()) {
            diffs.push_back("class '" + name +
                            "' member function count mismatch: " + std::to_string(ca.memberFunctions.size()) + " vs " +
                            std::to_string(cb.memberFunctions.size()));
        }
    }
}

void SemanticVerifier::compareLogicBlocks(const SymbolTable& a, const SymbolTable& b, std::vector<std::string>& diffs) {
    const auto& aBlocks = a.logicBlocks();
    const auto& bBlocks = b.logicBlocks();

    if (aBlocks.size() != bBlocks.size()) {
        diffs.push_back("logic block count mismatch: original has " + std::to_string(aBlocks.size()) +
                        ", transformed has " + std::to_string(bBlocks.size()));
    }

    std::set<std::string> aNames, bNames;
    for (const auto& [name, _] : aBlocks)
        aNames.insert(name);
    for (const auto& [name, _] : bBlocks)
        bNames.insert(name);

    for (const auto& name : aNames) {
        if (bNames.find(name) == bNames.end()) {
            diffs.push_back("logic block '" + name + "' missing in transformed output");
        }
    }
    for (const auto& name : bNames) {
        if (aNames.find(name) == aNames.end()) {
            diffs.push_back("logic block '" + name + "' unexpected in transformed output");
        }
    }

    for (const auto& name : aNames) {
        auto itB = bBlocks.find(name);
        if (itB == bBlocks.end()) continue;

        const auto& la = aBlocks.at(name);
        const auto& lb = itB->second;

        if (la.isPipeline != lb.isPipeline) {
            diffs.push_back("logic block '" + name +
                            "' pipeline flag mismatch: " + std::string(la.isPipeline ? "true" : "false") + " vs " +
                            std::string(lb.isPipeline ? "true" : "false"));
        }

        if (la.edges.size() != lb.edges.size()) {
            diffs.push_back("logic block '" + name + "' pipeline edge count mismatch: " +
                            std::to_string(la.edges.size()) + " vs " + std::to_string(lb.edges.size()));
        }
    }
}

void SemanticVerifier::compareConstraints(const SymbolTable& a, const SymbolTable& b, std::vector<std::string>& diffs) {
    const auto& aConstraints = a.constraintSymbols();
    const auto& bConstraints = b.constraintSymbols();

    if (aConstraints.size() != bConstraints.size()) {
        diffs.push_back("constraint count mismatch: original has " + std::to_string(aConstraints.size()) +
                        ", transformed has " + std::to_string(bConstraints.size()));
    }

    std::set<std::string> aNames, bNames;
    for (const auto& [name, _] : aConstraints)
        aNames.insert(name);
    for (const auto& [name, _] : bConstraints)
        bNames.insert(name);

    for (const auto& name : aNames) {
        if (bNames.find(name) == bNames.end()) {
            diffs.push_back("constraint '" + name + "' missing in transformed output");
        }
    }
    for (const auto& name : bNames) {
        if (aNames.find(name) == aNames.end()) {
            diffs.push_back("constraint '" + name + "' unexpected in transformed output");
        }
    }

    for (const auto& name : aNames) {
        auto itB = bConstraints.find(name);
        if (itB == bConstraints.end()) continue;

        const auto& ca = aConstraints.at(name);
        const auto& cb = itB->second;

        if (ca.members.size() != cb.members.size()) {
            diffs.push_back("constraint '" + name + "' member count mismatch: " + std::to_string(ca.members.size()) +
                            " vs " + std::to_string(cb.members.size()));
        }
    }
}

} // namespace topo::transform
