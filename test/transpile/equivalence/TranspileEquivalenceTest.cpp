/// M6.2 Cross-Language Transpile Equivalence Tests
///
/// Each fixture builds a TranspileModule, emits through all 4 emitters
/// (C++/Rust/Java/Python), wraps each in a compilable program, compiles
/// with the available toolchain, runs, and asserts identical stdout output.
///
/// Languages whose toolchain is unavailable are skipped (not failed).

#include "topo/Platform/Process.h"
#include "topo/Stdlib/Types.h"
#include "topo/Transpile/Emitter.h"
#include "topo/Transpile/TranspileModel.h"
#include "CppEmitter.h"
#include "JavaEmitter.h"
#include "PythonEmitter.h"
#include "RustEmitter.h"
#include "V8Codegen.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace topo::transpile;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers for building TranspileModule nodes
// ---------------------------------------------------------------------------

static std::unique_ptr<LiteralExpr> intLit(const std::string& v) {
    auto e = std::make_unique<LiteralExpr>();
    e->litKind = LiteralKind::Integer;
    e->value = v;
    return e;
}

static std::unique_ptr<VarRefExpr> varRef(const std::string& name) {
    auto e = std::make_unique<VarRefExpr>();
    e->name = name;
    return e;
}

static topo::Parameter makeParam(const std::string& typeName, const std::string& name) {
    topo::Parameter p;
    p.type.nameParts = {typeName};
    p.name = name;
    return p;
}

// ---------------------------------------------------------------------------
// Program-wrapping templates (per language)
// ---------------------------------------------------------------------------

static std::string wrapCpp(const std::string& emitted, const std::string& callExpr) {
    return "#include <iostream>\n\n" + emitted + "\nint main() {\n"
           "    std::cout << " + callExpr + " << std::endl;\n"
           "    return 0;\n}\n";
}

static std::string wrapRust(const std::string& emitted, const std::string& callExpr) {
    return emitted + "\nfn main() {\n"
           "    println!(\"{}\", " + callExpr + ");\n}\n";
}

static std::string wrapJava(const std::string& emitted, const std::string& callExpr) {
    // Check if emitted code already contains a class definition (from namespace wrapping).
    // If not, wrap bare methods inside Main class with static modifier.
    bool hasClass = emitted.find("class ") != std::string::npos;

    if (hasClass) {
        // Namespace-wrapped code: emitter produced `class Foo { ... }`.
        // Append Main class alongside (package-private class + public Main is valid Java).
        return emitted + "\npublic class Main {\n"
               "    public static void main(String[] args) {\n"
               "        System.out.println(" + callExpr + ");\n"
               "    }\n}\n";
    }
    // Bare methods: wrap everything inside public class Main with static methods.
    return "public class Main {\n" + emitted + "\n"
           "    public static void main(String[] args) {\n"
           "        System.out.println(" + callExpr + ");\n"
           "    }\n}\n";
}

static std::string wrapPython(const std::string& emitted, const std::string& callExpr) {
    return emitted + "\nif __name__ == \"__main__\":\n"
           "    print(" + callExpr + ")\n";
}

static std::string wrapTypeScript(const std::string& emitted, const std::string& callExpr) {
    // Emitted code contains `export function ...` / `export namespace ...`.
    // Append a top-level call; `node --experimental-transform-types` executes
    // module-level statements after declarations regardless of the `export`.
    return emitted + "\nconsole.log(" + callExpr + ");\n";
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

class TranspileEquivalenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Per-test temp directory
        auto info = ::testing::UnitTest::GetInstance()->current_test_info();
        std::string testName = std::string(info->test_suite_name()) + "_" + info->name();
        tempDir_ = fs::temp_directory_path() / "topo-transpile-equiv" / testName;
        fs::create_directories(tempDir_);

        // Detect toolchains once
        hasCpp_ = detectTool("clang++");
        hasRust_ = detectTool("rustc");
        hasJava_ = detectTool("javac");
        hasPython_ = detectTool("python3");
        hasTypeScript_ = detectTool("node") && probeTsExecFlags(tsExecFlags_);
    }

    void TearDown() override {
        // Clean up temp files
        std::error_code ec;
        fs::remove_all(tempDir_, ec);
    }

    /// Emit the module through all 5 emitters and collect the code.
    struct EmittedCode {
        std::string cpp;
        std::string rust;
        std::string java;
        std::string python;
        std::string typescript;
    };

    EmittedCode emitAll(const TranspileModule& module) {
        CppEmitter cpp;
        RustEmitter rust;
        JavaEmitter java;
        PythonEmitter python;
        V8Codegen typescript;
        return {
            cpp.emit(module).code,
            rust.emit(module).code,
            java.emit(module).code,
            python.emit(module).code,
            typescript.emit(module).code,
        };
    }

    /// Per-language call expressions for a function.
    struct CallExprs {
        std::string cpp;
        std::string rust;
        std::string java;
        std::string python;
        std::string typescript;
    };

    /// Build, compile, run, and compare all available languages.
    /// `expected` is the exact stdout (trimmed) expected from all languages.
    void assertEquivalence(const TranspileModule& module,
                           const CallExprs& calls,
                           const std::string& expected) {
        auto emitted = emitAll(module);

        // C++ (primary — always tested if clang++ available)
        if (hasCpp_) {
            std::string output = compileAndRunCpp(
                wrapCpp(emitted.cpp, calls.cpp));
            EXPECT_EQ(trim(output), expected)
                << "C++ output mismatch.\nGenerated code:\n" << emitted.cpp;
        } else {
            GTEST_SKIP() << "clang++ not available";
        }

        // Rust
        if (hasRust_) {
            std::string output = compileAndRunRust(
                wrapRust(emitted.rust, calls.rust));
            EXPECT_EQ(trim(output), expected)
                << "Rust output mismatch.\nGenerated code:\n" << emitted.rust;
        }

        // Java
        if (hasJava_) {
            std::string output = compileAndRunJava(
                wrapJava(emitted.java, calls.java));
            EXPECT_EQ(trim(output), expected)
                << "Java output mismatch.\nGenerated code:\n" << emitted.java;
        }

        // Python
        if (hasPython_) {
            std::string output = runPython(
                wrapPython(emitted.python, calls.python));
            EXPECT_EQ(trim(output), expected)
                << "Python output mismatch.\nGenerated code:\n" << emitted.python;
        }

        // TypeScript (runs via `node --experimental-transform-types`)
        if (hasTypeScript_) {
            std::string output = runTypeScript(
                wrapTypeScript(emitted.typescript, calls.typescript));
            EXPECT_EQ(trim(output), expected)
                << "TypeScript output mismatch.\nGenerated code:\n" << emitted.typescript;
        }
    }

private:
    fs::path tempDir_;
    bool hasCpp_ = false;
    bool hasRust_ = false;
    bool hasJava_ = false;
    bool hasPython_ = false;
    bool hasTypeScript_ = false;
    std::vector<std::string> tsExecFlags_;

    static bool detectTool(const std::string& name) {
        auto r = topo::platform::runProcessCapture(name, {"--version"});
        return r.exitCode == 0;
    }

    /// Probe which `node` invocation can execute a TS program containing
    /// `namespace` blocks (the shape produced by the V8Codegen emitter).
    /// Writes the working flag list into `flagsOut` and returns true on
    /// success. Tries (in order):
    ///   1. no flag (Node 23+ default-on TS, if it ever supports namespace)
    ///   2. `--experimental-strip-types --no-warnings` (Node 22.6+)
    ///   3. `--experimental-transform-types --no-warnings` (Node 22.0–22.5)
    ///
    /// Returns false (= skip TS leg) when none accept namespace blocks,
    /// which is the case for stock Node 22.7+ where transform-types was
    /// removed and strip-only mode rejects namespace.
    static bool probeTsExecFlags(std::vector<std::string>& flagsOut) {
        fs::path probeDir = fs::temp_directory_path() / "topo-transpile-equiv-probe";
        std::error_code ec;
        fs::create_directories(probeDir, ec);
        fs::path src = probeDir / "probe.ts";
        {
            std::ofstream(src) <<
                "namespace probe { export function f(x: number): number { return x; } }\n"
                "console.log(probe.f(0));\n";
        }
        const std::vector<std::vector<std::string>> candidates = {
            {},
            {"--experimental-strip-types", "--no-warnings"},
            {"--experimental-transform-types", "--no-warnings"},
        };
        for (const auto& flags : candidates) {
            std::vector<std::string> args = flags;
            args.push_back(src.string());
            auto r = topo::platform::runProcessCaptureWithTimeout("node", args, 5000);
            if (r.exitCode == 0) {
                flagsOut = flags;
                return true;
            }
        }
        return false;
    }

    static std::string trim(const std::string& s) {
        auto start = s.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) return "";
        auto end = s.find_last_not_of(" \t\n\r");
        return s.substr(start, end - start + 1);
    }

    void writeFile(const fs::path& path, const std::string& content) {
        std::ofstream ofs(path);
        ASSERT_TRUE(ofs.good()) << "Failed to write: " << path;
        ofs << content;
    }

    std::string compileAndRunCpp(const std::string& code) {
        fs::path src = tempDir_ / "test.cpp";
        fs::path exe = tempDir_ / "test_cpp";
        writeFile(src, code);

        auto compile = topo::platform::runProcessCapture(
            "clang++", {"-std=c++17", "-o", exe.string(), src.string()});
        EXPECT_EQ(compile.exitCode, 0)
            << "C++ compilation failed:\n" << compile.stderrOutput
            << "\nSource:\n" << code;
        if (compile.exitCode != 0) return "";

        auto run = topo::platform::runProcessCaptureWithTimeout(
            exe.string(), {}, 10000);
        EXPECT_EQ(run.exitCode, 0)
            << "C++ execution failed (exit " << run.exitCode << "):\n"
            << run.stderrOutput;
        return run.stdoutOutput;
    }

    std::string compileAndRunRust(const std::string& code) {
        fs::path src = tempDir_ / "test.rs";
        fs::path exe = tempDir_ / "test_rs";
        writeFile(src, code);

        auto compile = topo::platform::runProcessCapture(
            "rustc", {"-o", exe.string(), src.string()});
        EXPECT_EQ(compile.exitCode, 0)
            << "Rust compilation failed:\n" << compile.stderrOutput
            << "\nSource:\n" << code;
        if (compile.exitCode != 0) return "";

        auto run = topo::platform::runProcessCaptureWithTimeout(
            exe.string(), {}, 10000);
        EXPECT_EQ(run.exitCode, 0)
            << "Rust execution failed (exit " << run.exitCode << "):\n"
            << run.stderrOutput;
        return run.stdoutOutput;
    }

    std::string compileAndRunJava(const std::string& code) {
        // Java needs the file named Main.java (matching the public class)
        fs::path src = tempDir_ / "Main.java";
        writeFile(src, code);

        auto compile = topo::platform::runProcessCapture(
            "javac", {src.string()});
        EXPECT_EQ(compile.exitCode, 0)
            << "Java compilation failed:\n" << compile.stderrOutput
            << "\nSource:\n" << code;
        if (compile.exitCode != 0) return "";

        auto run = topo::platform::runProcessCaptureWithTimeout(
            "java", {"-cp", tempDir_.string(), "Main"}, 10000);
        EXPECT_EQ(run.exitCode, 0)
            << "Java execution failed (exit " << run.exitCode << "):\n"
            << run.stderrOutput;
        return run.stdoutOutput;
    }

    std::string runPython(const std::string& code) {
        fs::path src = tempDir_ / "test.py";
        writeFile(src, code);

        auto run = topo::platform::runProcessCaptureWithTimeout(
            "python3", {src.string()}, 10000);
        EXPECT_EQ(run.exitCode, 0)
            << "Python execution failed (exit " << run.exitCode << "):\n"
            << run.stderrOutput << "\nSource:\n" << code;
        return run.stdoutOutput;
    }

    std::string runTypeScript(const std::string& code) {
        fs::path src = tempDir_ / "test.ts";
        writeFile(src, code);

        // tsExecFlags_ is whatever the SetUp-time probe found that can run
        // a TS program containing `namespace` blocks. On Node 22.7+ the
        // `--experimental-transform-types` flag is gone and strip-only mode
        // rejects `namespace`, so hasTypeScript_ is already false and this
        // branch isn't entered. See probeTsExecFlags().
        std::vector<std::string> args = tsExecFlags_;
        args.push_back(src.string());
        auto run = topo::platform::runProcessCaptureWithTimeout(
            "node", args, 10000);
        EXPECT_EQ(run.exitCode, 0)
            << "TypeScript execution failed (exit " << run.exitCode << "):\n"
            << run.stderrOutput << "\nSource:\n" << code;
        return run.stdoutOutput;
    }
};

// =====================================================================
// Fixture 01: Simple return — int double_it(int x) { return x * 2; }
// =====================================================================

TEST_F(TranspileEquivalenceTest, SimpleReturn) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "math::double_it";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "x")};

    auto ret = std::make_unique<ReturnStmt>();
    auto mul = std::make_unique<BinaryOpExpr>();
    mul->op = BinaryOp::Mul;
    mul->lhs = varRef("x");
    mul->rhs = intLit("2");
    ret->value = std::move(mul);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    assertEquivalence(mod,
        {"math::double_it(21)",
         "math::double_it(21)",
         "Math.double_it(21)",
         "Math.double_it(21)",
         "Math.double_it(21)"},
        "42");
}

// =====================================================================
// Fixture 02: Arithmetic — (a + b) * (a - b)
// =====================================================================

TEST_F(TranspileEquivalenceTest, Arithmetic) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "compute";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "a"), makeParam("int", "b")};

    auto ret = std::make_unique<ReturnStmt>();
    auto mul = std::make_unique<BinaryOpExpr>();
    mul->op = BinaryOp::Mul;

    auto add = std::make_unique<BinaryOpExpr>();
    add->op = BinaryOp::Add;
    add->lhs = varRef("a");
    add->rhs = varRef("b");

    auto sub = std::make_unique<BinaryOpExpr>();
    sub->op = BinaryOp::Sub;
    sub->lhs = varRef("a");
    sub->rhs = varRef("b");

    mul->lhs = std::move(add);
    mul->rhs = std::move(sub);
    ret->value = std::move(mul);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    // (10+3) * (10-3) = 13 * 7 = 91
    assertEquivalence(mod,
        {"compute(10, 3)",
         "compute(10, 3)",
         "compute(10, 3)",
         "compute(10, 3)",
         "compute(10, 3)"},
        "91");
}

// =====================================================================
// Fixture 03: If/else — absolute value
// =====================================================================

TEST_F(TranspileEquivalenceTest, IfElse) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "abs_val";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "x")};

    auto ifStmt = std::make_unique<IfStmt>();
    auto cond = std::make_unique<BinaryOpExpr>();
    cond->op = BinaryOp::Less;
    cond->lhs = varRef("x");
    cond->rhs = intLit("0");
    ifStmt->condition = std::move(cond);

    // then: return -x
    auto retNeg = std::make_unique<ReturnStmt>();
    auto neg = std::make_unique<UnaryOpExpr>();
    neg->op = UnaryOp::Negate;
    neg->operand = varRef("x");
    retNeg->value = std::move(neg);
    ifStmt->thenBody.push_back(std::move(retNeg));

    // else: return x
    auto retPos = std::make_unique<ReturnStmt>();
    retPos->value = varRef("x");
    ifStmt->elseBody.push_back(std::move(retPos));

    fn.body.push_back(std::move(ifStmt));
    mod.functions.push_back(std::move(fn));

    assertEquivalence(mod,
        {"abs_val(-7)",
         "abs_val(-7)",
         "abs_val(-7)",
         "abs_val(-7)",
         "abs_val(-7)"},
        "7");
}

// =====================================================================
// Fixture 04: For loop — sum 0..n
// =====================================================================

