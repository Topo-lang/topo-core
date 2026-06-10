#include <gtest/gtest.h>

#include "topo/Debug/Server/WebSocket.h"

using topo::debug_server::wsOriginAllowed;

// The debug server's WebSocket upgrade must reject cross-origin browser pages
// (Cross-Site WebSocket Hijacking) while still admitting native clients, which
// send no Origin, and the server's own loopback-served SPA.

TEST(WebSocketOriginTest, AllowsAbsentOrigin) {
    // Native clients (e.g. the serve_ws_query.sh raw-socket path) send no Origin.
    EXPECT_TRUE(wsOriginAllowed(""));
}

TEST(WebSocketOriginTest, AllowsLoopbackOrigins) {
    EXPECT_TRUE(wsOriginAllowed("http://127.0.0.1:7300"));
    EXPECT_TRUE(wsOriginAllowed("http://127.0.0.1"));
    EXPECT_TRUE(wsOriginAllowed("http://localhost:7300"));
    EXPECT_TRUE(wsOriginAllowed("https://localhost"));
    EXPECT_TRUE(wsOriginAllowed("http://[::1]:7300"));
}

TEST(WebSocketOriginTest, RejectsCrossOrigin) {
    EXPECT_FALSE(wsOriginAllowed("http://evil.com"));
    EXPECT_FALSE(wsOriginAllowed("https://evil.example:443"));
    // Look-alike hosts must not satisfy the loopback check.
    EXPECT_FALSE(wsOriginAllowed("http://127.0.0.1.evil.com"));
    EXPECT_FALSE(wsOriginAllowed("http://localhost.evil.com"));
}

TEST(WebSocketOriginTest, RejectsOpaqueAndMalformed) {
    EXPECT_FALSE(wsOriginAllowed("null"));     // sandboxed iframe / file:// origin
    EXPECT_FALSE(wsOriginAllowed("evil.com")); // no scheme
    EXPECT_FALSE(wsOriginAllowed("http://"));  // empty host
}
