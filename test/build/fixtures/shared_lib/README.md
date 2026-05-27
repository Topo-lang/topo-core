# 06 — Shared Library

Demonstrates shared library output with aggressive mode and symbol obfuscation.

## Features

- 6-level nested namespaces (`crypto::hash::impl::detail::core::internal`)
- Heavy `private` / `protected` usage (only 2 `public` functions)
- `output_type = "shared"` for shared library output
- `mode = "aggressive"` for ThinLTO optimization
- `obfuscation = "salted"` with custom salt for symbol obfuscation
- Rust-style return: `finalize(int* ctx) -> int;`

## Build (pure C++)

```bash
cmake -B build && cmake --build build
./build/hashlib_driver
```

## Build (with Topo)

```bash
topo-build
```
