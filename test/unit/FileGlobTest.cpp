#include "topo/Platform/FileGlob.h"
#include "topo/Platform/TempFile.h"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using topo::platform::globExpand;

namespace {

// Create a unique scratch dir under the platform temp dir (honours TMPDIR),
// tagged with a monotonic counter so tests running in the same process
// never collide.
fs::path makeScratchRoot(const std::string& tag) {
    static std::atomic<unsigned long> g_counter{0};
    fs::path base = topo::platform::tempDirectory();
    unsigned long n = g_counter.fetch_add(1, std::memory_order_relaxed);
    fs::path dir = base / ("topo-glob-" + tag + "-" + std::to_string(n));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "";
}

// Fixture tree:
//   <root>/a.cpp b.cpp c.hpp main.py
//   <root>/src/a.cpp b.cpp main.cpp notes.txt util.h
//   <root>/src/sub/deep.cpp data.txt
fs::path createFixtureTree() {
    fs::path root = makeScratchRoot("tree");
    touch(root / "a.cpp");
    touch(root / "b.cpp");
    touch(root / "c.hpp");
    touch(root / "main.py");
    touch(root / "src" / "a.cpp");
    touch(root / "src" / "b.cpp");
    touch(root / "src" / "main.cpp");
    touch(root / "src" / "notes.txt");
    touch(root / "src" / "util.h");
    touch(root / "src" / "sub" / "deep.cpp");
    touch(root / "src" / "sub" / "data.txt");
    return root;
}

// Re-express expansion results relative to root (with forward slashes) so
// expectations are readable and platform-independent.
std::vector<std::string> relative(const fs::path& root, const std::vector<std::string>& paths) {
    std::vector<std::string> rel;
    rel.reserve(paths.size());
    for (const auto& p : paths) {
        rel.push_back(fs::path(p).lexically_relative(root).generic_string());
    }
    return rel;
}

} // namespace

// ── single '*' (pre-existing behaviour, pinned) ──────────────────────

TEST(FileGlob, SingleStarFilenameMatchesDirectChildren) {
    fs::path root = createFixtureTree();
    auto got = relative(root, globExpand(root, "src/*.cpp"));
    EXPECT_EQ(got, (std::vector<std::string>{"src/a.cpp", "src/b.cpp", "src/main.cpp"}));
    fs::remove_all(root);
}

TEST(FileGlob, StarPrefixAndSuffixAnchoring) {
    fs::path root = createFixtureTree();
    EXPECT_EQ(relative(root, globExpand(root, "src/m*.cpp")),
              (std::vector<std::string>{"src/main.cpp"}));
    EXPECT_EQ(relative(root, globExpand(root, "src/*.h")),
              (std::vector<std::string>{"src/util.h"}));
    EXPECT_EQ(relative(root, globExpand(root, "src/*")),
              (std::vector<std::string>{"src/a.cpp", "src/b.cpp", "src/main.cpp",
                                        "src/notes.txt", "src/util.h"}));
    fs::remove_all(root);
}

// ── '**' recursive matching ───────────────────────────────────────────

TEST(FileGlob, DoublestarMatchesZeroOrMoreDirectories) {
    // '**/' also matches ZERO segments: src/main.cpp sits directly under
    // src/, and src/sub/deep.cpp is one level deeper.
    fs::path root = createFixtureTree();
    auto got = relative(root, globExpand(root, "src/**/*.cpp"));
    EXPECT_EQ(got, (std::vector<std::string>{"src/a.cpp", "src/b.cpp", "src/main.cpp",
                                             "src/sub/deep.cpp"}));
    fs::remove_all(root);
}

TEST(FileGlob, DoublestarMidPatternFindsDeepFile) {
    fs::path root = createFixtureTree();
    EXPECT_EQ(relative(root, globExpand(root, "src/**/deep.cpp")),
              (std::vector<std::string>{"src/sub/deep.cpp"}));
    fs::remove_all(root);
}