TEST_F(TranspileEquivalenceTest, ForLoop) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "sum_range";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "n")};

    // int s = 0;
    auto varDecl = std::make_unique<VarDeclStmt>();
    varDecl->type.nameParts = {"int"};
    varDecl->name = "s";
    varDecl->init = intLit("0");
    fn.body.push_back(std::move(varDecl));

    // for (int i = 0; i < n; i += 1) { s += i; }
    auto forStmt = std::make_unique<ForStmt>();

    auto init = std::make_unique<VarDeclStmt>();
    init->type.nameParts = {"int"};
    init->name = "i";
    init->init = intLit("0");
    forStmt->init = std::move(init);

    auto cond = std::make_unique<BinaryOpExpr>();
    cond->op = BinaryOp::Less;
    cond->lhs = varRef("i");
    cond->rhs = varRef("n");
    forStmt->condition = std::move(cond);

    auto incr = std::make_unique<CompoundAssignExpr>();
    incr->op = BinaryOp::Add;
    incr->target = varRef("i");
    incr->value = intLit("1");
    forStmt->increment = std::move(incr);

    auto addS = std::make_unique<CompoundAssignExpr>();
    addS->op = BinaryOp::Add;
    addS->target = varRef("s");
    addS->value = varRef("i");
    auto addStmt = std::make_unique<ExprStmt>();
    addStmt->expr = std::move(addS);
    forStmt->body.push_back(std::move(addStmt));

    fn.body.push_back(std::move(forStmt));

    // return s;
    auto ret = std::make_unique<ReturnStmt>();
    ret->value = varRef("s");
    fn.body.push_back(std::move(ret));

    mod.functions.push_back(std::move(fn));

    // sum(0..10) = 0+1+2+...+9 = 45
    assertEquivalence(mod,
        {"sum_range(10)",
         "sum_range(10)",
         "sum_range(10)",
         "sum_range(10)",
         "sum_range(10)"},
        "45");
}

// =====================================================================
// Fixture 05: Namespace — function in a namespace
// =====================================================================

TEST_F(TranspileEquivalenceTest, Namespace) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "math::square";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "x")};

    auto ret = std::make_unique<ReturnStmt>();
    auto mul = std::make_unique<BinaryOpExpr>();
    mul->op = BinaryOp::Mul;
    mul->lhs = varRef("x");
    mul->rhs = varRef("x");
    ret->value = std::move(mul);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    assertEquivalence(mod,
        {"math::square(7)",
         "math::square(7)",
         "Math.square(7)",
         "Math.square(7)",
         "Math.square(7)"},
        "49");
}

// =====================================================================
// Fixture 06: Ternary expression — max of two values
// =====================================================================

TEST_F(TranspileEquivalenceTest, Ternary) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "max_val";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "a"), makeParam("int", "b")};

    auto ret = std::make_unique<ReturnStmt>();
    auto tern = std::make_unique<TernaryExpr>();
    auto cond = std::make_unique<BinaryOpExpr>();
    cond->op = BinaryOp::Greater;
    cond->lhs = varRef("a");
    cond->rhs = varRef("b");
    tern->condition = std::move(cond);
    tern->trueExpr = varRef("a");
    tern->falseExpr = varRef("b");
    ret->value = std::move(tern);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    assertEquivalence(mod,
        {"max_val(15, 8)",
         "max_val(15, 8)",
         "max_val(15, 8)",
         "max_val(15, 8)",
         "max_val(15, 8)"},
        "15");
}

// =====================================================================
// Fixture 07: While loop — factorial
// =====================================================================

TEST_F(TranspileEquivalenceTest, WhileLoop) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "factorial";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "n")};

    // int result = 1;
    auto varResult = std::make_unique<VarDeclStmt>();
    varResult->type.nameParts = {"int"};
    varResult->name = "result";
    varResult->init = intLit("1");
    fn.body.push_back(std::move(varResult));

    // int i = 1;
    auto varI = std::make_unique<VarDeclStmt>();
    varI->type.nameParts = {"int"};
    varI->name = "i";
    varI->init = intLit("1");
    fn.body.push_back(std::move(varI));

    // while (i <= n) { result *= i; i += 1; }
    auto whileStmt = std::make_unique<WhileStmt>();
    auto cond = std::make_unique<BinaryOpExpr>();
    cond->op = BinaryOp::LessEq;
    cond->lhs = varRef("i");
    cond->rhs = varRef("n");
    whileStmt->condition = std::move(cond);

    auto mulAssign = std::make_unique<CompoundAssignExpr>();
    mulAssign->op = BinaryOp::Mul;
    mulAssign->target = varRef("result");
    mulAssign->value = varRef("i");
    auto mulStmt = std::make_unique<ExprStmt>();
    mulStmt->expr = std::move(mulAssign);
    whileStmt->body.push_back(std::move(mulStmt));

    auto incr = std::make_unique<CompoundAssignExpr>();
    incr->op = BinaryOp::Add;
    incr->target = varRef("i");
    incr->value = intLit("1");
    auto incrStmt = std::make_unique<ExprStmt>();
    incrStmt->expr = std::move(incr);
    whileStmt->body.push_back(std::move(incrStmt));

    fn.body.push_back(std::move(whileStmt));

    // return result;
    auto ret = std::make_unique<ReturnStmt>();
    ret->value = varRef("result");
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    // 6! = 720
    assertEquivalence(mod,
        {"factorial(6)",
         "factorial(6)",
         "factorial(6)",
         "factorial(6)",
         "factorial(6)"},
        "720");
}

// =====================================================================
// Fixture 08: Compound assign with multiple operators
// =====================================================================

TEST_F(TranspileEquivalenceTest, CompoundAssign) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "compound";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "x")};

    // int r = x; r += 10; r -= 3; r *= 2; return r;
    auto varDecl = std::make_unique<VarDeclStmt>();
    varDecl->type.nameParts = {"int"};
    varDecl->name = "r";
    varDecl->init = varRef("x");
    fn.body.push_back(std::move(varDecl));

    auto add = std::make_unique<CompoundAssignExpr>();
    add->op = BinaryOp::Add;
    add->target = varRef("r");
    add->value = intLit("10");
    auto addStmt = std::make_unique<ExprStmt>();
    addStmt->expr = std::move(add);
    fn.body.push_back(std::move(addStmt));

    auto sub = std::make_unique<CompoundAssignExpr>();
    sub->op = BinaryOp::Sub;
    sub->target = varRef("r");
    sub->value = intLit("3");
    auto subStmt = std::make_unique<ExprStmt>();
    subStmt->expr = std::move(sub);
    fn.body.push_back(std::move(subStmt));

    auto mul = std::make_unique<CompoundAssignExpr>();
    mul->op = BinaryOp::Mul;
    mul->target = varRef("r");
    mul->value = intLit("2");
    auto mulStmt = std::make_unique<ExprStmt>();
    mulStmt->expr = std::move(mul);
    fn.body.push_back(std::move(mulStmt));

    auto ret = std::make_unique<ReturnStmt>();
    ret->value = varRef("r");
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    // x=5: r=5, r+=10=15, r-=3=12, r*=2=24
    assertEquivalence(mod,
        {"compound(5)",
         "compound(5)",
         "compound(5)",
         "compound(5)",
         "compound(5)"},
        "24");
}

// =====================================================================
// Fixture 09: Bitwise operations
// =====================================================================

TEST_F(TranspileEquivalenceTest, BitwiseOps) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "bitwise";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "a"), makeParam("int", "b")};

    // return (a & b) | (a ^ b)
    auto ret = std::make_unique<ReturnStmt>();
    auto orExpr = std::make_unique<BinaryOpExpr>();
    orExpr->op = BinaryOp::BitOr;

    auto andExpr = std::make_unique<BinaryOpExpr>();
    andExpr->op = BinaryOp::BitAnd;
    andExpr->lhs = varRef("a");
    andExpr->rhs = varRef("b");

    auto xorExpr = std::make_unique<BinaryOpExpr>();
    xorExpr->op = BinaryOp::BitXor;
    xorExpr->lhs = varRef("a");
    xorExpr->rhs = varRef("b");

    orExpr->lhs = std::move(andExpr);
    orExpr->rhs = std::move(xorExpr);
    ret->value = std::move(orExpr);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    // a=12(1100), b=10(1010): a&b=8(1000), a^b=6(0110), 8|6=14(1110)
    assertEquivalence(mod,
        {"bitwise(12, 10)",
         "bitwise(12, 10)",
         "bitwise(12, 10)",
         "bitwise(12, 10)",
         "bitwise(12, 10)"},
        "14");
}

// =====================================================================
// Fixture 10: Struct with fields
// =====================================================================

TEST_F(TranspileEquivalenceTest, StructFields) {
    TranspileModule mod;

    // namespace geom { struct Point { int x; int y; } }
    TranspileType ty;
    ty.qualifiedName = "geom::Point";
    TranspileField fx;
    fx.type.nameParts = {"int"};
    fx.name = "x";
    TranspileField fy;
    fy.type.nameParts = {"int"};
    fy.name = "y";
    ty.fields = {fx, fy};
    mod.types.push_back(std::move(ty));

    // namespace geom { int sum_coords(int px, int py) { return px + py; } }
    TranspileFunction fn;
    fn.qualifiedName = "geom::sum_coords";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "px"), makeParam("int", "py")};

    auto ret = std::make_unique<ReturnStmt>();
    auto add = std::make_unique<BinaryOpExpr>();
    add->op = BinaryOp::Add;
    add->lhs = varRef("px");
    add->rhs = varRef("py");
    ret->value = std::move(add);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    assertEquivalence(mod,
        {"geom::sum_coords(3, 4)",
         "geom::sum_coords(3, 4)",
         "Geom.sum_coords(3, 4)",
         "Geom.sum_coords(3, 4)",
         "Geom.sum_coords(3, 4)"},
        "7");
}

// =====================================================================
// Fixture 11: Variable reassignment
// =====================================================================

TEST_F(TranspileEquivalenceTest, VarReassign) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "reassign";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "x")};

    // int a = x; a = a + 5; a = a * 2; return a;
    auto vd = std::make_unique<VarDeclStmt>();
    vd->type.nameParts = {"int"};
    vd->name = "a";
    vd->init = varRef("x");
    fn.body.push_back(std::move(vd));

    auto assign1 = std::make_unique<AssignStmt>();
    assign1->target = varRef("a");
    auto add = std::make_unique<BinaryOpExpr>();
    add->op = BinaryOp::Add;
    add->lhs = varRef("a");
    add->rhs = intLit("5");
    assign1->value = std::move(add);
    fn.body.push_back(std::move(assign1));

    auto assign2 = std::make_unique<AssignStmt>();
    assign2->target = varRef("a");
    auto mul = std::make_unique<BinaryOpExpr>();
    mul->op = BinaryOp::Mul;
    mul->lhs = varRef("a");
    mul->rhs = intLit("2");
    assign2->value = std::move(mul);
    fn.body.push_back(std::move(assign2));

    auto ret = std::make_unique<ReturnStmt>();
    ret->value = varRef("a");
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    // x=3: a=3, a=8, a=16
    assertEquivalence(mod,
        {"reassign(3)",
         "reassign(3)",
         "reassign(3)",
         "reassign(3)",
         "reassign(3)"},
        "16");
}

// =====================================================================
// Fixture 12: Nested if/else — classify number
// =====================================================================

TEST_F(TranspileEquivalenceTest, NestedIfElse) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "classify";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "x")};

    // if (x > 0) { if (x > 100) return 2; else return 1; } else { return 0; }
    auto outerIf = std::make_unique<IfStmt>();
    auto cond1 = std::make_unique<BinaryOpExpr>();
    cond1->op = BinaryOp::Greater;
    cond1->lhs = varRef("x");
    cond1->rhs = intLit("0");
    outerIf->condition = std::move(cond1);

    auto innerIf = std::make_unique<IfStmt>();
    auto cond2 = std::make_unique<BinaryOpExpr>();
    cond2->op = BinaryOp::Greater;
    cond2->lhs = varRef("x");
    cond2->rhs = intLit("100");
    innerIf->condition = std::move(cond2);

    auto ret2 = std::make_unique<ReturnStmt>();
    ret2->value = intLit("2");
    innerIf->thenBody.push_back(std::move(ret2));

    auto ret1 = std::make_unique<ReturnStmt>();
    ret1->value = intLit("1");
    innerIf->elseBody.push_back(std::move(ret1));

    outerIf->thenBody.push_back(std::move(innerIf));

    auto ret0 = std::make_unique<ReturnStmt>();
    ret0->value = intLit("0");
    outerIf->elseBody.push_back(std::move(ret0));

    fn.body.push_back(std::move(outerIf));
    mod.functions.push_back(std::move(fn));

    assertEquivalence(mod,
        {"classify(50)",
         "classify(50)",
         "classify(50)",
         "classify(50)",
         "classify(50)"},
        "1");
}

// =====================================================================
// Fixture 13: Modular arithmetic
// =====================================================================

TEST_F(TranspileEquivalenceTest, ModArithmetic) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "mod_check";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "n")};

    // return (n % 3 == 0) ? 1 : 0  (is n divisible by 3?)
    auto ret = std::make_unique<ReturnStmt>();
    auto tern = std::make_unique<TernaryExpr>();
    auto eq = std::make_unique<BinaryOpExpr>();
    eq->op = BinaryOp::Eq;
    auto modExpr = std::make_unique<BinaryOpExpr>();
    modExpr->op = BinaryOp::Mod;
    modExpr->lhs = varRef("n");
    modExpr->rhs = intLit("3");
    eq->lhs = std::move(modExpr);
    eq->rhs = intLit("0");
    tern->condition = std::move(eq);
    tern->trueExpr = intLit("1");
    tern->falseExpr = intLit("0");
    ret->value = std::move(tern);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    assertEquivalence(mod,
        {"mod_check(27)",
         "mod_check(27)",
         "mod_check(27)",
         "mod_check(27)",
         "mod_check(27)"},
        "1");
}

// =====================================================================
// Fixture 14: Shift operations
// =====================================================================

TEST_F(TranspileEquivalenceTest, ShiftOps) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "shift_test";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "x")};

    // return (x << 2) + (x >> 1)
    auto ret = std::make_unique<ReturnStmt>();
    auto add = std::make_unique<BinaryOpExpr>();
    add->op = BinaryOp::Add;

    auto shl = std::make_unique<BinaryOpExpr>();
    shl->op = BinaryOp::Shl;
    shl->lhs = varRef("x");
    shl->rhs = intLit("2");

    auto shr = std::make_unique<BinaryOpExpr>();
    shr->op = BinaryOp::Shr;
    shr->lhs = varRef("x");
    shr->rhs = intLit("1");

    add->lhs = std::move(shl);
    add->rhs = std::move(shr);
    ret->value = std::move(add);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    // x=8: (8<<2)=32, (8>>1)=4, 32+4=36
    assertEquivalence(mod,
        {"shift_test(8)",
         "shift_test(8)",
         "shift_test(8)",
         "shift_test(8)",
         "shift_test(8)"},
        "36");
}

// =====================================================================
// Fixture 15: Boolean logic
// =====================================================================

