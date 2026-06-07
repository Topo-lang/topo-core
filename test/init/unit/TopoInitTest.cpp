#include "TopoGenerator.h"
#include "InitConfig.h"
#include "topo/Check/SymbolExtractor.h"
#include "topo/AST/ASTNode.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

using namespace topo::check;
using namespace topo::init;
using topo::HostLanguage;
using topo::Visibility;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

static HostSymbol makeSymbol(const std::string& qualifiedName,
                             const std::string& simpleName,
                             HostSymbolKind kind,
                             std::optional<Visibility> vis = std::nullopt,
                             const std::string& enclosingClass = "") {
    HostSymbol sym;
    sym.qualifiedName = qualifiedName;
    sym.simpleName = simpleName;
    sym.kind = kind;
    sym.hostVisibility = vis;
    sym.enclosingClass = enclosingClass;
    return sym;
}

// ---------------------------------------------------------------------------
// TopoGenerator tests
// ---------------------------------------------------------------------------

TEST(TopoGeneratorTest, SingleNamespaceFreeFunctions) {
    std::vector<HostSymbol> syms = {
        makeSymbol("app::init", "init", HostSymbolKind::Function, Visibility::Public),
        makeSymbol("app::helper", "helper", HostSymbolKind::Function, Visibility::Protected),
        makeSymbol("app::internal", "internal", HostSymbolKind::Function, Visibility::Private),
    };

    TopoGenerator gen(HostLanguage::Cpp, "myproject");
    auto result = gen.generate(syms, "src/**/*.cpp");

    ASSERT_FALSE(result.topoFiles.empty());
    const auto& content = result.topoFiles[0].content;

    EXPECT_NE(content.find("namespace app {"), std::string::npos);
    EXPECT_NE(content.find("public:"), std::string::npos);
    EXPECT_NE(content.find("void init();"), std::string::npos);
    EXPECT_NE(content.find("protected:"), std::string::npos);
    EXPECT_NE(content.find("void helper();"), std::string::npos);
    EXPECT_NE(content.find("private:"), std::string::npos);
    EXPECT_NE(content.find("void internal();"), std::string::npos);
    EXPECT_NE(content.find("}"), std::string::npos);
}

TEST(TopoGeneratorTest, ClassWithMembers) {
    std::vector<HostSymbol> syms = {
        makeSymbol("ui::Widget", "Widget", HostSymbolKind::Class, Visibility::Public),
        makeSymbol("ui::Widget::Widget", "Widget", HostSymbolKind::Constructor, Visibility::Public, "ui::Widget"),
        makeSymbol("ui::Widget::~Widget", "~Widget", HostSymbolKind::Destructor, Visibility::Public, "ui::Widget"),
        makeSymbol("ui::Widget::render", "render", HostSymbolKind::Method, Visibility::Public, "ui::Widget"),
        makeSymbol("ui::Widget::internal", "internal", HostSymbolKind::Method, Visibility::Private, "ui::Widget"),
    };

    TopoGenerator gen(HostLanguage::Cpp, "myproject");
    auto result = gen.generate(syms, "src/**/*.cpp");

    ASSERT_FALSE(result.topoFiles.empty());
    const auto& content = result.topoFiles[0].content;

    EXPECT_NE(content.find("class Widget {"), std::string::npos);
    EXPECT_NE(content.find("Widget();"), std::string::npos);
    EXPECT_NE(content.find("~Widget();"), std::string::npos);
    EXPECT_NE(content.find("void render();"), std::string::npos);
    EXPECT_NE(content.find("public:"), std::string::npos);
    EXPECT_NE(content.find("private:"), std::string::npos);
}

TEST(TopoGeneratorTest, MixedNamespaces) {
    std::vector<HostSymbol> syms = {
        makeSymbol("app::run", "run", HostSymbolKind::Function, Visibility::Public),
        makeSymbol("net::connect", "connect", HostSymbolKind::Function, Visibility::Public),
    };

    TopoGenerator gen(HostLanguage::Cpp, "myproject");
    auto result = gen.generate(syms, "src/**/*.cpp");

    ASSERT_FALSE(result.topoFiles.empty());

    // Collect all content across generated files
    std::string allContent;
    for (const auto& f : result.topoFiles)
        allContent += f.content;

    EXPECT_NE(allContent.find("namespace app {"), std::string::npos);
    EXPECT_NE(allContent.find("namespace net {"), std::string::npos);
}

