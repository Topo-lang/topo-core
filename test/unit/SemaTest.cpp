#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/SemanticAnalyzer.h"
#include "topo/Sema/SymbolTable.h"
#include "topo/Sema/TypeBinder.h"
#include "topo/Sema/TypeRegistry.h"
#include "topo/Sema/TypeResolver.h"
#include "topo/Sema/VisibilityCollector.h"
#include "topo/Stdlib/Types.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>

using namespace topo;

static std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static std::unique_ptr<TopoFile> parseTopo(const std::string& source, DiagnosticEngine& diag) {
    Lexer lexer(source, "<test>", diag);
    Parser parser(lexer, diag);
    return parser.parseTopoFile();
}

// --- Demo fixture analysis ---

TEST(Sema, DemoTopoAnalysis) {
    std::string source = readFile(TOPO_TEST_FIXTURES_DIR "/demo.topo");
    ASSERT_FALSE(source.empty());

    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    // demo.topo: run, init, process
    EXPECT_EQ(symbols.functions().size(), 3u);
    EXPECT_NE(symbols.findFunction("app::run"), nullptr);
    EXPECT_NE(symbols.findFunction("app::init"), nullptr);
    EXPECT_NE(symbols.findFunction("app::process"), nullptr);
}

// --- Duplicate function declaration ---

TEST(Sema, DuplicateFunctionDecl) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void foo();\n"
        "    void foo();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_TRUE(diag.hasErrors());
}

// --- Orphan fn block ---

TEST(Sema, OrphanFnBlock) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    fn nonexistent {\n"
        "      stage<1> something();\n"
        "    }\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_TRUE(diag.hasErrors());
}

// --- Undeclared operation reference ---

TEST(Sema, UndeclaredOperationReference) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      stage<1> nonexistent();\n"
        "    }\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_TRUE(diag.hasErrors());
}

// --- Stage monotonicity: valid ---

TEST(Sema, StageMonotonicitValid) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      stage<1> a();\n"
        "      stage<1> b();\n"
        "      stage<2> c();\n"
        "    }\n"
        "  protected:\n"
        "    void a();\n"
        "    void b();\n"
        "    void c();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_FALSE(diag.hasErrors());
}

// --- Stage monotonicity: invalid ---

TEST(Sema, StageMonotonicityInvalid) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      stage<2> a();\n"
        "      stage<1> b();\n"
        "    }\n"
        "  protected:\n"
        "    void a();\n"
        "    void b();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_TRUE(diag.hasErrors());
}

// --- Auto-stage assignment ---

TEST(Sema, AutoStageAssignment) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      stage<1> a();\n"
        "      b();\n"
        "      c();\n"
        "    }\n"
        "  protected:\n"
        "    void a();\n"
        "    void b();\n"
        "    void c();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    auto* block = symbols.findLogicBlock("x::run");
    ASSERT_NE(block, nullptr);
    ASSERT_EQ(block->stages.size(), 3u);
    EXPECT_EQ(block->stages[0], 1); // explicit stage<1>
    EXPECT_EQ(block->stages[1], 2); // auto: max+1
    EXPECT_EQ(block->stages[2], 3); // auto: max+2
}

// --- Type validation: built-in types pass ---

TEST(Sema, BuiltinTypesPass) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void foo();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_FALSE(diag.hasErrors());
}

TEST(Sema, BareIntFails) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    int bar();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_TRUE(diag.hasErrors()); // bare int without using
}

TEST(Sema, UsingIntPass) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "using int = std::cpp17::int;\n"
        "using bool = std::cpp17::bool;\n"
        "namespace x {\n"
        "  public:\n"
        "    int bar();\n"
        "    bool baz();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_FALSE(diag.hasErrors());
}

// --- Type validation: std::cpp17 types ---

TEST(Sema, StdCpp17TypesPass) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void foo(std::cpp17::intptr_t val);\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_FALSE(diag.hasErrors());
}

// --- Type validation: unknown type fails ---

TEST(Sema, UnknownTypeFails) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    UnknownType foo();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_TRUE(diag.hasErrors());
}

// --- Type validation: using alias makes type valid ---

TEST(Sema, UsingAliasAllowsType) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "using IntPtr = std::cpp17::intptr_t;\n"
        "namespace x {\n"
        "  public:\n"
        "    IntPtr foo();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_FALSE(diag.hasErrors());
}

// --- TypeResolver unit tests ---

TEST(TypeResolver, BuiltinTypes) {
    SymbolTable symbols;
    TypeResolver resolver(symbols);
    std::string reason;

    // `void` remains the only true built-in (no payload).
    TypeNode voidType;
    voidType.nameParts = {"void"};
    EXPECT_TRUE(resolver.isValidType(voidType, reason));

    // `int` remains a reserved keyword that requires a `using` binding —
    // bare `int` still fails to resolve.
    TypeNode intType;
    intType.nameParts = {"int"};
    EXPECT_FALSE(resolver.isValidType(intType, reason));

    // `bool` is a stdlib bridging type and resolves
    // directly without a `using` binding. Same goes for i64/f64/string.
    TypeNode boolType;
    boolType.nameParts = {"bool"};
    EXPECT_TRUE(resolver.isValidType(boolType, reason));

    TypeNode i64Type;
    i64Type.nameParts = {"i64"};
    EXPECT_TRUE(resolver.isValidType(i64Type, reason));

    TypeNode f64Type;
    f64Type.nameParts = {"f64"};
    EXPECT_TRUE(resolver.isValidType(f64Type, reason));

    TypeNode stringType;
    stringType.nameParts = {"string"};
    EXPECT_TRUE(resolver.isValidType(stringType, reason));
}

TEST(TypeResolver, StdlibGenericArity) {
    SymbolTable symbols;
    TypeResolver resolver(symbols);
    std::string reason;

    // `optional<i64>` is valid.
    TypeNode optI64;
    optI64.nameParts = {"optional"};
    {
        TypeNode inner;
        inner.nameParts = {"i64"};
        optI64.templateArgs.push_back(inner);
    }
    EXPECT_TRUE(resolver.isValidType(optI64, reason));

    // `optional<>` (no T) — arity violation.
    TypeNode optEmpty;
    optEmpty.nameParts = {"optional"};
    EXPECT_FALSE(resolver.isValidType(optEmpty, reason));

    // `slice<UnknownType>` — inner T cannot resolve.
    TypeNode sliceBad;
    sliceBad.nameParts = {"slice"};
    {
        TypeNode bad;
        bad.nameParts = {"UnknownType"};
        sliceBad.templateArgs.push_back(bad);
    }
    EXPECT_FALSE(resolver.isValidType(sliceBad, reason));
}

// Build a single-field record TypeNode helper for the tests below.
static TypeNode makeRecordField(TypeNode& rec, const std::string& name, TypeNode fieldType) {
    TypeNode::RecordField f;
    f.name = name;
    f.typeBox.push_back(std::move(fieldType));
    rec.recordFields.push_back(std::move(f));
    return rec;
}