TEST_F(TranspileEquivalenceTest, BooleanLogic) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "logic_test";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "a"), makeParam("int", "b")};

    // if (a > 0 && b > 0) return 1; else if (a > 0 || b > 0) return 2; else return 0;
    auto outerIf = std::make_unique<IfStmt>();
    auto andCond = std::make_unique<BinaryOpExpr>();
    andCond->op = BinaryOp::And;
    auto aGt0_1 = std::make_unique<BinaryOpExpr>();
    aGt0_1->op = BinaryOp::Greater; aGt0_1->lhs = varRef("a"); aGt0_1->rhs = intLit("0");
    auto bGt0_1 = std::make_unique<BinaryOpExpr>();
    bGt0_1->op = BinaryOp::Greater; bGt0_1->lhs = varRef("b"); bGt0_1->rhs = intLit("0");
    andCond->lhs = std::move(aGt0_1);
    andCond->rhs = std::move(bGt0_1);
    outerIf->condition = std::move(andCond);

    auto ret1 = std::make_unique<ReturnStmt>(); ret1->value = intLit("1");
    outerIf->thenBody.push_back(std::move(ret1));

    auto innerIf = std::make_unique<IfStmt>();
    auto orCond = std::make_unique<BinaryOpExpr>();
    orCond->op = BinaryOp::Or;
    auto aGt0_2 = std::make_unique<BinaryOpExpr>();
    aGt0_2->op = BinaryOp::Greater; aGt0_2->lhs = varRef("a"); aGt0_2->rhs = intLit("0");
    auto bGt0_2 = std::make_unique<BinaryOpExpr>();
    bGt0_2->op = BinaryOp::Greater; bGt0_2->lhs = varRef("b"); bGt0_2->rhs = intLit("0");
    orCond->lhs = std::move(aGt0_2);
    orCond->rhs = std::move(bGt0_2);
    innerIf->condition = std::move(orCond);

    auto ret2 = std::make_unique<ReturnStmt>(); ret2->value = intLit("2");
    innerIf->thenBody.push_back(std::move(ret2));

    auto ret0 = std::make_unique<ReturnStmt>(); ret0->value = intLit("0");
    innerIf->elseBody.push_back(std::move(ret0));

    outerIf->elseBody.push_back(std::move(innerIf));

    fn.body.push_back(std::move(outerIf));
    mod.functions.push_back(std::move(fn));

    // a=5, b=-1: a>0 && b>0 = false, a>0 || b>0 = true → 2
    assertEquivalence(mod,
        {"logic_test(5, -1)",
         "logic_test(5, -1)",
         "logic_test(5, -1)",
         "logic_test(5, -1)",
         "logic_test(5, -1)"},
        "2");
}

// =====================================================================
// Fixture 16: Multiple namespaces
// =====================================================================

TEST_F(TranspileEquivalenceTest, MultipleNamespaces) {
    TranspileModule mod;

    TranspileFunction fn1;
    fn1.qualifiedName = "alpha::get_a";
    fn1.returnType.nameParts = {"int"};
    auto ret1 = std::make_unique<ReturnStmt>(); ret1->value = intLit("10");
    fn1.body.push_back(std::move(ret1));
    mod.functions.push_back(std::move(fn1));

    TranspileFunction fn2;
    fn2.qualifiedName = "beta::get_b";
    fn2.returnType.nameParts = {"int"};
    auto ret2 = std::make_unique<ReturnStmt>(); ret2->value = intLit("20");
    fn2.body.push_back(std::move(ret2));
    mod.functions.push_back(std::move(fn2));

    // Test calls one function — demonstrates multiple namespace groups coexist
    assertEquivalence(mod,
        {"alpha::get_a() + beta::get_b()",
         "alpha::get_a() + beta::get_b()",
         "Alpha.get_a() + Beta.get_b()",
         "Alpha.get_a() + Beta.get_b()",
         "Alpha.get_a() + Beta.get_b()"},
        "30");
}

// =====================================================================
// Fixture 17: Countdown loop (for pattern: non-zero start)
// =====================================================================

TEST_F(TranspileEquivalenceTest, ForNonZeroStart) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "partial_sum";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "lo"), makeParam("int", "hi")};

    // int s = 0; for (int i = lo; i < hi; i += 1) s += i; return s;
    auto vd = std::make_unique<VarDeclStmt>();
    vd->type.nameParts = {"int"}; vd->name = "s"; vd->init = intLit("0");
    fn.body.push_back(std::move(vd));

    auto forStmt = std::make_unique<ForStmt>();
    auto init = std::make_unique<VarDeclStmt>();
    init->type.nameParts = {"int"}; init->name = "i"; init->init = varRef("lo");
    forStmt->init = std::move(init);

    auto cond = std::make_unique<BinaryOpExpr>();
    cond->op = BinaryOp::Less; cond->lhs = varRef("i"); cond->rhs = varRef("hi");
    forStmt->condition = std::move(cond);

    auto incr = std::make_unique<CompoundAssignExpr>();
    incr->op = BinaryOp::Add; incr->target = varRef("i"); incr->value = intLit("1");
    forStmt->increment = std::move(incr);

    auto addS = std::make_unique<CompoundAssignExpr>();
    addS->op = BinaryOp::Add; addS->target = varRef("s"); addS->value = varRef("i");
    auto addStmt = std::make_unique<ExprStmt>(); addStmt->expr = std::move(addS);
    forStmt->body.push_back(std::move(addStmt));
    fn.body.push_back(std::move(forStmt));

    auto ret = std::make_unique<ReturnStmt>(); ret->value = varRef("s");
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    // sum(5..10) = 5+6+7+8+9 = 35
    assertEquivalence(mod,
        {"partial_sum(5, 10)",
         "partial_sum(5, 10)",
         "partial_sum(5, 10)",
         "partial_sum(5, 10)",
         "partial_sum(5, 10)"},
        "35");
}

// =====================================================================
// Fixture 18: Comparison operators
// =====================================================================

TEST_F(TranspileEquivalenceTest, ComparisonOps) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "count_flags";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "a"), makeParam("int", "b")};

    // int c = 0;
    // if (a == b) c += 1;
    // if (a != b) c += 2;
    // if (a <= b) c += 4;
    // if (a >= b) c += 8;
    // return c;
    auto vd = std::make_unique<VarDeclStmt>();
    vd->type.nameParts = {"int"}; vd->name = "c"; vd->init = intLit("0");
    fn.body.push_back(std::move(vd));

    auto addIf = [&](BinaryOp op, const std::string& val) {
        auto ifStmt = std::make_unique<IfStmt>();
        auto cond = std::make_unique<BinaryOpExpr>();
        cond->op = op; cond->lhs = varRef("a"); cond->rhs = varRef("b");
        ifStmt->condition = std::move(cond);
        auto ca = std::make_unique<CompoundAssignExpr>();
        ca->op = BinaryOp::Add; ca->target = varRef("c"); ca->value = intLit(val);
        auto es = std::make_unique<ExprStmt>(); es->expr = std::move(ca);
        ifStmt->thenBody.push_back(std::move(es));
        fn.body.push_back(std::move(ifStmt));
    };
    addIf(BinaryOp::Eq, "1");
    addIf(BinaryOp::NotEq, "2");
    addIf(BinaryOp::LessEq, "4");
    addIf(BinaryOp::GreaterEq, "8");

    auto ret = std::make_unique<ReturnStmt>(); ret->value = varRef("c");
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    // a=5, b=5: eq→+1, neq→no, le→+4, ge→+8 = 13
    assertEquivalence(mod,
        {"count_flags(5, 5)",
         "count_flags(5, 5)",
         "count_flags(5, 5)",
         "count_flags(5, 5)",
         "count_flags(5, 5)"},
        "13");
}

// =====================================================================
// Fixture 19: GCD (Euclidean algorithm)
// =====================================================================

TEST_F(TranspileEquivalenceTest, GCDAlgorithm) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "algo::gcd";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "a"), makeParam("int", "b")};

    // Parameters reassigned directly — the Rust emitter detects this and emits
    // `mut a: i32, mut b: i32` (issue: rust-emitter-parameter-reassignment-immutable).
    // while (b != 0) { int t = b; b = a % b; a = t; } return a;
    auto whileStmt = std::make_unique<WhileStmt>();
    auto cond = std::make_unique<BinaryOpExpr>();
    cond->op = BinaryOp::NotEq; cond->lhs = varRef("b"); cond->rhs = intLit("0");
    whileStmt->condition = std::move(cond);

    auto tdecl = std::make_unique<VarDeclStmt>();
    tdecl->type.nameParts = {"int"}; tdecl->name = "t"; tdecl->init = varRef("b");
    whileStmt->body.push_back(std::move(tdecl));

    auto assignB = std::make_unique<AssignStmt>(); assignB->target = varRef("b");
    auto modExpr = std::make_unique<BinaryOpExpr>();
    modExpr->op = BinaryOp::Mod; modExpr->lhs = varRef("a"); modExpr->rhs = varRef("b");
    assignB->value = std::move(modExpr);
    whileStmt->body.push_back(std::move(assignB));

    auto assignA = std::make_unique<AssignStmt>();
    assignA->target = varRef("a"); assignA->value = varRef("t");
    whileStmt->body.push_back(std::move(assignA));

    fn.body.push_back(std::move(whileStmt));
    auto ret = std::make_unique<ReturnStmt>(); ret->value = varRef("a");
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    // gcd(48, 18) = 6
    assertEquivalence(mod,
        {"algo::gcd(48, 18)",
         "algo::gcd(48, 18)",
         "Algo.gcd(48, 18)",
         "Algo.gcd(48, 18)",
         "Algo.gcd(48, 18)"},
        "6");
}

// =====================================================================
// Fixture 20: Power function (exponentiation by loop)
// =====================================================================

TEST_F(TranspileEquivalenceTest, PowerFunction) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "algo::power";
    fn.returnType.nameParts = {"int"};
    fn.params = {makeParam("int", "base"), makeParam("int", "exp")};

    // int result = 1; for (int i = 0; i < exp; i += 1) result *= base; return result;
    auto vd = std::make_unique<VarDeclStmt>();
    vd->type.nameParts = {"int"}; vd->name = "result"; vd->init = intLit("1");
    fn.body.push_back(std::move(vd));

    auto forStmt = std::make_unique<ForStmt>();
    auto init = std::make_unique<VarDeclStmt>();
    init->type.nameParts = {"int"}; init->name = "i"; init->init = intLit("0");
    forStmt->init = std::move(init);

    auto cond = std::make_unique<BinaryOpExpr>();
    cond->op = BinaryOp::Less; cond->lhs = varRef("i"); cond->rhs = varRef("exp");
    forStmt->condition = std::move(cond);

    auto incr = std::make_unique<CompoundAssignExpr>();
    incr->op = BinaryOp::Add; incr->target = varRef("i"); incr->value = intLit("1");
    forStmt->increment = std::move(incr);

    auto mulAssign = std::make_unique<CompoundAssignExpr>();
    mulAssign->op = BinaryOp::Mul; mulAssign->target = varRef("result"); mulAssign->value = varRef("base");
    auto mulStmt = std::make_unique<ExprStmt>(); mulStmt->expr = std::move(mulAssign);
    forStmt->body.push_back(std::move(mulStmt));
    fn.body.push_back(std::move(forStmt));

    auto ret = std::make_unique<ReturnStmt>(); ret->value = varRef("result");
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    // 3^5 = 243
    assertEquivalence(mod,
        {"algo::power(3, 5)",
         "algo::power(3, 5)",
         "Algo.power(3, 5)",
         "Algo.power(3, 5)",
         "Algo.power(3, 5)"},
        "243");
}

// =====================================================================
// RustEmitter stdlib bridging-type mapping.
//
// Verifies RustEmitter routes the 6 first-batch stdlib types
// (bool / i64 / f64 / string / optional<T> / slice<T>) through the
// `type.isStdlib()` branch, producing the contract output:
//
//   bool        -> bool
//   i64         -> i64
//   f64         -> f64
//   string      -> &str             (UTF-8 string slice; non-owning)
//   optional<T> -> Option<T>
//   slice<T>    -> &[T]             (non-owning borrow; lifetime elided)
//
// Cross-language equivalence (compile+run via rustc) is intentionally NOT
// asserted here: the other emitters (Java, etc.) have not yet routed
// stdlib types yet, so a 5-language
// equivalence run would fail. We string-assert on the Rust emitter output
// only; optional rustc check appended when the toolchain is available.
// =====================================================================

namespace {

// Build a TypeNode for a scalar stdlib type.
topo::TypeNode stdlibScalarType(topo::stdlib::TypeId id) {
    topo::TypeNode t;
    t.nameParts = {topo::stdlib::keywordOf(id)};
    t.stdlibId = id;
    return t;
}

// Build a TypeNode for a parameterized stdlib type (optional<T> / slice<T>).
topo::TypeNode stdlibParametricType(topo::stdlib::TypeId id, topo::TypeNode inner) {
    topo::TypeNode t;
    t.nameParts = {topo::stdlib::keywordOf(id)};
    t.stdlibId = id;
    t.templateArgs.push_back(std::move(inner));
    return t;
}

} // namespace

TEST_F(TranspileEquivalenceTest, StdlibTypesRustEmitter) {
    // Build a module whose single fn `boundary` covers every scalar stdlib
    // type in one signature — acceptance case:
    //   optional<i64> boundary(i64 id,
    //                          string name,
    //                          optional<bool> flags,
    //                          slice<f64> values)
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "boundary";
    fn.returnType = stdlibParametricType(topo::stdlib::TypeId::Optional,
                                         stdlibScalarType(topo::stdlib::TypeId::I64));

    auto addParam = [&](const std::string& name, topo::TypeNode ty) {
        topo::Parameter p;
        p.name = name;
        p.type = std::move(ty);
        fn.params.push_back(std::move(p));
    };
    addParam("id",     stdlibScalarType(topo::stdlib::TypeId::I64));
    addParam("name",   stdlibScalarType(topo::stdlib::TypeId::String));
    addParam("flags",  stdlibParametricType(topo::stdlib::TypeId::Optional,
                                            stdlibScalarType(topo::stdlib::TypeId::Bool)));
    addParam("values", stdlibParametricType(topo::stdlib::TypeId::Slice,
                                            stdlibScalarType(topo::stdlib::TypeId::F64)));

    // Body: return None so the function is well-formed Rust.
    auto ret = std::make_unique<ReturnStmt>();
    auto noneRef = std::make_unique<VarRefExpr>();
    noneRef->name = "None";
    ret->value = std::move(noneRef);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    RustEmitter emitter;
    std::string code = emitter.emit(mod).code;

    // String-assert the §4 contract appears in the emitted signature.
    EXPECT_NE(code.find(
                  "fn boundary("
                  "id: i64, "
                  "name: &str, "
                  "flags: Option<bool>, "
                  "values: &[f64]"
                  ") -> Option<i64>"),
              std::string::npos)
        << "Stdlib signature mapping mismatch.\nGenerated:\n" << code;

    // Optional runtime check: if rustc is available, the wrapped program
    // (with a main printing the boundary call result) should compile and
    // run without errors. This is a strong sanity check that the elided
    // lifetimes work end-to-end. We detect rustc inline rather than via
    // the fixture's private `hasRust_` flag (TEST_F-generated subclass
    // bodies cannot reach the fixture's private members).
    auto rustcProbe = topo::platform::runProcessCapture("rustc", {"--version"});
    if (rustcProbe.exitCode == 0) {
        // Wrap the emitted boundary fn with a main that calls it and
        // prints the discriminator of the returned Option. The body
        // returns None unconditionally, so the program prints "none".
        std::string wrapped =
            code +
            "\nfn main() {\n"
            "    let r = boundary(7, \"x\", Some(true), &[1.0, 2.0]);\n"
            "    println!(\"{}\", match r { Some(_) => \"some\", None => \"none\" });\n"
            "}\n";
        fs::path src = fs::temp_directory_path() /
                       "topo-rust-stdlib-emitter-test.rs";
        fs::path exe = fs::temp_directory_path() /
                       "topo-rust-stdlib-emitter-test_bin";
        {
            std::ofstream ofs(src);
            ofs << wrapped;
        }
        auto compile = topo::platform::runProcessCapture(
            "rustc", {"-o", exe.string(), src.string()});
        EXPECT_EQ(compile.exitCode, 0)
            << "Rust compilation of emitted stdlib signature failed:\n"
            << compile.stderrOutput << "\nSource:\n" << wrapped;
        if (compile.exitCode == 0) {
            auto run = topo::platform::runProcessCaptureWithTimeout(
                exe.string(), {}, 10000);
            EXPECT_EQ(run.exitCode, 0)
                << "Rust execution failed: " << run.stderrOutput;
            // Strip trailing whitespace from output before compare.
            std::string out = run.stdoutOutput;
            while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
                out.pop_back();
            EXPECT_EQ(out, "none") << "Unexpected stdout: '" << out << "'";
        }
        std::error_code ec;
        fs::remove(src, ec);
        fs::remove(exe, ec);
    }
}

// =====================================================================
// Cross-language stdlib equivalence.
//
// ONE boundary signature emitted through ALL wired emitters
// (C++, Rust, Python, V8/TypeScript, Java) — each output asserted against
// the canonical stdlib mapping table. Catches per-emitter drift in a single
// test: if any emitter routes a stdlib type differently from the
// contract, this test fails.
//
// Signature:
//   boundary(id: i64,
//            name: string,
//            flags: optional<bool>,
//            values: slice<f64>) -> optional<i64>
// =====================================================================

