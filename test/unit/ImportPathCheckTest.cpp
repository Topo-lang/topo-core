#include "topo/Analysis/ImportPathCheck.h"
#include "topo/Basic/HostLanguage.h"
#include "topo/Check/CheckTypes.h"
#include "topo/Sema/SymbolTable.h"
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace topo;
using namespace topo::analysis;
namespace fs = std::filesystem;

// ImportPathCheck walks SymbolTable::imports() and verifies that each
// distinct path resolves under one of the search directories (or the
// project directory). Missing paths produce an error diagnostic with
// check == "import-path"; duplicates are deduplicated; when no search
// dirs are provided the severity is downgraded to warning.

namespace {

// Create a unique temporary directory for a single test, so tests do not
// interfere with each other's filesystem state. Portable across POSIX and
// Windows via std::filesystem and a per-instance counter.
class TempDir {
public:
    TempDir() {
        static std::atomic<uint64_t> counter{0};
        auto stamp = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        auto n = counter.fetch_add(1);
        base_ = fs::temp_directory_path() /
                ("topo-import-path-check-" + std::to_string(stamp) + "-" + std::to_string(n));
        fs::create_directories(base_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(base_, ec);
    }

    fs::path path() const { return base_; }

    fs::path touch(const std::string& relative) const {
        fs::path p = base_ / relative;
        fs::create_directories(p.parent_path());
        std::ofstream(p).close();
        return p;
    }

private:
    fs::path base_;
};

static void addImport(SymbolTable& symbols, const std::string& path) {
    ImportEntry entry;
    entry.path = path;
    entry.typeName = "";
    entry.location.file = "test.topo";
    entry.location.line = 1;
    entry.location.column = 1;
    symbols.addImport(entry);
}

} // namespace

TEST(ImportPathCheck, ExistingPathProducesNoDiagnostic) {
    TempDir temp;
    temp.touch("vendor/math.hpp");

    SymbolTable symbols;
    addImport(symbols, "vendor/math.hpp");

    ImportPathConfig config;
    config.projectDir = temp.path().string();
    config.searchDirs = {temp.path().string()};
    config.language = HostLanguage::Cpp;

    check::CheckResult result;
    checkImportPaths(symbols, config, result);

    EXPECT_TRUE(result.diagnostics.empty());
    EXPECT_EQ(result.errorCount, 0);
    EXPECT_EQ(result.warningCount, 0);
}

TEST(ImportPathCheck, MissingPathProducesErrorWithSearchDirs) {
    TempDir temp;

    SymbolTable symbols;
    addImport(symbols, "nope/does_not_exist.h");

    ImportPathConfig config;
    config.projectDir = temp.path().string();
    config.searchDirs = {temp.path().string()};
    config.language = HostLanguage::Cpp;

    check::CheckResult result;
    checkImportPaths(symbols, config, result);

    ASSERT_EQ(result.diagnostics.size(), 1u);
    const auto& diag = result.diagnostics[0];
    EXPECT_EQ(diag.severity, check::Severity::Error);
    EXPECT_EQ(diag.check, "import-path");
    EXPECT_NE(diag.message.find("nope/does_not_exist.h"), std::string::npos);
    EXPECT_EQ(result.errorCount, 1);
}

TEST(ImportPathCheck, WarnOnlyDowngradesToWarning) {
    TempDir temp;

    SymbolTable symbols;
    addImport(symbols, "missing.h");

    ImportPathConfig config;
    config.projectDir = temp.path().string();
    config.searchDirs = {temp.path().string()};
    config.warnOnly = true;

    check::CheckResult result;
    checkImportPaths(symbols, config, result);

    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics[0].severity, check::Severity::Warning);
    EXPECT_EQ(result.warningCount, 1);
    EXPECT_EQ(result.errorCount, 0);
}

TEST(ImportPathCheck, NoSearchDirsDowngradesToWarning) {
    SymbolTable symbols;
    addImport(symbols, "anything.h");

    ImportPathConfig config;
    // no projectDir, no searchDirs -> no usable dirs -> warning severity.

    check::CheckResult result;
    checkImportPaths(symbols, config, result);

    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics[0].severity, check::Severity::Warning);
    EXPECT_EQ(result.warningCount, 1);
    EXPECT_EQ(result.errorCount, 0);
}

TEST(ImportPathCheck, EmptyImportsProducesNoDiagnostic) {
    SymbolTable symbols;
    ImportPathConfig config;
    config.projectDir = "/tmp";

    check::CheckResult result;
    checkImportPaths(symbols, config, result);

    EXPECT_TRUE(result.diagnostics.empty());
}

TEST(ImportPathCheck, DuplicatePathsDeduplicated) {
    TempDir temp;

    SymbolTable symbols;
    addImport(symbols, "missing.h");
    addImport(symbols, "missing.h"); // duplicate — must be deduplicated
    addImport(symbols, "missing.h");

    ImportPathConfig config;
    config.projectDir = temp.path().string();
    config.searchDirs = {temp.path().string()};

    check::CheckResult result;
    checkImportPaths(symbols, config, result);

    // Only one diagnostic despite three duplicate import entries.
    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.errorCount, 1);
}

TEST(ImportPathCheck, ProjectDirFallbackResolvesPath) {
    TempDir temp;
    temp.touch("header.h");

    SymbolTable symbols;
    addImport(symbols, "header.h");

    // No explicit searchDirs — resolver falls back to projectDir.
    ImportPathConfig config;
    config.projectDir = temp.path().string();

    check::CheckResult result;
    checkImportPaths(symbols, config, result);

    EXPECT_TRUE(result.diagnostics.empty());
    EXPECT_EQ(result.errorCount, 0);
}

TEST(ImportPathCheck, InvalidSearchDirsAreIgnoredButProjectDirStillWorks) {
    TempDir temp;
    temp.touch("header.h");

    SymbolTable symbols;
    addImport(symbols, "header.h");

    ImportPathConfig config;
    config.projectDir = temp.path().string();
    // Include a non-existent dir alongside the real project dir — the
    // bogus dir is dropped and lookup still succeeds via projectDir.
    config.searchDirs = {"/this/path/does/not/exist/topo-test-xyz"};

    check::CheckResult result;
    checkImportPaths(symbols, config, result);

    EXPECT_TRUE(result.diagnostics.empty());
}
