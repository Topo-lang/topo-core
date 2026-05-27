#ifndef TOPO_DEBUG_QUERY_FRAME_VIEW_H
#define TOPO_DEBUG_QUERY_FRAME_VIEW_H

// Compute-side "frame view": the deserialized variable
// captured by the Extract adapter, ready for query evaluation.
//
// A FrameView owns either:
//   * a contiguous byte buffer (`bytes`) deserialized from a var_bytes
//     binary frame, or
//   * a reference to bytes living elsewhere (used by slice operations to
//     stay zero-copy), via std::span over the parent buffer.
//
// The query engine treats every value uniformly through this view. Scalars
// (intermediate reduction results) have shape == [] and bytes encoded in
// native machine representation matching `layout.dtype`.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace topo::debug_query {

// C++17-friendly non-owning byte view used by FrameView. Pointer + size, no
// std::span dependency. Slice operations return ByteSpans pointing into the
// parent buffer for zero-copy semantics.
struct ByteSpan {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

// Aggregate-leaf v2 fields.
// When `isStruct` is true, the parent FrameView describes an array of
// fixed-size structs laid out AoS: bytes contains N × strideBytes total, and
// each struct element exposes its primitive fields at byte offsets from the
// element base. Field access (`p.x`) builds a child FrameView whose
// `elemStride` is the parent's struct stride — i.e. a non-contiguous view
// over one column of the AoS array.
struct StructField {
    std::string name;
    uint64_t offset = 0;   // byte offset within one struct element
    std::string dtype;     // primitive dtype string ("f32", "i32", ...)
};

struct StructLayout {
    std::string name;                       // e.g. "Particle"
    uint64_t strideBytes = 0;               // sizeof(one struct element)
    std::vector<StructField> fields;        // in DWARF declaration order
};

struct LayoutDescriptor {
    std::string dtype;                   // "i32", "f32", ..., or "struct"
    std::vector<int64_t> shape;          // e.g. [16, 16]; empty => scalar
    std::vector<int64_t> strides;        // element strides; defaults to contiguous row-major
    bool soa = false;                    // marker; SoA layout needs extra fields (not yet wired)

    // aggregate-leaf descriptor.
    // `isStruct` is true when `dtype == "struct"` and `structLayout` has
    // been populated from the wire `struct` block. The dtype string stays
    // "struct" so existing code that branches on dtype keeps working —
    // `isStruct` is a convenience flag, not a separate dtype.
    bool isStruct = false;
    StructLayout structLayout;
};

// Element size in bytes for a supported dtype. Returns 0 for unknown.
size_t dtypeSize(const std::string& dtype);

// True if dtype is in the supported set (i8,i16,i32,i64,u8,u16,u32,u64,f32,f64).
bool isSupportedDtype(const std::string& dtype);

// Total element count implied by shape (product). Empty shape -> 1 (scalar).
int64_t numel(const std::vector<int64_t>& shape);

class FrameView {
public:
    FrameView() = default;

    // Construct a FrameView that owns its byte buffer.
    static FrameView owned(std::string variable,
                           std::vector<uint8_t> bytes,
                           LayoutDescriptor layout);

    // Construct a FrameView that is a non-owning slice into a parent buffer.
    static FrameView reference(std::string variable,
                               ByteSpan bytes,
                               LayoutDescriptor layout);

    // Raw bytes (read-only view).
    ByteSpan bytes() const { return bytesView_; }
    const LayoutDescriptor& layout() const { return layout_; }
    const std::string& variable() const { return variable_; }

    // True when this view points into bytes owned elsewhere (zero-copy
    // slice). Compute uses this to assert "no copy" in tests.
    bool isReference() const { return ownedBytes_ == nullptr; }

    // True when this view is backed by a shared-memory reference rather than
    // bytes. Always returns false from a real adapter today; reserved for
    // the SHM_REF path on the binary-frame wire.
    bool isShmRef() const { return shmRef_; }
    void markShmRef() { shmRef_ = true; }

    // byte stride between consecutive logical
    // elements. Defaults to 0 = "contiguous primitive view, stride equals
    // sizeof(dtype)". A strided view (returned by struct field access)
    // overrides this to the parent struct's stride so the fold loop walks
    // one column of an AoS array. Use `effectiveElemStride()` to read it.
    uint64_t elemStride() const { return elemStride_; }
    void setElemStride(uint64_t s) { elemStride_ = s; }

    // Returns the byte step between consecutive elements: explicit override
    // when set, otherwise the dtype size. Fold helpers branch once on
    // `isStrided()` to keep the contiguous fast-path tight.
    uint64_t effectiveElemStride() const;
    bool isStrided() const;

private:
    std::string variable_;
    LayoutDescriptor layout_;
    // Either owns its bytes (ownedBytes_ set, bytesView_ aliases it) or
    // borrows bytes from a parent (ownedBytes_ == nullptr, bytesView_ set).
    std::shared_ptr<std::vector<uint8_t>> ownedBytes_;
    ByteSpan bytesView_;
    bool shmRef_ = false;
    // 0 = "implicit, == dtypeSize(layout.dtype)"; non-zero = explicit stride.
    uint64_t elemStride_ = 0;
};

} // namespace topo::debug_query

#endif // TOPO_DEBUG_QUERY_FRAME_VIEW_H
