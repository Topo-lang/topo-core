// Minimal RFC 6455 WebSocket helpers for `topo debug serve`.
//
// SHA1 and base64 are implemented inline (≈70 lines combined) to avoid
// pulling in OpenSSL just for one handshake. The frame codec only supports
// what the SPA needs: text frames, no extension framing, no fragmentation,
// payload size in the short form (≤ 65535 B). See WebSocket.h for rationale.

#include "topo/Debug/Server/WebSocket.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace topo::debug_server {

namespace {

// ---------- SHA-1 (RFC 3174, 160-bit, 20-byte digest) -----------------------
//
// Single-call API: `sha1(input)` returns the 20-byte digest as std::string.
// Inputs are bounded by HTTP header sizes and small JSON frames, so the
// straightforward block-by-block implementation is fine.

inline uint32_t rotl32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

std::string sha1(const std::string& msg) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    // Padded message: original + 0x80 + zero-pad + 64-bit length in bits.
    std::string m = msg;
    uint64_t bitLen = static_cast<uint64_t>(msg.size()) * 8u;
    m.push_back(static_cast<char>(0x80));
    while (m.size() % 64 != 56) m.push_back('\0');
    for (int i = 7; i >= 0; --i) {
        m.push_back(static_cast<char>((bitLen >> (i * 8)) & 0xff));
    }
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(static_cast<uint8_t>(m[off + i * 4])) << 24)
                 | (static_cast<uint32_t>(static_cast<uint8_t>(m[off + i * 4 + 1])) << 16)
                 | (static_cast<uint32_t>(static_cast<uint8_t>(m[off + i * 4 + 2])) << 8)
                 | static_cast<uint32_t>(static_cast<uint8_t>(m[off + i * 4 + 3]));
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;            k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;            k = 0xCA62C1D6; }
            uint32_t tmp = rotl32(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl32(b, 30); b = a; a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    std::string out(20, '\0');
    auto putBE = [&](size_t idx, uint32_t v) {
        out[idx]     = static_cast<char>((v >> 24) & 0xff);
        out[idx + 1] = static_cast<char>((v >> 16) & 0xff);
        out[idx + 2] = static_cast<char>((v >> 8)  & 0xff);
        out[idx + 3] = static_cast<char>(v         & 0xff);
    };
    putBE(0, h0); putBE(4, h1); putBE(8, h2); putBE(12, h3); putBE(16, h4);
    return out;
}

// ---------- base64 (standard alphabet, padded) ------------------------------

std::string base64(const std::string& in) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 3 <= in.size(); i += 3) {
        uint32_t v = (static_cast<uint8_t>(in[i]) << 16)
                   | (static_cast<uint8_t>(in[i + 1]) << 8)
                   | static_cast<uint8_t>(in[i + 2]);
        out.push_back(tbl[(v >> 18) & 0x3f]);
        out.push_back(tbl[(v >> 12) & 0x3f]);
        out.push_back(tbl[(v >>  6) & 0x3f]);
        out.push_back(tbl[v & 0x3f]);
    }
    if (i < in.size()) {
        uint32_t v = static_cast<uint8_t>(in[i]) << 16;
        if (i + 1 < in.size()) v |= static_cast<uint8_t>(in[i + 1]) << 8;
        out.push_back(tbl[(v >> 18) & 0x3f]);
        out.push_back(tbl[(v >> 12) & 0x3f]);
        out.push_back((i + 1 < in.size()) ? tbl[(v >> 6) & 0x3f] : '=');
        out.push_back('=');
    }
    return out;
}

#ifndef _WIN32

bool writeAll(int fd, const char* data, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = ::send(fd, data + off, n - off, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += static_cast<size_t>(w);
    }
    return true;
}

bool readAll(int fd, char* out, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = ::recv(fd, out + off, n - off, 0);
        if (r == 0) return false;
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += static_cast<size_t>(r);
    }
    return true;
}

#endif // !_WIN32

// Send a one-frame, server→client (unmasked) message with the given opcode.
// Used for both text replies and control frames (pong, close).
bool sendFrameOp(int fd, uint8_t opcode, const std::string& payload) {
#ifdef _WIN32
    (void)fd; (void)opcode; (void)payload;
    return false;
#else
    // FIN=1, RSV=0, opcode in low 4 bits.
    std::string head;
    head.push_back(static_cast<char>(0x80 | (opcode & 0x0f)));
    if (payload.size() <= 125) {
        head.push_back(static_cast<char>(payload.size()));
    } else if (payload.size() <= 65535) {
        head.push_back(static_cast<char>(126));
        head.push_back(static_cast<char>((payload.size() >> 8) & 0xff));
        head.push_back(static_cast<char>(payload.size() & 0xff));
    } else {
        // Per WebSocket.h contract, we never produce frames this large.
        return false;
    }
    if (!writeAll(fd, head.data(), head.size())) return false;
    if (!payload.empty() && !writeAll(fd, payload.data(), payload.size())) return false;
    return true;
#endif
}

} // namespace

