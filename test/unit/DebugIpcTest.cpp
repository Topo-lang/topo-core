// IPC framing tests.
//
// Coverage matrix:
//   1. Round-trip 50+ frames through serialize/parse, mixed types/flags.
//   2. Interleaved JSON + binary stream (10 JSON, 10 binary) decodes
//      correctly when interleaved.
//   3. Truncated frame returns ReadStatus::Truncated (clean error).
//   4. JSON line read works; bad JSON yields ReadStatus::BadJson.
//   5. SHM_REF descriptor parses (the body layout is defined; the
//      Compute query path errors on read — covered in evaluator tests).
//   6. Reserved-nonzero bytes in header are rejected.

#include "topo/Debug/Ipc/BinaryFrame.h"
#include "topo/Debug/Ipc/FrameReader.h"
#include "topo/Debug/Ipc/FrameWriter.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

using namespace topo::debug_ipc;

namespace {

BinaryFrame makeFrame(BinaryFrameType type, uint64_t id, std::vector<uint8_t> payload) {
    BinaryFrame f;
    f.type = type;
    f.frameId = id;
    f.payload = std::move(payload);
    return f;
}

ByteSource sourceOver(const std::string& s, size_t& cursor) {
    cursor = 0;
    return byteSourceFromString(s, &cursor);
}

} // namespace

TEST(DebugIpc, BinaryFrameRoundTrip50) {
    // 50 frames with varying types, ids, payload sizes.
    std::string buf;
    auto sink = byteSinkToString(buf);
    std::vector<BinaryFrame> originals;
    for (int i = 0; i < 50; ++i) {
        BinaryFrameType t = (i % 3 == 0) ? BinaryFrameType::VarBytes
                          : (i % 3 == 1) ? BinaryFrameType::LayoutDescriptor
                                         : BinaryFrameType::ChunkContinuation;
        std::vector<uint8_t> payload((i * 7) % 257, 0);
        for (size_t k = 0; k < payload.size(); ++k) payload[k] = static_cast<uint8_t>(i + k);
        auto f = makeFrame(t, /*id=*/static_cast<uint64_t>(i), std::move(payload));
        originals.push_back(f);
        ASSERT_TRUE(writeBinaryFrame(sink, f));
    }
    size_t cursor = 0;
    auto src = sourceOver(buf, cursor);
    for (size_t i = 0; i < originals.size(); ++i) {
        ReadResult r = readRecord(src);
        ASSERT_EQ(r.status, ReadStatus::Ok) << "frame " << i << ": " << r.error;
        ASSERT_TRUE(r.frame.has_value());
        EXPECT_EQ(r.frame->type, originals[i].type);
        EXPECT_EQ(r.frame->frameId, originals[i].frameId);
        EXPECT_EQ(r.frame->payload, originals[i].payload);
    }
    ReadResult tail = readRecord(src);
    EXPECT_EQ(tail.status, ReadStatus::Eof);
}

