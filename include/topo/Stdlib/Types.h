#ifndef TOPO_STDLIB_TYPES_H
#define TOPO_STDLIB_TYPES_H

// Topo stdlib bridging types.
//
// This header is the SINGLE SOURCE OF TRUTH for which stdlib type names are
// recognized by the frontend, how many template parameters they take, and
// what ABI byte layout the cross-language Functor boundary must produce.
//
// Per-language Emitters and topo-check catalogs read this table
// to compile/check stdlib uses. The frontend itself uses it for:
//   - Lexer keyword recognition (TokenKinds.h adds KW_bool, KW_i64, etc.)
//   - Parser type-position acceptance
//   - Sema validation: stdlib name → type-param count check; recurse into T
//     of optional<T> / slice<T>
//
// Stdlib type names are lowercase by convention (i64, optional, slice, ...),
// distinct from user types (CamelCase). The frontend treats stdlib names as
// reserved keywords once a Batch-1 stdlib type is referenced in type position.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace topo::stdlib {

// Enumeration of all stdlib type IDs.
//
// New entries extend this enum without rotating prior values. ID 0 is
// reserved for "not a stdlib type" to make TypeNode lookups defensive
// against zero-init.
enum class TypeId : uint16_t {
    None = 0,

    // --- Core bridging types ---
    Bool,      // bool   — u8 (0/1)
    I64,       // i64    — signed 64-bit little-endian, natural align
    F64,       // f64    — IEEE-754 LE 64-bit, natural align
    String,    // string — {u32 len_bytes, u8* utf8_ptr} 16B on 64-bit
    Optional,  // optional<T> — {u8 tag, padding, T value}
    Slice,     // slice<T>    — {u32 len_elems, T* ptr}
    Bytes,     // bytes       — slice<u8>-isomorphic non-owning byte view

    // --- Width-extension integer/float types: same query path as
    //     I64/F64, no new design (new entries, not new code paths) ---
    U8,        // u8     — unsigned 8-bit, align 1
    I32,       // i32    — signed 32-bit LE, align 4
    U32,       // u32    — unsigned 32-bit LE, align 4
    U64,       // u64    — unsigned 64-bit LE, align 8
    F32,       // f32    — IEEE-754 LE 32-bit, align 4
    I8,        // i8     — signed 8-bit, align 1
    I16,       // i16    — signed 16-bit LE, align 2
    U16,       // u16    — unsigned 16-bit LE, align 2

    // --- Semantic scalars: i64-isomorphic ABI, distinct TypeId so a
    //     debug/observability layer can render them domain-aware (ns since
    //     epoch) instead of as a bare integer. Same {8,8} signed-LE bytes
    //     as i64 — no new Sema path or Emitter branch, just a table entry. ---
    TimeNs,    // time_ns — signed 64-bit nanoseconds since the Unix epoch

    // 16-byte value scalar: a raw RFC 4122 byte sequence (network/big-endian
    // field order). Not integer-isomorphic — it maps to the host's native
    // UUID where one exists (Java/Python) and to a fixed 16-byte buffer
    // where it does not (C++/Rust/TS). Real {16,8} layout (not the
    // caller-computes sentinel); arity 0; no `<...>`. No runtime helper.
    Uuid,      // uuid — 16-byte RFC 4122 value

    // 16-byte IEEE 754-2008 decimal128 value. No host has a fixed-layout
    // native decimal, so (unlike uuid's native Java/Python UUID) every host
    // surfaces it as a raw 16-byte buffer; the byte contract is the decimal128
    // encoding and host code interprets it. Deliberately NO runtime codec:
    // a correct BID/DPD encoder is far past the thin-runtime budget and no
    // other stdlib type ships one. Real {16,16} layout; arity 0; no `<...>`.
    Decimal128, // decimal128 — 16-byte IEEE 754-2008 value

    // --- Composite ---
    // record<name1: T1, name2: T2, ...> — named-field aggregate. Layout
    // depends entirely on the field list, so (like Optional) the table
    // reports the caller-computes sentinel and traversal lives in the
    // Emitter. The aggregate uses natural alignment with no implicit
    // tail/inter-field expansion beyond what alignment forces — users who
    // want a specific footprint insert an explicit scalar field (a "padding"
    // field) themselves; there is no `align` keyword.
    Record,    // record<...> — caller computes layout from fields