TEST(TypeResolver, RecordValidAndInvalidFields) {
    SymbolTable symbols;
    TypeResolver resolver(symbols);
    std::string reason;

    auto scalar = [](const char* kw, stdlib::TypeId id) {
        TypeNode t;
        t.nameParts = {kw};
        t.stdlibId = id;
        return t;
    };

    // record<id: i64, price: f64> — all stdlib fields, valid.
    TypeNode rec;
    rec.nameParts = {"record"};
    rec.stdlibId = stdlib::TypeId::Record;
    makeRecordField(rec, "id", scalar("i64", stdlib::TypeId::I64));
    makeRecordField(rec, "price", scalar("f64", stdlib::TypeId::F64));
    EXPECT_TRUE(resolver.isValidType(rec, reason)) << reason;

    // Nested: record<inner: optional<i64>> — recursion accepts composites.
    TypeNode nested;
    nested.nameParts = {"record"};
    nested.stdlibId = stdlib::TypeId::Record;
    TypeNode opt = scalar("optional", stdlib::TypeId::Optional);
    opt.templateArgs.push_back(scalar("i64", stdlib::TypeId::I64));
    makeRecordField(nested, "inner", std::move(opt));
    EXPECT_TRUE(resolver.isValidType(nested, reason)) << reason;

    // Bare user type as a field is rejected.
    TypeNode badField;
    badField.nameParts = {"record"};
    badField.stdlibId = stdlib::TypeId::Record;
    TypeNode userT;
    userT.nameParts = {"UnknownThing"};
    makeRecordField(badField, "x", std::move(userT));
    EXPECT_FALSE(resolver.isValidType(badField, reason));

    // Duplicate field names rejected.
    TypeNode dup;
    dup.nameParts = {"record"};
    dup.stdlibId = stdlib::TypeId::Record;
    makeRecordField(dup, "a", scalar("i64", stdlib::TypeId::I64));
    makeRecordField(dup, "a", scalar("f64", stdlib::TypeId::F64));
    EXPECT_FALSE(resolver.isValidType(dup, reason));

    // Empty record rejected.
    TypeNode empty;
    empty.nameParts = {"record"};
    empty.stdlibId = stdlib::TypeId::Record;
    EXPECT_FALSE(resolver.isValidType(empty, reason));
}

TEST(TypeResolver, UnionValidAndInvalidFields) {
    SymbolTable symbols;
    TypeResolver resolver(symbols);
    std::string reason;

    auto scalar = [](const char* kw, stdlib::TypeId id) {
        TypeNode t;
        t.nameParts = {kw};
        t.stdlibId = id;
        return t;
    };

    // union<tag: u8, a: i64, b: f64> — integer tag + stdlib variants, valid.
    TypeNode uni;
    uni.nameParts = {"union"};
    uni.stdlibId = stdlib::TypeId::Union;
    makeRecordField(uni, "tag", scalar("u8", stdlib::TypeId::U8));
    makeRecordField(uni, "a", scalar("i64", stdlib::TypeId::I64));
    makeRecordField(uni, "b", scalar("f64", stdlib::TypeId::F64));
    EXPECT_TRUE(resolver.isValidType(uni, reason)) << reason;

    // i8/i16/u16 are valid tag types on this base (the stale-worktree
    // implementation wrongly excluded them — this guards the fix).
    TypeNode i16tag;
    i16tag.nameParts = {"union"};
    i16tag.stdlibId = stdlib::TypeId::Union;
    makeRecordField(i16tag, "t", scalar("i16", stdlib::TypeId::I16));
    makeRecordField(i16tag, "v", scalar("i64", stdlib::TypeId::I64));
    EXPECT_TRUE(resolver.isValidType(i16tag, reason)) << reason;

    // Fewer than 2 fields (tag only) rejected.
    TypeNode tagOnly;
    tagOnly.nameParts = {"union"};
    tagOnly.stdlibId = stdlib::TypeId::Union;
    makeRecordField(tagOnly, "tag", scalar("u8", stdlib::TypeId::U8));
    EXPECT_FALSE(resolver.isValidType(tagOnly, reason));

    // Non-integer (float) tag rejected.
    TypeNode floatTag;
    floatTag.nameParts = {"union"};
    floatTag.stdlibId = stdlib::TypeId::Union;
    makeRecordField(floatTag, "kind", scalar("f64", stdlib::TypeId::F64));
    makeRecordField(floatTag, "a", scalar("i64", stdlib::TypeId::I64));
    EXPECT_FALSE(resolver.isValidType(floatTag, reason));

    // Duplicate field names rejected.
    TypeNode dup;
    dup.nameParts = {"union"};
    dup.stdlibId = stdlib::TypeId::Union;
    makeRecordField(dup, "tag", scalar("u8", stdlib::TypeId::U8));
    makeRecordField(dup, "v", scalar("i64", stdlib::TypeId::I64));
    makeRecordField(dup, "v", scalar("f64", stdlib::TypeId::F64));
    EXPECT_FALSE(resolver.isValidType(dup, reason));

    // Bare user type as a variant rejected.
    TypeNode badField;
    badField.nameParts = {"union"};
    badField.stdlibId = stdlib::TypeId::Union;
    makeRecordField(badField, "tag", scalar("u8", stdlib::TypeId::U8));
    TypeNode userT;
    userT.nameParts = {"UnknownThing"};
    makeRecordField(badField, "payload", std::move(userT));
    EXPECT_FALSE(resolver.isValidType(badField, reason));
}

TEST(TypeResolver, BytesScalarShape) {
    SymbolTable symbols;
    TypeResolver resolver(symbols);
    std::string reason;

    // `bytes` is scalar-shaped: valid with no template args.
    TypeNode bytes;
    bytes.nameParts = {"bytes"};
    bytes.stdlibId = stdlib::TypeId::Bytes;
    EXPECT_TRUE(resolver.isValidType(bytes, reason)) << reason;

    // `bytes<i64>` — arity 0, rejected by the generic arity check
    // (same precedent as the width-extension scalars).
    TypeNode bytesArg;
    bytesArg.nameParts = {"bytes"};
    bytesArg.stdlibId = stdlib::TypeId::Bytes;
    TypeNode inner;
    inner.nameParts = {"i64"};
    bytesArg.templateArgs.push_back(inner);
    EXPECT_FALSE(resolver.isValidType(bytesArg, reason));

    // Table contract: slice<u8>-isomorphic 16B/align8, arity 0.
    EXPECT_EQ(stdlib::fromKeyword("bytes"), stdlib::TypeId::Bytes);
    EXPECT_STREQ(stdlib::keywordOf(stdlib::TypeId::Bytes), "bytes");
    EXPECT_EQ(stdlib::typeParamArity(stdlib::TypeId::Bytes), 0u);
    EXPECT_EQ(stdlib::layoutOf(stdlib::TypeId::Bytes).size, 16u);
    EXPECT_EQ(stdlib::layoutOf(stdlib::TypeId::Bytes).align, 8u);
}