std::string wsAcceptHeader(const std::string& clientKey) {
    // The fixed GUID is required by RFC 6455 §1.3.
    static const char* kMagic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    return base64(sha1(clientKey + kMagic));
}

// Pure (no socket I/O): defined unconditionally so it is testable on every
// platform. Allows an absent Origin (non-browser client) or a loopback origin;
// rejects every cross-origin value, including the opaque "null".
bool wsOriginAllowed(const std::string& origin) {
    if (origin.empty()) return true;
    auto schemeEnd = origin.find("://");
    if (schemeEnd == std::string::npos) return false; // "null" / opaque / malformed
    std::string rest = origin.substr(schemeEnd + 3);
    std::string host;
    if (!rest.empty() && rest.front() == '[') {        // IPv6 literal, e.g. [::1]
        auto close = rest.find(']');
        if (close == std::string::npos) return false;
        host = rest.substr(1, close - 1);
    } else {
        host = rest.substr(0, rest.find_first_of(":/"));
    }
    return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

bool wsCompleteHandshake(int connFd,
                         const std::map<std::string, std::string>& headers) {
#ifdef _WIN32
    (void)connFd; (void)headers;
    return false;
#else
    auto it = headers.find("sec-websocket-key");
    if (it == headers.end() || it->second.empty()) return false;
    // Cross-Site WebSocket Hijacking defense: browsers always send Origin on a
    // WS upgrade, so a cross-origin page the developer happens to visit must not
    // be able to open ws://127.0.0.1:<port>/ws and drive the debugger. Reject
    // any non-loopback Origin; native clients (no Origin) are unaffected.
    auto originIt = headers.find("origin");
    if (originIt != headers.end() && !wsOriginAllowed(originIt->second)) {
        std::string deny =
            "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        writeAll(connFd, deny.data(), deny.size());
        return false;
    }
    std::string accept = wsAcceptHeader(it->second);
    std::string reply;
    reply += "HTTP/1.1 101 Switching Protocols\r\n";
    reply += "Upgrade: websocket\r\n";
    reply += "Connection: Upgrade\r\n";
    reply += "Sec-WebSocket-Accept: " + accept + "\r\n";
    reply += "\r\n";
    return writeAll(connFd, reply.data(), reply.size());
#endif
}

bool wsRecvTextFrame(int connFd, std::string& out) {
#ifdef _WIN32
    (void)connFd; (void)out;
    return false;
#else
    out.clear();
    for (;;) {
        char hdr[2];
        if (!readAll(connFd, hdr, 2)) return false;
        uint8_t b0 = static_cast<uint8_t>(hdr[0]);
        uint8_t b1 = static_cast<uint8_t>(hdr[1]);
        bool fin = (b0 & 0x80) != 0;
        uint8_t opcode = b0 & 0x0f;
        bool masked = (b1 & 0x80) != 0;
        // Per RFC 6455 §5.1 every client→server frame MUST be masked.
        if (!masked) return false;
        uint64_t payLen = b1 & 0x7f;
        if (payLen == 126) {
            char extra[2];
            if (!readAll(connFd, extra, 2)) return false;
            payLen = (static_cast<uint64_t>(static_cast<uint8_t>(extra[0])) << 8)
                   | static_cast<uint64_t>(static_cast<uint8_t>(extra[1]));
        } else if (payLen == 127) {
            // We never advertise / accept this. Cap at 64 KiB to match the
            // outbound contract; bail otherwise.
            return false;
        }
        // Defensive — even the short form must not exceed 64 KiB on
        // this server (SPA requests are tiny JSON).
        if (payLen > 65535) return false;
        char mask[4];
        if (!readAll(connFd, mask, 4)) return false;
        std::string payload(static_cast<size_t>(payLen), '\0');
        if (payLen && !readAll(connFd, payload.data(), payload.size())) return false;
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<char>(static_cast<uint8_t>(payload[i]) ^
                                            static_cast<uint8_t>(mask[i % 4]));
        }
        switch (opcode) {
            case 0x0: // continuation — we don't fragment; treat as protocol error
                return false;
            case 0x1: // text
                if (!fin) return false; // no fragmentation support
                out = std::move(payload);
                return true;
            case 0x2: // binary — unsupported on this server
                return false;
            case 0x8: // close
                // Reply with a close frame (echo payload if present) and
                // signal end-of-stream to the caller.
                sendFrameOp(connFd, 0x8, payload);
                return false;
            case 0x9: // ping → auto-pong, then loop for next frame
                sendFrameOp(connFd, 0xA, payload);
                continue;
            case 0xA: // pong — silently swallow
                continue;
            default:
                return false;
        }
    }
#endif
}

bool wsSendTextFrame(int connFd, const std::string& payload) {
    return sendFrameOp(connFd, 0x1, payload);
}

} // namespace topo::debug_server
