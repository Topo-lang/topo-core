#include "topo/Debug/Ipc/BinaryFrame.h"

#include <cstring>

namespace topo::debug_ipc {

namespace {

// Magic is stored big-endian on the wire so its first byte is 'T' (0x54).
// Compute discriminates by first byte (`{` JSON / 0x54 binary).
inline uint32_t loadU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

inline void storeU32BE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

// Little-endian load/store for everything else (frame_id, payload_len, ShmRef
// fields). The debug IPC wire format is little-endian — the magic
// is the lone exception so byte 0 routes deterministically.
inline uint32_t loadU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t loadU64LE(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<uint64_t>(p[i]) << (i * 8);
    }
    return v;
}

inline void storeU32LE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

inline void storeU64LE(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<uint8_t>(v >> (i * 8));
    }
}

} // namespace

std::vector<uint8_t> BinaryFrame::serialize() const {
    std::vector<uint8_t> out;
    out.resize(kBinaryFrameHeaderSize + payload.size());
    uint8_t* p = out.data();

    storeU32BE(p + 0, kBinaryFrameMagic);
    p[4] = static_cast<uint8_t>(type);
    p[5] = flags;
    p[6] = 0; // reserved
    p[7] = 0; // reserved
    storeU64LE(p + 8, frameId);
    storeU64LE(p + 16, static_cast<uint64_t>(payload.size()));

    if (!payload.empty()) {
        std::memcpy(p + kBinaryFrameHeaderSize, payload.data(), payload.size());
    }
    return out;
}

std::optional<BinaryFrame> BinaryFrame::parse(ByteSpan data) {
    if (data.size < kBinaryFrameHeaderSize) return std::nullopt;

    uint32_t magic = loadU32BE(data.data);
    if (magic != kBinaryFrameMagic) return std::nullopt;

    BinaryFrame f;
    uint8_t typeByte = data.data[4];
    if (typeByte == 0 || typeByte > static_cast<uint8_t>(BinaryFrameType::ChunkContinuation)) {
        return std::nullopt;
    }
    f.type = static_cast<BinaryFrameType>(typeByte);
    f.flags = data.data[5];
    // reserved bytes
    if (data.data[6] != 0 || data.data[7] != 0) return std::nullopt;
    f.frameId = loadU64LE(data.data + 8);
    uint64_t payloadLen = loadU64LE(data.data + 16);
    if (data.size < kBinaryFrameHeaderSize + payloadLen) return std::nullopt;

    f.payload.resize(payloadLen);
    if (payloadLen > 0) {
        std::memcpy(f.payload.data(), data.data + kBinaryFrameHeaderSize, payloadLen);
    }
    return f;
}

std::array<uint8_t, 16> ShmRef::serialize() const {
    std::array<uint8_t, 16> out{};
    storeU32LE(out.data() + 0, pathOffset);
    storeU32LE(out.data() + 4, len);
    storeU32LE(out.data() + 8, reserved0);
    storeU32LE(out.data() + 12, reserved1);
    return out;
}

std::optional<ShmRef> ShmRef::parse(ByteSpan body) {
    if (body.size < 16) return std::nullopt;
    ShmRef r;
    r.pathOffset = loadU32LE(body.data + 0);
    r.len = loadU32LE(body.data + 4);
    r.reserved0 = loadU32LE(body.data + 8);
    r.reserved1 = loadU32LE(body.data + 12);
    return r;
}

} // namespace topo::debug_ipc