TEST_F(TranspileEquivalenceTest, StdlibTypesCrossLanguageMapping) {
    // Build the boundary module ONCE; emit through each emitter below.
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "boundary";
    fn.returnType = stdlibParametricType(topo::stdlib::TypeId::Optional,
                                         stdlibScalarType(topo::stdlib::TypeId::I64));

    auto addParam = [&](const std::string& name, topo::TypeNode ty) {
        topo::Parameter p;
        p.name = name;
        p.type = std::move(ty);
        fn.params.push_back(std::move(p));
    };
    addParam("id",     stdlibScalarType(topo::stdlib::TypeId::I64));
    addParam("name",   stdlibScalarType(topo::stdlib::TypeId::String));
    addParam("flags",  stdlibParametricType(topo::stdlib::TypeId::Optional,
                                            stdlibScalarType(topo::stdlib::TypeId::Bool)));
    addParam("values", stdlibParametricType(topo::stdlib::TypeId::Slice,
                                            stdlibScalarType(topo::stdlib::TypeId::F64)));

    // Trivial body — return a None-like value so the emitted code is
    // well-formed in each language. The body content is not asserted;
    // only the signature/type mapping is.
    auto ret = std::make_unique<ReturnStmt>();
    auto noneRef = std::make_unique<VarRefExpr>();
    noneRef->name = "None";
    ret->value = std::move(noneRef);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    // -----------------------------------------------------------------
    // C++ — mapping table row:
    //   i64         -> std::int64_t
    //   string      -> std::string_view
    //   optional<T> -> std::optional<T>
    //   slice<T>    -> topo::span<const T>
    // -----------------------------------------------------------------
    {
        CppEmitter emitter;
        std::string code = emitter.emit(mod).code;
        EXPECT_NE(code.find("std::optional<std::int64_t> boundary("),
                  std::string::npos)
            << "C++ return-type drift from §4.\nGenerated:\n" << code;
        EXPECT_NE(code.find("std::int64_t id"), std::string::npos)
            << "C++ i64 param drift from §4.\nGenerated:\n" << code;
        EXPECT_NE(code.find("std::string_view name"), std::string::npos)
            << "C++ string param drift from §4.\nGenerated:\n" << code;
        EXPECT_NE(code.find("std::optional<bool> flags"), std::string::npos)
            << "C++ optional<bool> drift from §4.\nGenerated:\n" << code;
        EXPECT_NE(code.find("topo::span<const double> values"),
                  std::string::npos)
            << "C++ slice<f64> drift from §4.\nGenerated:\n" << code;
        // Include preamble — all 4 header-bearing types are present.
        EXPECT_NE(code.find("#include <cstdint>"), std::string::npos);
        EXPECT_NE(code.find("#include <optional>"), std::string::npos);
        EXPECT_NE(code.find("#include <string_view>"), std::string::npos);
        EXPECT_NE(code.find("#include <topo/span.h>"), std::string::npos);
    }

    // -----------------------------------------------------------------
    // Rust — mapping table row:
    //   i64         -> i64
    //   string      -> &str
    //   optional<T> -> Option<T>
    //   slice<T>    -> &[T]
    // -----------------------------------------------------------------
    {
        RustEmitter emitter;
        std::string code = emitter.emit(mod).code;
        EXPECT_NE(code.find(
                      "fn boundary("
                      "id: i64, "
                      "name: &str, "
                      "flags: Option<bool>, "
                      "values: &[f64]"
                      ") -> Option<i64>"),
                  std::string::npos)
            << "Rust signature drift from §4.\nGenerated:\n" << code;
    }

    // -----------------------------------------------------------------
    // Python — mapping table row:
    //   i64         -> int
    //   string      -> str
    //   optional<T> -> T | None        (PEP 604)
    //   slice<T>    -> list[T]
    // -----------------------------------------------------------------
    {
        PythonEmitter emitter;
        std::string code = emitter.emit(mod).code;
        EXPECT_NE(code.find("def boundary(id: int, name: str, "
                            "flags: bool | None, values: list[float])"
                            " -> int | None:"),
                  std::string::npos)
            << "Python signature drift from §4.\nGenerated:\n" << code;
    }

    // -----------------------------------------------------------------
    // TypeScript (V8Codegen) — mapping table row:
    //   i64         -> bigint
    //   string      -> string
    //   optional<T> -> T | null
    //   slice<T>    -> readonly T[]
    // -----------------------------------------------------------------
    {
        V8Codegen emitter;
        std::string code = emitter.emit(mod).code;
        EXPECT_NE(code.find("export function boundary("
                            "id: bigint, "
                            "name: string, "
                            "flags: boolean | null, "
                            "values: readonly number[]"
                            "): bigint | null"),
                  std::string::npos)
            << "V8Codegen signature drift from §4.\nGenerated:\n" << code;
    }

    // -----------------------------------------------------------------
    // Java — mapping table row:
    //   i64         -> long
    //   string      -> String                       (UTF-16; boundary
    //                                                 transcoding via
    //                                                 dev.topo.StringBoundary)
    //   optional<T> -> Boxed<T>                     (nullable reference;
    //                                                 primitive T is boxed)
    //   slice<T>    -> java.util.List<Boxed<T>>     (Java 17-compatible)
    // -----------------------------------------------------------------
    {
        JavaEmitter emitter;
        std::string code = emitter.emit(mod).code;
        EXPECT_NE(code.find(
                      "static Long boundary("
                      "long id, "
                      "String name, "
                      "Boolean flags, "
                      "java.util.List<Double> values"
                      ")"),
                  std::string::npos)
            << "Java signature drift from §4.\nGenerated:\n" << code;
    }
}

// record<...> cross-language mapping. Field order is the load-bearing
// byte contract; field names live in the .topo declaration, not the host
// type — so every host emits an ordered, positional aggregate. One .topo
// signature, all 5 emitters, each asserted against its record idiom.
// Also exercises a nested stdlib field (slice<f64>) to pin recursion.
TEST_F(TranspileEquivalenceTest, StdlibRecordCrossLanguageMapping) {
    auto makeRecord = [](std::vector<std::pair<std::string, topo::TypeNode>> fs) {
        topo::TypeNode rec;
        rec.nameParts = {topo::stdlib::keywordOf(topo::stdlib::TypeId::Record)};
        rec.stdlibId = topo::stdlib::TypeId::Record;
        for (auto& [n, t] : fs) {
            topo::TypeNode::RecordField f;
            f.name = n;
            f.typeBox.push_back(std::move(t));
            rec.recordFields.push_back(std::move(f));
        }
        return rec;
    };

    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "deal";
    // return: record<id: i64, name: string>
    fn.returnType = makeRecord(
        [&] {
            std::vector<std::pair<std::string, topo::TypeNode>> v;
            v.emplace_back("id", stdlibScalarType(topo::stdlib::TypeId::I64));
            v.emplace_back("name", stdlibScalarType(topo::stdlib::TypeId::String));
            return v;
        }());
    // param: record<key: i64, vals: slice<f64>> — nested stdlib field
    {
        topo::Parameter p;
        p.name = "row";
        std::vector<std::pair<std::string, topo::TypeNode>> v;
        v.emplace_back("key", stdlibScalarType(topo::stdlib::TypeId::I64));
        v.emplace_back("vals", stdlibParametricType(topo::stdlib::TypeId::Slice,
                                                    stdlibScalarType(topo::stdlib::TypeId::F64)));
        p.type = makeRecord(std::move(v));
        fn.params.push_back(std::move(p));
    }
    auto ret = std::make_unique<ReturnStmt>();
    auto z = std::make_unique<VarRefExpr>();
    z->name = "zero";
    ret->value = std::move(z);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    {   // C++ — std::tuple<...>, pulls in <tuple> + nested <topo/span.h>
        CppEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("std::tuple<std::int64_t, std::string_view> deal("),
                  std::string::npos) << "C++ record return drift.\n" << c;
        EXPECT_NE(c.find("std::tuple<std::int64_t, topo::span<const double>> row"),
                  std::string::npos) << "C++ nested record param drift.\n" << c;
        EXPECT_NE(c.find("#include <tuple>"), std::string::npos)
            << "C++ missing <tuple> for record.\n" << c;
    }
    {   // Rust — (T1, T2)
        RustEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("fn deal(row: (i64, &[f64])) -> (i64, &str)"),
                  std::string::npos) << "Rust record signature drift.\n" << c;
    }
    {   // Python — tuple[T1, T2]
        PythonEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("def deal(row: tuple[int, list[float]]) "
                         "-> tuple[int, str]:"),
                  std::string::npos) << "Python record signature drift.\n" << c;
    }
    {   // TypeScript — readonly [T1, T2]
        V8Codegen e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("export function deal("
                         "row: readonly [bigint, readonly number[]]"
                         "): readonly [bigint, string]"),
                  std::string::npos) << "V8 record signature drift.\n" << c;
    }
    {   // Java — Object[] + ordered-field-types comment
        JavaEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("Object[] /* record<long, java.util.List<Double>>: "
                         "ordered field types */ row"),
                  std::string::npos) << "Java record param drift.\n" << c;
        EXPECT_NE(c.find("Object[] /* record<long, String>: "
                         "ordered field types */ deal("),
                  std::string::npos) << "Java record return drift.\n" << c;
    }
}

// union<...> cross-language mapping. union reuses record's named-field
// vector; the first field is the discriminant tag. Every host emits the
// same order-preserving positional aggregate it uses for record (the
// variant overlap is a byte-contract fact the .topo declaration owns, not
// expressible in the host type). One .topo signature, all 5 emitters,
// each asserted against its idiom; a nested slice<f64> variant pins
// recursion.
TEST_F(TranspileEquivalenceTest, StdlibUnionCrossLanguageMapping) {
    auto makeUnion = [](std::vector<std::pair<std::string, topo::TypeNode>> fs) {
        topo::TypeNode u;
        u.nameParts = {topo::stdlib::keywordOf(topo::stdlib::TypeId::Union)};
        u.stdlibId = topo::stdlib::TypeId::Union;
        for (auto& [n, t] : fs) {
            topo::TypeNode::RecordField f;
            f.name = n;
            f.typeBox.push_back(std::move(t));
            u.recordFields.push_back(std::move(f));
        }
        return u;
    };

    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "deal";
    // return: union<kind: u8, name: string>
    fn.returnType = makeUnion(
        [&] {
            std::vector<std::pair<std::string, topo::TypeNode>> v;
            v.emplace_back("kind", stdlibScalarType(topo::stdlib::TypeId::U8));
            v.emplace_back("name", stdlibScalarType(topo::stdlib::TypeId::String));
            return v;
        }());
    // param: union<tag: u8, count: i64, vals: slice<f64>> — nested variant
    {
        topo::Parameter p;
        p.name = "row";
        std::vector<std::pair<std::string, topo::TypeNode>> v;
        v.emplace_back("tag", stdlibScalarType(topo::stdlib::TypeId::U8));
        v.emplace_back("count", stdlibScalarType(topo::stdlib::TypeId::I64));
        v.emplace_back("vals", stdlibParametricType(topo::stdlib::TypeId::Slice,
                                                    stdlibScalarType(topo::stdlib::TypeId::F64)));
        p.type = makeUnion(std::move(v));
        fn.params.push_back(std::move(p));
    }
    auto ret = std::make_unique<ReturnStmt>();
    auto z = std::make_unique<VarRefExpr>();
    z->name = "zero";
    ret->value = std::move(z);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    {   // C++ — std::tuple<...>, pulls in <tuple> + nested <topo/span.h>
        CppEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("std::tuple<std::uint8_t, std::string_view> deal("),
                  std::string::npos) << "C++ union return drift.\n" << c;
        EXPECT_NE(c.find("std::tuple<std::uint8_t, std::int64_t, topo::span<const double>> row"),
                  std::string::npos) << "C++ nested union param drift.\n" << c;
        EXPECT_NE(c.find("#include <tuple>"), std::string::npos)
            << "C++ missing <tuple> for union.\n" << c;
    }
    {   // Rust — (T1, T2, ...)
        RustEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("fn deal(row: (u8, i64, &[f64])) -> (u8, &str)"),
                  std::string::npos) << "Rust union signature drift.\n" << c;
    }
    {   // Python — tuple[T1, T2, ...]
        PythonEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("def deal(row: tuple[int, int, list[float]]) "
                         "-> tuple[int, str]:"),
                  std::string::npos) << "Python union signature drift.\n" << c;
    }
    {   // TypeScript — readonly [T1, T2, ...]
        V8Codegen e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("export function deal("
                         "row: readonly [number, bigint, readonly number[]]"
                         "): readonly [number, string]"),
                  std::string::npos) << "V8 union signature drift.\n" << c;
    }
    {   // Java — Object[] + ordered-field-types comment
        JavaEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("Object[] /* union<byte, long, java.util.List<Double>>: "
                         "tag + overlapping variants */ row"),
                  std::string::npos) << "Java union param drift.\n" << c;
        EXPECT_NE(c.find("Object[] /* union<byte, String>: "
                         "tag + overlapping variants */ deal("),
                  std::string::npos) << "Java union return drift.\n" << c;
    }
}

// time_ns cross-language mapping. ABI is i64-isomorphic, so every host
// emits exactly what it emits for i64 (C++ std::int64_t / Rust i64 / Java
// long / Python int / TS bigint — bigint required because the i64 ns range
// exceeds the JS number safe-integer range). One .topo signature, all 5
// emitters, asserted against the i64 idiom.
TEST_F(TranspileEquivalenceTest, StdlibTimeNsCrossLanguageMapping) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "now";
    fn.returnType = stdlibScalarType(topo::stdlib::TypeId::TimeNs);
    {
        topo::Parameter p;
        p.name = "since";
        p.type = stdlibScalarType(topo::stdlib::TypeId::TimeNs);
        fn.params.push_back(std::move(p));
    }
    auto ret = std::make_unique<ReturnStmt>();
    auto z = std::make_unique<VarRefExpr>();
    z->name = "zero";
    ret->value = std::move(z);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    {   // C++ — std::int64_t (pulls <cstdint>), identical to i64
        CppEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("std::int64_t now(std::int64_t since)"),
                  std::string::npos) << "C++ time_ns drift.\n" << c;
        EXPECT_NE(c.find("#include <cstdint>"), std::string::npos) << c;
    }
    {   // Rust — i64
        RustEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("fn now(since: i64) -> i64"),
                  std::string::npos) << "Rust time_ns drift.\n" << c;
    }
    {   // Python — int
        PythonEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("def now(since: int) -> int:"),
                  std::string::npos) << "Python time_ns drift.\n" << c;
    }
    {   // TypeScript — bigint (i64 range exceeds JS number safe int)
        V8Codegen e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("export function now(since: bigint): bigint"),
                  std::string::npos) << "V8 time_ns drift.\n" << c;
    }
    {   // Java — long
        JavaEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("long now(long since)"),
                  std::string::npos) << "Java time_ns drift.\n" << c;
    }
}

