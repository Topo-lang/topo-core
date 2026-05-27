#include "topo/Platform/Process.h"

#include <reproc++/drain.hpp>
#include <reproc++/reproc.hpp>

#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace topo::platform {

// Wraps reproc++ (third_party/reproc, MIT). Public Process.h surface
// unchanged; the Windows-/POSIX-specific private fields on PipedProcess are
// vestigial after this refactor (kept for ABI, retire in a future break).
// Behavior preserved (covered by ctest):
//   - closeStdin() closes ONLY stdin; stdout remains drainable.
//   - stop(timeoutMs) waits, then terminates, then kills the child.
//   - runProcessCaptureWithTimeout returns exitCode == -1 on timeout.
//   - isRunning() reaps a terminated child so exitCode() reflects the status.
//   - argv passed without a shell — no quoting, no env substitution.

namespace {

void logCommand(bool verbose, const std::string& exe,
                const std::vector<std::string>& args,
                const std::string& workingDir = {},
                int timeoutMs = -1) {
    if (!verbose) return;
    std::cerr << "  $ ";
    if (!workingDir.empty()) std::cerr << "(cd " << workingDir << ") ";
    std::cerr << exe;
    for (const auto& a : args) std::cerr << " " << a;
    if (timeoutMs >= 0) std::cerr << " (timeout " << timeoutMs << "ms)";
    std::cerr << "\n";
}

// Build the argv vector reproc expects (argv[0] = executable, then args).
std::vector<std::string> buildArgv(const std::string& exe,
                                   const std::vector<std::string>& args) {
    std::vector<std::string> argv;
    argv.reserve(args.size() + 1);
    argv.push_back(exe);
    for (const auto& a : args) argv.push_back(a);
    return argv;
}

// Run helper used by runProcessCapture / runProcessCaptureWithTimeout. When
// timeoutMs < 0 the call waits indefinitely; otherwise it kills the child on
// timeout and sets exitCode = -1.
CapturedProcessResult runCapture(const std::string& exe,
                                 const std::vector<std::string>& args,
                                 const std::string& workingDir,
                                 int timeoutMs) {
    CapturedProcessResult result;

    reproc::options options;
    if (!workingDir.empty()) options.working_directory = workingDir.c_str();
    options.redirect.in.type = reproc::redirect::discard;
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::pipe;

    reproc::process process;
    auto argv = buildArgv(exe, args);
    std::error_code ec = process.start(argv, options);
    if (ec) {
        std::cerr << "error: failed to start process '" << exe << "': "
                  << ec.message() << "\n";
        return result;
    }

    // Drain both pipes via reproc's poll-driven sink (no deadlock vs
    // sequential reads on a full pipe).
    (void) reproc::drain(process,
                         reproc::sink::string(result.stdoutOutput),
                         reproc::sink::string(result.stderrOutput));

    if (timeoutMs < 0) {
        auto [status, waitEc] =
            process.wait(reproc::milliseconds(reproc::infinite));
        if (!waitEc) result.exitCode = status;
        return result;
    }

    auto [status, waitEc] = process.wait(reproc::milliseconds(timeoutMs));
    if (waitEc == std::errc::timed_out) {
        // Force-kill and reap so the child is gone before we return.
        process.stop({{reproc::stop::kill, reproc::milliseconds(0)},
                      {reproc::stop::wait, reproc::milliseconds(5000)},
                      {reproc::stop::noop, reproc::milliseconds(0)}});
        result.exitCode = -1;
    } else if (!waitEc) {
        result.exitCode = status;
    }
    return result;
}

} // namespace

// --- runProcess (fire-and-forget) ---

ProcessResult runProcess(const std::string& executable,
                         const std::vector<std::string>& args, bool verbose) {
    logCommand(verbose, executable, args);

    reproc::options options;
    options.redirect.in.type = reproc::redirect::parent;
    options.redirect.out.type = reproc::redirect::parent;
    options.redirect.err.type = reproc::redirect::parent;

    reproc::process process;
    auto argv = buildArgv(executable, args);
    std::error_code ec = process.start(argv, options);
    if (ec) {
        std::cerr << "error: failed to start process '" << executable
                  << "': " << ec.message() << "\n";
        return ProcessResult{-1};
    }

    auto [status, waitEc] =
        process.wait(reproc::milliseconds(reproc::infinite));
    if (waitEc) return ProcessResult{-1};
    return ProcessResult{status};
}

// --- runProcessCapture / runProcessCaptureWithTimeout ---

CapturedProcessResult runProcessCapture(const std::string& executable,
                                        const std::vector<std::string>& args,
                                        bool verbose) {
    logCommand(verbose, executable, args);
    return runCapture(executable, args, {}, /*timeoutMs=*/-1);
}

CapturedProcessResult runProcessCapture(const std::string& executable,
                                        const std::vector<std::string>& args,
                                        const std::string& workingDir,
                                        bool verbose) {
    logCommand(verbose, executable, args, workingDir);
    return runCapture(executable, args, workingDir, /*timeoutMs=*/-1);
}