TEST(DebugIpc, InterleavedJsonAndBinary) {
    // Pattern: 10 JSON, then 10 binary, then 10 JSON, then 10 binary —
    // 40 records in mixed boundary order to exercise the discriminator path.
    std::string buf;
    auto sink = byteSinkToString(buf);
    std::vector<nlohmann::json> jsons;
    std::vector<BinaryFrame> bins;
    for (int i = 0; i < 10; ++i) {
        nlohmann::json j = {{"op", "read_var"}, {"frame", i}};
        jsons.push_back(j);
        writeJsonLine(sink, j);
    }
    for (int i = 0; i < 10; ++i) {
        auto f = makeFrame(BinaryFrameType::VarBytes, i, std::vector<uint8_t>(i * 3, 0xAB));
        bins.push_back(f);
        writeBinaryFrame(sink, f);
    }
    for (int i = 10; i < 20; ++i) {
        nlohmann::json j = {{"op", "continue"}, {"i", i}};
        jsons.push_back(j);
        writeJsonLine(sink, j);
    }
    for (int i = 10; i < 20; ++i) {
        auto f = makeFrame(BinaryFrameType::LayoutDescriptor, i,
                           std::vector<uint8_t>(i, 0xCD));
        bins.push_back(f);
        writeBinaryFrame(sink, f);
    }

    // Drain — expect the same interleave: 10 JSON, 10 binary, 10 JSON, 10 binary.
    size_t cursor = 0;
    auto src = sourceOver(buf, cursor);
    size_t jIdx = 0, bIdx = 0;
    auto drainPhase = [&](int count, bool expectJson) {
        for (int i = 0; i < count; ++i) {
            ReadResult r = readRecord(src);
            ASSERT_EQ(r.status, ReadStatus::Ok)
                << "rec " << i << " in phase: " << r.error;
            if (expectJson) {
                ASSERT_TRUE(r.json.has_value());
                EXPECT_EQ(*r.json, jsons[jIdx++]);
            } else {
                ASSERT_TRUE(r.frame.has_value());
                EXPECT_EQ(r.frame->frameId, bins[bIdx].frameId);
                EXPECT_EQ(r.frame->type, bins[bIdx].type);
                EXPECT_EQ(r.frame->payload, bins[bIdx].payload);
                ++bIdx;
            }
        }
    };
    drainPhase(10, /*expectJson=*/true);
    drainPhase(10, /*expectJson=*/false);
    drainPhase(10, /*expectJson=*/true);
    drainPhase(10, /*expectJson=*/false);

    ReadResult eof = readRecord(src);
    EXPECT_EQ(eof.status, ReadStatus::Eof);
}

TEST(DebugIpc, TruncatedFrameReturnsCleanError) {
    // Build a frame; cut bytes from the end. Confirm Truncated status.
    std::string buf;
    auto sink = byteSinkToString(buf);
    auto f = makeFrame(BinaryFrameType::VarBytes, 42,
                       std::vector<uint8_t>(64, 0xEE));
    writeBinaryFrame(sink, f);
    // Drop the last 10 bytes of the payload.
    std::string truncated = buf.substr(0, buf.size() - 10);
    size_t cursor = 0;
    auto src = sourceOver(truncated, cursor);
    ReadResult r = readRecord(src);
    EXPECT_EQ(r.status, ReadStatus::Truncated);

    // Cut to mid-header: keep first 5 bytes only.
    std::string hdrTrunc = buf.substr(0, 5);
    cursor = 0;
    auto src2 = sourceOver(hdrTrunc, cursor);
    ReadResult r2 = readRecord(src2);
    EXPECT_EQ(r2.status, ReadStatus::Truncated);
}

TEST(DebugIpc, BadJsonLine) {
    std::string buf = "{not valid json\n";
    size_t cursor = 0;
    auto src = sourceOver(buf, cursor);
    ReadResult r = readRecord(src);
    EXPECT_EQ(r.status, ReadStatus::BadJson);
}

TEST(DebugIpc, ShmRefDescriptorRoundTrip) {
    ShmRef ref{};
    ref.pathOffset = 0xDEADBEEF;
    ref.len = 1024 * 1024;
    auto bytes = ref.serialize();
    EXPECT_EQ(bytes.size(), 16u);
    ByteSpan span{bytes.data(), bytes.size()};
    auto parsed = ShmRef::parse(span);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->pathOffset, ref.pathOffset);
    EXPECT_EQ(parsed->len, ref.len);
}

TEST(DebugIpc, ReservedNonZeroRejected) {
    // Manually craft a frame with reserved[0] = 1 — must be rejected.
    BinaryFrame f = makeFrame(BinaryFrameType::VarBytes, 1, {0xAA});
    auto bytes = f.serialize();
    bytes[6] = 0x01; // poison reserved
    size_t cursor = 0;
    std::string buf(bytes.begin(), bytes.end());
    auto src = sourceOver(buf, cursor);
    ReadResult r = readRecord(src);
    EXPECT_EQ(r.status, ReadStatus::ReservedNonZero);
}

TEST(DebugIpc, BinaryFrameMagicFirstByteIsT) {
    // Asserts the on-wire byte 0 is 'T' (0x54); the reader routes by this.
    BinaryFrame f = makeFrame(BinaryFrameType::VarBytes, 0, {});
    auto bytes = f.serialize();
    ASSERT_GE(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], 0x54u);
}
