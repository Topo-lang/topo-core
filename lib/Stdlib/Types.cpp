#include "topo/Stdlib/Types.h"

#include <unordered_map>

namespace topo::stdlib {

namespace {

// Canonical stdlib bridging-type table.
//
// Layout values (sizes/aligns in bytes on 64-bit platforms):
//   bool:     1 byte (u8 boolean), align 1
//   i64:      8 bytes, align 8
//   f64:      8 bytes, align 8
//   string:   16 bytes  ({u32 len_bytes, u8* ptr}) — natural align 8
//   optional: caller-computed (size depends on T), sentinel size=0
//   slice:    16 bytes  ({u32 len_elems, T* ptr})  — natural align 8
//   bytes:    16 bytes  (slice<u8>-isomorphic)      — natural align 8
//   array:    caller-computed (N * stride(T)), sentinel size=0
const std::vector<Entry>& entries() {
    static const std::vector<Entry> kEntries = {
        // --- Batch 1 ---
        {TypeId::Bool, "bool", 0, {1, 1}, "boolean (u8 0/1)"},
        {TypeId::I64, "i64", 0, {8, 8}, "signed 64-bit integer (LE)"},
        {TypeId::F64, "f64", 0, {8, 8}, "IEEE-754 binary64 (LE)"},
        {TypeId::String, "string", 0, {16, 8}, "UTF-8 length-prefixed string (16B)"},
        {TypeId::Optional, "optional", 1, {0, 1}, "optional<T> (caller computes layout)"},
        {TypeId::Slice, "slice", 1, {16, 8}, "slice<T> non-owning (16B)"},
        {TypeId::Bytes, "bytes", 0, {16, 8}, "bytes — slice<u8>-isomorphic byte view (16B)"},
        // --- width-extension scalar arities ---
        {TypeId::U8, "u8", 0, {1, 1}, "unsigned 8-bit integer"},
        {TypeId::I32, "i32", 0, {4, 4}, "signed 32-bit integer (LE)"},
        {TypeId::U32, "u32", 0, {4, 4}, "unsigned 32-bit integer (LE)"},
        {TypeId::U64, "u64", 0, {8, 8}, "unsigned 64-bit integer (LE)"},
        {TypeId::F32, "f32", 0, {4, 4}, "IEEE-754 binary32 (LE)"},
        {TypeId::I8, "i8", 0, {1, 1}, "signed 8-bit integer"},
        {TypeId::I16, "i16", 0, {2, 2}, "signed 16-bit integer (LE)"},
        {TypeId::U16, "u16", 0, {2, 2}, "unsigned 16-bit integer (LE)"},
        // --- semantic scalar: i64-isomorphic bytes, domain-tagged ---
        {TypeId::TimeNs, "time_ns", 0, {8, 8}, "signed 64-bit nanoseconds since the Unix epoch (i64-isomorphic ABI)"},
        {TypeId::Uuid, "uuid", 0, {16, 8}, "16-byte RFC 4122 value (raw byte order)"},
        {TypeId::Decimal128, "decimal128", 0, {16, 16}, "16-byte IEEE 754-2008 decimal128 value (raw bytes; host interprets)"},
        // --- composite ---
        // arity 0: record carries named fields, not positional template
        // args, so the generic arity check does not apply; the parser and
        // Sema validate the field list directly. Layout is caller-computed
        // (size=0 sentinel) and composed via composeRecordLayout().
        {TypeId::Record, "record", 0, {0, 1}, "record<...> named-field aggregate (caller computes layout)"},
        // arity 0: array carries a type arg plus a compile-time integer N
        // (not a uniform list of type params), so the generic arity check
        // does not apply; Sema validates the <T, N> pair directly. Layout
        // is caller-computed (size=0 sentinel): Emitter computes
        // N * align_up(sizeof(T), align(T)) at align(T).
        {TypeId::Array, "array", 0, {0, 1}, "array<T,N> fixed inline buffer (caller computes layout)"},
        // arity 0: union carries a discriminant tag plus overlapping variant
        // fields (named, not positional template args); same caller-computed
        // sentinel as record, but layout via composeUnionLayout() because
        // variants share storage, so size != field sum.
        {TypeId::Union, "union", 0, {0, 1}, "union<...> tagged union (caller computes layout)"},
    };
    return kEntries;
}

// Round offset up to the next multiple of align. align is always >= 1.
static uint32_t alignUp(uint32_t offset, uint32_t align) {
    return (offset + align - 1) / align * align;
}

const Entry* findById(TypeId id) {
    for (const auto& e : entries()) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

} // namespace

const char* keywordOf(TypeId id) {
    const Entry* e = findById(id);
    return e ? e->keyword : "";
}

TypeId fromKeyword(const std::string& name) {
    // Lowercase, exact match. Keep as linear scan — 6 entries.
    for (const auto& e : entries()) {
        if (name == e.keyword) return e.id;
    }
    return TypeId::None;
}

unsigned typeParamArity(TypeId id) {
    const Entry* e = findById(id);
    return e ? e->arity : 0;
}

AbiLayout layoutOf(TypeId id) {
    const Entry* e = findById(id);
    return e ? e->layout : AbiLayout{};
}

AbiLayout composeRecordLayout(const std::vector<AbiLayout>& fields) {
    uint32_t offset = 0;
    uint32_t maxAlign = 1;
    for (const auto& f : fields) {
        const uint32_t a = f.align < 1 ? 1 : f.align;
        offset = alignUp(offset, a);
        offset += f.size;
        if (a > maxAlign) maxAlign = a;
    }
    uint32_t total = alignUp(offset, maxAlign);
    // A zero-field record still occupies one byte: distinct instances must
    // have distinct addresses, matching host-language empty-aggregate rules.
    if (total == 0) total = 1;
    return AbiLayout{total, maxAlign};
}

AbiLayout composeUnionLayout(const AbiLayout& tag,
                             const std::vector<AbiLayout>& variants) {
    const uint32_t tagAlign = tag.align < 1 ? 1 : tag.align;
    uint32_t maxVariantSize = 0;
    uint32_t maxVariantAlign = 1;
    for (const auto& v : variants) {
        const uint32_t a = v.align < 1 ? 1 : v.align;
        if (v.size > maxVariantSize) maxVariantSize = v.size;
        if (a > maxVariantAlign) maxVariantAlign = a;
    }
    // Tag sits first; the shared variant storage begins at the next offset
    // that satisfies the strictest variant alignment. Variants overlap, so
    // the storage is sized for the largest one — not their sum.
    const uint32_t variantOffset = alignUp(tag.size, maxVariantAlign);
    const uint32_t aggAlign = tagAlign > maxVariantAlign ? tagAlign : maxVariantAlign;
    uint32_t total = alignUp(variantOffset + maxVariantSize, aggAlign);
    // A variant-less union is rejected by Sema; stay well-formed defensively
    // so no caller ever sees a zero-size aggregate.
    if (total == 0) total = 1;
    return AbiLayout{total, aggAlign};
}

const char* descriptionOf(TypeId id) {
    const Entry* e = findById(id);
    return e ? e->description : "";
}

const std::vector<Entry>& allEntries() {
    return entries();
}

} // namespace topo::stdlib
