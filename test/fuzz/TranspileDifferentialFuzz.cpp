// Differential fuzz across the 4 language emitters (C++, Rust, Java, Python).
//
// Strategy:
//   1. Parse fuzzer input as .topo source. Parse failure -> early return 0.
//   2. Run semantic analysis -> SymbolTable. Sema failure -> early return 0.
//   3. Build a minimal TranspileModule from SymbolTable declarations
//      (signatures only; extractors are what normally fill bodies, and we
//      cannot spawn them inside a fuzzer).
//   4. Invoke all 4 emitters on the same module.
//
// Divergence semantics:
//   Each emitter should either produce a non-empty string (success) or an
//   empty string (refused to emit). The fuzzer treats *crashes / hangs* as
//   bugs. String-level output differences are expected and ignored -- the
//   languages have legitimately different syntax. The goal is coverage of
//   shared model-walking code paths across 4 independent emitters with the
//   same inputs, maximizing the chance of hitting parser/model edge cases
//   that one emitter mishandles.
//
// If any emitter crashes, libFuzzer saves the offending input to
// crash-<sha>. Treat such inputs as P1 bugs.

#include "topo/Basic/Diagnostic.h"
#include "topo/Lexer/Lexer.h"
#include "topo/Parser/Parser.h"
#include "topo/Sema/SemanticAnalyzer.h"
#include "topo/Sema/SymbolTable.h"
#include "topo/Transpile/TranspileModel.h"

#include "CppEmitter.h"
#include "JavaEmitter.h"
#include "PythonEmitter.h"
#include "RustEmitter.h"

#include <cstdint>
#include <memory>
#include <string>

namespace {

// Build a minimal TranspileModule from a SymbolTable -- signatures with
// empty bodies. This exercises emitters' signature rendering, type binding,
// access modifier mapping, and module-level framing without requiring the
// extractor subprocesses that normally populate bodies.
topo::transpile::TranspileModule buildModule(const topo::SymbolTable& symbols) {
    using namespace topo::transpile;
    TranspileModule module;

    // Map SymbolTable classes -> TranspileType (fields only, no methods).
    for (const auto& [qname, cls] : symbols.classSymbols()) {
        TranspileType t;
        t.qualifiedName = cls.qualifiedName;
        for (const auto& mv : cls.memberVars) {
            TranspileField f;
            f.name = mv.name;
            f.type = mv.type;
            t.fields.push_back(std::move(f));
        }
        module.types.push_back(std::move(t));
    }

    // Map SymbolTable functions -> TranspileFunction with an empty body.
    for (const auto& [qname, fn] : symbols.functions()) {
        TranspileFunction f;
        f.qualifiedName = fn.qualifiedName;
        f.returnType = fn.returnType;
        f.params = fn.params;
        // body is empty -- emitters should still be able to produce a
        // valid signature + empty block.
        switch (fn.visibility) {
            case topo::Visibility::Public:
                f.accessModifier = "public";
                break;
            case topo::Visibility::Protected:
                f.accessModifier = "protected";
                break;
            case topo::Visibility::Private:
                f.accessModifier = "private";
                break;
            default:
                f.accessModifier = "";
                break;
        }
        module.functions.push_back(std::move(f));
    }

    return module;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Cap input size -- fuzzer guidance: extremely large inputs exercise
    // parser scaling rather than emitter logic.
    if (size > 16 * 1024) size = 16 * 1024;
    std::string input(reinterpret_cast<const char*>(data), size);

    // Step 1: Parse
    topo::DiagnosticEngine parseDiag;
    topo::Lexer lexer(input, "<fuzz>", parseDiag);
    topo::Parser parser(lexer, parseDiag);
    auto ast = parser.parseTopoFile();
    if (!ast) return 0;

    // Step 2: Sema
    topo::DiagnosticEngine semaDiag;
    topo::SemanticAnalyzer sema(semaDiag);
    topo::SymbolTable symbols = sema.analyze(*ast);

    // Step 3: Build TranspileModule
    auto module = buildModule(symbols);

    // Step 4: Run all 4 emitters. Crashes inside any emitter are bugs.
    // We intentionally do not assert on string equality -- the 4 languages
    // have different syntax. What matters is that no emitter crashes or
    // hangs on whatever model we produced.
    {
        topo::transpile::CppEmitter e;
        volatile auto s = e.emit(module);
        (void)s;
    }
    {
        topo::transpile::RustEmitter e;
        volatile auto s = e.emit(module);
        (void)s;
    }
    {
        topo::transpile::JavaEmitter e;
        volatile auto s = e.emit(module);
        (void)s;
    }
    {
        topo::transpile::PythonEmitter e;
        volatile auto s = e.emit(module);
        (void)s;
    }

    return 0;
}
