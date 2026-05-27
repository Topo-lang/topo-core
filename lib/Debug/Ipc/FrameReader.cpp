#include "topo/Debug/Ipc/FrameReader.h"

#include <cstring>
#include <sstream>

namespace topo::debug_ipc {

namespace {

inline uint64_t loadU64LE(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (i * 8);
    return v;
}

inline uint32_t loadU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

// Read exactly `n` bytes; returns true on success, false on EOF/truncation.
bool readExact(ByteSource& src, uint8_t* buf, size_t n) {
    size_t got = 0;
    if (src.readBulk) {
        while (got < n) {
            size_t r = src.readBulk(buf + got, n - got);
            if (r == 0) return false;
            got += r;
        }
        return true;
    }
    while (got < n) {
        int b = src.readByte();
        if (b < 0) return false;
        buf[got++] = static_cast<uint8_t>(b);
    }
    return true;
}

// Read a JSON line into `out`. First byte was already consumed and is `first`.
bool readJsonLine(ByteSource& src, uint8_t first, std::string& out) {
    out.clear();
    out.push_back(static_cast<char>(first));
    while (true) {
        int b = src.readByte();
        if (b < 0) return false;
        if (b == '\n') return true;
        out.push_back(static_cast<char>(b));
    }
}

} // namespace

ReadResult readRecord(ByteSource& source, size_t maxPayload) {
    ReadResult result;

    int first = source.readByte();
    if (first < 0) {
        result.status = ReadStatus::Eof;
        return result;
    }

    uint8_t fb = static_cast<uint8_t>(first);

    if (fb == '{') {
        std::string line;
        if (!readJsonLine(source, fb, line)) {
            result.status = ReadStatus::Truncated;
            result.error = "EOF in JSON line";
            return result;
        }
        try {
            result.json = nlohmann::json::parse(line);
            result.status = ReadStatus::Ok;
        } catch (const std::exception& e) {
            result.status = ReadStatus::BadJson;
            result.error = std::string("JSON parse error: ") + e.what();
        }
        return result;
    }

    if (fb == kFirstHeaderByte) {
        // Read remaining 23 bytes of the header.
        uint8_t hdr[kBinaryFrameHeaderSize];
        hdr[0] = fb;
        if (!readExact(source, hdr + 1, kBinaryFrameHeaderSize - 1)) {
            result.status = ReadStatus::Truncated;
            result.error = "EOF mid-header";
            return result;
        }
        uint32_t magic = loadU32BE(hdr);
        if (magic != kBinaryFrameMagic) {
            result.status = ReadStatus::BadHeader;
            result.error = "bad magic";
            return result;
        }
        uint8_t typeByte = hdr[4];
        if (typeByte == 0 ||
            typeByte > static_cast<uint8_t>(BinaryFrameType::ChunkContinuation)) {
            result.status = ReadStatus::BadHeader;
            result.error = "unknown frame type";
            return result;
        }
        if (hdr[6] != 0 || hdr[7] != 0) {
            result.status = ReadStatus::ReservedNonZero;
            result.error = "reserved bytes non-zero";
            return result;
        }
        BinaryFrame f;
        f.type = static_cast<BinaryFrameType>(typeByte);
        f.flags = hdr[5];
        f.frameId = loadU64LE(hdr + 8);
        uint64_t payloadLen = loadU64LE(hdr + 16);
        if (payloadLen > maxPayload) {
            result.status = ReadStatus::PayloadTooBig;
            result.error = "payload exceeds max";
            return result;
        }
        f.payload.resize(static_cast<size_t>(payloadLen));
        if (payloadLen > 0 && !readExact(source, f.payload.data(),
                                         static_cast<size_t>(payloadLen))) {
            result.status = ReadStatus::Truncated;
            result.error = "EOF mid-payload";
            return result;
        }
        result.frame = std::move(f);
        result.status = ReadStatus::Ok;
        return result;
    }

    result.status = ReadStatus::BadMagic;
    result.error = "unexpected first byte (expected '{' or 0x54)";
    return result;
}

ByteSource byteSourceFromString(const std::string& s, size_t* cursor) {
    ByteSource src;
    src.readByte = [&s, cursor]() -> int {
        if (*cursor >= s.size()) return -1;
        return static_cast<unsigned char>(s[(*cursor)++]);
    };
    src.readBulk = [&s, cursor](uint8_t* buf, size_t n) -> size_t {
        size_t avail = s.size() - *cursor;
        size_t take = avail < n ? avail : n;
        if (take == 0) return 0;
        std::memcpy(buf, s.data() + *cursor, take);
        *cursor += take;
        return take;
    };
    return src;
}

} // namespace topo::debug_ipc