TEST(FileGlob, LeadingDoublestarMatchesFromBaseDir) {
    fs::path root = createFixtureTree();
    EXPECT_EQ(relative(root, globExpand(root, "**/deep.cpp")),
              (std::vector<std::string>{"src/sub/deep.cpp"}));
    EXPECT_EQ(relative(root, globExpand(root, "**/*.py")),
              (std::vector<std::string>{"main.py"}));
    fs::remove_all(root);
}

TEST(FileGlob, TrailingDoublestarCollectsEveryFileBelow) {
    fs::path root = createFixtureTree();
    auto expected = (std::vector<std::string>{"src/a.cpp", "src/b.cpp", "src/main.cpp",
                                              "src/notes.txt", "src/sub/data.txt",
                                              "src/sub/deep.cpp", "src/util.h"});
    EXPECT_EQ(relative(root, globExpand(root, "src/**")), expected);
    // Trailing-separator form "src/**/" is the same pattern.
    EXPECT_EQ(relative(root, globExpand(root, "src/**/")), expected);
    fs::remove_all(root);
}

TEST(FileGlob, DoublestarThenLiteralDirectory) {
    fs::path root = createFixtureTree();
    EXPECT_EQ(relative(root, globExpand(root, "src/**/sub/*.cpp")),
              (std::vector<std::string>{"src/sub/deep.cpp"}));
    fs::remove_all(root);
}

TEST(FileGlob, DoublestarResultsAreSortedAndDuplicateFree) {
    fs::path root = createFixtureTree();
    auto got = relative(root, globExpand(root, "src/**/**/*.cpp"));
    EXPECT_EQ(got, (std::vector<std::string>{"src/a.cpp", "src/b.cpp", "src/main.cpp",
                                             "src/sub/deep.cpp"}));
    fs::remove_all(root);
}

// ── literal paths (pre-existing behaviour, pinned) ────────────────────

TEST(FileGlob, LiteralFileExpandsToItself) {
    fs::path root = createFixtureTree();
    EXPECT_EQ(globExpand(root, "src/main.cpp"),
              (std::vector<std::string>{(root / "src" / "main.cpp").string()}));
    fs::remove_all(root);
}

TEST(FileGlob, LiteralDirectoryExpandsToItself) {
    // topo-build consumes sources entries that name a directory whole
    // (java/python/typescript drivers recurse internally) — a bare
    // directory pattern must come back as that directory.
    fs::path root = createFixtureTree();
    EXPECT_EQ(globExpand(root, "src"),
              (std::vector<std::string>{(root / "src").string()}));
    fs::remove_all(root);
}

TEST(FileGlob, MissingLiteralExpandsEmpty) {
    fs::path root = createFixtureTree();
    EXPECT_TRUE(globExpand(root, "src/nope.cpp").empty());
    fs::remove_all(root);
}

// ── non-matching / documented-subset limits ───────────────────────────

TEST(FileGlob, MissingDirectoryExpandsEmptyWithoutThrowing) {
    fs::path root = createFixtureTree();
    EXPECT_TRUE(globExpand(root, "nope/*.cpp").empty());
    EXPECT_TRUE(globExpand(root, "src/nope/**/*.cpp").empty());
    fs::remove_all(root);
}

TEST(FileGlob, WildcardInDirectoryComponentStaysLiteral) {
    // 'src*' is not a complete '**' — directory components other than a
    // complete '**' are literal, so this matches nothing (documented
    // subset, unchanged from the pre-'**' implementation).
    fs::path root = createFixtureTree();
    EXPECT_TRUE(globExpand(root, "src*/main.cpp").empty());
    fs::remove_all(root);
}

#ifndef _WIN32
TEST(FileGlob, DoublestarDoesNotFollowSymlinkedDirectories) {
    fs::path root = createFixtureTree();
    // Cycle target: descending into root/loop would never terminate if
    // symlinked directories were followed.
    fs::create_directory_symlink(root, root / "loop");
    fs::create_directory_symlink(root / "src", root / "link-sub");
    auto got = relative(root, globExpand(root, "**/*.cpp"));
    EXPECT_EQ(got, (std::vector<std::string>{"a.cpp", "b.cpp", "src/a.cpp", "src/b.cpp",
                                             "src/main.cpp", "src/sub/deep.cpp"}));
    fs::remove_all(root);
}
#endif