TEST(TopoGeneratorTest, NullVisibilityTodo) {
    std::vector<HostSymbol> syms = {
        makeSymbol("app::unknown", "unknown", HostSymbolKind::Function),
    };

    TopoGenerator gen(HostLanguage::Cpp, "myproject");
    auto result = gen.generate(syms, "src/**/*.cpp");

    ASSERT_FALSE(result.topoFiles.empty());
    const auto& content = result.topoFiles[0].content;

    EXPECT_NE(content.find("// TODO: verify visibility"), std::string::npos);
}

TEST(TopoGeneratorTest, StaticAndConstMethods) {
    auto staticSym =
        makeSymbol("app::Widget::create", "create", HostSymbolKind::StaticMethod, Visibility::Public, "app::Widget");

    auto constSym =
        makeSymbol("app::Widget::getId", "getId", HostSymbolKind::Method, Visibility::Public, "app::Widget");
    constSym.isConst = true;

    std::vector<HostSymbol> syms = {
        makeSymbol("app::Widget", "Widget", HostSymbolKind::Class, Visibility::Public),
        staticSym,
        constSym,
    };

    TopoGenerator gen(HostLanguage::Cpp, "myproject");
    auto result = gen.generate(syms, "src/**/*.cpp");

    ASSERT_FALSE(result.topoFiles.empty());
    const auto& content = result.topoFiles[0].content;

    EXPECT_NE(content.find("static void create();"), std::string::npos);
    EXPECT_NE(content.find("void getId() const;"), std::string::npos);
}

TEST(TopoGeneratorTest, RustTypeBindings) {
    std::vector<HostSymbol> syms = {
        makeSymbol("app::init", "init", HostSymbolKind::Function, Visibility::Public),
    };

    TopoGenerator gen(HostLanguage::Rust, "myproject");
    auto result = gen.generate(syms, "src/**/*.cpp");

    ASSERT_FALSE(result.topoFiles.empty());
    const auto& content = result.topoFiles[0].content;

    EXPECT_NE(content.find("using i32 = std::rust::i32;"), std::string::npos);
    EXPECT_EQ(content.find("std::cpp17"), std::string::npos);
}

TEST(TopoGeneratorTest, JavaTypeBindings) {
    std::vector<HostSymbol> syms = {
        makeSymbol("app::init", "init", HostSymbolKind::Function, Visibility::Public),
    };

    TopoGenerator gen(HostLanguage::Java, "myproject");
    auto result = gen.generate(syms, "src/**/*.cpp");

    ASSERT_FALSE(result.topoFiles.empty());
    const auto& content = result.topoFiles[0].content;

    // Java init must emit std::java::* bindings to match the working
    // examples (quickstart, showcase) and what JavaSymbolExtractor /
    // JavaEmitter resolve. C++ std::cpp17 aliases here produce a project
    // that fails to type-check at the very first `topo build`.
    EXPECT_NE(content.find("using Int = std::java::int;"), std::string::npos);
    EXPECT_NE(content.find("using Boolean = std::java::boolean;"), std::string::npos);
    EXPECT_NE(content.find("using Double = std::java::double;"), std::string::npos);
    EXPECT_EQ(content.find("std::cpp17"), std::string::npos);
}

TEST(TopoGeneratorTest, TypeScriptTypeBindings) {
    std::vector<HostSymbol> syms = {
        makeSymbol("app::init", "init", HostSymbolKind::Function, Visibility::Public),
    };

    TopoGenerator gen(HostLanguage::TypeScript, "myproject");
    auto result = gen.generate(syms, "src/**/*.ts");

    ASSERT_FALSE(result.topoFiles.empty());
    const auto& content = result.topoFiles[0].content;

    // TypeScript bindings must alias the TS type names (number/string/boolean)
    // to std::typescript::*. The divergent provider once emitted
    // `using int = std::typescript::number` — the LHS must be `number`, not a
    // C++/Python-style name, or the alias resolves nothing in V8 codegen.
    EXPECT_NE(content.find("using number = std::typescript::number;"), std::string::npos);
    EXPECT_NE(content.find("using string = std::typescript::string;"), std::string::npos);
    EXPECT_NE(content.find("using boolean = std::typescript::boolean;"), std::string::npos);
    EXPECT_EQ(content.find("using int = std::typescript"), std::string::npos);
}

