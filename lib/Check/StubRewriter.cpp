// StubRewriter — Batch function body replacement for pruning verification.
//
// Locates functions in source files and replaces their bodies with minimal
// stubs via the language-specific StubGenerator. Maintains backup state
// for atomic rollback on failure.

#include "topo/Check/StubRewriter.h"

#include <fstream>
#include <iostream>

namespace topo::check {

StubRewriter::StubRewriter(std::unique_ptr<StubGenerator> stubGen, std::vector<std::string> sourceFiles)
    : stubGen_(std::move(stubGen)), sourceFiles_(std::move(sourceFiles)) {}

std::string StubRewriter::findSourceFile(const std::string& funcName) const {
    for (const auto& filePath : sourceFiles_) {
        std::ifstream ifs(filePath);
        if (!ifs) continue;
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.find(funcName) != std::string::npos) {
                return filePath;
            }
        }
    }
    return {};
}

StubRewriteResult StubRewriter::stubFunctions(const std::unordered_set<std::string>& funcNames) {
    StubRewriteResult result;
    int skippedCount = 0;

    for (const auto& funcName : funcNames) {
        std::string srcFile = findSourceFile(funcName);
        if (srcFile.empty()) {
            // Function not found in any source file.
            // This can happen for functions declared in .topo but
            // implemented in external/generated code.
            ++skippedCount;
            continue;
        }

        auto stubRes = stubGen_->stubFunction(srcFile, funcName);
        if (!stubRes.success) {
            // Rollback all previously applied stubs
            for (const auto& [path, original] : result.originalContents) {
                std::ofstream ofs(path, std::ios::trunc);
                if (ofs) ofs << original;
            }
            result.success = false;
            result.error = "failed to stub " + funcName + ": " + stubRes.error;
            result.originalContents.clear();
            result.stubbedFunctions.clear();
            return result;
        }

        // Save original content (only first time we touch this file)
        if (result.originalContents.find(srcFile) == result.originalContents.end()) {
            result.originalContents[srcFile] = stubRes.originalContent;
        }
        result.stubbedFunctions.push_back(funcName);
    }

    result.skippedCount = skippedCount;
    result.success = true;
    return result;
}

bool StubRewriter::restore(const StubRewriteResult& result) {
    bool allOk = true;
    for (const auto& [path, original] : result.originalContents) {
        std::ofstream ofs(path, std::ios::trunc);
        if (!ofs) {
            std::cerr << "error: failed to restore " << path << "\n";
            allOk = false;
            continue;
        }
        ofs << original;
    }
    return allOk;
}

} // namespace topo::check