TEST(TypeResolver, ArrayElementAndCount) {
    SymbolTable symbols;
    TypeResolver resolver(symbols);
    std::string reason;

    auto typeArg = [](const char* kw, stdlib::TypeId id) {
        TypeNode t;
        t.nameParts = {kw};
        t.stdlibId = id;
        return t;
    };
    auto intArg = [](int n) {
        TypeNode t;
        t.nameParts = {std::to_string(n)};
        t.nonTypeValue = n;
        return t;
    };

    // array<i64, 4> — valid.
    TypeNode arr;
    arr.nameParts = {"array"};
    arr.stdlibId = stdlib::TypeId::Array;
    arr.templateArgs.push_back(typeArg("i64", stdlib::TypeId::I64));
    arr.templateArgs.push_back(intArg(4));
    EXPECT_TRUE(resolver.isValidType(arr, reason)) << reason;

    // array<record<a: i64>, 2> — nested composite element recurses.
    TypeNode nested;
    nested.nameParts = {"array"};
    nested.stdlibId = stdlib::TypeId::Array;
    TypeNode rec;
    rec.nameParts = {"record"};
    rec.stdlibId = stdlib::TypeId::Record;
    makeRecordField(rec, "a", typeArg("i64", stdlib::TypeId::I64));
    nested.templateArgs.push_back(std::move(rec));
    nested.templateArgs.push_back(intArg(2));
    EXPECT_TRUE(resolver.isValidType(nested, reason)) << reason;

    // Wrong argument count (only T, no N).
    TypeNode oneArg;
    oneArg.nameParts = {"array"};
    oneArg.stdlibId = stdlib::TypeId::Array;
    oneArg.templateArgs.push_back(typeArg("i64", stdlib::TypeId::I64));
    EXPECT_FALSE(resolver.isValidType(oneArg, reason));

    // N is a type, not an integer literal: array<i64, i64>.
    TypeNode nNotInt;
    nNotInt.nameParts = {"array"};
    nNotInt.stdlibId = stdlib::TypeId::Array;
    nNotInt.templateArgs.push_back(typeArg("i64", stdlib::TypeId::I64));
    nNotInt.templateArgs.push_back(typeArg("i64", stdlib::TypeId::I64));
    EXPECT_FALSE(resolver.isValidType(nNotInt, reason));

    // N < 1: array<i64, 0>.
    TypeNode zeroN;
    zeroN.nameParts = {"array"};
    zeroN.stdlibId = stdlib::TypeId::Array;
    zeroN.templateArgs.push_back(typeArg("i64", stdlib::TypeId::I64));
    zeroN.templateArgs.push_back(intArg(0));
    EXPECT_FALSE(resolver.isValidType(zeroN, reason));

    // Bad element type: array<UnknownType, 4>.
    TypeNode badElem;
    badElem.nameParts = {"array"};
    badElem.stdlibId = stdlib::TypeId::Array;
    TypeNode bad;
    bad.nameParts = {"UnknownType"};
    badElem.templateArgs.push_back(std::move(bad));
    badElem.templateArgs.push_back(intArg(4));
    EXPECT_FALSE(resolver.isValidType(badElem, reason));

    // First argument an integer where a type is expected: array<4, 4>.
    TypeNode intFirst;
    intFirst.nameParts = {"array"};
    intFirst.stdlibId = stdlib::TypeId::Array;
    intFirst.templateArgs.push_back(intArg(4));
    intFirst.templateArgs.push_back(intArg(4));
    EXPECT_FALSE(resolver.isValidType(intFirst, reason));

    // Table contract: caller-computes sentinel, arity 0 (special-cased).
    EXPECT_EQ(stdlib::fromKeyword("array"), stdlib::TypeId::Array);
    EXPECT_STREQ(stdlib::keywordOf(stdlib::TypeId::Array), "array");
    EXPECT_EQ(stdlib::layoutOf(stdlib::TypeId::Array).size, 0u);
}

// Locked design: natural alignment + explicit padding field, no `align`
// keyword. The expected values below are hand-calculated.
TEST(StdlibLayout, ComposeRecordLayoutMatchesHandCalculation) {
    using namespace stdlib;

    // record<a: u8, b: i64>:
    //   a @0 size1; b needs align8 -> @8 size8 -> offset 16;
    //   max align 8 -> total 16.
    EXPECT_EQ(composeRecordLayout({{1, 1}, {8, 8}}).size, 16u);
    EXPECT_EQ(composeRecordLayout({{1, 1}, {8, 8}}).align, 8u);

    // record<a: u8, pad: u8 ... explicit padding> tightens nothing here but
    // proves a user padding field is just a regular field:
    // record<flag: u8, n: u32>: flag@0; n align4 -> @4 size4 -> 8;
    //   align 4 -> total 8.
    EXPECT_EQ(composeRecordLayout({{1, 1}, {4, 4}}).size, 8u);
    EXPECT_EQ(composeRecordLayout({{1, 1}, {4, 4}}).align, 4u);

    // All-8-aligned, no padding inserted: i64,f64 -> 16/8.
    EXPECT_EQ(composeRecordLayout({{8, 8}, {8, 8}}).size, 16u);
    EXPECT_EQ(composeRecordLayout({{8, 8}, {8, 8}}).align, 8u);

    // string (16,8) + slice (16,8): 32/8.
    EXPECT_EQ(composeRecordLayout({{16, 8}, {16, 8}}).size, 32u);

    // Tail padding to satisfy struct align: record<n: i64, flag: u8>:
    //   n@0 size8; flag@8 size1 -> offset 9; align 8 -> total 16.
    EXPECT_EQ(composeRecordLayout({{8, 8}, {1, 1}}).size, 16u);
    EXPECT_EQ(composeRecordLayout({{8, 8}, {1, 1}}).align, 8u);

    // Zero-field record still occupies one byte.
    EXPECT_EQ(composeRecordLayout({}).size, 1u);
    EXPECT_EQ(composeRecordLayout({}).align, 1u);
}

// Tagged union: tag at offset 0, variants share one storage region sized
// for the largest variant (NOT the field sum). Hand-calculated below.
TEST(StdlibLayout, ComposeUnionLayoutMatchesHandCalculation) {
    using namespace stdlib;

    // union<tag: u8, a: i64, b: f64>:
    //   tag@0 size1; variant storage needs align8 -> @8; max variant
    //   size 8; align 8 -> total align_up(8+8,8)=16. The two 8-byte
    //   variants OVERLAP — size is 16, not tag + 8 + 8.
    EXPECT_EQ(composeUnionLayout({1, 1}, {{8, 8}, {8, 8}}).size, 16u);
    EXPECT_EQ(composeUnionLayout({1, 1}, {{8, 8}, {8, 8}}).align, 8u);

    // union<tag: u8, x: u8, y: i16>:
    //   tag@0; variant align2 -> @2; max variant size 2; align 2 ->
    //   total align_up(2+2,2)=4.
    EXPECT_EQ(composeUnionLayout({1, 1}, {{1, 1}, {2, 2}}).size, 4u);
    EXPECT_EQ(composeUnionLayout({1, 1}, {{1, 1}, {2, 2}}).align, 2u);

    // Tag wider than variants: union<tag: i32, v: i64>:
    //   tag size4; variant align8 -> @8; size8; agg align 8 ->
    //   align_up(8+8,8)=16.
    EXPECT_EQ(composeUnionLayout({4, 4}, {{8, 8}}).size, 16u);
    EXPECT_EQ(composeUnionLayout({4, 4}, {{8, 8}}).align, 8u);

    // Single large variant: union<tag: u8, s: string{16,8}>:
    //   tag@0; storage@8; size16 -> align_up(8+16,8)=24.
    EXPECT_EQ(composeUnionLayout({1, 1}, {{16, 8}}).size, 24u);

    // Defensive: a variant-less union (Sema rejects upstream) still yields
    // a well-formed non-zero aggregate.
    EXPECT_EQ(composeUnionLayout({1, 1}, {}).size, 1u);
    EXPECT_EQ(composeUnionLayout({1, 1}, {}).align, 1u);
}