CapturedProcessResult runProcessCaptureWithTimeout(
    const std::string& executable, const std::vector<std::string>& args,
    int timeoutMs, bool verbose) {
    logCommand(verbose, executable, args, /*workingDir=*/{}, timeoutMs);
    return runCapture(executable, args, {}, timeoutMs);
}

// PipedProcess — bidirectional pipes backed by reproc::process.
// Per-instance reproc state lives in a side table keyed by `this` because we
// can't add a real member without breaking the public-header ABI (57
// downstream TUs). The map mutex guards only insert/erase/lookup; concrete
// use is single-digit live PipedProcess instances at a time.
// reproc::process is single-use, so each PipedProcess::start() creates a
// fresh PipedProcessImpl — the old one is discarded so a stop()+start()
// cycle on the same PipedProcess works (rust-analyzer restart tests rely
// on this).

struct PipedProcessImpl {
    reproc::process process;
    bool started = false;
    bool stdinClosed = false;
};

namespace {

std::mutex& implsMutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<const PipedProcess*, std::unique_ptr<PipedProcessImpl>>&
impls() {
    static std::unordered_map<const PipedProcess*,
                              std::unique_ptr<PipedProcessImpl>> m;
    return m;
}

PipedProcessImpl* findImpl(const PipedProcess* self) {
    std::lock_guard<std::mutex> lock(implsMutex());
    auto it = impls().find(self);
    return it == impls().end() ? nullptr : it->second.get();
}

void eraseImpl(const PipedProcess* self) {
    std::lock_guard<std::mutex> lock(implsMutex());
    impls().erase(self);
}

} // namespace

PipedProcess::~PipedProcess() {
    stop(1000);
    eraseImpl(this);
}

bool PipedProcess::start(const std::string& executable,
                         const std::vector<std::string>& args) {
    // reproc::process is single-use: after stop() it cannot be restarted.
    // Tear down any prior impl so a restart cycle gets a fresh process.
    // Refuse a true double-start (start called twice without stop in between).
    {
        std::lock_guard<std::mutex> lock(implsMutex());
        auto& slot = impls()[this];
        if (slot && slot->started) return false;
        slot = std::make_unique<PipedProcessImpl>();
    }
    auto& impl = *findImpl(this);

    reproc::options options;
    options.redirect.in.type = reproc::redirect::pipe;
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::parent;

    auto argv = buildArgv(executable, args);
    std::error_code ec = impl.process.start(argv, options);
    if (ec) return false;
    impl.started = true;
    return true;
}

bool PipedProcess::write(const void* data, size_t len) {
    auto* impl = findImpl(this);
    if (!impl || !impl->started || impl->stdinClosed) return false;
    const auto* buf = static_cast<const uint8_t*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        auto [n, ec] = impl->process.write(buf, remaining);
        if (ec || n == 0) return false;
        buf += n;
        remaining -= n;
    }
    return true;
}

int PipedProcess::readByte() {
    auto* impl = findImpl(this);
    if (!impl || !impl->started) return -1;
    uint8_t b;
    auto [n, ec] = impl->process.read(reproc::stream::out, &b, 1);
    if (ec || n == 0) return -1;
    return static_cast<int>(b);
}

size_t PipedProcess::read(void* buf, size_t len) {
    auto* impl = findImpl(this);
    if (!impl || !impl->started) return 0;
    auto [n, ec] = impl->process.read(reproc::stream::out,
                                      static_cast<uint8_t*>(buf), len);
    if (ec) return 0;
    return n;
}

void PipedProcess::closeStdin() {
    auto* impl = findImpl(this);
    if (!impl || !impl->started || impl->stdinClosed) return;
    impl->process.close(reproc::stream::in);
    impl->stdinClosed = true;
}

void PipedProcess::closePipes() {
    auto* impl = findImpl(this);
    if (!impl || !impl->started) return;
    if (!impl->stdinClosed) {
        impl->process.close(reproc::stream::in);
        impl->stdinClosed = true;
    }
    impl->process.close(reproc::stream::out);
    impl->process.close(reproc::stream::err);
}

void PipedProcess::stop(int timeoutMs) {
    auto* impl = findImpl(this);
    if (!impl || !impl->started) return;

    // wait → terminate → kill. Matches the previous SIGTERM/TerminateProcess
    // + blocking waitpid behavior.
    auto [status, ec] = impl->process.stop(
        {{reproc::stop::wait, reproc::milliseconds(timeoutMs)},
         {reproc::stop::terminate, reproc::milliseconds(timeoutMs)},
         {reproc::stop::kill, reproc::milliseconds(5000)}});

    exitStatus_ = ec ? -1 : status;
    impl->started = false;
}

bool PipedProcess::isRunning() const {
    auto* impl = findImpl(this);
    if (!impl || !impl->started) return false;
    // Non-blocking wait — timed_out → still alive; success → reap + capture
    // status so a subsequent exitCode() call returns it (mirrors the old
    // waitpid(WNOHANG) reap path).
    auto [status, ec] = impl->process.wait(reproc::milliseconds(0));
    if (ec == std::errc::timed_out) return true;
    if (!ec) exitStatus_ = status;
    impl->started = false;
    return false;
}

} // namespace topo::platform
