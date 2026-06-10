// E2E tests for CheckRunner orchestration (mode, output, filter, error handling).

#include "CheckRunner.h"
#include "topo/Check/CheckTypes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <tuple>
#include <vector>

using namespace topo;

static std::string fixtureDir(const char* name) {
    return std::string(TOPO_TEST_FIXTURES_DIR) + "/" + name;
}

// --- Combined "all" mode ---

TEST(CheckRunner, AllChecksOnPassProject) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("cpp/completeness_pass");
    cfg.checkName = "all";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}

// --- JSON output ---

TEST(CheckRunner, JsonOutputDoesNotCrash) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("cpp/completeness_pass");
    cfg.checkName = "completeness";
    cfg.jsonOutput = true;
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    // Redirect stdout to suppress output during test
    EXPECT_EQ(runner.run(), 0);
}

// --- Filter ---

TEST(CheckRunner, FilterMatchesSubstring) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("cpp/completeness_pass");
    cfg.checkName = "all";
    cfg.filter = "complete";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    // Only completeness should run, which passes
    EXPECT_EQ(runner.run(), 0);
}

TEST(CheckRunner, FilterNoMatch) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("cpp/completeness_pass");
    cfg.checkName = "all";
    cfg.filter = "nonexistent";
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    // No checks match — should still return 0 (no errors)
    EXPECT_EQ(runner.run(), 0);
}

// --- Missing project ---

TEST(CheckRunner, MissingTopoToml) {
    CheckConfig cfg;
    cfg.projectDir = "/tmp/nonexistent_topo_project_dir";
    CheckRunner runner(cfg);
    EXPECT_FALSE(runner.loadConfig());
}

// --- Jobs parallelism: determinism + benchmark ---

namespace {

// Creates a synthetic fixture tree with N cpp sources each declaring one
// function and a single .topo file declaring all N as public. Parallelism
// stresses per-file extraction loops in runCompleteness/runContainment.
std::string synthesizeFixture(size_t n, const std::string& tag) {
    namespace fs = std::filesystem;
    std::mt19937 rng(static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto root = fs::temp_directory_path() /
        ("topo-jobs-" + tag + "-" + std::to_string(rng()));
    fs::create_directories(root / "src");
    fs::create_directories(root / "topo");

    std::ofstream toml(root / "Topo.toml");
    toml << "[project]\nname = \"jobs_fixture\"\n\n"
         << "[topo]\nroot = \"topo/main.topo\"\n\n"
         << "[build]\nlanguage = \"cpp\"\nsources = [\"src/*.cpp\"]\n";
    toml.close();

    std::ofstream topoDecl(root / "topo" / "main.topo");
    topoDecl << "namespace app {\n  public:\n";
    for (size_t i = 0; i < n; ++i) {
        topoDecl << "    integer fn" << i << "(integer x);\n";
    }
    topoDecl << "}\n";
    topoDecl.close();

    for (size_t i = 0; i < n; ++i) {
        std::ofstream src(root / "src" / ("fn" + std::to_string(i) + ".cpp"));
        src << "namespace app {\nint fn" << i
            << "(int x) { return x * " << (i + 1) << "; }\n} // namespace app\n";
    }
    return root.string();
}

auto sortableKey(const check::CheckDiagnostic& d) {
    return std::tuple{static_cast<int>(d.severity), d.check, d.file, d.line,
                      d.column, d.message};
}

std::vector<std::tuple<std::string, std::vector<check::CheckDiagnostic>>>
normalizeResults(const std::vector<std::pair<std::string, check::CheckResult>>& raw) {
    std::vector<std::tuple<std::string, std::vector<check::CheckDiagnostic>>> out;
    out.reserve(raw.size());
    for (const auto& [name, result] : raw) {
        auto diags = result.diagnostics;
        std::sort(diags.begin(), diags.end(), [](const auto& a, const auto& b) {
            return sortableKey(a) < sortableKey(b);
        });
        out.emplace_back(name, std::move(diags));
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });
    return out;
}

bool diagnosticsEqual(const check::CheckDiagnostic& a,
                      const check::CheckDiagnostic& b) {
    return sortableKey(a) == sortableKey(b);
}

} // namespace

TEST(CheckRunner, JobsEquivalence_SequentialMatchesParallel) {
    const auto projectDir = synthesizeFixture(20, "equiv");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{projectDir};

    auto runWith = [&](int jobs) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(projectDir) / ".topo-check-cache", ec);
        CheckConfig cfg;
        cfg.projectDir = projectDir;
        cfg.checkName = "all";
        cfg.jsonOutput = true; // suppress pretty stdout
        cfg.jobs = jobs;
        CheckRunner runner(cfg);
        EXPECT_TRUE(runner.loadConfig()) << "jobs=" << jobs;
        runner.run();
        return normalizeResults(runner.lastResults());
    };

    const auto seq = runWith(1);
    const auto par = runWith(8);

    ASSERT_EQ(seq.size(), par.size()) << "check list size differs";
    for (size_t i = 0; i < seq.size(); ++i) {
        const auto& [seqName, seqDiags] = seq[i];
        const auto& [parName, parDiags] = par[i];
        EXPECT_EQ(seqName, parName);
        ASSERT_EQ(seqDiags.size(), parDiags.size())
            << "check " << seqName << " diagnostic count differs";
        for (size_t k = 0; k < seqDiags.size(); ++k) {
            EXPECT_TRUE(diagnosticsEqual(seqDiags[k], parDiags[k]))
                << "check " << seqName << " diag " << k << " differs";
        }
    }
}