// uuid cross-language mapping. 16-byte RFC 4122 value: native UUID where
// the host has one (Java/Python), a fixed 16-byte buffer where it does
// not (C++/Rust/TS). One .topo signature, all 5 emitters, each asserted
// against its idiom (incl. C++ <array>/<cstdint> and Python `import uuid`
// preamble).
TEST_F(TranspileEquivalenceTest, StdlibUuidCrossLanguageMapping) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "mint";
    fn.returnType = stdlibScalarType(topo::stdlib::TypeId::Uuid);
    {
        topo::Parameter p;
        p.name = "prev";
        p.type = stdlibScalarType(topo::stdlib::TypeId::Uuid);
        fn.params.push_back(std::move(p));
    }
    auto ret = std::make_unique<ReturnStmt>();
    auto z = std::make_unique<VarRefExpr>();
    z->name = "zero";
    ret->value = std::move(z);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    {   // C++ — std::array<std::uint8_t, 16>, pulls <array> + <cstdint>
        CppEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("std::array<std::uint8_t, 16> mint("
                         "std::array<std::uint8_t, 16> prev)"),
                  std::string::npos) << "C++ uuid drift.\n" << c;
        EXPECT_NE(c.find("#include <array>"), std::string::npos) << c;
        EXPECT_NE(c.find("#include <cstdint>"), std::string::npos) << c;
    }
    {   // Rust — [u8; 16]
        RustEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("fn mint(prev: [u8; 16]) -> [u8; 16]"),
                  std::string::npos) << "Rust uuid drift.\n" << c;
    }
    {   // Python — uuid.UUID with `import uuid` preamble
        PythonEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("import uuid"), std::string::npos)
            << "Python uuid missing import.\n" << c;
        EXPECT_NE(c.find("def mint(prev: uuid.UUID) -> uuid.UUID:"),
                  std::string::npos) << "Python uuid drift.\n" << c;
    }
    {   // TypeScript — Uint8Array (byte-faithful)
        V8Codegen e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("export function mint(prev: Uint8Array): Uint8Array"),
                  std::string::npos) << "V8 uuid drift.\n" << c;
    }
    {   // Java — native java.util.UUID (fully-qualified)
        JavaEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("java.util.UUID mint(java.util.UUID prev)"),
                  std::string::npos) << "Java uuid drift.\n" << c;
    }
}

// decimal128 cross-language mapping. No host has a fixed-layout native
// decimal, so every host surfaces a raw 16-byte buffer (byte contract =
// IEEE 754-2008; host interprets). Deliberately no runtime codec. One
// .topo signature, all 5 emitters, each asserted against its buffer idiom
// (incl. C++ <array>/<cstdint>; Python builtin `bytes`, no import).
TEST_F(TranspileEquivalenceTest, StdlibDecimal128CrossLanguageMapping) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "zero";
    fn.returnType = stdlibScalarType(topo::stdlib::TypeId::Decimal128);
    {
        topo::Parameter p;
        p.name = "spread";
        p.type = stdlibScalarType(topo::stdlib::TypeId::Decimal128);
        fn.params.push_back(std::move(p));
    }
    auto ret = std::make_unique<ReturnStmt>();
    auto z = std::make_unique<VarRefExpr>();
    z->name = "zeroval";
    ret->value = std::move(z);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    {   // C++ — std::array<std::uint8_t, 16>, pulls <array> + <cstdint>
        CppEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("std::array<std::uint8_t, 16> zero("
                         "std::array<std::uint8_t, 16> spread)"),
                  std::string::npos) << "C++ decimal128 drift.\n" << c;
        EXPECT_NE(c.find("#include <array>"), std::string::npos) << c;
        EXPECT_NE(c.find("#include <cstdint>"), std::string::npos) << c;
    }
    {   // Rust — [u8; 16]
        RustEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("fn zero(spread: [u8; 16]) -> [u8; 16]"),
                  std::string::npos) << "Rust decimal128 drift.\n" << c;
    }
    {   // Python — builtin bytes, NO import
        PythonEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("def zero(spread: bytes) -> bytes:"),
                  std::string::npos) << "Python decimal128 drift.\n" << c;
        EXPECT_EQ(c.find("import uuid"), std::string::npos)
            << "decimal128 must not pull the uuid import.\n" << c;
    }
    {   // TypeScript — Uint8Array
        V8Codegen e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("export function zero(spread: Uint8Array): Uint8Array"),
                  std::string::npos) << "V8 decimal128 drift.\n" << c;
    }
    {   // Java — byte[]
        JavaEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("byte[] zero(byte[] spread)"),
                  std::string::npos) << "Java decimal128 drift.\n" << c;
    }
}

// =====================================================================
// Width-extension stdlib types (u8 / i32 / u32 / u64 /
// f32 / i8 / i16 / u16) routed through all 5 emitters from a single .topo
// signature. Same shape as StdlibTypesCrossLanguageMapping but exercising
// only the width-extension scalar entries so a regression on any one width
// is localized.
// =====================================================================

TEST_F(TranspileEquivalenceTest, StdlibTypesBatch2WidthCrossLanguageMapping) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "widths";
    fn.returnType = stdlibScalarType(topo::stdlib::TypeId::U64);

    auto addParam = [&](const std::string& name, topo::TypeNode ty) {
        topo::Parameter p;
        p.name = name;
        p.type = std::move(ty);
        fn.params.push_back(std::move(p));
    };
    addParam("tag",   stdlibScalarType(topo::stdlib::TypeId::U8));
    addParam("count", stdlibScalarType(topo::stdlib::TypeId::I32));
    addParam("hash",  stdlibScalarType(topo::stdlib::TypeId::U32));
    addParam("norm",  stdlibScalarType(topo::stdlib::TypeId::F32));
    addParam("trim",  stdlibScalarType(topo::stdlib::TypeId::I8));
    addParam("pcm",   stdlibScalarType(topo::stdlib::TypeId::I16));
    addParam("port",  stdlibScalarType(topo::stdlib::TypeId::U16));

    auto ret = std::make_unique<ReturnStmt>();
    auto zeroRef = std::make_unique<VarRefExpr>();
    zeroRef->name = "zero";
    ret->value = std::move(zeroRef);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    // C++: <cstdint> typedefs; f32 -> float (built-in).
    {
        CppEmitter emitter;
        std::string code = emitter.emit(mod).code;
        EXPECT_NE(code.find(
                      "std::uint64_t widths("
                      "std::uint8_t tag, "
                      "std::int32_t count, "
                      "std::uint32_t hash, "
                      "float norm, "
                      "std::int8_t trim, "
                      "std::int16_t pcm, "
                      "std::uint16_t port"
                      ")"),
                  std::string::npos)
            << "C++ batch-2 width drift.\nGenerated:\n" << code;
        EXPECT_NE(code.find("#include <cstdint>"), std::string::npos);
    }

    // Rust: native scalar names.
    {
        RustEmitter emitter;
        std::string code = emitter.emit(mod).code;
        EXPECT_NE(code.find(
                      "fn widths(tag: u8, count: i32, hash: u32, norm: f32, "
                      "trim: i8, pcm: i16, port: u16) -> u64"),
                  std::string::npos)
            << "Rust batch-2 width drift.\nGenerated:\n" << code;
    }

    // Python: arbitrary-precision int / float covers everything.
    {
        PythonEmitter emitter;
        std::string code = emitter.emit(mod).code;
        EXPECT_NE(code.find(
                      "def widths(tag: int, count: int, hash: int, norm: float, "
                      "trim: int, pcm: int, port: int)"
                      " -> int:"),
                  std::string::npos)
            << "Python batch-2 width drift.\nGenerated:\n" << code;
    }

    // TypeScript (V8Codegen): u8/i32/u32/f32 fit in `number`; u64 -> bigint.
    {
        V8Codegen emitter;
        std::string code = emitter.emit(mod).code;
        EXPECT_NE(code.find(
                      "export function widths("
                      "tag: number, "
                      "count: number, "
                      "hash: number, "
                      "norm: number, "
                      "trim: number, "
                      "pcm: number, "
                      "port: number"
                      "): bigint"),
                  std::string::npos)
            << "V8Codegen batch-2 width drift.\nGenerated:\n" << code;
    }

    // Java: u8 -> byte; u32/i32 -> int; u64 -> long (signed bit-pattern;
    // not Optional, so primitive emission); f32 -> float.
    {
        JavaEmitter emitter;
        std::string code = emitter.emit(mod).code;
        EXPECT_NE(code.find(
                      "static long widths("
                      "byte tag, "
                      "int count, "
                      "int hash, "
                      "float norm, "
                      "byte trim, "
                      "short pcm, "
                      "short port"
                      ")"),
                  std::string::npos)
            << "Java batch-2 width drift.\nGenerated:\n" << code;
    }
}

// =====================================================================
// bytes + array<T,N> bridging types — V8/TypeScript codegen mapping.
//
// This block string-asserts the V8Codegen signature specifically,
// mirroring the per-language blocks above. bytes/array are mapped by all
// five host emitters; a unified five-way equivalence run for them is not
// asserted here — that lives with the cross-language spec fixtures.
//
//   bytes              -> readonly number[]   (slice<u8>-isomorphic)
//   array<i64, 4>      -> readonly [bigint, bigint, bigint, bigint]
//                         (fixed-length tuple preserves N; element type
//                          recurses like slice<T>)
//   array<record<...>> -> nested element recurses; Record is not yet
//                         emitted by V8Codegen, so we only assert the
//                         array arity/shape wrapping here.
// =====================================================================

namespace {

// Build a TypeNode for `array<T, N>`: element T in templateArgs[0]
// (recursing like slice<T>), N as the integer literal in the second
// templateArg's nonTypeValue — exactly the contract topo-core's parser
// produces.
topo::TypeNode stdlibArrayType(topo::TypeNode elem, int n) {
    topo::TypeNode t;
    t.nameParts = {topo::stdlib::keywordOf(topo::stdlib::TypeId::Array)};
    t.stdlibId = topo::stdlib::TypeId::Array;
    t.templateArgs.push_back(std::move(elem));
    topo::TypeNode nArg;
    nArg.nonTypeValue = n;
    t.templateArgs.push_back(std::move(nArg));
    return t;
}

} // namespace

TEST_F(TranspileEquivalenceTest, StdlibBytesArrayV8Codegen) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "buf";
    // return: array<i64, 4>
    fn.returnType =
        stdlibArrayType(stdlibScalarType(topo::stdlib::TypeId::I64), 4);

    auto addParam = [&](const std::string& name, topo::TypeNode ty) {
        topo::Parameter p;
        p.name = name;
        p.type = std::move(ty);
        fn.params.push_back(std::move(p));
    };
    // bytes  — slice<u8>-isomorphic
    addParam("raw", stdlibScalarType(topo::stdlib::TypeId::Bytes));
    // array<f64, 3>
    addParam("samples",
             stdlibArrayType(stdlibScalarType(topo::stdlib::TypeId::F64), 3));

    auto ret = std::make_unique<ReturnStmt>();
    auto zeroRef = std::make_unique<VarRefExpr>();
    zeroRef->name = "zero";
    ret->value = std::move(zeroRef);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    V8Codegen emitter;
    std::string code = emitter.emit(mod).code;

    // bytes -> readonly number[] (identical to what slice<u8> emits)
    EXPECT_NE(code.find("raw: readonly number[]"), std::string::npos)
        << "bytes did not map to slice<u8> form.\nGenerated:\n" << code;

    // array<f64, 3> -> readonly [number, number, number]
    EXPECT_NE(code.find("samples: readonly [number, number, number]"),
              std::string::npos)
        << "array<f64,3> param drift.\nGenerated:\n" << code;

    // return array<i64, 4> -> readonly [bigint, bigint, bigint, bigint]
    EXPECT_NE(code.find("): readonly [bigint, bigint, bigint, bigint]"),
              std::string::npos)
        << "array<i64,4> return drift.\nGenerated:\n" << code;

    // Full signature sanity (catches param ordering / extra-token drift).
    EXPECT_NE(code.find("export function buf("
                        "raw: readonly number[], "
                        "samples: readonly [number, number, number]"
                        "): readonly [bigint, bigint, bigint, bigint]"),
              std::string::npos)
        << "V8Codegen bytes/array signature drift.\nGenerated:\n" << code;
}

TEST_F(TranspileEquivalenceTest, StdlibNestedArrayRecordElementV8Codegen) {
    // array<record<a: i64>, 2> — element type is itself a composite.
    // record<a: i64> -> `readonly [bigint]`; array<…, 2> -> a 2-slot
    // readonly tuple of that element, so the full return is the nested
    // `readonly [readonly [bigint], readonly [bigint]]`. Pins that the
    // array wrapper recurses into a composite element exactly N times.
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "rows";

    topo::TypeNode rec;
    rec.nameParts = {topo::stdlib::keywordOf(topo::stdlib::TypeId::Record)};
    rec.stdlibId = topo::stdlib::TypeId::Record;
    {
        topo::TypeNode::RecordField f;
        f.name = "a";
        f.typeBox.push_back(stdlibScalarType(topo::stdlib::TypeId::I64));
        rec.recordFields.push_back(std::move(f));
    }
    fn.returnType = stdlibArrayType(std::move(rec), 2);

    auto ret = std::make_unique<ReturnStmt>();
    auto zeroRef = std::make_unique<VarRefExpr>();
    zeroRef->name = "zero";
    ret->value = std::move(zeroRef);
    fn.body.push_back(std::move(ret));
    mod.functions.push_back(std::move(fn));

    V8Codegen emitter;
    std::string code = emitter.emit(mod).code;

    // record<a: i64> -> readonly [bigint]; array<…, 2> wraps it twice.
    EXPECT_NE(code.find("): readonly [readonly [bigint], readonly [bigint]]"),
              std::string::npos)
        << "array<record<a:i64>,2> did not compose to the expected nested "
           "readonly tuple.\nGenerated:\n" << code;
}

// =====================================================================
// V8Codegen inheritance — `extends Base implements I1, I2`.
//
// TypeScript supports the exact Java shape: a single class base on
// `extends`, multiple interface bases on `implements`. The placement is
// driven by baseClassKinds (parallel-length discriminator array). With
// kinds absent the legacy heuristic kicks in (first base = extends).
// Mirrors `TranspileJavaInheritance.*` so the cross-language guarantees
// stay symmetric.
// =====================================================================

namespace {

topo::TypeNode namedBase(const std::string& name) {
    topo::TypeNode t;
    t.nameParts = {name};
    return t;
}

std::string emitV8Type(const std::string& qname, std::vector<topo::TypeNode> bases,
                       std::vector<BaseClassKind> kinds = {}) {
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = qname;
    ty.baseClasses = std::move(bases);
    ty.baseClassKinds = std::move(kinds);
    mod.types.push_back(std::move(ty));
    V8Codegen emitter;
    return emitter.emit(mod).code;
}

} // namespace

TEST_F(TranspileEquivalenceTest, V8CodegenInheritanceExtendsClassAndImplementsInterfaces) {
    // class Dog extends Animal implements Comparable — discriminator-driven.
    std::string code = emitV8Type(
        "Dog", {namedBase("Animal"), namedBase("Comparable")},
        {BaseClassKind::Class, BaseClassKind::Interface});
    EXPECT_NE(code.find("export class Dog extends Animal implements Comparable {"),
              std::string::npos)
        << "V8Codegen inheritance shape drift.\nGenerated:\n" << code;
}

TEST_F(TranspileEquivalenceTest, V8CodegenInheritanceInterfaceOnlyClassUsesImplements) {
    // No Class base → no `extends`. The legacy first-base-is-extends
    // heuristic would mis-render this as `extends Runnable`; the
    // discriminator MUST suppress that.
    std::string code = emitV8Type("Handler", {namedBase("Runnable")},
                                  {BaseClassKind::Interface});
    EXPECT_NE(code.find("export class Handler implements Runnable {"),
              std::string::npos)
        << "Interface-only class must use implements:\n" << code;
    EXPECT_EQ(code.find("extends"), std::string::npos)
        << "Interface-only class must NOT emit extends:\n" << code;
}

TEST_F(TranspileEquivalenceTest, V8CodegenInheritanceInterfaceExtendsInterfaceNoExtendsClass) {
    // An InterfaceDeclaration whose parents are all interfaces — every base
    // kind = Interface, no Class base → only `implements`. (TS in source
    // would say `interface I extends A, B`; the cross-language model emits
    // it as a `class` with all-interface implements, same as Java.)
    std::string code = emitV8Type(
        "Comparable2", {namedBase("Comparable"), namedBase("Serializable")},
        {BaseClassKind::Interface, BaseClassKind::Interface});
    EXPECT_NE(code.find("export class Comparable2 implements Comparable, Serializable {"),
              std::string::npos)
        << "All-interface bases drift:\n" << code;
    EXPECT_EQ(code.find("extends"), std::string::npos)
        << "All-interface bases must not emit extends:\n" << code;
}