TEST(TypeResolver, StdCpp17Types) {
    SymbolTable symbols;
    TypeResolver resolver(symbols);
    std::string reason;

    TypeNode t;
    t.nameParts = {"std", "cpp17", "intptr_t"};
    EXPECT_TRUE(resolver.isValidType(t, reason));

    t.nameParts = {"std", "cpp17", "size_t"};
    EXPECT_TRUE(resolver.isValidType(t, reason));

    t.nameParts = {"std", "cpp17", "uint64_t"};
    EXPECT_TRUE(resolver.isValidType(t, reason));
}

TEST(TypeResolver, UnknownStdCpp17Type) {
    SymbolTable symbols;
    TypeResolver resolver(symbols);
    std::string reason;

    TypeNode t;
    t.nameParts = {"std", "cpp17", "nonexistent_t"};
    EXPECT_FALSE(resolver.isValidType(t, reason));
    EXPECT_FALSE(reason.empty());
}

TEST(TypeResolver, UsingAlias) {
    SymbolTable symbols;
    TypeAliasEntry alias{"MyInt", {}, {}};
    alias.targetType.nameParts = {"int"};
    symbols.addTypeAlias(alias);

    TypeResolver resolver(symbols);
    std::string reason;

    TypeNode t;
    t.nameParts = {"MyInt"};
    EXPECT_TRUE(resolver.isValidType(t, reason));
}

TEST(TypeResolver, EmptyType) {
    SymbolTable symbols;
    TypeResolver resolver(symbols);
    std::string reason;

    TypeNode t;
    EXPECT_FALSE(resolver.isValidType(t, reason));
}

// --- Assignment operation tests ---

TEST(Sema, AssignmentInFnBlock) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    void init();\n"
        "    fn run {\n"
        "      stage<1> init();\n"
        "      result = 42;\n"
        "    }\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    auto* block = symbols.findLogicBlock("x::run");
    ASSERT_NE(block, nullptr);
    ASSERT_EQ(block->stages.size(), 2u);
    // First is explicit stage<1>, second is auto-assigned
    EXPECT_EQ(block->stages[0], 1);
    EXPECT_GT(block->stages[1], 1);
}

// --- std::import type validation ---

TEST(Sema, StdImportMakesTypeValid) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "std::import(\"cstdint\", uint64_t);\n"
        "namespace x {\n"
        "  public:\n"
        "    uint64_t getValue();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_FALSE(diag.hasErrors());
}

TEST(Sema, StdImportRecorded) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "std::import(\"cstdint\", uint64_t);\n"
        "std::import(\"string\");\n"
        "namespace x {\n"
        "  public:\n"
        "    void foo();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    EXPECT_EQ(symbols.imports().size(), 2u);
    EXPECT_TRUE(symbols.isImportedType("uint64_t"));
}

TEST(TypeResolver, ImportedType) {
    SymbolTable symbols;
    ImportEntry imp{"cstdint", "uint64_t", {}};
    symbols.addImport(imp);

    TypeResolver resolver(symbols);
    std::string reason;

    TypeNode t;
    t.nameParts = {"uint64_t"};
    EXPECT_TRUE(resolver.isValidType(t, reason));
}

// =============================================================
// Multi-return semantic tests
// =============================================================

TEST(Sema, MultiReturnBasic) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "using int = std::cpp17::int;\n"
        "namespace x {\n"
        "  public:\n"
        "    compute(int a) -> (int sum, int product);\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    auto* fn = symbols.findFunction("x::compute");
    ASSERT_NE(fn, nullptr);
    EXPECT_TRUE(fn->isMultiReturn);
    ASSERT_EQ(fn->returnParams.size(), 2u);
    EXPECT_EQ(fn->returnParams[0].name, "sum");
    EXPECT_EQ(fn->returnParams[1].name, "product");
}

TEST(Sema, MultiReturnDuplicateParamName) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "using int = std::cpp17::int;\n"
        "using bool = std::cpp17::bool;\n"
        "namespace x {\n"
        "  public:\n"
        "    compute() -> (int val, bool val);\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_TRUE(diag.hasErrors()); // duplicate "val"
}

TEST(Sema, MultiReturnTooFewParams) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "using int = std::cpp17::int;\n"
        "namespace x {\n"
        "  public:\n"
        "    compute() -> (int only);\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_TRUE(diag.hasErrors()); // needs >= 2
}

// --- `with returns(...)` ceiling propagation ---

TEST(Sema, WithReturnsPopulatesSymbolCeiling) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "using int = std::cpp17::int;\n"
        "namespace x {\n"
        "  public:\n"
        "    compute(int a) -> (int p, int q, int r) with returns(p, _, _);\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    auto* fn = symbols.findFunction("x::compute");
    ASSERT_NE(fn, nullptr);
    EXPECT_TRUE(fn->isMultiReturn);
    EXPECT_TRUE(fn->hasUsedReturnsClause);
    EXPECT_EQ(fn->usedReturns.size(), 1u);
    EXPECT_TRUE(fn->usedReturns.count("p"));
    EXPECT_FALSE(fn->usedReturns.count("q"));
}

TEST(Sema, WithReturnsUnknownNameErrors) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "using int = std::cpp17::int;\n"
        "namespace x {\n"
        "  public:\n"
        "    compute(int a) -> (int p, int q) with returns(p, bogus);\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_TRUE(diag.hasErrors());
}

TEST(Sema, WithReturnsClampsPipelineDemand) {
    // consume takes (int sum) but compute declares only `sum` observable
    // via with-returns.  The resulting CallSiteInfo usedReturns must be
    // clamped to the declared ceiling (still {sum} here — a degenerate
    // confirmation that declaration-level clamping preserves demand that
    // already fit).
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "using int = std::cpp17::int;\n"
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    compute(int a) -> (int sum, int product) with returns(sum, _);\n"
        "    void consume(int sum);\n"
        "    fn run {\n"
        "      compute -> consume;\n"
        "      consume -> void;\n"
        "    }\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    bool foundCompute = false;
    for (const auto& cs : symbols.callSites()) {
        if (cs.callee == "x::compute") {
            foundCompute = true;
            // Ceiling forbids product; pipeline demand was also sum, so
            // usedReturns == {sum}.
            EXPECT_TRUE(cs.usedReturns.count("sum"));
            EXPECT_FALSE(cs.usedReturns.count("product"));
        }
    }
    EXPECT_TRUE(foundCompute);
}