TEST(TopoGeneratorTest, PythonTypeBindings) {
    std::vector<HostSymbol> syms = {
        makeSymbol("app::init", "init", HostSymbolKind::Function, Visibility::Public),
    };

    TopoGenerator gen(HostLanguage::Python, "myproject");
    auto result = gen.generate(syms, "src/**/*.py");

    ASSERT_FALSE(result.topoFiles.empty());
    const auto& content = result.topoFiles[0].content;

    EXPECT_NE(content.find("using int = std::python::int;"), std::string::npos);
    EXPECT_NE(content.find("using str = std::python::str;"), std::string::npos);
    EXPECT_EQ(content.find("std::cpp17"), std::string::npos);
}

TEST(TopoGeneratorTest, PythonTopoTomlSchema) {
    std::vector<HostSymbol> syms = {
        makeSymbol("app::init", "init", HostSymbolKind::Function, Visibility::Public),
    };

    TopoGenerator gen(HostLanguage::Python, "myproject");
    auto result = gen.generate(syms, "src/**/*.py");

    const auto& toml = result.topoToml;
    // Python Topo.toml must follow the same [project]/topo/main.topo/src/
    // [completeness] schema every other language uses.
    EXPECT_NE(toml.find("name = \"myproject\""), std::string::npos);
    EXPECT_NE(toml.find("root = \"topo/main.topo\""), std::string::npos);
    EXPECT_NE(toml.find("language = \"python\""), std::string::npos);
    EXPECT_NE(toml.find("sources = [\"src/**/*.py\"]"), std::string::npos);
    EXPECT_NE(toml.find("ignore_main = true"), std::string::npos);
}

TEST(TopoGeneratorTest, TopoTomlGeneration) {
    std::vector<HostSymbol> syms = {
        makeSymbol("app::init", "init", HostSymbolKind::Function, Visibility::Public),
    };

    TopoGenerator gen(HostLanguage::Cpp, "myproject");
    auto result = gen.generate(syms, "src/**/*.cpp");

    const auto& toml = result.topoToml;
    EXPECT_NE(toml.find("name = \"myproject\""), std::string::npos);
    EXPECT_NE(toml.find("root = \"topo/main.topo\""), std::string::npos);
    EXPECT_NE(toml.find("language = \"cpp\""), std::string::npos);
    EXPECT_NE(toml.find("sources = [\"src/**/*.cpp\"]"), std::string::npos);
    EXPECT_NE(toml.find("ignore_main = true"), std::string::npos);
}