TEST_F(TranspileEquivalenceTest, V8CodegenInheritanceEmptyKindsFallsBackToLegacyHeuristic) {
    // No discriminator (empty baseClassKinds) ⇒ legacy heuristic kicks in:
    // first base = extends, rest = implements. Byte-identical to the
    // pre-discriminator emission so old extractors' payloads keep working.
    std::string code = emitV8Type(
        "Dog", {namedBase("Animal"), namedBase("Comparable")});
    EXPECT_NE(code.find("export class Dog extends Animal implements Comparable {"),
              std::string::npos)
        << "Legacy heuristic drift:\n" << code;
}

TEST_F(TranspileEquivalenceTest, V8CodegenInheritanceEmptyBasesByteIdenticalToPreInheritance) {
    // Empty baseClasses ⇒ no clause; output must look exactly like the
    // pre-inheritance emission (`export class Plain {`).
    std::string code = emitV8Type("Plain", {});
    EXPECT_NE(code.find("export class Plain {"), std::string::npos)
        << "Empty bases drift:\n" << code;
    EXPECT_EQ(code.find("extends"), std::string::npos) << code;
    EXPECT_EQ(code.find("implements"), std::string::npos) << code;
}

TEST_F(TranspileEquivalenceTest, V8CodegenInheritanceGenericsPrecedeExtends) {
    // Generic parameters bind to the class name BEFORE `extends` —
    // `class Box<T> extends Base { ... }`. Same as Java's binding rule.
    TranspileModule mod;
    TranspileType ty;
    ty.qualifiedName = "Box";
    topo::TemplateParamDecl tp;
    tp.kind = topo::TemplateParamDecl::TypeParam;
    tp.name = "T";
    ty.templateParams.push_back(std::move(tp));
    ty.baseClasses = {namedBase("Base")};
    ty.baseClassKinds = {BaseClassKind::Class};
    mod.types.push_back(std::move(ty));
    V8Codegen emitter;
    std::string code = emitter.emit(mod).code;
    EXPECT_NE(code.find("export class Box<T> extends Base {"), std::string::npos)
        << "Generics-before-extends drift:\n" << code;
}

// =====================================================================
// TypeScript-as-transpile-source equivalence.
//
// Unlike the fixtures above (which hand-build a TranspileModule), this
// drives the REAL pipeline: the topo-transpile CLI parses a .topo
// declaration, spawns the real topo-extract-typescript subprocess to
// lift a hand-written .ts implementation into a TranspileModule, and
// retargets it to C++ and Rust. Each retargeted source is compiled and
// run; its output must equal a direct reference implementation's.
//
// A hand-built Model would self-confirm the emitters only — this test
// fails if the extractor's lifted JSON diverges from what the topo-core
// deserializer expects, which is the whole point of Track T.
// =====================================================================

#ifndef TOPO_TRANSPILE_CLI_BINARY
#define TOPO_TRANSPILE_CLI_BINARY ""
#endif
#ifndef TOPO_EXTRACT_TS_TOOL_DIR
#define TOPO_EXTRACT_TS_TOOL_DIR ""
#endif
#ifndef TOPO_EXTRACT_CPP_TOOL_DIR
#define TOPO_EXTRACT_CPP_TOOL_DIR ""
#endif

namespace {

std::string trimWs(const std::string& s) {
    auto a = s.find_first_not_of(" \t\n\r");
    if (a == std::string::npos) return "";
    auto b = s.find_last_not_of(" \t\n\r");
    return s.substr(a, b - a + 1);
}

bool haveTool(const std::string& name) {
    return topo::platform::runProcessCapture(name, {"--version"}).exitCode == 0;
}

} // namespace

