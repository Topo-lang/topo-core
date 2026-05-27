#ifndef TOPO_DEBUG_IPC_FRAME_WRITER_H
#define TOPO_DEBUG_IPC_FRAME_WRITER_H

// Interleaved JSON + binary frame stream writer.
//
// Mirrors FrameReader on the producer side. Used by the mock adapter and by
// the Compute side when sending control messages back to Extract.

#include "topo/Debug/Ipc/BinaryFrame.h"

#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace topo::debug_ipc {

struct ByteSink {
    // Returns true on success. Caller is responsible for flushing.
    std::function<bool(const uint8_t* data, size_t n)> write;
    // Optional flush; FrameWriter calls this after each record.
    std::function<void()> flush;
};

// Write `j` as a single JSON line ending with '\n'. Returns true on success.
bool writeJsonLine(ByteSink& sink, const nlohmann::json& j);

// Write a binary frame (header + payload). Returns true on success.
bool writeBinaryFrame(ByteSink& sink, const BinaryFrame& frame);

// Convenience: build a ByteSink that appends to a std::string buffer.
ByteSink byteSinkToString(std::string& dest);

// Convenience: build a ByteSink that writes to stdout via printf-style fwrite.
ByteSink byteSinkToStdout();

} // namespace topo::debug_ipc

#endif // TOPO_DEBUG_IPC_FRAME_WRITER_H