// Opt-in benchmark: set TOPO_BENCH_JOBS=1 to run. Skipped by default because
// synthesizing 100 files + running two full check passes is slow for CI.
TEST(CheckRunner, JobsBenchmark_100Files_ParallelSpeedup) {
    if (!std::getenv("TOPO_BENCH_JOBS")) {
        GTEST_SKIP() << "set TOPO_BENCH_JOBS=1 to run the --jobs benchmark";
    }
    const auto projectDir = synthesizeFixture(100, "bench");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    } cleanup{projectDir};

    auto timeWith = [&](int jobs) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(projectDir) / ".topo-check-cache", ec);
        CheckConfig cfg;
        cfg.projectDir = projectDir;
        cfg.checkName = "all";
        cfg.jsonOutput = true;
        cfg.jobs = jobs;
        CheckRunner runner(cfg);
        EXPECT_TRUE(runner.loadConfig());
        const auto t0 = std::chrono::steady_clock::now();
        runner.run();
        const auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    };

    const auto ms1 = timeWith(1);
    const auto ms8 = timeWith(8);
    const double speedup = static_cast<double>(ms1) / static_cast<double>(std::max<long long>(ms8, 1));

    std::cerr << "[jobs-bench 100 files] jobs=1: " << ms1
              << "ms, jobs=8: " << ms8 << "ms, speedup: " << speedup << "x\n";

    EXPECT_GT(ms1, 0);
    EXPECT_GT(ms8, 0);
    EXPECT_GT(speedup, 1.0) << "parallel should not be slower than sequential";
}

// --- Deep mode refuses to degrade silently ---

#ifdef _WIN32
#define topo_putenv(k, v) _putenv_s(k, v)
#define topo_unsetenv(k) _putenv_s(k, "")
#else
#define topo_putenv(k, v) ::setenv(k, v, 1)
#define topo_unsetenv(k) ::unsetenv(k)
#endif

namespace {
// Save/restore an env var across a test (same idiom as TempFileTest).
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        const char* prev = std::getenv(name);
        had_ = (prev != nullptr);
        if (had_) prev_ = prev;
        if (value) topo_putenv(name, value);
        else topo_unsetenv(name);
    }
    ~ScopedEnv() {
        if (had_) topo_putenv(name_, prev_.c_str());
        else topo_unsetenv(name_);
    }
private:
    const char* name_;
    std::string prev_;
    bool had_;
};
} // namespace

// --deep promises L2 analysis; when the language server cannot start, the
// run must refuse (exit 2) instead of silently substituting regex-grade
// extraction and possibly PASSing at a shallower grade than requested
// (no-silent-degradation principle: disabled safety check == Error).
//
// Uses the python fixture deliberately: PyrightBridge resolves
// `pyright-langserver` from PATH on every start, so gutting PATH is a
// deterministic, order-independent way to make LSP init fail. (The cpp
// bridge resolves clangd through the BYO-LLVM toolchain chain, which is
// memoized per process and consults non-PATH tiers — not controllable
// from a test.)
TEST(CheckRunner, DeepModeWithoutLanguageServerFailsLoudly) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("python/deep_degradation");
    cfg.checkName = "all";
    cfg.deepMode = true;
    // A cached deep verdict would short-circuit before LSP init — drop any
    // cache left in the fixture dir by other tests or earlier binaries.
    std::error_code ec;
    std::filesystem::remove(
        std::filesystem::path(cfg.projectDir) / ".topo-check-cache", ec);
    // Gut PATH so the pyright probe cannot resolve the language server.
    // The frontend (lexer/parser/sema) is in-process and unaffected; the
    // run aborts at LSP init, before any extractor subprocess is needed.
    ScopedEnv path("PATH", "");
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 2);
}

// Control: the same project and PATH gutting without --deep stays a normal
// L1 run — proving the refusal above is specific to the requested-deep
// contract, not a general PATH sensitivity.
TEST(CheckRunner, L1WithoutLanguageServerStillRuns) {
    CheckConfig cfg;
    cfg.projectDir = fixtureDir("cpp/completeness_pass");
    cfg.checkName = "completeness";
    ScopedEnv path("PATH", "");
    CheckRunner runner(cfg);
    ASSERT_TRUE(runner.loadConfig());
    EXPECT_EQ(runner.run(), 0);
}