TEST(Sema, WithReturnsSeedsCallSiteWhenNoPipelineDemand) {
    // Here `compute` has no pipeline edges feeding its returns (terminal
    // node), so pipeline demand would otherwise produce Full style.  The
    // declared ceiling should still narrow usedReturns to the ceiling.
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "using int = std::cpp17::int;\n"
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    compute() -> (int sum, int product) with returns(sum, _);\n"
        "    void source();\n"
        "    fn run {\n"
        "      source -> compute;\n"
        "      compute -> void;\n"
        "    }\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    // compute is the terminal — pipeline demand marks all return fields
    // as demanded; ceiling then clamps to {sum}.
    for (const auto& cs : symbols.callSites()) {
        if (cs.callee == "x::compute") {
            EXPECT_TRUE(cs.usedReturns.count("sum"));
            EXPECT_FALSE(cs.usedReturns.count("product"));
        }
    }
}

// =============================================================
// Pipeline semantic tests
// =============================================================

TEST(Sema, PipelineBasicDAG) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      a -> b;\n"
        "      b -> void;\n"
        "    }\n"
        "  protected:\n"
        "    void a();\n"
        "    void b();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    auto* block = symbols.findLogicBlock("x::run");
    ASSERT_NE(block, nullptr);
    EXPECT_TRUE(block->isPipeline);
    ASSERT_TRUE(block->pipelineAnalysis.has_value());
    EXPECT_EQ(block->pipelineAnalysis->terminalNode, "b");
    EXPECT_EQ(block->pipelineAnalysis->terminalType, "void");
    EXPECT_EQ(block->pipelineAnalysis->stages.at("a"), 0);
    EXPECT_EQ(block->pipelineAnalysis->stages.at("b"), 1);
}

TEST(Sema, PipelineCycleDetection) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      a -> b;\n"
        "      b -> a;\n"
        "      a -> void;\n"
        "    }\n"
        "  protected:\n"
        "    void a();\n"
        "    void b();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_TRUE(diag.hasErrors()); // cycle
}

TEST(Sema, PipelineNoTerminal) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      a -> b;\n"
        "      b -> c;\n"
        "    }\n"
        "  protected:\n"
        "    void a();\n"
        "    void b();\n"
        "    void c();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_TRUE(diag.hasErrors()); // no terminal
}

TEST(Sema, PipelineMultipleTerminals) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      a -> void;\n"
        "      b -> void;\n"
        "    }\n"
        "  protected:\n"
        "    void a();\n"
        "    void b();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_TRUE(diag.hasErrors()); // multiple terminals
}

TEST(Sema, PipelineUndeclaredNode) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      a -> ghost;\n"
        "      ghost -> void;\n"
        "    }\n"
        "  protected:\n"
        "    void a();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_TRUE(diag.hasErrors()); // 'ghost' undeclared
}

TEST(Sema, PipelineStageInference) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      a -> b;\n"
        "      a -> c;\n"
        "      b -> d;\n"
        "      c -> d;\n"
        "      d -> void;\n"
        "    }\n"
        "  protected:\n"
        "    void a();\n"
        "    void b();\n"
        "    void c();\n"
        "    void d();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    auto* block = symbols.findLogicBlock("x::run");
    ASSERT_NE(block, nullptr);
    ASSERT_TRUE(block->pipelineAnalysis.has_value());
    auto& stages = block->pipelineAnalysis->stages;
    EXPECT_EQ(stages.at("a"), 0);
    EXPECT_EQ(stages.at("b"), 1);
    EXPECT_EQ(stages.at("c"), 1);
    EXPECT_EQ(stages.at("d"), 2); // max(b=1, c=1) + 1
}

TEST(Sema, PipelineFixture) {
    std::string source = readFile(TOPO_TEST_FIXTURES_DIR "/pipeline.topo");
    ASSERT_FALSE(source.empty());

    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    auto* block = symbols.findLogicBlock("pipeline::run");
    ASSERT_NE(block, nullptr);
    EXPECT_TRUE(block->isPipeline);
    ASSERT_TRUE(block->pipelineAnalysis.has_value());
    EXPECT_EQ(block->pipelineAnalysis->terminalType, "void");
}

// =============================================================
// Internal visibility semantic tests
// =============================================================

TEST(Sema, InternalSameFileAccess) {
    // Same file: fn block can reference internal functions
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    fn run {\n"
        "      stage<1> init();\n"
        "    }\n"
        "  internal:\n"
        "    void init();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    auto* fn = symbols.findFunction("x::init");
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->visibility, Visibility::Internal);
}

TEST(Sema, InternalCrossFileError) {
    // Cross-file: referencing an internal function should produce an error
    // First, build the "imported" symbol table with internal function
    DiagnosticEngine importDiag;
    auto importAst = parseTopo(
        "namespace lib {\n"
        "  internal:\n"
        "    void secret();\n"
        "}",
        importDiag);
    ASSERT_FALSE(importDiag.hasErrors());

    SemanticAnalyzer importSema(importDiag);
    auto importedSymbols = importSema.analyze(static_cast<const TopoFile&>(*importAst));
    ASSERT_FALSE(importDiag.hasErrors());

    // Verify the imported symbol is internal
    auto* secretFn = importedSymbols.findFunction("lib::secret");
    ASSERT_NE(secretFn, nullptr);
    EXPECT_EQ(secretFn->visibility, Visibility::Internal);

    // Now analyze a file that tries to reference it
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace caller {\n"
        "  public:\n"
        "    void doWork();\n"
        "    fn doWork {\n"
        "      secret();\n"
        "    }\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast), importedSymbols);
    // Should report error: secret is internal and cannot be accessed
    EXPECT_TRUE(diag.hasErrors());
}

TEST(Sema, InternalNamespaceForceVisibility) {
    // internal namespace forces all members to Internal visibility
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "internal namespace detail {\n"
        "  public:\n"
        "    void helper();\n"
        "  protected:\n"
        "    void worker();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    // Both functions should be Internal regardless of their section visibility
    auto* helper = symbols.findFunction("detail::helper");
    ASSERT_NE(helper, nullptr);
    EXPECT_EQ(helper->visibility, Visibility::Internal);

    auto* worker = symbols.findFunction("detail::worker");
    ASSERT_NE(worker, nullptr);
    EXPECT_EQ(worker->visibility, Visibility::Internal);
}

TEST(Sema, InternalBasicFixture) {
    std::string source = readFile(TOPO_TEST_FIXTURES_DIR "/internal_basic.topo");
    ASSERT_FALSE(source.empty());

    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    // init and process should be internal
    auto* init = symbols.findFunction("engine::init");
    ASSERT_NE(init, nullptr);
    EXPECT_EQ(init->visibility, Visibility::Internal);

    auto* process = symbols.findFunction("engine::process");
    ASSERT_NE(process, nullptr);
    EXPECT_EQ(process->visibility, Visibility::Internal);

    // run should be public
    auto* run = symbols.findFunction("engine::run");
    ASSERT_NE(run, nullptr);
    EXPECT_EQ(run->visibility, Visibility::Public);
}

// --- Class declaration semantic analysis ---

TEST(Sema, ClassSymbolCollection) {
    std::string source = R"(
using Int = std::cpp17::int32_t;
namespace math {
    public:
        class Vector {
            public:
                void push_back(Int value);
                Int size() const;
            private:
                Int data;
        }
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors()) << "Parse errors";

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(*ast);
    ASSERT_FALSE(diag.hasErrors());

    // Class should be registered
    auto* cls = symbols.findClassSymbol("math::Vector");
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->simpleName, "Vector");
    EXPECT_EQ(cls->visibility, Visibility::Public);

    // Member functions should be registered as functions
    auto* pushBack = symbols.findFunction("math::Vector::push_back");
    ASSERT_NE(pushBack, nullptr);
    EXPECT_EQ(pushBack->params.size(), 1u);

    auto* sizeFunc = symbols.findFunction("math::Vector::size");
    ASSERT_NE(sizeFunc, nullptr);
    EXPECT_TRUE(sizeFunc->isConst);

    // Member vars should be collected
    EXPECT_EQ(cls->memberVars.size(), 1u);
    EXPECT_EQ(cls->memberVars[0].name, "data");
}

