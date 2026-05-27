// topo-debug-mock-adapter — mock Extract adapter.
//
// Speaks the wire protocol (24B binary headers + JSON-line control)
// against a fixed in-process fixture. Used by topo-debug CLI tests so we
// can exercise the full Compute path without a real debugger.
//
// Behavior:
//   1. On startup, parse --fixture/--site/--delay-ms flags.
//   2. Write a JSON-line `{"kind":"breakpoint_hit","frame":1,"site":...}`
//      to stdout.
//   3. Write a binary frame of type=var_bytes (frame_id=1) carrying the
//      fixture's raw bytes (no shm in this mock).
//   4. Write a binary frame of type=layout_descriptor (frame_id=1) with a
//      JSON body describing shape/dtype.
//   5. Read JSON lines from stdin until `{"op":"continue"}` -> exit 0.
//
// Fixtures:
//   * 16x16_int_one_hot    i32[16,16], matrix[5][7]=42, rest 0
//   * 1024x1024_f32_ramp   f32[1024,1024], v = (i*1024+j)/1e6
//   * large_16MiB_f32      f32[4194304], all 1.0
//   * empty                i32[0]
//
// The variable always lives under name "matrix" — matches the demo query
// `sum(matrix)` in the planning doc.

#include "topo/Debug/Ipc/BinaryFrame.h"
#include "topo/Debug/Ipc/FrameWriter.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

using topo::debug_ipc::BinaryFrame;
using topo::debug_ipc::BinaryFrameType;
using topo::debug_ipc::ByteSink;
using topo::debug_ipc::byteSinkToStdout;
using topo::debug_ipc::writeBinaryFrame;
using topo::debug_ipc::writeJsonLine;
using nlohmann::json;

namespace {

struct Args {
    std::string fixture = "16x16_int_one_hot";
    std::string site = "test.c:10";
    int delayMs = 0;
};

bool parseArgs(int argc, char** argv, Args& a, std::string& err) {
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto eat = [&](const std::string& flag, std::string& dest) -> bool {
            if (s.rfind(flag + "=", 0) == 0) {
                dest = s.substr(flag.size() + 1);
                return true;
            }
            if (s == flag && i + 1 < argc) {
                dest = argv[++i];
                return true;
            }
            return false;
        };
        std::string ms;
        if (eat("--fixture", a.fixture)) continue;
        if (eat("--site", a.site)) continue;
        if (eat("--delay-ms", ms)) { a.delayMs = std::atoi(ms.c_str()); continue; }
        err = "unknown argument: " + s;
        return false;
    }
    return true;
}

struct Fixture {
    std::string dtype;
    std::vector<int64_t> shape;
    std::vector<uint8_t> bytes;
};

void writeI32LE(std::vector<uint8_t>& out, int32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

void writeF32LE(std::vector<uint8_t>& out, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
}

bool buildFixture(const std::string& name, Fixture& out) {
    if (name == "16x16_int_one_hot") {
        out.dtype = "i32";
        out.shape = {16, 16};
        out.bytes.reserve(16 * 16 * 4);
        for (int i = 0; i < 16; ++i) {
            for (int j = 0; j < 16; ++j) {
                int32_t v = (i == 5 && j == 7) ? 42 : 0;
                writeI32LE(out.bytes, v);
            }
        }
        return true;
    }
    if (name == "1024x1024_f32_ramp") {
        out.dtype = "f32";
        out.shape = {1024, 1024};
        out.bytes.reserve(1024 * 1024 * 4);
        for (int i = 0; i < 1024; ++i) {
            for (int j = 0; j < 1024; ++j) {
                writeF32LE(out.bytes, static_cast<float>((i * 1024 + j) / 1e6));
            }
        }
        return true;
    }
    if (name == "large_16MiB_f32") {
        out.dtype = "f32";
        out.shape = {4194304};
        out.bytes.resize(4194304 * 4);
        // All bytes representing 1.0f. IEEE-754 binary32 1.0 = 0x3F800000.
        // Endianness matches our LE wire convention.
        for (size_t k = 0; k < 4194304; ++k) {
            uint32_t bits = 0x3F800000;
            uint8_t* p = out.bytes.data() + k * 4;
            for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>((bits >> (i * 8)) & 0xFF);
        }
        return true;
    }
    if (name == "empty") {
        out.dtype = "i32";
        out.shape = {0};
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    Args args;
    std::string err;
    if (!parseArgs(argc, argv, args, err)) {
        std::fprintf(stderr, "topo-debug-mock-adapter: %s\n", err.c_str());
        return 2;
    }
    Fixture fx;
    if (!buildFixture(args.fixture, fx)) {
        std::fprintf(stderr, "topo-debug-mock-adapter: unknown fixture '%s'\n",
                     args.fixture.c_str());
        return 2;
    }

    ByteSink sink = byteSinkToStdout();

    // 1. breakpoint_hit event.
    json hit = {
        {"kind", "breakpoint_hit"},
        {"frame", 1},
        {"site", args.site},
    };
    writeJsonLine(sink, hit);

    if (args.delayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(args.delayMs));
    }

    // 2. var_bytes binary frame.
    BinaryFrame varFrame;
    varFrame.type = BinaryFrameType::VarBytes;
    varFrame.flags = 0;
    varFrame.frameId = 1;
    varFrame.payload = std::move(fx.bytes);
    writeBinaryFrame(sink, varFrame);

    // 3. layout_descriptor binary frame (JSON body inside binary envelope).
    json layout = {
        {"variable", "matrix"},
        {"dtype", fx.dtype},
        {"shape", fx.shape},
    };
    std::string layoutStr = layout.dump();
    BinaryFrame layoutFrame;
    layoutFrame.type = BinaryFrameType::LayoutDescriptor;
    layoutFrame.flags = 0;
    layoutFrame.frameId = 1;
    layoutFrame.payload.assign(layoutStr.begin(), layoutStr.end());
    writeBinaryFrame(sink, layoutFrame);

    // 4. Wait for a JSON line containing op=continue (or EOF) on stdin.
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            json j = json::parse(line);
            if (j.contains("op") && j["op"].is_string() && j["op"] == "continue") {
                return 0;
            }
        } catch (...) {
            // Ignore non-JSON lines silently; Compute may close stdin instead.
        }
    }
    return 0;
}