TEST(TranspileFromTypeScript, LiftAndRetargetToCppAndRust) {
    const std::string cli = TOPO_TRANSPILE_CLI_BINARY;
    const std::string toolDir = TOPO_EXTRACT_TS_TOOL_DIR;

    if (cli.empty() || !fs::exists(cli)) {
        GTEST_SKIP() << "topo-transpile CLI binary not available at '" << cli
                     << "'";
    }
    if (!haveTool("node")) {
        GTEST_SKIP() << "node not available (topo-extract-typescript needs it)";
    }
    // The extractor launcher must be resolvable by the bare name
    // `topo-extract-typescript` exactly as TranspileDriver spawns it.
    fs::path launcher = fs::path(toolDir) / "topo-extract-typescript";
    if (toolDir.empty() || !fs::exists(launcher)) {
        GTEST_SKIP() << "topo-extract-typescript launcher not staged at '"
                     << launcher.string()
                     << "' (build topo-build-typescript first)";
    }

    // Prepend the staged tool dir to PATH so execvp() finds the launcher.
    const char* oldPath = std::getenv("PATH");
    std::string newPath = toolDir + ":" + (oldPath ? oldPath : "");
    ASSERT_EQ(setenv("PATH", newPath.c_str(), 1), 0);

    fs::path dir = fs::temp_directory_path() / "topo-ts-source-equiv";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // .topo contract: one namespace, two public entry points. TS `number`
    // is IEEE-754 double, so the extractor maps it to f64 and the contract
    // declares f64 accordingly.
    fs::path topoFile = dir / "calc.topo";
    {
        std::ofstream o(topoFile);
        o << "namespace calc {\n"
             "  public:\n"
             "    f64 fib(f64 n);\n"
             "    f64 sum_to(f64 n);\n"
             "}\n";
    }

    // Hand-written TS implementation matching the declared symbols.
    // Exercises: params, return, if, for, vardecl, assign, binary ops,
    // self-update for-incrementor, calls, varref, and — deliberately —
    // bare INTEGER literals in `number`(→f64) context. These are the
    // natural literals a TS author writes (`let s: number = 0;`) and are
    // the exact shape that regression-pins issue
    // rust-emitter-integer-literal-in-f64-context: before the fix the
    // Rust leg emitted `let mut s: f64 = 0;` (rustc E0308/E0277, leg
    // failed); after, the f64-literal coercion makes the lifted Rust
    // compile and run. Float-literal coverage lives in the other
    // equivalence fixtures; here integers are the point.
    fs::path tsFile = dir / "calc.ts";
    {
        std::ofstream o(tsFile);
        o << "namespace calc {\n"
             "  export function fib(n: number): number {\n"
             "    if (n < 2) { return n; }\n"
             "    return fib(n - 1) + fib(n - 2);\n"
             "  }\n"
             "  export function sum_to(n: number): number {\n"
             "    let s: number = 0;\n"
             "    for (let i: number = 1; i <= n; i = i + 1) {\n"
             "      s = s + i;\n"
             "    }\n"
             "    return s;\n"
             "  }\n"
             "}\n";
    }

    // Reference values: fib(10)=55, sum_to(10)=55.
    const std::string expected = "55 55";

    auto runTranspile = [&](const std::string& target,
                            fs::path& outFile) -> bool {
        fs::path outDir = dir / ("out_" + target);
        auto r = topo::platform::runProcessCaptureWithTimeout(
            cli,
            {"--from", "typescript", "--to", target,
             "--sources", tsFile.string(),
             "--output", outDir.string(),
             topoFile.string()},
            30000);
        if (r.exitCode != 0) {
            ADD_FAILURE() << "topo-transpile --to " << target
                          << " failed (exit " << r.exitCode << "):\n"
                          << r.stderrOutput << r.stdoutOutput;
            return false;
        }
        // Output filename derives from the .topo stem.
        std::string ext = (target == "cpp") ? ".cpp" : ".rs";
        outFile = outDir / ("calc" + ext);
        if (!fs::exists(outFile)) {
            ADD_FAILURE() << "expected output " << outFile.string()
                          << " not produced";
            return false;
        }
        auto sz = fs::file_size(outFile);
        EXPECT_GT(sz, 0u) << target << " output is empty";
        return sz > 0;
    };

    // ---- C++ leg ----
    if (haveTool("clang++")) {
        fs::path emitted;
        ASSERT_TRUE(runTranspile("cpp", emitted));
        std::ifstream in(emitted);
        std::string body((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        std::string prog =
            "#include <iostream>\n" + body +
            "\nint main(){ std::cout << calc::fib(10.0) << \" \" "
            "<< calc::sum_to(10.0) << std::endl; return 0; }\n";
        fs::path src = dir / "main.cpp";
        fs::path exe = dir / "main_cpp";
        { std::ofstream o(src); o << prog; }
        auto c = topo::platform::runProcessCapture(
            "clang++", {"-std=c++17", "-o", exe.string(), src.string()});
        ASSERT_EQ(c.exitCode, 0)
            << "C++ compile of lifted TS failed:\n" << c.stderrOutput
            << "\nSource:\n" << prog;
        auto run = topo::platform::runProcessCaptureWithTimeout(
            exe.string(), {}, 10000);
        EXPECT_EQ(run.exitCode, 0) << run.stderrOutput;
        EXPECT_EQ(trimWs(run.stdoutOutput), expected)
            << "C++ (lifted from TS) output mismatch";
    } else {
        GTEST_SKIP() << "clang++ not available";
    }

    // ---- Rust leg ----
    if (haveTool("rustc")) {
        fs::path emitted;
        ASSERT_TRUE(runTranspile("rust", emitted));
        std::ifstream in(emitted);
        std::string body((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        std::string prog =
            body +
            "\nfn main(){ println!(\"{} {}\", calc::fib(10.0), "
            "calc::sum_to(10.0)); }\n";
        fs::path src = dir / "main.rs";
        fs::path exe = dir / "main_rs";
        { std::ofstream o(src); o << prog; }
        auto c = topo::platform::runProcessCapture(
            "rustc", {"-o", exe.string(), src.string()});
        ASSERT_EQ(c.exitCode, 0)
            << "Rust compile of lifted TS failed:\n" << c.stderrOutput
            << "\nSource:\n" << prog;
        auto run = topo::platform::runProcessCaptureWithTimeout(
            exe.string(), {}, 10000);
        EXPECT_EQ(run.exitCode, 0) << run.stderrOutput;
        EXPECT_EQ(trimWs(run.stdoutOutput), expected)
            << "Rust (lifted from TS) output mismatch";
    }

    fs::remove_all(dir, ec);
}

// =====================================================================
// Cross-language ownership: a C++ source whose function returns
// std::unique_ptr<Foo> must, when transpiled to Rust through the real
// topo-transpile CLI (which spawns the real topo-extract-cpp), surface
// as a Box<Foo> return type. This pins the full new path end-to-end:
// libclang template introspection in the extractor -> shared
// TranspileModel JSON ownership field -> RustEmitter::emitOwnership.
// A hand-built Model would only self-confirm the emitter.
// =====================================================================
TEST(TranspileFromCpp, UniquePtrBecomesRustBox) {
    const std::string cli = TOPO_TRANSPILE_CLI_BINARY;
    const std::string toolDir = TOPO_EXTRACT_CPP_TOOL_DIR;

    if (cli.empty() || !fs::exists(cli)) {
        GTEST_SKIP() << "topo-transpile CLI binary not available at '" << cli
                     << "'";
    }
    fs::path extractor = fs::path(toolDir) / "topo-extract-cpp";
    if (toolDir.empty() || !fs::exists(extractor)) {
        GTEST_SKIP() << "topo-extract-cpp not built at '" << extractor.string()
                     << "'";
    }

    // Prepend the extractor's dir to PATH so TranspileDriver's bare-name
    // spawn of `topo-extract-cpp` resolves.
    const char* oldPath = std::getenv("PATH");
    std::string newPath = toolDir + ":" + (oldPath ? oldPath : "");
    ASSERT_EQ(setenv("PATH", newPath.c_str(), 1), 0);

    fs::path dir = fs::temp_directory_path() / "topo-cpp-source-ownership";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // .topo contract: declare Foo as a class and one public entry point
    // returning an owned Foo. `owned T` is the boundary ownership modifier
    // that the C++ emitter maps to unique_ptr and the Rust emitter to Box.
    fs::path topoFile = dir / "own.topo";
    {
        std::ofstream o(topoFile);
        o << "namespace own {\n"
             "  public:\n"
             "    class Foo {\n"
             "      public:\n"
             "        void noop();\n"
             "    }\n"
             "    owned Foo make();\n"
             "}\n";
    }

    fs::path cppFile = dir / "own.cpp";
    {
        // topo-extract-cpp parses with no system include paths, so a real
        // <memory> include would not resolve. A minimal in-TU std::unique_ptr
        // template canonicalizes to exactly `std::unique_ptr<Foo>`, which is
        // what the ownership matcher keys on — the genuine end-to-end path.
        std::ofstream o(cppFile);
        o << "namespace std {\n"
             "  template <class T> class unique_ptr {\n"
             "    T* p;\n"
             "   public:\n"
             "    unique_ptr(T* q) : p(q) {}\n"
             "  };\n"
             "}\n"
             "namespace own {\n"
             "  struct Foo { int x; };\n"
             "  std::unique_ptr<Foo> make() {\n"
             "    return std::unique_ptr<Foo>(new Foo());\n"
             "  }\n"
             "}\n";
    }

    fs::path outDir = dir / "out_rust";
    auto r = topo::platform::runProcessCaptureWithTimeout(
        cli,
        {"--from", "cpp", "--to", "rust",
         "--sources", cppFile.string(),
         "--output", outDir.string(),
         topoFile.string()},
        30000);
    ASSERT_EQ(r.exitCode, 0)
        << "topo-transpile --from cpp --to rust failed (exit " << r.exitCode
        << "):\n" << r.stderrOutput << r.stdoutOutput;

    fs::path emitted = outDir / "own.rs";
    ASSERT_TRUE(fs::exists(emitted))
        << "expected Rust output " << emitted.string() << " not produced";
    std::ifstream in(emitted);
    std::string body((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());

    // The std::unique_ptr<Foo> return type must have been lifted to an
    // owned TypeNode and re-emitted by RustEmitter as a Box of the pointee
    // (the inner type may carry its namespace, e.g. Box<own::Foo>) — the
    // load-bearing fact is the Box wrapper, which only appears if the
    // extractor set ownership=owned on the return type.
    EXPECT_NE(body.find("Box<"), std::string::npos)
        << "expected a Box<...> in emitted Rust (ownership not recovered); "
           "got:\n" << body;
    EXPECT_NE(body.find("-> Box<own::Foo>"), std::string::npos)
        << "expected `make` to return Box<own::Foo>; got:\n" << body;

    fs::remove_all(dir, ec);
}

// =====================================================================
// Real-world ownership: same shape as the test above, but the C++ source
// uses a genuine `#include <memory>` rather than an in-TU std stub. This
// pins the verification gate of cpp-extractor-no-system-include-paths.md:
// topo-extract-cpp must wire libclang to the host SDK / bundled resource
// dir so stdlib headers resolve, otherwise `std::unique_ptr<Foo>` collapses
// to `int` under clang error-recovery and ownership is silently lost.
//
// macOS-only: the runtime SDK resolution path uses `xcrun --show-sdk-path`.
// Linux/Windows hosts rely on caller-supplied include paths (not exercised
// by this fixture), so we skip there rather than depending on whatever the
// host clang's default search paths happen to provide.
// =====================================================================
TEST(TranspileFromCpp, RealStdIncludeUniquePtrBecomesRustBox) {
#ifndef __APPLE__
    GTEST_SKIP() << "real <memory> include resolution exercised only on macOS";
#else
    const std::string cli = TOPO_TRANSPILE_CLI_BINARY;
    const std::string toolDir = TOPO_EXTRACT_CPP_TOOL_DIR;

    if (cli.empty() || !fs::exists(cli)) {
        GTEST_SKIP() << "topo-transpile CLI binary not available at '" << cli
                     << "'";
    }
    fs::path extractor = fs::path(toolDir) / "topo-extract-cpp";
    if (toolDir.empty() || !fs::exists(extractor)) {
        GTEST_SKIP() << "topo-extract-cpp not built at '" << extractor.string()
                     << "'";
    }

    const char* oldPath = std::getenv("PATH");
    std::string newPath = toolDir + ":" + (oldPath ? oldPath : "");
    ASSERT_EQ(setenv("PATH", newPath.c_str(), 1), 0);

    fs::path dir = fs::temp_directory_path() / "topo-cpp-source-ownership-stdmem";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    fs::path topoFile = dir / "own.topo";
    {
        std::ofstream o(topoFile);
        o << "namespace own {\n"
             "  public:\n"
             "    class Foo {\n"
             "      public:\n"
             "        void noop();\n"
             "    }\n"
             "    owned Foo make();\n"
             "}\n";
    }

    fs::path cppFile = dir / "own.cpp";
    {
        std::ofstream o(cppFile);
        o << "#include <memory>\n"
             "namespace own {\n"
             "  struct Foo { int x; };\n"
             "  std::unique_ptr<Foo> make() {\n"
             "    return std::unique_ptr<Foo>(new Foo());\n"
             "  }\n"
             "}\n";
    }

    fs::path outDir = dir / "out_rust";
    auto r = topo::platform::runProcessCaptureWithTimeout(
        cli,
        {"--from", "cpp", "--to", "rust",
         "--sources", cppFile.string(),
         "--output", outDir.string(),
         topoFile.string()},
        30000);
    ASSERT_EQ(r.exitCode, 0)
        << "topo-transpile --from cpp --to rust failed (exit " << r.exitCode
        << "):\n" << r.stderrOutput << r.stdoutOutput;

    fs::path emitted = outDir / "own.rs";
    ASSERT_TRUE(fs::exists(emitted))
        << "expected Rust output " << emitted.string() << " not produced";
    std::ifstream in(emitted);
    std::string body((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());

    // The load-bearing assertion: `std::unique_ptr<Foo>` parsed via the
    // real <memory> header must still surface ownership=owned. Pre-fix
    // this would have collapsed to `int` and the Box<...> would be absent.
    EXPECT_NE(body.find("-> Box<own::Foo>"), std::string::npos)
        << "expected `make` to return Box<own::Foo> (ownership lost on real "
           "#include <memory>); got:\n" << body;

    fs::remove_all(dir, ec);
#endif
}

// =====================================================================
// Python-as-transpile-source equivalence.
//
// Mirrors TranspileFromTypeScript: drives the REAL pipeline — the
// topo-transpile CLI parses a .topo declaration, spawns the staged
// topo-extract-python launcher (which execs python3 against
// topo_extract_transpile_python.py) to lift a hand-written .py
// implementation into a TranspileModule, and retargets it to C++ and
// Rust. Each retargeted source is compiled and run; its output must
// equal a direct reference implementation's.
//
// Python source restrictions vs the TS counterpart:
//   - The extractor's MVP downgrades `for x in iterable` to a while-stub
//     (no C-style for in Python), so the test uses an explicit `while`
//     loop for sum_to to keep fidelity = source end-to-end.
//   - No namespace wrapper — Python has no `namespace`, and the lifted
//     function keys top-level (`fib`, `sum_to`). The .topo declaration
//     drops the namespace accordingly.
// =====================================================================

#ifndef TOPO_EXTRACT_PY_TOOL_DIR
#define TOPO_EXTRACT_PY_TOOL_DIR ""
#endif

TEST(TranspileFromPython, LiftAndRetargetToCppAndRust) {
    const std::string cli = TOPO_TRANSPILE_CLI_BINARY;
    const std::string toolDir = TOPO_EXTRACT_PY_TOOL_DIR;

    if (cli.empty() || !fs::exists(cli)) {
        GTEST_SKIP() << "topo-transpile CLI binary not available at '" << cli
                     << "'";
    }
    if (!haveTool("python3")) {
        GTEST_SKIP() << "python3 not available (topo-extract-python needs it)";
    }
    // The launcher must be resolvable by the bare name `topo-extract-python`
    // exactly as TranspileDriver spawns it.
    fs::path launcher = fs::path(toolDir) / "topo-extract-python";
    if (toolDir.empty() || !fs::exists(launcher)) {
        GTEST_SKIP() << "topo-extract-python launcher not staged at '"
                     << launcher.string()
                     << "' (build topo-extract-python-stage first)";
    }

    const char* oldPath = std::getenv("PATH");
    std::string newPath = toolDir + ":" + (oldPath ? oldPath : "");
    ASSERT_EQ(setenv("PATH", newPath.c_str(), 1), 0);

    fs::path dir = fs::temp_directory_path() / "topo-py-source-equiv";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Python `float` extracts to f64 (IEEE-754 binary64, matching CPython).
    // The Python extractor derives a namespace from the source-file
    // basename (`calc.py` → `namespace calc`), mirroring how Python
    // modules namespace their symbols. The .topo declaration matches
    // by wrapping the functions in `namespace calc`.
    fs::path topoFile = dir / "calc.topo";
    {
        std::ofstream o(topoFile);
        o << "namespace calc {\n"
             "  public:\n"
             "    f64 fib(f64 n);\n"
             "    f64 sum_to(f64 n);\n"
             "}\n";
    }

    fs::path pyFile = dir / "calc.py";
    {
        std::ofstream o(pyFile);
        // Explicit `while` loop (the MVP's for-in lowers to a while-stub
        // that loses semantics) keeps both function bodies at fidelity =
        // source so the retargeted C++/Rust runs to the reference output.
        o << "def fib(n: float) -> float:\n"
             "    if n < 2:\n"
             "        return n\n"
             "    return fib(n - 1) + fib(n - 2)\n"
             "\n"
             "def sum_to(n: float) -> float:\n"
             "    s: float = 0\n"
             "    i: float = 1\n"
             "    while i <= n:\n"
             "        s = s + i\n"
             "        i = i + 1\n"
             "    return s\n";
    }

    // Reference values: fib(10)=55, sum_to(10)=55.
    const std::string expected = "55 55";

    auto runTranspile = [&](const std::string& target,
                            fs::path& outFile) -> bool {
        fs::path outDir = dir / ("out_" + target);
        auto r = topo::platform::runProcessCaptureWithTimeout(
            cli,
            {"--from", "python", "--to", target,
             "--sources", pyFile.string(),
             "--output", outDir.string(),
             topoFile.string()},
            30000);
        if (r.exitCode != 0) {
            ADD_FAILURE() << "topo-transpile --to " << target
                          << " failed (exit " << r.exitCode << "):\n"
                          << r.stderrOutput << r.stdoutOutput;
            return false;
        }
        std::string ext = (target == "cpp") ? ".cpp" : ".rs";
        outFile = outDir / ("calc" + ext);
        if (!fs::exists(outFile)) {
            ADD_FAILURE() << "expected output " << outFile.string()
                          << " not produced";
            return false;
        }
        auto sz = fs::file_size(outFile);
        EXPECT_GT(sz, 0u) << target << " output is empty";
        return sz > 0;
    };

    // ---- C++ leg ----
    if (haveTool("clang++")) {
        fs::path emitted;
        ASSERT_TRUE(runTranspile("cpp", emitted));
        std::ifstream in(emitted);
        std::string body((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        std::string prog =
            "#include <iostream>\n" + body +
            "\nint main(){ std::cout << calc::fib(10.0) << \" \" "
            "<< calc::sum_to(10.0) << std::endl; return 0; }\n";
        fs::path src = dir / "main.cpp";
        fs::path exe = dir / "main_cpp";
        { std::ofstream o(src); o << prog; }
        auto c = topo::platform::runProcessCapture(
            "clang++", {"-std=c++17", "-o", exe.string(), src.string()});
        ASSERT_EQ(c.exitCode, 0)
            << "C++ compile of lifted Python failed:\n" << c.stderrOutput
            << "\nSource:\n" << prog;
        auto run = topo::platform::runProcessCaptureWithTimeout(
            exe.string(), {}, 10000);
        EXPECT_EQ(run.exitCode, 0) << run.stderrOutput;
        EXPECT_EQ(trimWs(run.stdoutOutput), expected)
            << "C++ (lifted from Python) output mismatch";
    } else {
        GTEST_SKIP() << "clang++ not available";
    }

    // ---- Rust leg ----
    if (haveTool("rustc")) {
        fs::path emitted;
        ASSERT_TRUE(runTranspile("rust", emitted));
        std::ifstream in(emitted);
        std::string body((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        std::string prog =
            body +
            "\nfn main(){ println!(\"{} {}\", calc::fib(10.0), "
            "calc::sum_to(10.0)); }\n";
        fs::path src = dir / "main.rs";
        fs::path exe = dir / "main_rs";
        { std::ofstream o(src); o << prog; }
        auto c = topo::platform::runProcessCapture(
            "rustc", {"-o", exe.string(), src.string()});
        ASSERT_EQ(c.exitCode, 0)
            << "Rust compile of lifted Python failed:\n" << c.stderrOutput
            << "\nSource:\n" << prog;
        auto run = topo::platform::runProcessCaptureWithTimeout(
            exe.string(), {}, 10000);
        EXPECT_EQ(run.exitCode, 0) << run.stderrOutput;
        EXPECT_EQ(trimWs(run.stdoutOutput), expected)
            << "Rust (lifted from Python) output mismatch";
    }

    fs::remove_all(dir, ec);
}

// =====================================================================
// Rust-as-transpile-source equivalence.
//
// Closes the only remaining "source language" gap in the cross-language
// harness: every host (C++, Python, TS) has had a real-pipeline
// equivalence test for some time; Rust's extractor was covered only by
// the standalone fidelity goldens. This test drives the same real
// pipeline (topo-transpile CLI → spawn topo-extract-rust → lift →
// retarget → compile + run) for Rust → C++ + Rust → Java.
//
// Java is the second target (instead of Rust) so this test exercises a
// different combination than TranspileFromTypeScript/Python (which
// both target C++ + Rust). The cumulative cross-language matrix now
// covers Rust→Java end-to-end too.
// =====================================================================

#ifndef TOPO_EXTRACT_RUST_TOOL_DIR
#define TOPO_EXTRACT_RUST_TOOL_DIR ""
#endif

TEST(TranspileFromRust, LiftAndRetargetToCppAndJava) {
    const std::string cli = TOPO_TRANSPILE_CLI_BINARY;
    const std::string toolDir = TOPO_EXTRACT_RUST_TOOL_DIR;

    if (cli.empty() || !fs::exists(cli)) {
        GTEST_SKIP() << "topo-transpile CLI binary not available at '" << cli
                     << "'";
    }
    fs::path extractor = fs::path(toolDir) / "topo-extract-rust";
    if (toolDir.empty() || !fs::exists(extractor)) {
        GTEST_SKIP() << "topo-extract-rust not built at '"
                     << extractor.string()
                     << "' (cargo build via the topo-extract-rust target)";
    }

    const char* oldPath = std::getenv("PATH");
    std::string newPath = toolDir + ":" + (oldPath ? oldPath : "");
    ASSERT_EQ(setenv("PATH", newPath.c_str(), 1), 0);

    fs::path dir = fs::temp_directory_path() / "topo-rust-source-equiv";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // topo-extract-rust enforces TOPO_EXTRACT_ROOT (defaults to CWD) as a
    // sandbox root and rejects any input path outside it. The test writes
    // the source under fs::temp_directory_path(), which is unrelated to
    // the ctest WORKING_DIRECTORY — set the env var to the test's own temp
    // dir so the extractor accepts the .rs path.
    ASSERT_EQ(setenv("TOPO_EXTRACT_ROOT", dir.string().c_str(), 1), 0);

    // Rust source has no namespace construct; the extractor keys
    // top-level `fn fib` as the bare `fib` qualified name. The .topo
    // declaration must therefore declare functions at top level (no
    // wrapping namespace) — the parser accepts top-level public
    // declarations without one. Actually the parser requires either a
    // namespace or `import/using/namespace/debug`, so we use a single
    // anonymous-shaped namespace `calc { ... }`. The Rust extractor
    // pre-existing behavior treats top-level `fn` qnames as bare
    // identifiers, not namespaced, so the test wraps the Rust source
    // in `mod calc { ... }` to align with `namespace calc { ... }`
    // in the .topo declaration. `pub mod calc { pub fn fib ... }`
    // keys as `calc::fib` end-to-end (FunctionCollector::visit_item
    // handles syn::Item::Mod by extending ns).
    fs::path topoFile = dir / "calc.topo";
    {
        std::ofstream o(topoFile);
        o << "namespace calc {\n"
             "  public:\n"
             "    f64 fib(f64 n);\n"
             "    f64 sum_to(f64 n);\n"
             "}\n";
    }

    fs::path rsFile = dir / "calc.rs";
    {
        std::ofstream o(rsFile);
        // Same shape as the Python/TS equivalences: pure functions, no
        // ownership-bearing types so emit-side translation stays clean.
        // `pub mod calc` mirrors the .topo `namespace calc { ... }`.
        // Recursive `fib(...)` is intentionally UNQUALIFIED so the
        // lifted CallExpr.callee is just `"fib"`, not `"calc::fib"`.
        // The model's callee string is a textual qualified name that
        // host emitters render verbatim; Java's namespace separator is
        // `.` (not `::`), so a Rust-style qualified callee would render
        // as `calc::fib` in the lifted Java source and fail to compile.
        // Same-namespace unqualified calls round-trip cleanly across
        // all target hosts and match what the TS/Python equivalence
        // tests do.
        o << "pub mod calc {\n"
             "    pub fn fib(n: f64) -> f64 {\n"
             "        if n < 2.0 { return n; }\n"
             "        return fib(n - 1.0) + fib(n - 2.0);\n"
             "    }\n"
             "    pub fn sum_to(n: f64) -> f64 {\n"
             "        let mut s: f64 = 0.0;\n"
             "        let mut i: f64 = 1.0;\n"
             "        while i <= n {\n"
             "            s = s + i;\n"
             "            i = i + 1.0;\n"
             "        }\n"
             "        return s;\n"
             "    }\n"
             "}\n";
    }

    // Reference values: fib(10)=55, sum_to(10)=55.
    const std::string expected = "55 55";

    auto runTranspile = [&](const std::string& target,
                            fs::path& outFile) -> bool {
        fs::path outDir = dir / ("out_" + target);
        auto r = topo::platform::runProcessCaptureWithTimeout(
            cli,
            {"--from", "rust", "--to", target,
             "--sources", rsFile.string(),
             "--output", outDir.string(),
             topoFile.string()},
            30000);
        if (r.exitCode != 0) {
            ADD_FAILURE() << "topo-transpile --to " << target
                          << " failed (exit " << r.exitCode << "):\n"
                          << r.stderrOutput << r.stdoutOutput;
            return false;
        }
        std::string ext = (target == "cpp") ? ".cpp" : ".java";
        outFile = outDir / ("calc" + ext);
        if (!fs::exists(outFile)) {
            ADD_FAILURE() << "expected output " << outFile.string()
                          << " not produced";
            return false;
        }
        auto sz = fs::file_size(outFile);
        EXPECT_GT(sz, 0u) << target << " output is empty";
        return sz > 0;
    };

    // ---- C++ leg ----
    if (haveTool("clang++")) {
        fs::path emitted;
        ASSERT_TRUE(runTranspile("cpp", emitted));
        std::ifstream in(emitted);
        std::string body((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        std::string prog =
            "#include <iostream>\n" + body +
            "\nint main(){ std::cout << calc::fib(10.0) << \" \" "
            "<< calc::sum_to(10.0) << std::endl; return 0; }\n";
        fs::path src = dir / "main.cpp";
        fs::path exe = dir / "main_cpp";
        { std::ofstream o(src); o << prog; }
        auto c = topo::platform::runProcessCapture(
            "clang++", {"-std=c++17", "-o", exe.string(), src.string()});
        ASSERT_EQ(c.exitCode, 0)
            << "C++ compile of lifted Rust failed:\n" << c.stderrOutput
            << "\nSource:\n" << prog;
        auto run = topo::platform::runProcessCaptureWithTimeout(
            exe.string(), {}, 10000);
        EXPECT_EQ(run.exitCode, 0) << run.stderrOutput;
        EXPECT_EQ(trimWs(run.stdoutOutput), expected)
            << "C++ (lifted from Rust) output mismatch";
    } else {
        GTEST_SKIP() << "clang++ not available";
    }

    // ---- Java leg ----
    //
    // JavaEmitter outputs `public class calc` with `public static`
    // methods inside the namespace (the cross-language idiom for Java);
    // a tiny Driver class with main() calls them. Java's `double` is
    // IEEE-754 binary64, same numeric width as f64 / number, so the
    // reference values match without any coercion gymnastics.
    if (haveTool("javac") && haveTool("java")) {
        fs::path emitted;
        ASSERT_TRUE(runTranspile("java", emitted));
        std::ifstream in(emitted);
        std::string body((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

        // Place the emitted class file + a driver in dir/java/.
        fs::path javaDir = dir / "java";
        fs::create_directories(javaDir);
        fs::path classFile = javaDir / "calc.java";
        { std::ofstream o(classFile); o << body; }

        fs::path driver = javaDir / "Driver.java";
        {
            std::ofstream o(driver);
            // JavaEmitter capitalises the namespace name into the class
            // it emits (`namespace calc` → `class Calc`), so the
            // driver must reference `Calc.fib`, not `calc.fib`. Cast to
            // long to drop the decimal point so the formatted output
            // matches C++'s default `std::cout << double` rendering for
            // whole values (`55` rather than `55.0`).
            o << "public class Driver {\n"
                 "    public static void main(String[] args) {\n"
                 "        System.out.println((long) Calc.fib(10.0) + "
                 "\" \" + (long) Calc.sum_to(10.0));\n"
                 "    }\n"
                 "}\n";
        }
        auto c = topo::platform::runProcessCapture(
            "javac", {"-d", javaDir.string(),
                      classFile.string(), driver.string()});
        ASSERT_EQ(c.exitCode, 0)
            << "Java compile of lifted Rust failed:\n" << c.stderrOutput
            << "\nEmitted:\n" << body;
        auto run = topo::platform::runProcessCaptureWithTimeout(
            "java", {"-cp", javaDir.string(), "Driver"}, 10000);
        EXPECT_EQ(run.exitCode, 0) << run.stderrOutput;
        EXPECT_EQ(trimWs(run.stdoutOutput), expected)
            << "Java (lifted from Rust) output mismatch";
    }

    fs::remove_all(dir, ec);
}

// =====================================================================
// Python TypeVar constraint-tuple as a union bound, cross-host render.
//
// `T = TypeVar('T', int, str)` lowers to a type-param whose `bound` is a
// positional union TypeNode (`nameParts == ["union"]`, variant types in
// `templateArgs`). PythonEmitter renders the PEP 604 `int | str`;
// V8Codegen renders the native TypeScript union `bigint | string` — i64
// maps to TS `bigint` (not `number`) to preserve the upper 11 bits that
// would round off in IEEE-754 binary64. This is the untagged member-
// choice union, distinct from the stdlib *tagged* `union<tag:, v1:, ...>`
// (which rides `recordFields`).
//
// The C++/Rust/Java emitters have no positional-union bound path yet —
// see issue `non-python-emitters-no-positional-union-bound.md` — so this
// cross-host check covers the two hosts whose surface union syntax
// faithfully expresses the constraint.
// =====================================================================

TEST(TranspileUnionConstraintBound, PythonAndTypeScriptRenderUnionBound) {
    TranspileModule mod;
    TranspileFunction fn;
    fn.qualifiedName = "pick";
    fn.returnType.nameParts = {"void"};

    topo::TemplateParamDecl tp;
    tp.kind = topo::TemplateParamDecl::TypeParam;
    tp.name = "T";
    tp.constraintType.nameParts = {"union"};
    {
        topo::TypeNode i64;
        i64.nameParts = {"i64"};
        topo::TypeNode str;
        str.nameParts = {"string"};
        tp.constraintType.templateArgs = {i64, str};
    }
    fn.templateParams = {tp};
    mod.functions.push_back(std::move(fn));

    {   // Python — PEP 604 `[T: int | str]`
        PythonEmitter e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("[T: int | str]"), std::string::npos)
            << "Python union-bound TypeVar must render `[T: int | str]`.\n"
            << c;
    }
    {   // TypeScript — native union `bigint | string` (i64 → bigint)
        V8Codegen e;
        std::string c = e.emit(mod).code;
        EXPECT_NE(c.find("bigint | string"), std::string::npos)
            << "TypeScript union-bound type param must render "
               "`bigint | string` (i64 maps to bigint, not number — see "
               "V8Codegen.cpp file-level contract).\n"
            << c;
        EXPECT_EQ(c.find("union<"), std::string::npos)
            << "TypeScript must not leak the literal `union<...>` token.\n"
            << c;
    }
}

// =====================================================================
// `.topo`-as-transpile-source end-to-end.
//
// Drives the REAL topo-transpile CLI in `.topo`-source mode (`--from topo`):
// the `.topo` file itself is the source. A composite function with a
// `fn` logic block has its body generated from the logic block; leaf
// functions (signature only) are filled by the adapter resolver from
// the assembled adapter sources (M5).
//
// Two halves:
//   * With `--adapters <manifest>` (a topo-app adapter source): every leaf
//     resolves, the output is compiled in each of the 5 target languages,
//     and the composite body is asserted to reflect the logic block's
//     declaration-ordered call sequence.
//   * Without `--adapters`: leaves have no resolvable adapter, so the CLI
//     produces placeholder bodies, prints a stderr `warning:` naming each
//     leaf, and exits 1 — matching the existing not-transpilable handling.
//
// No host-source extractor subprocess is involved — that is the whole
// point of the `.topo`-source path.
// =====================================================================

namespace {

// The shared M6 fixture `.topo`. Leaves are declared BEFORE the composite
// so the C++/Java emitters (which emit functions in declaration order with
// no forward declarations) place each callee ahead of its caller.
constexpr const char* kM6FixtureTopo = R"topo(
namespace pipe {
  protected:
    f64  produce();
    f64  refine();
    void consume();
  public:
    void run();
    fn run {
      stage<1> produce() -> a;
      stage<2> refine() -> b;
      stage<3> consume();
    }
}
)topo";

// A topo-app adapter manifest covering every leaf for all 5 targets.
// `produce`/`refine` return a float literal; `consume` gets an empty body.
std::string m6AdapterManifest() {
    std::string out = "[\n";
    const char* langs[] = {"cpp", "rust", "java", "python", "typescript"};
    bool first = true;
    auto entry = [&](const std::string& fn, const std::string& lang,
                     const std::string& ret, const std::string& body) {
        if (!first) out += ",\n";
        first = false;
        out += "  { \"topoFunction\": \"" + fn + "\", \"targetLanguage\": \"" +
               lang + "\",\n    \"signature\": { \"returnType\": "
               "{ \"nameParts\": [\"" + ret + "\"] }, \"params\": [] },\n    " +
               body;
    };
    for (const char* lang : langs) {
        entry("pipe::produce", lang, "f64",
              "\"bodyModel\": [ { \"kind\": \"return\", \"fidelity\": "
              "\"source\", \"value\": { \"kind\": \"literal\", \"fidelity\": "
              "\"source\", \"litKind\": \"float\", \"value\": \"20.0\" } } ] }");
        entry("pipe::refine", lang, "f64",
              "\"bodyModel\": [ { \"kind\": \"return\", \"fidelity\": "
              "\"source\", \"value\": { \"kind\": \"literal\", \"fidelity\": "
              "\"source\", \"litKind\": \"float\", \"value\": \"22.0\" } } ] }");
        entry("pipe::consume", lang, "void", "\"bodyModel\": [] }");
    }
    out += "\n]\n";
    return out;
}

bool toolPresent(const std::string& name) {
    return topo::platform::runProcessCapture(name, {"--version"}).exitCode == 0;
}

} // namespace

TEST(TranspileFromTopoSource, AdapterFilledLeavesCompileInAllFiveTargets) {
    const std::string cli = TOPO_TRANSPILE_CLI_BINARY;
    if (cli.empty() || !fs::exists(cli)) {
        GTEST_SKIP() << "topo-transpile CLI binary not available at '" << cli
                     << "'";
    }

    fs::path dir = fs::temp_directory_path() / "topo-source-m6-resolved";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    fs::path topoFile = dir / "pipe.topo";
    { std::ofstream(topoFile) << kM6FixtureTopo; }
    fs::path manifest = dir / "adapters.json";
    { std::ofstream(manifest) << m6AdapterManifest(); }

    // Per-target leg: transpile, then compile the output with that
    // language's standard toolchain (skip when the toolchain is absent).
    auto transpile = [&](const std::string& target,
                         fs::path& outFile) -> bool {
        fs::path outDir = dir / ("out_" + target);
        auto r = topo::platform::runProcessCaptureWithTimeout(
            cli,
            {"--from", "topo", "--to", target,
             "--adapters", manifest.string(),
             "--output", outDir.string(),
             topoFile.string()},
            30000);
        if (r.exitCode != 0) {
            ADD_FAILURE() << "topo-transpile --from topo --to " << target
                          << " failed (exit " << r.exitCode << "):\n"
                          << r.stderrOutput << r.stdoutOutput;
            return false;
        }
        outFile = outDir / ("pipe." +
                            std::string(target == "cpp"        ? "cpp"
                                        : target == "rust"     ? "rs"
                                        : target == "java"     ? "java"
                                        : target == "python"   ? "py"
                                                               : "ts"));
        if (!fs::exists(outFile)) {
            ADD_FAILURE() << "expected output " << outFile.string()
                          << " not produced";
            return false;
        }
        return true;
    };

    auto readAll = [](const fs::path& p) {
        std::ifstream in(p);
        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    };

    // The composite body must reflect the logic block: a call to `produce`
    // and a call to `refine` (declaration-ordered), and a call to `consume`.
    auto assertCompositeBody = [](const std::string& code,
                                  const std::string& target) {
        EXPECT_NE(code.find("produce()"), std::string::npos)
            << target << ": composite body missing produce() call";
        EXPECT_NE(code.find("refine()"), std::string::npos)
            << target << ": composite body missing refine() call";
        EXPECT_NE(code.find("consume()"), std::string::npos)
            << target << ": composite body missing consume() call";
        // Adapter-resolved leaves: no unsupported-construct marker anywhere.
        EXPECT_EQ(code.find("TOPO-TRANSPILE: unsupported"), std::string::npos)
            << target << ": adapter-resolved output still has an unsupported "
                         "marker:\n" << code;
    };

    // ---- C++ ----
    {
        fs::path out;
        ASSERT_TRUE(transpile("cpp", out));
        std::string code = readAll(out);
        assertCompositeBody(code, "cpp");
        if (toolPresent("clang++")) {
            fs::path obj = dir / "pipe_cpp.o";
            auto c = topo::platform::runProcessCapture(
                "clang++", {"-std=c++17", "-c", "-o", obj.string(),
                            out.string()});
            EXPECT_EQ(c.exitCode, 0)
                << "C++ output failed to compile:\n" << c.stderrOutput
                << "\nSource:\n" << code;
        }
    }

    // ---- Rust ----
    {
        fs::path out;
        ASSERT_TRUE(transpile("rust", out));
        std::string code = readAll(out);
        assertCompositeBody(code, "rust");
        if (toolPresent("rustc")) {
            fs::path outDir = dir / "rust_build";
            fs::create_directories(outDir);
            auto c = topo::platform::runProcessCapture(
                "rustc", {"--crate-type", "lib", "--out-dir",
                          outDir.string(), out.string()});
            EXPECT_EQ(c.exitCode, 0)
                << "Rust output failed to compile:\n" << c.stderrOutput
                << "\nSource:\n" << code;
        }
    }

    // ---- Java ----
    {
        fs::path out;
        ASSERT_TRUE(transpile("java", out));
        std::string code = readAll(out);
        assertCompositeBody(code, "java");
        if (toolPresent("javac")) {
            // javac requires the file name to match the public-ish class.
            fs::path javaDir = dir / "java_build";
            fs::create_directories(javaDir);
            fs::path src = javaDir / "Pipe.java";
            { std::ofstream(src) << code; }
            auto c = topo::platform::runProcessCapture("javac", {src.string()});
            EXPECT_EQ(c.exitCode, 0)
                << "Java output failed to compile:\n" << c.stderrOutput
                << "\nSource:\n" << code;
        }
    }

    // ---- Python ----
    {
        fs::path out;
        ASSERT_TRUE(transpile("python", out));
        std::string code = readAll(out);
        assertCompositeBody(code, "python");
        if (toolPresent("python3")) {
            auto c = topo::platform::runProcessCapture(
                "python3", {"-c",
                            "import py_compile,sys; py_compile.compile('" +
                                out.string() + "', doraise=True)"});
            EXPECT_EQ(c.exitCode, 0)
                << "Python output failed to compile:\n" << c.stderrOutput
                << "\nSource:\n" << code;
        }
    }

    // ---- TypeScript ----
    {
        fs::path out;
        ASSERT_TRUE(transpile("typescript", out));
        std::string code = readAll(out);
        assertCompositeBody(code, "typescript");
        // Type-check via tsc when available; otherwise a runtime strip-types
        // execution still confirms the emitted TS parses.
        if (toolPresent("tsc")) {
            auto c = topo::platform::runProcessCapture(
                "tsc", {"--noEmit", "--strict", out.string()});
            EXPECT_EQ(c.exitCode, 0)
                << "TypeScript output failed to type-check:\n"
                << c.stdoutOutput << c.stderrOutput << "\nSource:\n" << code;
        } else if (toolPresent("node")) {
            fs::path probe = dir / "probe.ts";
            { std::ofstream(probe) << code << "\nPipe.run();\n"; }
            auto c = topo::platform::runProcessCaptureWithTimeout(
                "node", {"--experimental-strip-types", "--no-warnings",
                         probe.string()}, 10000);
            // strip-types rejects `namespace`; only assert when it ran.
            if (c.exitCode != 0 &&
                c.stderrOutput.find("namespace") == std::string::npos) {
                ADD_FAILURE() << "TypeScript output failed to run:\n"
                              << c.stderrOutput << "\nSource:\n" << code;
            }
        }
    }

    fs::remove_all(dir, ec);
}

TEST(TranspileFromTopoSource, UnresolvedLeafDegradesWithWarningAndExit1) {
    const std::string cli = TOPO_TRANSPILE_CLI_BINARY;
    if (cli.empty() || !fs::exists(cli)) {
        GTEST_SKIP() << "topo-transpile CLI binary not available at '" << cli
                     << "'";
    }

    fs::path dir = fs::temp_directory_path() / "topo-source-m6-degraded";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    fs::path topoFile = dir / "pipe.topo";
    { std::ofstream(topoFile) << kM6FixtureTopo; }

    // No `--adapters`: only the always-on builtin source is assembled, and it
    // has no entry for pipe::produce / refine / consume. Every leaf degrades.
    fs::path outDir = dir / "out";
    auto r = topo::platform::runProcessCaptureWithTimeout(
        cli,
        {"--from", "topo", "--to", "cpp",
         "--output", outDir.string(), topoFile.string()},
        30000);

    // Exit 1: unresolved leaves produce unsupported constructs (cli.md
    // "Exit Codes" — unsupportedCount > 0).
    EXPECT_EQ(r.exitCode, 1)
        << "expected exit 1 for a .topo with unresolvable leaves; got "
        << r.exitCode << "\n" << r.stderrOutput;

    // A stderr `warning:` must name each unresolved leaf.
    for (const char* leaf : {"pipe::produce", "pipe::refine", "pipe::consume"}) {
        EXPECT_NE(r.stderrOutput.find(leaf), std::string::npos)
            << "stderr does not name unresolved leaf '" << leaf << "':\n"
            << r.stderrOutput;
    }
    EXPECT_NE(r.stderrOutput.find("warning:"), std::string::npos)
        << "no warning emitted for unresolved leaves:\n" << r.stderrOutput;

    // The output file is still written, with placeholder bodies.
    fs::path out = outDir / "pipe.cpp";
    ASSERT_TRUE(fs::exists(out)) << "degraded output file not written";
    std::ifstream in(out);
    std::string code((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    EXPECT_NE(code.find("TOPO-TRANSPILE: unsupported"), std::string::npos)
        << "degraded leaf body is missing the unsupported placeholder:\n"
        << code;

    fs::remove_all(dir, ec);
}
