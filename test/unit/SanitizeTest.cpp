#include "topo/Platform/Sanitize.h"
#include "topo/Platform/TempFile.h"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using topo::platform::is_safe_basename;
using topo::platform::is_subpath;
using topo::platform::sanitizePath;

namespace {

// Create a unique scratch dir under the platform temp dir. Returned path
// is canonicalised so subsequent containment comparisons are stable on
// systems where the temp root itself is a symlink (e.g. macOS
// /tmp -> /private/tmp). Tag + monotonic counter keeps the directory
// unique across tests running in the same process.
fs::path makeScratchRoot(const std::string& tag) {
    static std::atomic<unsigned long> g_counter{0};
    fs::path base = topo::platform::tempDirectory();
    unsigned long n = g_counter.fetch_add(1, std::memory_order_relaxed);
    fs::path dir = base / ("topo-sanitize-" + tag + "-" + std::to_string(n));
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::error_code ec;
    return fs::weakly_canonical(dir, ec);
}

} // namespace

// ── sanitizePath ─────────────────────────────────────────────────────

TEST(SanitizePathTest, AcceptsRelativeSubpath) {
    fs::path root = makeScratchRoot("ok");
    auto resolved = sanitizePath("foo/bar.txt", root);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_TRUE(is_subpath(*resolved, root));
    fs::remove_all(root);
}

TEST(SanitizePathTest, RejectsParentRefAttack) {
    fs::path root = makeScratchRoot("parent");
    // "../../etc/passwd" is the canonical path-traversal payload — must
    // be rejected, never resolved against the filesystem.
    EXPECT_FALSE(sanitizePath("../../etc/passwd", root).has_value());
    EXPECT_FALSE(sanitizePath("foo/../../etc/passwd", root).has_value());
    fs::remove_all(root);
}

TEST(SanitizePathTest, RejectsAbsoluteOutsideRoot) {
    fs::path root = makeScratchRoot("abs");
    EXPECT_FALSE(sanitizePath("/etc/passwd", root).has_value());
    fs::remove_all(root);
}

TEST(SanitizePathTest, RejectsSymlinkCrossingRoot) {
    fs::path root = makeScratchRoot("symlink");
    // Pre-create a symlink inside ``root`` whose target is outside; the
    // weakly_canonical resolution must follow it and the subpath check
    // then rejects. (POSIX-only; on Windows symlink creation needs
    // privileges, so we simply skip the assertion there.)
#ifndef _WIN32
    fs::path target = "/etc/passwd";
    fs::path link = root / "link-out";
    std::error_code ec;
    fs::create_symlink(target, link, ec);
    if (!ec) {
        EXPECT_FALSE(sanitizePath(link.filename(), root).has_value());
    }
#endif
    fs::remove_all(root);
}

TEST(SanitizePathTest, RejectsEmptyInput) {
    fs::path root = makeScratchRoot("empty");
    EXPECT_FALSE(sanitizePath("", root).has_value());
    fs::remove_all(root);
}

// ── is_subpath ───────────────────────────────────────────────────────

TEST(IsSubpathTest, IdenticalIsSubpath) {
    EXPECT_TRUE(is_subpath("/a/b/c", "/a/b/c"));
}

TEST(IsSubpathTest, DescendantIsSubpath) {
    EXPECT_TRUE(is_subpath("/a/b/c/d.txt", "/a/b"));
}

TEST(IsSubpathTest, SiblingIsNotSubpath) {
    EXPECT_FALSE(is_subpath("/a/b2", "/a/b"));
}

TEST(IsSubpathTest, ParentIsNotSubpath) {
    EXPECT_FALSE(is_subpath("/a", "/a/b"));
}

// ── is_safe_basename ────────────────────────────────────────────────

TEST(SafeBasenameTest, AcceptsTypicalFilenames) {
    EXPECT_TRUE(is_safe_basename("rules.toml"));
    EXPECT_TRUE(is_safe_basename("0.2-to-0.3.migration.toml"));
    EXPECT_TRUE(is_safe_basename("a_b.c"));
}

TEST(SafeBasenameTest, RejectsDirectorySeparators) {
    EXPECT_FALSE(is_safe_basename("a/b"));
    EXPECT_FALSE(is_safe_basename("a\\b"));
    EXPECT_FALSE(is_safe_basename("../etc/passwd"));
}

TEST(SafeBasenameTest, RejectsDotAndDotDot) {
    EXPECT_FALSE(is_safe_basename("."));
    EXPECT_FALSE(is_safe_basename(".."));
}

TEST(SafeBasenameTest, RejectsLeadingDash) {
    // ``--rules=evil.toml`` would otherwise be argv-injectable.
    EXPECT_FALSE(is_safe_basename("-rules.toml"));
}

TEST(SafeBasenameTest, RejectsShellMetacharacters) {
    EXPECT_FALSE(is_safe_basename("rules;rm -rf /"));
    EXPECT_FALSE(is_safe_basename("rules$(id)"));
    EXPECT_FALSE(is_safe_basename("rules\nfile"));
    EXPECT_FALSE(is_safe_basename("rules file"));
    EXPECT_FALSE(is_safe_basename(""));
}
