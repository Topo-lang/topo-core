# topo-core -- Declaration Language Core

The .topo declaration language frontend, analysis, and checking libraries. Zero LLVM dependency -- all components are backend-agnostic.

## Libraries

| Library | Directory | Purpose |
|---------|-----------|---------|
| Lexer | lib/Lexer/ | Tokenization of .topo source |
| Parser | lib/Parser/ | Recursive descent parser producing AST |
| AST | lib/AST/ | Three-object model: FnDecl, TypeDecl, DataDecl + 15 ModifierData::Kind values |
| Sema | lib/Sema/ | TypeResolver + three-pass SemanticAnalyzer |
| Analysis | lib/Analysis/ | Stage, Pipeline, Visibility, Lifetime, Priority analysis |
| Check | lib/Check/ | Completeness, Containment, ImportPath, StageIsolation, Visibility, Purity checks |
| Build | lib/Build/ | Topo.toml configuration, validation, incremental cache |
| Transpile | lib/Transpile/ | TranspileModel, TranspileDriver, ModelOptimizer, JSON serialization |
| Transform | lib/Transform/ | TokenStreamRewriter (syntax normalization), SemanticVerifier |
| Format | lib/Format/ | Formatter logic for topo-fmt |
| Platform | lib/Platform/ | Cross-platform abstraction (Process, SharedLibrary, FileGlob, ToolResolution) |

## Tools

| Tool | Directory | Purpose |
|------|-----------|---------|
| topo | tools/topo/ | Compiler frontend -- parses .topo and emits SymbolTable metadata |
| topo-fmt | tools/topo-fmt/ | Syntax normalization (--check / --fix / --diff) |
| topo-debug | tools/topo-debug/ | Declaration-aware debugger frontend |
| topo-debug-mock-adapter | tools/topo-debug-mock-adapter/ | DAP mock adapter (test-only) |

The plugin-dispatch CLIs (topo-build / topo-transpile / topo-check /
topo-init / topo-profile) live in the sibling `topo-cli` package
because each one links every TopoXxxPlugin from `topo-lang-*`.

## Headers

Public API headers are in `include/topo/`, mirroring the library structure.

## Build

Part of the Topo project. Build with:
```bash
cmake -B build && cmake --build build
```

## Tests

```bash
ctest --test-dir build -R "topo-core"
```