TEST(TopoGeneratorTest, DeduplicateSymbols) {
    std::vector<HostSymbol> syms = {
        makeSymbol("app::init", "init", HostSymbolKind::Function, Visibility::Public),
        makeSymbol("app::init", "init", HostSymbolKind::Function, Visibility::Public),
    };

    TopoGenerator gen(HostLanguage::Cpp, "myproject");
    auto result = gen.generate(syms, "src/**/*.cpp");

    ASSERT_FALSE(result.topoFiles.empty());
    const auto& content = result.topoFiles[0].content;

    // Count occurrences of "void init();" -- should be exactly 1
    size_t count = 0;
    std::string needle = "void init();";
    size_t pos = 0;
    while ((pos = content.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    EXPECT_EQ(count, 1u);
}

TEST(TopoGeneratorTest, StructUsesTypeKeyword) {
    std::vector<HostSymbol> syms = {
        makeSymbol("app::Point", "Point", HostSymbolKind::Struct, Visibility::Public),
    };

    TopoGenerator gen(HostLanguage::Cpp, "myproject");
    auto result = gen.generate(syms, "src/**/*.cpp");

    ASSERT_FALSE(result.topoFiles.empty());
    const auto& content = result.topoFiles[0].content;

    EXPECT_NE(content.find("type"), std::string::npos);
    // Struct should use "type" keyword, not "class"
    // Only check that "class Point" does not appear (class keyword for this symbol)
    EXPECT_EQ(content.find("class Point"), std::string::npos);
}

TEST(TopoGeneratorTest, NoNamespaceSymbols) {
    std::vector<HostSymbol> syms = {
        makeSymbol("main", "main", HostSymbolKind::Function, Visibility::Public),
    };

    TopoGenerator gen(HostLanguage::Cpp, "myproject");
    auto result = gen.generate(syms, "src/**/*.cpp");

    ASSERT_FALSE(result.topoFiles.empty());
    const auto& content = result.topoFiles[0].content;

    // Should not be inside a namespace block
    EXPECT_EQ(content.find("namespace"), std::string::npos);
    // Visibility section at indent 0
    EXPECT_NE(content.find("public:"), std::string::npos);
    EXPECT_NE(content.find("void main();"), std::string::npos);
}

// ---------------------------------------------------------------------------
// InitConfig tests
// ---------------------------------------------------------------------------

TEST(InitConfigTest, ParseArgsDefaults) {
    char arg0[] = "topo-init";
    char* args[] = {arg0};
    InitConfig config;
    bool ok = parseArgs(1, args, config);

    EXPECT_TRUE(ok);
    EXPECT_EQ(config.projectDir, ".");
    EXPECT_TRUE(config.autoDetectLanguage);
    EXPECT_EQ(config.outputDir, "topo");
    EXPECT_FALSE(config.dryRun);
    EXPECT_FALSE(config.verbose);
}

TEST(InitConfigTest, ParseArgsAllOptions) {
    char a0[] = "topo-init";
    char a1[] = "--project";
    char a2[] = "/tmp/proj";
    char a3[] = "--language";
    char a4[] = "rust";
    char a5[] = "--output";
    char a6[] = "out";
    char a7[] = "--dry-run";
    char a8[] = "--verbose";
    char* args[] = {a0, a1, a2, a3, a4, a5, a6, a7, a8};
    InitConfig config;
    bool ok = parseArgs(9, args, config);

    EXPECT_TRUE(ok);
    EXPECT_EQ(config.projectDir, "/tmp/proj");
    EXPECT_FALSE(config.autoDetectLanguage);
    EXPECT_EQ(config.language, HostLanguage::Rust);
    EXPECT_EQ(config.outputDir, "out");
    EXPECT_TRUE(config.dryRun);
    EXPECT_TRUE(config.verbose);
}

TEST(InitConfigTest, ParseArgsUnknownOption) {
    char a0[] = "topo-init";
    char a1[] = "--unknown";
    char* args[] = {a0, a1};
    InitConfig config;
    bool ok = parseArgs(2, args, config);

    EXPECT_FALSE(ok);
}

// ---------------------------------------------------------------------------
// DetectLanguage / CollectSourceFiles tests (file-system dependent)
// ---------------------------------------------------------------------------

class InitConfigFSTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir_ = fs::temp_directory_path() / ("topo_init_test_" + std::to_string(getpid()));
        fs::create_directories(tempDir_);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(tempDir_, ec);
    }

    void createFile(const std::string& relPath) {
        auto p = tempDir_ / relPath;
        fs::create_directories(p.parent_path());
        std::ofstream(p) << "// placeholder\n";
    }

    fs::path tempDir_;
};

TEST_F(InitConfigFSTest, DetectLanguageCpp) {
    createFile("src/main.cpp");
    createFile("src/util.cpp");
    createFile("src/helper.cpp");
    createFile("src/lib.rs");

    auto lang = detectLanguage(tempDir_.string());
    EXPECT_EQ(lang, HostLanguage::Cpp);
}