TEST(Sema, ClassInheritanceValidation) {
    std::string source = R"(
using Int = std::cpp17::int32_t;
namespace shapes {
    public:
        class Shape {
            public:
                Int area() const;
        }
        class Circle : public Shape {
            public:
                Int radius() const;
        }
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(*ast);
    ASSERT_FALSE(diag.hasErrors());

    auto* circle = symbols.findClassSymbol("shapes::Circle");
    ASSERT_NE(circle, nullptr);
    ASSERT_TRUE(circle->baseClass.has_value());
    EXPECT_EQ(circle->baseClass->nameParts[0], "Shape");
}

TEST(Sema, ClassTypeResolution) {
    // Class names should be usable as types
    std::string source = R"(
using Int = std::cpp17::int32_t;
namespace container {
    public:
        class Item {
            public:
                Int value() const;
        }
        void process(Item item);
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(*ast);
    // "Item" should be recognized as a valid type via class declaration
    EXPECT_FALSE(diag.hasErrors());

    auto* process = symbols.findFunction("container::process");
    ASSERT_NE(process, nullptr);
    EXPECT_EQ(process->params[0].type.nameParts[0], "Item");
}

TEST(Sema, ClassConstructorDestructor) {
    std::string source = R"(
using Int = std::cpp17::int32_t;
namespace math {
    public:
        class Matrix {
            public:
                Matrix(Int rows, Int cols);
                explicit Matrix(Int size);
                ~Matrix();
        }
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(*ast);
    ASSERT_FALSE(diag.hasErrors());

    auto* cls = symbols.findClassSymbol("math::Matrix");
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->constructors.size(), 2u);
    EXPECT_FALSE(cls->destructor.empty());
}

// --- Template function semantic analysis ---

TEST(Sema, TemplateParamAsValidType) {
    std::string source = R"(
namespace math {
    public:
        template<typename T>
        T normalize(T x, T y);
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(*ast);
    // T should be accepted as a valid type for template functions
    EXPECT_FALSE(diag.hasErrors());

    auto* fn = symbols.findFunction("math::normalize");
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->templateParams.size(), 1u);
    EXPECT_EQ(fn->templateParams[0].name, "T");
}

TEST(Sema, DuplicateTemplateParam) {
    std::string source = R"(
namespace math {
    public:
        template<typename T, typename T>
        void bad(T x);
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(*ast);
    // Should report T003: duplicate template parameter name
    EXPECT_TRUE(diag.hasErrors());
}

TEST(Sema, ClassTemplateParamAsType) {
    std::string source = R"(
namespace container {
    public:
        template<typename T>
        class Vector {
            public:
                void push_back(T value);
                T front() const;
            private:
                T data;
        }
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(*ast);
    // T should be valid inside the class template
    EXPECT_FALSE(diag.hasErrors());

    auto* cls = symbols.findClassSymbol("container::Vector");
    ASSERT_NE(cls, nullptr);
    EXPECT_FALSE(cls->templateParams.empty());
}

TEST(Sema, TemplateArgCountMismatch) {
    std::string source = R"(
namespace container {
    public:
        template<typename T>
        class Vector {
            public:
                void push_back(T value);
        }

        // Wrong: Vector expects 1 arg, got 2
        void process(Vector<Int, Double> data);

    using Int = std::cpp17::int32_t;
    using Double = std::cpp17::double;
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    SemanticAnalyzer sema(diag);
    sema.analyze(*ast);
    // Should report T002: template argument count mismatch
    EXPECT_TRUE(diag.hasErrors());
}

TEST(Sema, TemplateArgCountCorrect) {
    std::string source = R"(
namespace container {
    public:
        template<typename K, typename V>
        class HashMap {
            public:
                void insert(K key, V value);
        }

        void process(HashMap<Int, Double> data);

    using Int = std::cpp17::int32_t;
    using Double = std::cpp17::double;
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    SemanticAnalyzer sema(diag);
    sema.analyze(*ast);
    // Correct: HashMap expects 2 args, got 2
    EXPECT_FALSE(diag.hasErrors());
}

TEST(Sema, CRTPBaseClassValid) {
    std::string source = R"(
namespace pattern {
    public:
        template<typename T>
        class Base {
            public:
                void interface_method();
        }

        class Impl : public Base<Impl> {
            public:
                void implementation();
        }
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors()) << "Parse should succeed";

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(*ast);
    // Base<Impl> should be valid — 1 arg matches Base's 1 param
    EXPECT_FALSE(diag.hasErrors());

    auto* impl = symbols.findClassSymbol("pattern::Impl");
    ASSERT_NE(impl, nullptr);
    ASSERT_TRUE(impl->baseClass.has_value());
    EXPECT_EQ(impl->baseClass->nameParts[0], "Base");
    EXPECT_EQ(impl->baseClass->templateArgs.size(), 1u);
    EXPECT_EQ(impl->baseClass->templateArgs[0].nameParts[0], "Impl");
}

TEST(Sema, ConstraintSymbolCollection) {
    std::string source = R"(
namespace math {
    public:
        constraint Numeric {
            T add(T a, T b);
            T zero;
        }
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(*ast);
    EXPECT_FALSE(diag.hasErrors());

    auto* cs = symbols.findConstraintSymbol("math::Numeric");
    ASSERT_NE(cs, nullptr);
    EXPECT_EQ(cs->simpleName, "Numeric");
    EXPECT_EQ(cs->members.size(), 2u);
}

TEST(Sema, ConstraintParentNotFound) {
    std::string source = R"(
namespace math {
    public:
        constraint Ordered : NonExistentConstraint {
            Bool less_than(T a, T b);
        }
    using Bool = std::cpp17::bool;
    using T = std::cpp17::int32_t;
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(*ast);
    // Should report error: parent constraint not found
    EXPECT_TRUE(diag.hasErrors());
}

TEST(Sema, ConstrainedParamValidation) {
    std::string source = R"(
namespace math {
    public:
        constraint Numeric {
            T add(T a, T b);
        }

        template<typename T : Numeric>
        T normalize(T x, T y);
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(*ast);
    // Numeric constraint exists, so no error
    EXPECT_FALSE(diag.hasErrors());
}

TEST(Sema, UnknownConstraintOnParam) {
    std::string source = R"(
namespace math {
    public:
        template<typename T : FakeConstraint>
        T normalize(T x, T y);
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(*ast);
    // Should report T001: unknown constraint
    EXPECT_TRUE(diag.hasErrors());
}

TEST(Sema, ClassMemberVisibilityCollection) {
    std::string source = R"(
namespace engine {
    public:
        class Renderer {
            public:
                void draw();
                void present();
            private:
                void internal_setup();
        }
}
)";
    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(*ast);
    EXPECT_FALSE(diag.hasErrors());

    // draw and present should be registered as public functions
    auto* draw = symbols.findFunction("engine::Renderer::draw");
    ASSERT_NE(draw, nullptr);
    EXPECT_EQ(draw->visibility, Visibility::Public);

    auto* present = symbols.findFunction("engine::Renderer::present");
    ASSERT_NE(present, nullptr);
    EXPECT_EQ(present->visibility, Visibility::Public);

    // internal_setup should be private
    auto* setup = symbols.findFunction("engine::Renderer::internal_setup");
    ASSERT_NE(setup, nullptr);
    EXPECT_EQ(setup->visibility, Visibility::Private);
}

// --- Abstract type names ---

TEST(TypeRegistry, ClassifyAbstractName) {
    EXPECT_EQ(TypeRegistry::classifyAbstractName("integer"), LogicalTypeKind::Integer);
    EXPECT_EQ(TypeRegistry::classifyAbstractName("unsigned"), LogicalTypeKind::UnsignedInteger);
    EXPECT_EQ(TypeRegistry::classifyAbstractName("floating"), LogicalTypeKind::Floating);
    EXPECT_EQ(TypeRegistry::classifyAbstractName("boolean"), LogicalTypeKind::Boolean);
    EXPECT_EQ(TypeRegistry::classifyAbstractName("text"), LogicalTypeKind::Text);
    EXPECT_FALSE(TypeRegistry::classifyAbstractName("int").has_value());
    EXPECT_FALSE(TypeRegistry::classifyAbstractName("string").has_value());
}

TEST(TypeResolver, AcceptsAbstractTypeNames) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace app {\n"
        "  public:\n"
        "    integer compute(floating x);\n"
        "    boolean check(text name);\n"
        "    unsigned count();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    auto* compute = symbols.findFunction("app::compute");
    ASSERT_NE(compute, nullptr);
    EXPECT_EQ(compute->returnType.nameParts.back(), "integer");
    EXPECT_EQ(compute->params[0].type.nameParts.back(), "floating");

    auto* check = symbols.findFunction("app::check");
    ASSERT_NE(check, nullptr);
    EXPECT_EQ(check->returnType.nameParts.back(), "boolean");
}

// --- TypeBinder ---

TEST(TypeBinder, DefaultCppBindings) {
    auto binder = TypeBinder::createDefault(HostLanguage::Cpp);
    EXPECT_EQ(binder.resolve("integer"), "int32_t");
    EXPECT_EQ(binder.resolve("unsigned"), "uint32_t");
    EXPECT_EQ(binder.resolve("floating"), "double");
    EXPECT_EQ(binder.resolve("boolean"), "bool");
    EXPECT_EQ(binder.resolve("text"), "std::string");
    EXPECT_FALSE(binder.resolve("int").has_value());
}

TEST(TypeBinder, DefaultRustBindings) {
    auto binder = TypeBinder::createDefault(HostLanguage::Rust);
    EXPECT_EQ(binder.resolve("integer"), "i32");
    EXPECT_EQ(binder.resolve("unsigned"), "u32");
    EXPECT_EQ(binder.resolve("floating"), "f64");
    EXPECT_EQ(binder.resolve("boolean"), "bool");
    EXPECT_EQ(binder.resolve("text"), "String");
}

TEST(TypeBinder, UserOverride) {
    auto binder = TypeBinder::createDefault(HostLanguage::Cpp);
    EXPECT_TRUE(binder.addBindingByName("integer", "int64_t"));
    EXPECT_EQ(binder.resolve("integer"), "int64_t");
    EXPECT_FALSE(binder.addBindingByName("nonexistent", "foo"));
}

// --- Type adapter (adapt for abstract type) ---

TEST(Sema, TypeAdapterValid) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace app {\n"
        "  public:\n"
        "    void compute();\n"
        "    adapt integer for CustomInt {\n"
        "        from = custom_from;\n"
        "        to = custom_to;\n"
        "    }\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    ASSERT_EQ(symbols.adapts().size(), 1u);
    EXPECT_EQ(symbols.adapts()[0].constraintName, "integer");
    EXPECT_EQ(symbols.adapts()[0].targetType.toString(), "CustomInt");
}

TEST(Sema, TypeAdapterMissingFrom) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace app {\n"
        "  public:\n"
        "    adapt integer for CustomInt {\n"
        "        to = custom_to;\n"
        "    }\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    EXPECT_TRUE(diag.hasErrors());
}

// --- Binding target in VisibilityCollector ---

TEST(VisibilityCollector, BindingTargetExtracted) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace app {\n"
        "  public:\n"
        "    void sort(int* data, int n) = std::sort;\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    VisibilityCollector collector;
    auto entries = collector.collect(static_cast<const TopoFile&>(*ast));
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].qualifiedName, "app::sort");
    ASSERT_TRUE(entries[0].bindingTarget.has_value());
    EXPECT_EQ(*entries[0].bindingTarget, "std::sort");
}

TEST(VisibilityCollector, NoBindingTarget) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace app {\n"
        "  public:\n"
        "    void run();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    VisibilityCollector collector;
    auto entries = collector.collect(static_cast<const TopoFile&>(*ast));
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_FALSE(entries[0].bindingTarget.has_value());
}

// =============================================================
// Multi-return: CallSiteInfo with usedReturns (pipeline demand)
// =============================================================

TEST(Sema, MultiReturnCallSiteUsedReturns) {
    // Pipeline where a multi-return node feeds a downstream node.
    // Demand analysis propagates usedReturns through CallSiteInfo.
    // consume takes 'sum' param — name-matches compute's return param.
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "using int = std::cpp17::int;\n"
        "namespace x {\n"
        "  public:\n"
        "    void run();\n"
        "    compute(int a) -> (int sum, int product);\n"
        "    void consume(int sum);\n"
        "    fn run {\n"
        "      compute -> consume;\n"
        "      consume -> void;\n"
        "    }\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    // CallSiteInfo should record demand: compute only needs to produce 'sum'
    bool foundCompute = false;
    for (const auto& cs : symbols.callSites()) {
        if (cs.callee == "x::compute") {
            foundCompute = true;
            EXPECT_EQ(cs.style, CallSiteInfo::SmartPass);
            EXPECT_TRUE(cs.usedReturns.count("sum"));
            // 'product' is not demanded by consume
            EXPECT_FALSE(cs.usedReturns.count("product"));
        }
    }
    EXPECT_TRUE(foundCompute);
}

TEST(Sema, MultiReturnFixture) {
    std::string source = readFile(TOPO_TEST_FIXTURES_DIR "/multi_return.topo");
    ASSERT_FALSE(source.empty());

    DiagnosticEngine diag;
    auto ast = parseTopo(source, diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    // compute: multi-return with 2 params
    auto* compute = symbols.findFunction("signal::compute");
    ASSERT_NE(compute, nullptr);
    EXPECT_TRUE(compute->isMultiReturn);
    ASSERT_EQ(compute->returnParams.size(), 2u);
    EXPECT_EQ(compute->returnParams[0].name, "sum");
    EXPECT_EQ(compute->returnParams[1].name, "product");

    // analyze: multi-return with 2 params
    auto* analyze = symbols.findFunction("signal::analyze");
    ASSERT_NE(analyze, nullptr);
    EXPECT_TRUE(analyze->isMultiReturn);
    ASSERT_EQ(analyze->returnParams.size(), 2u);
    EXPECT_EQ(analyze->returnParams[0].name, "valid");
    EXPECT_EQ(analyze->returnParams[1].name, "score");
}

// =============================================================
// Ownership semantic tests
// =============================================================

TEST(Sema, OwnershipOwnedParam) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "std::import(\"widget.h\", Widget);\n"
        "namespace x {\n"
        "  public:\n"
        "    void process(owned Widget w);\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    auto* fn = symbols.findFunction("x::process");
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->params.size(), 1u);
    EXPECT_EQ(fn->params[0].type.ownership, OwnershipKind::Owned);
}

TEST(Sema, OwnershipSharedParam) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "std::import(\"widget.h\", Widget);\n"
        "namespace x {\n"
        "  public:\n"
        "    void observe(shared Widget w);\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    auto* fn = symbols.findFunction("x::observe");
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->params.size(), 1u);
    EXPECT_EQ(fn->params[0].type.ownership, OwnershipKind::Shared);
}

TEST(Sema, OwnershipWeakParam) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "std::import(\"widget.h\", Widget);\n"
        "namespace x {\n"
        "  public:\n"
        "    void cache(weak Widget w);\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    auto* fn = symbols.findFunction("x::cache");
    ASSERT_NE(fn, nullptr);
    ASSERT_EQ(fn->params.size(), 1u);
    EXPECT_EQ(fn->params[0].type.ownership, OwnershipKind::Weak);
}

TEST(Sema, OwnershipOwnedPrimitiveError) {
    // S-OWN-005: ownership qualifier on primitive type should be an error
    // Must use bare 'int' (not alias 'Int') since the check operates on nameParts[0]
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "using int = std::cpp17::int;\n"
        "namespace x {\n"
        "  public:\n"
        "    void process(owned int val);\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    // S-OWN-005: ownership qualifier meaningless on primitive
    EXPECT_TRUE(diag.hasErrors());
}

// =============================================================
// Declaration semantic tests (using, import, binding)
// =============================================================

TEST(Sema, TypeAliasRegistered) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "using Int = std::cpp17::int32_t;\n"
        "namespace x {\n"
        "  public:\n"
        "    Int getValue();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    // TypeAliasEntry should exist in the symbol table
    auto* alias = symbols.findTypeAlias("Int");
    ASSERT_NE(alias, nullptr);
    EXPECT_EQ(alias->name, "Int");
    // Target type should reference std::cpp17::int32_t
    ASSERT_GE(alias->targetType.nameParts.size(), 1u);
    EXPECT_EQ(alias->targetType.nameParts.back(), "int32_t");
}

TEST(Sema, StdImportCreatesType) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "std::import(\"cstdint\", uint64_t);\n"
        "namespace x {\n"
        "  public:\n"
        "    uint64_t getValue();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    // Import should be recorded
    ASSERT_GE(symbols.imports().size(), 1u);
    EXPECT_EQ(symbols.imports()[0].path, "cstdint");
    EXPECT_EQ(symbols.imports()[0].typeName, "uint64_t");
    // isImportedType should recognize it
    EXPECT_TRUE(symbols.isImportedType("uint64_t"));
    EXPECT_FALSE(symbols.isImportedType("int32_t")); // not imported
}

TEST(Sema, FunctionBindingTarget) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "using Int = std::cpp17::int32_t;\n"
        "namespace x {\n"
        "  public:\n"
        "    void sort(Int* data, Int n) = std::sort;\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    auto* fn = symbols.findFunction("x::sort");
    ASSERT_NE(fn, nullptr);
    ASSERT_TRUE(fn->bindingTarget.has_value());
    EXPECT_EQ(*fn->bindingTarget, "std::sort");
}

