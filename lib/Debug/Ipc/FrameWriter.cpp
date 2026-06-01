#include "topo/Debug/Ipc/FrameWriter.h"

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace topo::debug_ipc {

bool writeJsonLine(ByteSink& sink, const nlohmann::json& j) {
    std::string s = j.dump();
    s.push_back('\n');
    if (!sink.write(reinterpret_cast<const uint8_t*>(s.data()), s.size())) {
        return false;
    }
    if (sink.flush) sink.flush();
    return true;
}

bool writeBinaryFrame(ByteSink& sink, const BinaryFrame& frame) {
    auto bytes = frame.serialize();
    if (!sink.write(bytes.data(), bytes.size())) return false;
    if (sink.flush) sink.flush();
    return true;
}

ByteSink byteSinkToString(std::string& dest) {
    ByteSink s;
    s.write = [&dest](const uint8_t* data, size_t n) {
        dest.append(reinterpret_cast<const char*>(data), n);
        return true;
    };
    s.flush = nullptr;
    return s;
}

ByteSink byteSinkToStdout() {
#ifdef _WIN32
    // The wire protocol is raw binary frames plus LF-delimited JSON lines.
    // Windows text-mode stdout expands every 0x0A to 0x0D 0x0A, which corrupts
    // binary frame payloads and the LF framing (the adapter→debugger channel
    // then fails with "unexpected first byte"). Force stdout to binary mode.
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    ByteSink s;
    s.write = [](const uint8_t* data, size_t n) {
        size_t total = 0;
        while (total < n) {
            size_t w = std::fwrite(data + total, 1, n - total, stdout);
            if (w == 0) return false;
            total += w;
        }
        return true;
    };
    s.flush = []() { std::fflush(stdout); };
    return s;
}

} // namespace topo::debug_ipc
