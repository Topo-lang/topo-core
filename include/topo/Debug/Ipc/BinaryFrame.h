#ifndef TOPO_DEBUG_IPC_BINARY_FRAME_H
#define TOPO_DEBUG_IPC_BINARY_FRAME_H

// Extract <-> Compute IPC framed binary protocol.
//
// The data channel uses fixed 24-byte headers followed by
// raw payload bytes. The 24-byte header layout (little-endian on the wire):
//
//   offset  size  field
//   ------  ----  -----
//        0     4  magic = 0x544F504F  (stored big-endian so byte 0 == 'T' = 0x54)
//        4     1  type  (BinaryFrameType: 0x01 var_bytes, 0x02 layout_descriptor,
//                                          0x03 chunk_continuation)
//        5     1  flags (bit 0 = SHM_REF)
//        6     2  reserved (must be 0)
//        8     8  frame_id  (u64 LE)
//       16     8  payload_len (u64 LE)
//
// Endianness: little-endian for frame_id / payload_len / ShmRef fields. The
// magic is the lone exception — it is laid down byte-for-byte 'T','O','P','O'
// so the very first byte of every binary frame on the wire is 0x54, which
// gives the FrameReader an unambiguous discriminator against JSON lines
// (which always start with '{' = 0x7B).
//
// When the SHM_REF flag is set, the payload body is a 16-byte descriptor
// `{path_offset_u32, len_u32, reserved_u32, reserved_u32}` indicating that
// the real payload lives in shared memory. The layout is defined but the
// implementation does **not** mmap — Compute records the reference and any
// reduction that tries to read it errors with "shm not yet implemented".
// The mock adapter never triggers SHM, so this stays unblocking-but-defined.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace topo::debug_ipc {

// Lightweight non-owning byte view (C++17-friendly subset of std::span).
struct ByteSpan {
    const uint8_t* data = nullptr;
    size_t size = 0;
    const uint8_t* begin() const { return data; }
    const uint8_t* end() const { return data + size; }
};

inline constexpr uint32_t kBinaryFrameMagic = 0x544F504F; // "POT" LE / "TOPO" by first byte
inline constexpr size_t kBinaryFrameHeaderSize = 24;
inline constexpr uint8_t kFirstHeaderByte = 0x54;          // 'T', what FrameReader looks for

enum class BinaryFrameType : uint8_t {
    VarBytes = 0x01,
    LayoutDescriptor = 0x02,
    ChunkContinuation = 0x03,
};

enum class BinaryFrameFlag : uint8_t {
    None = 0x00,
    ShmRef = 0x01,
};

inline uint8_t operator|(BinaryFrameFlag a, BinaryFrameFlag b) {
    return static_cast<uint8_t>(a) | static_cast<uint8_t>(b);
}

struct BinaryFrame {
    BinaryFrameType type = BinaryFrameType::VarBytes;
    uint8_t flags = 0;
    uint64_t frameId = 0;
    std::vector<uint8_t> payload;

    bool hasFlag(BinaryFrameFlag f) const {
        return (flags & static_cast<uint8_t>(f)) != 0;
    }

    // Serialize header + payload into a fresh byte vector ready for write().
    std::vector<uint8_t> serialize() const;

    // Try to parse a full frame (header + payload) from `data`. Returns
    // nullopt if `data` is too short to contain a complete frame or the
    // header is malformed. The data range must be exactly one frame; use
    // FrameReader for streaming.
    static std::optional<BinaryFrame> parse(ByteSpan data);
};

// 16-byte SHM reference body. Payload bytes are reinterpreted
// as this struct when BinaryFrameFlag::ShmRef is set.
struct ShmRef {
    uint32_t pathOffset = 0; // reserved for future path-table style addressing
    uint32_t len = 0;
    uint32_t reserved0 = 0;
    uint32_t reserved1 = 0;

    std::array<uint8_t, 16> serialize() const;
    static std::optional<ShmRef> parse(ByteSpan body);
};

} // namespace topo::debug_ipc

#endif // TOPO_DEBUG_IPC_BINARY_FRAME_H