TEST(Sema, FunctionBindingConflictsWithFnBlock) {
    // A function with bindingTarget must not have a fn block
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace x {\n"
        "  public:\n"
        "    void sort() = std::sort;\n"
        "    fn sort {\n"
        "      stage<1> something();\n"
        "    }\n"
        "  protected:\n"
        "    void something();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    sema.analyze(static_cast<const TopoFile&>(*ast));
    // Should report error: fn block conflicts with binding target
    EXPECT_TRUE(diag.hasErrors());
}

// --- Priority propagation to FunctionSymbol ---

TEST(Sema, PriorityPropagatedToSymbol) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace app {\n"
        "  public:\n"
        "    priority(critical):\n"
        "      void init();\n"
        "    priority(low):\n"
        "      void cleanup();\n"
        "    void normal_func();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    SemanticAnalyzer sema(diag);
    auto symbols = sema.analyze(static_cast<const TopoFile&>(*ast));
    ASSERT_FALSE(diag.hasErrors());

    auto* init = symbols.findFunction("app::init");
    ASSERT_NE(init, nullptr);
    EXPECT_EQ(init->priority, PriorityLevel::Critical);

    auto* cleanup = symbols.findFunction("app::cleanup");
    ASSERT_NE(cleanup, nullptr);
    EXPECT_EQ(cleanup->priority, PriorityLevel::Low);

    auto* normal = symbols.findFunction("app::normal_func");
    ASSERT_NE(normal, nullptr);
    EXPECT_EQ(normal->priority, PriorityLevel::Low);
}

TEST(VisibilityCollector, PriorityCollected) {
    DiagnosticEngine diag;
    auto ast = parseTopo(
        "namespace app {\n"
        "  public:\n"
        "    priority(high):\n"
        "      void process();\n"
        "    void idle();\n"
        "}",
        diag);
    ASSERT_FALSE(diag.hasErrors());

    VisibilityCollector collector;
    auto entries = collector.collect(static_cast<const TopoFile&>(*ast));
    ASSERT_EQ(entries.size(), 2u);

    // process: high priority
    EXPECT_EQ(entries[0].priority, PriorityLevel::High);

    // idle: inherits High from preceding priority(high): section
    EXPECT_EQ(entries[1].priority, PriorityLevel::High);
}
