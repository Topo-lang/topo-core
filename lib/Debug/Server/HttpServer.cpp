// POSIX HTTP/1.1 server for `topo debug serve`.
//
// Single-threaded accept loop. Each connection: read request, dispatch one
// handler, write response, close. No keep-alive. No TLS. 127.0.0.1 binding
// is enforced by the caller (HttpServer constructor takes `host`); we do not
// validate it here, since users may legitimately bind to `0.0.0.0` for LAN
// testing.

#include "topo/Debug/Server/HttpServer.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sstream>

#ifdef _WIN32
// Winsock is not linked into this build; serve mode is POSIX-only for now.
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace topo::debug_server {

HttpResponse htmlOk(std::string body) {
    return {200, "text/html; charset=utf-8", std::move(body)};
}
HttpResponse jsOk(std::string body) {
    return {200, "application/javascript; charset=utf-8", std::move(body)};
}
HttpResponse jsonOk(std::string body) {
    return {200, "application/json; charset=utf-8", std::move(body)};
}
HttpResponse textErr(int status, std::string msg) {
    return {status, "text/plain; charset=utf-8", std::move(msg)};
}

struct HttpServer::Impl {
    std::string host;
    uint16_t port;          // requested port (0 = ask OS)
    uint16_t actualPort = 0;// bound port (== port unless 0 requested)
    std::map<std::string, HttpHandler> routes;    // key: "METHOD PATH"
    std::map<std::string, RawHandler> rawRoutes;  // WS upgrade hook
    HttpHandler fallback;
    std::atomic<bool> stopRequested{false};
#ifndef _WIN32
    int listenerFd = -1;
    // Self-pipe: requestStop() (including from a signal handler) writes one byte
    // to wakeWriteFd; the accept loop poll()s wakeReadFd alongside the listener
    // so it wakes promptly on stop instead of relying on close() to interrupt a
    // blocked accept() — which is unreliable cross-thread / cross-context on
    // Linux (the root cause of the CI SIGTERM hang).
    int wakeReadFd = -1;
    int wakeWriteFd = -1;
#endif

    static std::string routeKey(const std::string& method, const std::string& path) {
        return method + " " + path;
    }

    static std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    static const char* reasonPhrase(int status) {
        switch (status) {
            case 200: return "OK";
            case 400: return "Bad Request";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 500: return "Internal Server Error";
            case 503: return "Service Unavailable";
            default:  return "OK";
        }
    }

    // Read until \r\n\r\n is seen, or EOF. Returns the header block string,
    // or empty on error. `leftoverBody` receives any bytes already past the
    // CRLF boundary that belong to the body.
    //
    // POSIX-only — Windows path is unreachable because HttpServer::run()
    // returns early on Windows. Bodies are stubbed for translation only.
    static bool readHeaderBlock(int fd, std::string& out, std::string& leftoverBody) {
#ifdef _WIN32
        (void)fd; (void)out; (void)leftoverBody; return false;
#else
        out.clear();
        leftoverBody.clear();
        char buf[1024];
        constexpr size_t kMaxHeaders = 64 * 1024;
        while (out.size() < kMaxHeaders) {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n == 0) return !out.empty();      // peer closed
            if (n < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            out.append(buf, buf + n);
            auto pos = out.find("\r\n\r\n");
            if (pos != std::string::npos) {
                leftoverBody = out.substr(pos + 4);
                out.resize(pos);
                return true;
            }
        }
        return false; // header block too large
#endif
    }

    static bool readNBytes(int fd, std::string& out, size_t n) {
#ifdef _WIN32
        (void)fd; (void)out; (void)n; return false;
#else
        char buf[4096];
        while (out.size() < n) {
            size_t want = std::min(sizeof(buf), n - out.size());
            ssize_t got = ::recv(fd, buf, want, 0);
            if (got == 0) return false; // peer closed early
            if (got < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            out.append(buf, buf + got);
        }
        return true;
#endif
    }

    static bool parseRequest(const std::string& headerBlock,
                             const std::string& bodyHead,
                             int fd,
                             HttpRequest& req) {
#ifdef _WIN32
        (void)headerBlock; (void)bodyHead; (void)fd; (void)req; return false;
#else
        // Request line: METHOD SP TARGET SP HTTP/x\r\n
        std::istringstream is(headerBlock);
        std::string line;
        if (!std::getline(is, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        {
            std::istringstream rl(line);
            std::string ver;
            if (!(rl >> req.method >> req.path >> ver)) return false;
        }
        // Headers
        while (std::getline(is, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break;
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string name = lower(line.substr(0, colon));
            std::string val = line.substr(colon + 1);
            // trim leading SP/HT
            size_t i = 0;
            while (i < val.size() && (val[i] == ' ' || val[i] == '\t')) ++i;
            val.erase(0, i);
            req.headers[name] = val;
        }
        // Body
        req.body = bodyHead;
        auto cl = req.headers.find("content-length");
        if (cl != req.headers.end()) {
            size_t expected = 0;
            try { expected = static_cast<size_t>(std::stoul(cl->second)); }
            catch (...) { return false; }
            constexpr size_t kMaxBody = 16 * 1024 * 1024; // 16 MiB
            if (expected > kMaxBody) return false;
            if (req.body.size() < expected) {
                if (!readNBytes(fd, req.body, expected)) return false;
            } else if (req.body.size() > expected) {
                req.body.resize(expected);
            }
        }
        return true;
#endif
    }

    static bool writeAll(int fd, const char* data, size_t n) {
#ifdef _WIN32
        (void)fd; (void)data; (void)n; return false;
#else
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
#endif
    }

    static bool writeResponse(int fd, const HttpResponse& r) {
#ifdef _WIN32
        (void)fd; (void)r; return false;
#else
        std::ostringstream os;
        os << "HTTP/1.1 " << r.status << " " << reasonPhrase(r.status) << "\r\n";
        os << "Content-Type: " << r.contentType << "\r\n";
        os << "Content-Length: " << r.body.size() << "\r\n";
        os << "Connection: close\r\n";
        os << "Cache-Control: no-cache\r\n";
        os << "X-Content-Type-Options: nosniff\r\n";
        os << "\r\n";
        std::string head = os.str();
        if (!writeAll(fd, head.data(), head.size())) return false;
        if (!r.body.empty() && !writeAll(fd, r.body.data(), r.body.size())) return false;
        return true;
#endif
    }
};

HttpServer::HttpServer(std::string host, uint16_t port)
    : impl_(std::make_unique<Impl>()) {
    impl_->host = std::move(host);
    impl_->port = port;
}

HttpServer::~HttpServer() {
#ifndef _WIN32
    if (impl_->listenerFd >= 0) ::close(impl_->listenerFd);
    if (impl_->wakeReadFd >= 0) ::close(impl_->wakeReadFd);
    if (impl_->wakeWriteFd >= 0) ::close(impl_->wakeWriteFd);
#endif
}

void HttpServer::route(const std::string& method, const std::string& path, HttpHandler h) {
    impl_->routes[Impl::routeKey(method, path)] = std::move(h);
}

void HttpServer::routeRaw(const std::string& method, const std::string& path, RawHandler h) {
    impl_->rawRoutes[Impl::routeKey(method, path)] = std::move(h);
}

void HttpServer::fallback(HttpHandler h) {
    impl_->fallback = std::move(h);
}

uint16_t HttpServer::actualPort() const {
    return impl_->actualPort != 0 ? impl_->actualPort : impl_->port;
}

void HttpServer::requestStop() {
    impl_->stopRequested.store(true);
#ifndef _WIN32
    // Wake the accept loop via the self-pipe. write() is async-signal-safe, so
    // this is safe to call from a SIGTERM/SIGINT handler. We deliberately do NOT
    // close the listener here: poll() needs a valid listener fd, and closing it
    // from another thread/handler races the accept loop. The listener is closed
    // in the destructor.
    if (impl_->wakeWriteFd >= 0) {
        const char b = 1;
        ssize_t n = ::write(impl_->wakeWriteFd, &b, 1);
        (void)n;  // best-effort; a full non-blocking pipe already means a wake is pending
    } else {
        // Fallback when the self-pipe could not be created: close the listener
        // to interrupt accept() (legacy behavior).
        int fd = impl_->listenerFd;
        if (fd >= 0) {
            impl_->listenerFd = -1;
            ::close(fd);
        }
    }
#endif
}

int HttpServer::run(std::string& listeningAddr, std::function<void()> onListen) {
#ifdef _WIN32
    (void)listeningAddr;
    (void)onListen;
    std::fprintf(stderr, "topo-debug serve: not supported on Windows in this build\n");
    return 2;
#else
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::fprintf(stderr, "topo-debug serve: socket() failed: %s\n", std::strerror(errno));
        return 1;
    }
    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(impl_->port);
    if (::inet_pton(AF_INET, impl_->host.c_str(), &addr.sin_addr) != 1) {
        std::fprintf(stderr, "topo-debug serve: invalid host '%s' (use a numeric IPv4 like 127.0.0.1)\n",
                     impl_->host.c_str());
        ::close(fd);
        return 1;
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "topo-debug serve: bind %s:%u failed: %s\n",
                     impl_->host.c_str(), impl_->port, std::strerror(errno));
        ::close(fd);
        return 1;
    }
    if (::listen(fd, 8) < 0) {
        std::fprintf(stderr, "topo-debug serve: listen() failed: %s\n", std::strerror(errno));
        ::close(fd);
        return 1;
    }
    impl_->listenerFd = fd;

    // Self-pipe for prompt, signal-safe wakeup of the accept loop on stop. If
    // pipe creation fails we leave the fds at -1 and requestStop() falls back to
    // closing the listener.
    {
        int wp[2];
        if (::pipe(wp) == 0) {
            impl_->wakeReadFd = wp[0];
            impl_->wakeWriteFd = wp[1];
            for (int wfd : {impl_->wakeReadFd, impl_->wakeWriteFd}) {
                int fl = ::fcntl(wfd, F_GETFL, 0);
                if (fl >= 0) ::fcntl(wfd, F_SETFL, fl | O_NONBLOCK);
                int fdFlags = ::fcntl(wfd, F_GETFD, 0);
                if (fdFlags >= 0) ::fcntl(wfd, F_SETFD, fdFlags | FD_CLOEXEC);
            }
        }
    }

    // when the caller requested port 0, resolve the OS-assigned
    // port via getsockname() so `actualPort()` reads correctly and the
    // logging line below names the real port. Tests rely on this to grab a
    // free ephemeral port without coordinating an allocation table.
    {
        sockaddr_in bound{};
        socklen_t boundLen = sizeof(bound);
        if (::getsockname(fd, reinterpret_cast<sockaddr*>(&bound), &boundLen) == 0) {
            impl_->actualPort = ntohs(bound.sin_port);
        } else {
            impl_->actualPort = impl_->port;
        }
    }
    {
        std::ostringstream os;
        os << "http://" << impl_->host << ":" << impl_->actualPort;
        listeningAddr = os.str();
    }
    if (onListen) onListen();

    while (!impl_->stopRequested.load()) {
        // Wait for either an incoming connection or a stop wakeup. poll() (rather
        // than a bare blocking accept) lets requestStop() — including from a
        // signal handler — wake us immediately via the self-pipe, instead of
        // depending on close() to interrupt accept() (unreliable on Linux).
        struct pollfd pfds[2];
        pfds[0].fd = impl_->listenerFd;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        nfds_t nfds = 1;
        if (impl_->wakeReadFd >= 0) {
            pfds[1].fd = impl_->wakeReadFd;
            pfds[1].events = POLLIN;
            pfds[1].revents = 0;
            nfds = 2;
        }
        int pr = ::poll(pfds, nfds, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            if (impl_->stopRequested.load()) break;
            std::fprintf(stderr, "topo-debug serve: poll failed: %s\n", std::strerror(errno));
            break;
        }
        if (impl_->stopRequested.load()) break;
        if (nfds == 2 && (pfds[1].revents & POLLIN)) {
            // Drain the wake pipe; the stopRequested check is the real exit gate,
            // so we just clear the readable state and re-loop.
            char drain[64];
            while (::read(impl_->wakeReadFd, drain, sizeof(drain)) > 0) {
            }
            if (impl_->stopRequested.load()) break;
        }
        if (!(pfds[0].revents & POLLIN)) continue;

        sockaddr_in cli{};
        socklen_t cliLen = sizeof(cli);
        int conn = ::accept(impl_->listenerFd, reinterpret_cast<sockaddr*>(&cli), &cliLen);
        if (conn < 0) {
            if (errno == EINTR) continue;
            if (impl_->stopRequested.load()) break;
            std::fprintf(stderr, "topo-debug serve: accept failed: %s\n", std::strerror(errno));
            break;
        }
        std::string headerBlock, bodyHead;
        if (!Impl::readHeaderBlock(conn, headerBlock, bodyHead)) {
            Impl::writeResponse(conn, textErr(400, "malformed request"));
            ::close(conn);
            continue;
        }
        HttpRequest req;
        if (!Impl::parseRequest(headerBlock, bodyHead, conn, req)) {
            Impl::writeResponse(conn, textErr(400, "malformed request"));
            ::close(conn);
            continue;
        }
        // raw routes get first crack (WS upgrade). The handler
        // takes ownership of the response cycle on this connection; we just
        // close the fd after it returns. A return value of `false` signals
        // "fall back to normal 400" (e.g. handshake failure).
        auto rawIt = impl_->rawRoutes.find(Impl::routeKey(req.method, req.path));
        if (rawIt != impl_->rawRoutes.end()) {
            bool handled = false;
            try {
                handled = rawIt->second(req, conn);
            } catch (const std::exception& e) {
                Impl::writeResponse(conn, textErr(500, std::string("ws handler threw: ") + e.what()));
            }
            (void)handled; // success or failure, the handler owned the socket
            ::close(conn);
            continue;
        }

        HttpResponse resp;
        auto it = impl_->routes.find(Impl::routeKey(req.method, req.path));
        if (it != impl_->routes.end()) {
            try {
                resp = it->second(req);
            } catch (const std::exception& e) {
                resp = textErr(500, std::string("handler threw: ") + e.what());
            }
        } else if (impl_->fallback) {
            try {
                resp = impl_->fallback(req);
            } catch (const std::exception& e) {
                resp = textErr(500, std::string("handler threw: ") + e.what());
            }
        } else {
            resp = textErr(404, "no route for " + req.method + " " + req.path);
        }
        Impl::writeResponse(conn, resp);
        ::close(conn);
    }
    if (impl_->listenerFd >= 0) {
        ::close(impl_->listenerFd);
        impl_->listenerFd = -1;
    }
    return 0;
#endif
}

} // namespace topo::debug_server
