#ifndef TOPO_DEBUG_SERVER_HTTPSERVER_H
#define TOPO_DEBUG_SERVER_HTTPSERVER_H

// Minimal HTTP/1.1 server backing `topo debug serve`.
//
// Single-threaded, blocking accept loop bound to 127.0.0.1 by default. Each
// connection is read/served/closed serially — `Connection: close` is the only
// supported keep-alive policy. Sufficient for the local debug UI: a single
// browser tab against one project. No TLS, no auth — local loopback only.
//
// Cross-platform: POSIX sockets on macOS/Linux. Windows currently emits a
// runtime error from `bind()` since the build does not link Winsock; gating
// at the CMake level is out of scope right now (no Windows debug story).

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace topo::debug_server {

struct HttpRequest {
    std::string method;
    std::string path;
    // Lower-cased header names → raw value (last write wins).
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/octet-stream";
    std::string body;
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

// WebSocket upgrade hook. The "raw" handler receives the parsed
// request *and* the connection fd. It is responsible for completing the WS
// handshake (Sec-WebSocket-Accept) and running the framed loop until the
// peer closes; the server only closes the socket after the handler returns.
// Returning false signals "fall back to a normal 400 response".
using RawHandler = std::function<bool(const HttpRequest& req, int connFd)>;

// Convenience constructors for common response shapes.
HttpResponse htmlOk(std::string body);
HttpResponse jsOk(std::string body);
HttpResponse jsonOk(std::string body);
HttpResponse textErr(int status, std::string msg);

class HttpServer {
public:
    // `port == 0` requests OS-assigned ephemeral port. Read back via
    // `actualPort()` after `start()` succeeds.
    HttpServer(std::string host, uint16_t port);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void route(const std::string& method, const std::string& path, HttpHandler h);

    // register a route that bypasses the normal response-builder
    // path and takes over the connection fd (used by GET /ws upgrades).
    void routeRaw(const std::string& method, const std::string& path, RawHandler h);

    // Called when no exact (method, path) route matches. Default returns 404.
    void fallback(HttpHandler h);

    // Blocks until `requestStop()` is called from a signal handler or the
    // socket layer errors out. Returns 0 on clean stop, non-zero on bind/listen
    // failure. The string `out` receives the listening address ("http://...")
    // once the socket is up — useful for logging from the caller. When
    // `onListen` is set, it fires from inside `run()` once the listener is
    // bound; `actualPort()` is safe to query from there.
    int run(std::string& listeningAddr, std::function<void()> onListen = {});

    // Signal-safe. Sets a flag the accept loop checks between connections.
    // To unblock a blocked accept(), the kernel needs poke — we close the
    // listener fd from the same thread on the next iteration; callers must
    // hit Ctrl-C to wake an idle accept().
    void requestStop();

    // actual bound port (resolved via getsockname() when the
    // constructor's `port == 0`). Returns the constructor port if `run()`
    // has not yet succeeded in binding.
    uint16_t actualPort() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace topo::debug_server

#endif // TOPO_DEBUG_SERVER_HTTPSERVER_H