    // array<T, N> — fixed-length inline buffer: N contiguous T with no
    // header, orthogonal to slice (pointer+len). Like Record/Optional the
    // table reports the caller-computes sentinel; the Emitter computes
    // size = N * stride(T) at natural align(T). N is a compile-time
    // integer literal carried in the second templateArg's nonTypeValue;
    // T is the first templateArg (recursing for nested stdlib types).
    Array,     // array<T,N> — caller computes layout from T and N

    // union<tag: TagT, v1: T1, v2: T2, ...> — tagged union. The first field
    // is the discriminant tag (an integer scalar); every remaining field is
    // a variant. Variants share one storage region big enough for the
    // largest variant, so the aggregate size is NOT the field sum — it is
    // tag + max(sizeof variant). Like Record it carries named fields (not
    // positional template args), reports the caller-computes sentinel, and
    // its byte layout is composed in exactly one place (composeUnionLayout)
    // so every host emitter agrees.
    Union,     // union<...> — caller computes layout from tag + variants
};

// Lowercase keyword form used in .topo source (always single token).
const char* keywordOf(TypeId id);

// Reverse: given a single-element TypeNode nameParts[0], classify it.
// Returns TypeId::None when name is not a stdlib type.
TypeId fromKeyword(const std::string& name);

// Whether a name is one of the recognized stdlib type keywords.
inline bool isStdlibKeyword(const std::string& name) {
    return fromKeyword(name) != TypeId::None;
}

// Type-parameter arity. For the core types:
//   bool / i64 / f64 / string -> 0
//   optional / slice          -> 1
// Returns 0 for TypeId::None as well (defensive).
unsigned typeParamArity(TypeId id);

// ABI layout (size + align in bytes, 64-bit platform).
// For parameterized stdlib types the layout describes the OUTER struct
// (e.g. `slice<T>` always 16 bytes regardless of T because T is held by
// pointer). The caller composes layouts recursively for `optional<T>` —
// `optional`'s size depends on T, so optional reports `align=1, size=0` and
// callers must add `align_up(1, align(T)) + sizeof(T)` themselves. This keeps
// the table data-only and lets layout traversal stay in the Emitter.
struct AbiLayout {
    // 0 = caller must compute (Optional, Record, Array, and Union).
    uint32_t size = 0;
    uint32_t align = 1;
};
AbiLayout layoutOf(TypeId id);

// Natural-alignment field accumulation, shared by every host emitter so the
// cross-language byte contract for `record<...>` is computed in exactly one
// place. The caller resolves each field's own AbiLayout (recursing into
// nested stdlib types — that traversal stays in the Emitter, this stays
// data-only) and feeds them in declaration order. Each field is placed at
// the next offset that satisfies its alignment; the aggregate's alignment is
// the maximum field alignment (minimum 1); the total size is rounded up to
// the aggregate alignment so arrays of the record tile correctly. An empty
// field list yields {1, 1} — a zero-field record still occupies a byte so
// distinct instances have distinct addresses, matching every host language's
// empty-struct behavior at the bridging boundary.
AbiLayout composeRecordLayout(const std::vector<AbiLayout>& fields);

// Tagged-union layout, the single source of truth for the cross-language
// `union<...>` byte contract. The tag occupies offset 0. The shared variant
// storage starts at the next offset that satisfies the strictest variant
// alignment, and is sized to hold the largest variant. The aggregate
// alignment is the max of the tag alignment and every variant alignment; the
// total size is rounded up to that aggregate alignment so arrays of the
// union tile correctly. Unlike a record the variants overlap, so the size is
// tag + max(variant) rather than the field sum. `variants` must be non-empty
// (Sema rejects a tag-only union upstream); a defensive empty input still
// yields a well-formed {1,1} so callers never produce a zero-size aggregate.
AbiLayout composeUnionLayout(const AbiLayout& tag,
                             const std::vector<AbiLayout>& variants);

// Brief human-readable description used in diagnostics & docs.
const char* descriptionOf(TypeId id);

// Iterate every Batch-1 stdlib type. Used by SymbolTable diagnostics, LSP
// completion, and skill-doc generation.
struct Entry {
    TypeId id;
    const char* keyword;
    unsigned arity;
    AbiLayout layout;
    const char* description;
};
const std::vector<Entry>& allEntries();

} // namespace topo::stdlib

#endif // TOPO_STDLIB_TYPES_H
