#ifndef TOPO_DEBUG_SERVER_WEBSOCKET_H
#define TOPO_DEBUG_SERVER_WEBSOCKET_H

// Minimal RFC 6455 WebSocket helpers for the embedded debug
// server. Implements only the subset the SPA needs:
//   - server-side handshake (Sec-WebSocket-Accept).
//   - text frames in both directions, client→server masked, server→client
//     unmasked, payload ≤ 64 KiB (no extended length form).
//   - clean close (opcode 0x8) handling.
//
// No streaming, no fragmentation, no continuation frames, no binary frames,
// no permessage-deflate. The SPA only sends small JSON requests; the server
// only emits small JSON replies — well under the 65535 byte short-form cap.

#include <cstdint>
#include <map>
#include <string>

namespace topo::debug_server {

// Compute Sec-WebSocket-Accept = base64(sha1(clientKey + magic_guid)).
std::string wsAcceptHeader(const std::string& clientKey);

// Returns true if a WebSocket upgrade carrying this `Origin` header value may
// proceed: an empty value (non-browser client, which sends no Origin) or a
// loopback origin (127.0.0.1 / localhost / ::1). Any cross-origin browser page
// — and the opaque "null" origin — is rejected. This is the Cross-Site
// WebSocket Hijacking defense for the loopback debug server.
bool wsOriginAllowed(const std::string& originHeaderValue);

// Write the RFC 6455 server handshake reply to `connFd`. Returns false when
// the request is missing Sec-WebSocket-Key or any write fails. The caller
// has already received the HTTP headers; we only inspect them.
bool wsCompleteHandshake(int connFd, const std::map<std::string, std::string>& headers);

// Read one text-frame payload from the connection. On EOF, peer-close, or
// any frame violation, returns false and leaves `out` undefined. Non-text
// opcodes (close=0x8 → false; ping=0x9 → auto-pong; pong=0xA → ignored)
// are handled internally and the loop continues until a text frame arrives.
bool wsRecvTextFrame(int connFd, std::string& out);

// Send `payload` as a single unmasked server→client text frame. Returns
// false on any write error. Payload must be ≤ 65535 bytes (the SPA replies
// fit easily).
bool wsSendTextFrame(int connFd, const std::string& payload);

} // namespace topo::debug_server

#endif // TOPO_DEBUG_SERVER_WEBSOCKET_H
