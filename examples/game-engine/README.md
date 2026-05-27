# Topo Language Showcase: Game Engine

This example demonstrates the full expressiveness of the Topo declaration language
using a realistic game engine architecture. It contains only `.topo` files —
no host code — because its purpose is to showcase the **declaration language itself**.

For complete projects with matching host code, see the SDK examples:
- C++: `topo-lang-cpp/examples/`
- Rust: `topo-lang-rust/examples/`
- Java: `topo-lang-java/examples/`

## Mode: fixture-only (not in the CMake build)

This example is deliberately **not** wired into the topo-core CMake
build. It contains no host source files — only `.topo` declarations —
so building it as a binary would be meaningless. Run it directly with
the installed toolchain.

## Validate

Frontend syntax + semantic check (uses only `topo` from topo-core):

```sh
topo --check topo/main.topo
```

## Build the showcase (optional)

`topo-build` (shipped in topo-cli) consumes `Topo.toml` and dispatches to
a backend per the project's `[build].language`. The showcase intentionally
ships without a host language pinned — pair it with a topo-lang-* SDK
example to produce a runnable binary:

```sh
cd path/to/topo-lang-cpp/examples/<matching-example>   # or rust/java/python
topo-build
```

## Structure

| Directory | Files | Features Demonstrated |
|-----------|-------|----------------------|
| `topo/` | `main.topo` | Cross-directory imports, selective import, 5-stage parallel game loop, multi-return |
| `topo/core/` | `types.topo` | Constraint hierarchy, adapt chains, constrained template, typefn, comptime fn, ownership, instantiate |
| `topo/core/` | `math.topo` | Type with operators, static methods, const accessors, adapt, comptime if, constrained template fn, instantiate |
| `topo/core/` | `memory.topo` | Lifetime (bounded, open-ended), priority sections, ownership, staged fn block, comptime fn |
| `topo/engine/` | `renderer.topo` | Pipeline DAG (fork/join), template type + instantiate, lifetime over staged block, priority |
| `topo/engine/` | `physics.topo` | Constraint, adapt, constrained template + instantiate, staged fn block with multi-return |
| `topo/engine/` | `resources.topo` | Ownership qualifiers, variadic template, staged fn block, lifetime, priority |
| `topo/game/` | `entity.topo` | Constraint hierarchy, constrained template, full specialization, adapt, nested namespace, instantiate |
| `topo/game/` | `scene.topo` | Nested namespace, parallel stage branches, cross-namespace references, priority |
| `topo/game/` | `systems.topo` | Comptime if, selective import, dual staged fn blocks, ownership, priority |

## Feature Coverage

This showcase covers all major Topo declaration features:

- **Visibility tiers**: public, protected, private, internal
- **Staged fn blocks**: 5-stage game loop with parallel branches
- **Pipeline DAG**: render pipeline with fork/join topology
- **Constraints**: Hashable -> Identifiable (inheritance), Serializable, Collidable, Component -> Tickable/Renderable
- **Adapt**: wiring constraint methods to implementations
- **Templates**: constrained type parameters (`SortedMap<K:Hashable, V>`)
- **Instantiate**: explicit template specialization
- **Lifetime**: bounded (`begin..end`), open-ended (`init..`)
- **Priority**: critical, high, low, background
- **Ownership**: owned, shared, weak qualifiers
- **Type functions**: compile-time type selection (`typefn StorageType`)
- **Comptime**: compile-time constants and conditional declarations
- **Multi-return**: `get_version() -> (int major, int minor, int patch)`
- **Nested namespaces**: `namespace scene::graph { ... }`
- **Cross-module imports**: selective and full imports across directories
