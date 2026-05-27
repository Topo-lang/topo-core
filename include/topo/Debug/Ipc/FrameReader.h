#ifndef TOPO_DEBUG_IPC_FRAME_READER_H
#define TOPO_DEBUG_IPC_FRAME_READER_H

// Interleaved JSON + binary frame stream reader.
//
// The Extract adapter emits two kinds of records on its stdout:
//   * JSON lines (each terminated by '\n', starts with '{')
//   * binary frames (24B header beginning with magic byte 0x54 = 'T')
//
// FrameReader routes each record to the right handler based on the first
// byte of the stream. It is byte-oriented and reads from a `Source`
// abstraction so the same code drives both tests (std::stringstream) and
// the real CLI (PipedProcess subprocess stdout).
//
// Design choices:
//   * Single thread, blocking reads — Compute is one process and only needs
//     to drain in lockstep with the adapter's emit cadence.
//   * No prefetching past frame boundary so a truncated stream returns a
//     clean ReadError::Truncated.
//   * JSON lines are returned verbatim (including final '\n' stripped); the
//     caller parses with nlohmann::json.

#include "topo/Debug/Ipc/BinaryFrame.h"

#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>

namespace topo::debug_ipc {

// Byte source abstraction. Returns -1 on EOF, otherwise the next byte.
// For a string source: increment an index. For a pipe: read(2).
struct ByteSource {
    std::function<int()> readByte;
    // Optional bulk read; FrameReader falls back to readByte() if absent.
    // Returns number of bytes read, or 0 on EOF.
    std::function<size_t(uint8_t* buf, size_t n)> readBulk;
};

enum class ReadStatus {
    Ok,             // record was produced (Json or Binary)
    Eof,            // clean EOF on a record boundary
    Truncated,      // EOF in the middle of a record
    BadMagic,       // first byte not '{' or 0x54
    BadHeader,      // magic mismatch in 4-byte magic field
    BadJson,        // JSON parse error
    ReservedNonZero,// reserved bytes in header are non-zero (forward-compat)
    PayloadTooBig,  // payload_len exceeds limit
};

struct ReadResult {
    ReadStatus status = ReadStatus::Eof;
    std::optional<nlohmann::json> json;
    std::optional<BinaryFrame> frame;
    // Diagnostic for non-Ok states.
    std::string error;
};

// Read one record (JSON line or binary frame) from `source`. On Ok, exactly
// one of result.json / result.frame is populated.
ReadResult readRecord(ByteSource& source, size_t maxPayload = 64 * 1024 * 1024);

// Convenience: build a ByteSource over a std::string.
ByteSource byteSourceFromString(const std::string& s, size_t* cursor);

} // namespace topo::debug_ipc

#endif // TOPO_DEBUG_IPC_FRAME_READER_H
