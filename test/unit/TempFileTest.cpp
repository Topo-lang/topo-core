#include "topo/Platform/TempFile.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define topo_putenv(k, v) _putenv_s(k, v)
#define topo_unsetenv(k) _putenv_s(k, "")
#else
#include <unistd.h>
#define topo_putenv(k, v) ::setenv(k, v, 1)
#define topo_unsetenv(k) ::unsetenv(k)
#endif

namespace fs = std::filesystem;
using topo::platform::TempFile;
using topo::platform::tempDirectory;

namespace {

// Save/restore an env var across a test to keep environment changes
// local. RAII so a thrown assertion does not leak into sibling tests.
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

// tempDirectory() must honour TMPDIR before falling back to the system
// default. This is the contract the audit fix depends on: callers that
// previously wrote `/tmp/...` literals now get a path that respects the
// user's TMPDIR override.
TEST(TempFile, TempDirectoryHonoursTmpdirEnv) {
    fs::path scratch = fs::temp_directory_path() / "topo-temp-dir-test";
    fs::create_directories(scratch);

    ScopedEnv tmp("TMPDIR", scratch.string().c_str());
    ScopedEnv xdg("XDG_RUNTIME_DIR", nullptr);

    fs::path resolved = tempDirectory();
    EXPECT_EQ(fs::weakly_canonical(resolved), fs::weakly_canonical(scratch));

    fs::remove_all(scratch);
}

// TempFile must produce a real on-disk file that exists for the
// lifetime of the handle and is removed on destruction. This is what
// makes RAII the right abstraction over hand-rolled /tmp/foo.txt code.
TEST(TempFile, CreatesAndRemovesFile) {
    fs::path captured;
    {
        TempFile f("topotest", ".tmp");
        captured = f.path();
        EXPECT_TRUE(fs::exists(captured));
        EXPECT_EQ(captured.extension(), ".tmp");
    }
    EXPECT_FALSE(fs::exists(captured));
}

// Uniqueness across rapidly-created TempFiles. The pid+counter scheme
// must not produce duplicates within a process; absence of duplicates
// is what makes the API race-free for parallel test runners.
TEST(TempFile, GeneratesUniqueNamesUnderRapidFire) {
    std::set<fs::path> seen;
    std::vector<TempFile> files;
    files.reserve(32);
    for (int i = 0; i < 32; ++i) {
        files.emplace_back("uniq", ".bin");
        auto [it, inserted] = seen.insert(files.back().path());
        EXPECT_TRUE(inserted) << "duplicate path: " << it->string();
    }
}

// release() must transfer ownership: the file survives the destructor.
// This supports the "atomic install" pattern (write temp, rename into
// place, release) without making the caller pay for a duplicate stat
// to suppress the cleanup.
TEST(TempFile, ReleaseSuppressesCleanup) {
    fs::path kept;
    {
        TempFile f("keep", ".txt");
        std::ofstream(f.path()) << "payload";
        kept = f.release();
    }
    EXPECT_TRUE(fs::exists(kept));
    fs::remove(kept);
}

// Uniqueness across multiple producer threads sharing the same stem +
// extension. This is the regression that backs the AppStaticAnalyze
// fix (audit issue topo-lang-cpp-app-static-wrapped-temp-collision):
// concurrent CppDriver compile jobs both produce a wrapped temp
// source from the same input basename, and the atomic counter inside
// TempFile must keep their paths distinct so neither subprocess
// overwrites the other's input.
TEST(TempFile, GeneratesUniqueNamesUnderConcurrentProducers) {
    constexpr int kThreads = 8;
    constexpr int kPerThread = 32;

    std::mutex m;
    std::set<fs::path> seen;
    std::vector<std::thread> ws;
    std::vector<TempFile> keep;  // hold files alive until end of test
    keep.reserve(kThreads * kPerThread);

    auto worker = [&] {
        std::vector<TempFile> local;
        local.reserve(kPerThread);
        for (int i = 0; i < kPerThread; ++i) {
            local.emplace_back("topo-app-static-wrapped", ".cpp");
        }
        std::lock_guard<std::mutex> g(m);
        for (auto& f : local) {
            auto [it, inserted] = seen.insert(f.path());
            EXPECT_TRUE(inserted) << "duplicate path under concurrent "
                                     "producers: " << it->string();
            keep.emplace_back(std::move(f));
        }
    };

    ws.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) ws.emplace_back(worker);
    for (auto& th : ws) th.join();

    EXPECT_EQ(static_cast<int>(seen.size()), kThreads * kPerThread);
}

// Move construction transfers ownership; the source no longer cleans up.
TEST(TempFile, MoveTransfersOwnership) {
    fs::path captured;
    {
        TempFile src("mv", ".dat");
        captured = src.path();
        TempFile dst(std::move(src));
        EXPECT_EQ(dst.path(), captured);
        EXPECT_TRUE(fs::exists(captured));
    }
    EXPECT_FALSE(fs::exists(captured));
}