TEST_F(InitConfigFSTest, CollectSourceFilesCpp) {
    createFile("src/main.cpp");
    createFile("src/util.cpp");
    createFile("include/header.h");

    auto files = collectSourceFiles(tempDir_.string(), HostLanguage::Cpp);

    // Should find the .cpp files
    EXPECT_GE(files.size(), 2u);

    // Verify at least one .cpp file is present
    bool hasCpp = false;
    for (const auto& f : files) {
        if (f.find(".cpp") != std::string::npos) hasCpp = true;
    }
    EXPECT_TRUE(hasCpp);
}

TEST_F(InitConfigFSTest, CollectSourceFilesPython) {
    // Regression: collectSourceFiles had no Python branch, so it returned an
    // empty vector and `topo init` aborted with "no source files found" for
    // Python projects even though detectLanguage/matchesExtension handle .py.
    createFile("src/main.py");
    createFile("src/util.py");
    createFile("src/pkg/mod.py");
    createFile("top_level.py");
    createFile("src/notes.txt"); // non-source, must be ignored

    auto files = collectSourceFiles(tempDir_.string(), HostLanguage::Python);

    EXPECT_GE(files.size(), 3u);
    bool hasPy = false;
    bool hasTxt = false;
    for (const auto& f : files) {
        EXPECT_FALSE(f.empty()); // never insert an empty path
        if (f.find(".py") != std::string::npos) hasPy = true;
        if (f.find(".txt") != std::string::npos) hasTxt = true;
    }
    EXPECT_TRUE(hasPy);
    EXPECT_FALSE(hasTxt);
}

TEST_F(InitConfigFSTest, CollectSourceFilesTypeScript) {
    // Regression: same missing-branch gap as Python — .ts/.tsx projects
    // returned empty and could not be scaffolded.
    createFile("src/main.ts");
    createFile("src/component.tsx");
    createFile("src/sub/helper.ts");

    auto files = collectSourceFiles(tempDir_.string(), HostLanguage::TypeScript);

    EXPECT_GE(files.size(), 3u);
    for (const auto& f : files)
        EXPECT_FALSE(f.empty());
}

TEST_F(InitConfigFSTest, CollectSourceFilesSkipsDanglingSymlink) {
    // Regression (throwing FS ops): a dangling symlink in src/ used to make
    // the throwing is_regular_file()/operator++ raise filesystem_error out of
    // a try/catch-less caller, terminating the tool. With the ec-overloads the
    // bad entry is skipped and real sources are still collected. Also asserts
    // no empty "" path leaks in when canonical() fails on the broken link.
    createFile("src/main.cpp");
    std::error_code ec;
    fs::create_symlink(tempDir_ / "src" / "does_not_exist.cpp",
                       tempDir_ / "src" / "broken.cpp", ec);
    if (ec) GTEST_SKIP() << "symlink creation unsupported: " << ec.message();

    auto files = collectSourceFiles(tempDir_.string(), HostLanguage::Cpp);

    // The real source survives; no crash, no empty string.
    bool hasMain = false;
    for (const auto& f : files) {
        EXPECT_FALSE(f.empty());
        if (f.find("main.cpp") != std::string::npos) hasMain = true;
    }
    EXPECT_TRUE(hasMain);
}

TEST_F(InitConfigFSTest, DetectLanguageSurvivesDanglingSymlink) {
    // Regression (throwing FS ops): detectLanguage's status queries used the
    // throwing overloads; a dangling symlink would terminate the tool. It must
    // now skip the broken entry and still detect the dominant language.
    createFile("a.cpp");
    createFile("b.cpp");
    std::error_code ec;
    fs::create_symlink(tempDir_ / "missing_target.cpp",
                       tempDir_ / "dangling.cpp", ec);
    if (ec) GTEST_SKIP() << "symlink creation unsupported: " << ec.message();

    // Must not throw / terminate.
    auto lang = detectLanguage(tempDir_.string());
    EXPECT_EQ(lang, HostLanguage::Cpp);
}

TEST_F(InitConfigFSTest, CollectSourceFilesPythonEmptyWhenNoSources) {
    // Sanity: with no .py files the result is empty (so main.cpp's
    // "no source files found" guard still fires) rather than carrying junk.
    createFile("README.md");

    auto files = collectSourceFiles(tempDir_.string(), HostLanguage::Python);
    EXPECT_TRUE(files.empty());
}
