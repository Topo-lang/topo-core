#include "topo/Platform/Process.h"

#include <reproc++/reproc.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <process.h> // _getpid
#else
#  include <unistd.h>  // getpid
#endif

namespace topo::platform {

// Wraps reproc++ (third_party/reproc, MIT). Public Process.h surface
// unchanged; the Windows-/POSIX-specific private fields on PipedProcess are
// vestigial after this refactor (kept for ABI, retire in a future break).
// Behavior preserved (covered by ctest):
//   - closeStdin() closes ONLY stdin; stdout remains drainable.
//   - stop(timeoutMs) waits, then terminates, then kills the child.
//   - runProcessCaptureWithTimeout returns exitCode == -1 on timeout.
//   - capture redirects stdout/stderr to temp files (not pipes) so the Windows
//     loopback-socket EOF that fails to arrive for MinGW children can't hang
//     the call; wait is bounded by a process deadline (exitCode == -1 on hit).
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

int capturePid() {
#ifdef _WIN32
    return _getpid();
#else
    return static_cast<int>(::getpid());
#endif
}

// Unique temp path for capturing one child stream. temp_dir + pid + a
// process-monotonic counter, so concurrent captures (e.g. the e2e harness
// spawning many tools) cannot collide. reproc opens/creates the path for the
// child's redirected stream.
std::string makeCaptureTempPath(const char* tag) {
    static std::atomic<unsigned long long> counter{0};
    const unsigned long long n = counter.fetch_add(1, std::memory_order_relaxed);
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec) dir = std::filesystem::path("."); // fall back to cwd
    std::filesystem::path p =
        dir / ("topo-capture-" + std::to_string(capturePid()) + "-" +
               std::to_string(n) + "-" + tag);
    return p.string();
}

std::string readCaptureTempFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

void removeCaptureTempFile(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

#ifdef _WIN32

// ---- Windows raw-Win32 capture path ----
// reproc's Windows backend drains stdout/stderr and detects child exit over
// loopback sockets; on GHA windows-2022 that exit/EOF notification is not
// reliably delivered for MinGW children, so reproc's drain/wait blocks even for
// an instant-exit process (the tpm-cli / input-trust suites timed out). Bypass
// reproc on Windows: spawn with CreateProcess, redirect stdout/stderr to temp
// FILES (inheritable handles), discard stdin (NUL), and detect exit with
// WaitForSingleObject on the real process HANDLE (reliable) + GetExitCodeProcess.

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

// Quote one argument per the CommandLineToArgvW / MSVC-runtime rules so the
// child reconstructs argv exactly (backslashes are doubled only before a quote
// or at the end of a quoted token).
void appendQuotedArg(std::wstring& cmd, const std::wstring& arg) {
    if (!arg.empty() &&
        arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        cmd += arg;
        return;
    }
    cmd += L'"';
    for (auto it = arg.begin();; ++it) {
        unsigned backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            cmd.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            cmd.append(backslashes * 2 + 1, L'\\');
            cmd += L'"';
        } else {
            cmd.append(backslashes, L'\\');
            cmd += *it;
        }
    }
    cmd += L'"';
}

std::wstring buildCommandLineW(const std::string& exe,
                               const std::vector<std::string>& args) {
    std::wstring cmd;
    appendQuotedArg(cmd, utf8ToWide(exe));
    for (const auto& a : args) {
        cmd += L' ';
        appendQuotedArg(cmd, utf8ToWide(a));
    }
    return cmd;
}

HANDLE openInheritableFile(const std::string& path, DWORD access,
                           DWORD disposition) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;
    return CreateFileW(utf8ToWide(path).c_str(), access,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, disposition,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}

CapturedProcessResult runCaptureWindows(const std::string& exe,
                                        const std::vector<std::string>& args,
                                        const std::string& workingDir,
                                        int timeoutMs) {
    CapturedProcessResult result;
    const std::string outPath = makeCaptureTempPath("out");
    const std::string errPath = makeCaptureTempPath("err");

    HANDLE hOut = openInheritableFile(outPath, GENERIC_WRITE, CREATE_ALWAYS);
    HANDLE hErr = openInheritableFile(errPath, GENERIC_WRITE, CREATE_ALWAYS);
    HANDLE hIn = openInheritableFile("NUL", GENERIC_READ, OPEN_EXISTING);
    if (hOut == INVALID_HANDLE_VALUE || hErr == INVALID_HANDLE_VALUE ||
        hIn == INVALID_HANDLE_VALUE) {
        std::cerr << "error: failed to open capture handles for '" << exe
                  << "'\n";
        if (hOut != INVALID_HANDLE_VALUE) CloseHandle(hOut);
        if (hErr != INVALID_HANDLE_VALUE) CloseHandle(hErr);
        if (hIn != INVALID_HANDLE_VALUE) CloseHandle(hIn);
        removeCaptureTempFile(outPath);
        removeCaptureTempFile(errPath);
        return result;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hIn;
    si.hStdOutput = hOut;
    si.hStdError = hErr;

    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = buildCommandLineW(exe, args);
    std::wstring wcwd =
        workingDir.empty() ? std::wstring() : utf8ToWide(workingDir);

    BOOL ok = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr,
                             /*bInheritHandles=*/TRUE, /*flags=*/0, nullptr,
                             wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);

    // Drop our copies of the child's std handles; the child holds its own and
    // is the sole writer once we've closed ours.
    CloseHandle(hOut);
    CloseHandle(hErr);
    CloseHandle(hIn);

    if (!ok) {
        std::cerr << "error: failed to start process '" << exe
                  << "': CreateProcess failed (" << GetLastError() << ")\n";
        removeCaptureTempFile(outPath);
        removeCaptureTempFile(errPath);
        return result;
    }

    constexpr int kDefaultCaptureDeadlineMs = 120000; // 120s
    const DWORD waitMs = static_cast<DWORD>(
        timeoutMs >= 0 ? timeoutMs : kDefaultCaptureDeadlineMs);
    DWORD wr = WaitForSingleObject(pi.hProcess, waitMs);
    if (wr == WAIT_OBJECT_0) {
        DWORD code = 0;
        result.exitCode = GetExitCodeProcess(pi.hProcess, &code)
                              ? static_cast<int>(code)
                              : -1;
    } else {
        // Timeout (or wait failure): kill + reap, report failure (exitCode -1).
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        result.exitCode = -1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    result.stdoutOutput = readCaptureTempFile(outPath);
    result.stderrOutput = readCaptureTempFile(errPath);
    removeCaptureTempFile(outPath);
    removeCaptureTempFile(errPath);
    return result;
}

#endif // _WIN32

// Run helper used by runProcessCapture / runProcessCaptureWithTimeout. Windows
// uses the raw-Win32 path above — reproc's loopback-socket exit detection is
// unreliable for MinGW children on GHA windows-2022. Elsewhere reproc spawns the
// child with stdout/stderr redirected to temp FILES (not pipes): the child
// writes to disk and we read it after it exits, so only reproc's dedicated exit
// socket remains (pipe EOF is reliable on POSIX). wait is bounded by a deadline
// so a child that never exits fails at the bound (exitCode = -1) rather than
// hanging.
CapturedProcessResult runCapture(const std::string& exe,
                                 const std::vector<std::string>& args,
                                 const std::string& workingDir,
                                 int timeoutMs) {
#ifdef _WIN32
    return runCaptureWindows(exe, args, workingDir, timeoutMs);
#else
    CapturedProcessResult result;

    const std::string outPath = makeCaptureTempPath("out");
    const std::string errPath = makeCaptureTempPath("err");

    reproc::options options;
    if (!workingDir.empty()) options.working_directory = workingDir.c_str();
    options.redirect.in.type = reproc::redirect::discard;
    options.redirect.out.type = reproc::redirect::path_;
    options.redirect.out.path = outPath.c_str();
    options.redirect.err.type = reproc::redirect::path_;
    options.redirect.err.path = errPath.c_str();

    // Bound the wait. The no-timeout overloads use a default bound (matches CI
    // ctest --timeout); the explicit-timeout overload uses the caller's value.
    constexpr int kDefaultCaptureDeadlineMs = 120000; // 120s
    const int deadlineMs = timeoutMs >= 0 ? timeoutMs : kDefaultCaptureDeadlineMs;
    options.deadline = reproc::milliseconds(deadlineMs);

    reproc::process process;
    auto argv = buildArgv(exe, args);
    std::error_code ec = process.start(argv, options);
    if (ec) {
        std::cerr << "error: failed to start process '" << exe << "': "
                  << ec.message() << "\n";
        removeCaptureTempFile(outPath);
        removeCaptureTempFile(errPath);
        return result;
    }

    auto [status, waitEc] = process.wait(reproc::milliseconds(deadlineMs));
    if (waitEc == std::errc::timed_out) {
        // Deadline hit — force-kill and reap so the child is gone before we
        // return; report failure via exitCode == -1.
        process.stop({{reproc::stop::kill, reproc::milliseconds(0)},
                      {reproc::stop::wait, reproc::milliseconds(5000)},
                      {reproc::stop::noop, reproc::milliseconds(0)}});
        result.exitCode = -1;
    } else if (!waitEc) {
        result.exitCode = status;
    }

    // Read whatever the child wrote (present even on a timeout/partial run),
    // then remove the temp files.
    result.stdoutOutput = readCaptureTempFile(outPath);
    result.stderrOutput = readCaptureTempFile(errPath);
    removeCaptureTempFile(outPath);
    removeCaptureTempFile(errPath);
    return result;
#endif // _WIN32
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
    // Flags are atomic because the reader thread (read/readByte) observes
    // `started` while the control thread (stop/isRunning) flips it, and
    // `stdinClosed` is likewise touched from both sides. ctlMutex serializes
    // the stop()/isRunning() control transition (concurrent reproc control
    // calls on one handle are not guaranteed safe); it is deliberately NOT
    // held across a blocking read, so stop() can always interrupt a stalled
    // reader without deadlock.
    std::atomic<bool> started{false};
    std::atomic<bool> stdinClosed{false};
    std::mutex ctlMutex;
};

namespace {

std::mutex& implsMutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<const PipedProcess*, std::shared_ptr<PipedProcessImpl>>&
impls() {
    static std::unordered_map<const PipedProcess*,
                              std::shared_ptr<PipedProcessImpl>> m;
    return m;
}

// Returns a shared_ptr copy taken under the map mutex, so a concurrent
// start()/destructor that swaps or erases the map slot cannot free the impl
// while this caller still holds it.
std::shared_ptr<PipedProcessImpl> findImpl(const PipedProcess* self) {
    std::lock_guard<std::mutex> lock(implsMutex());
    auto it = impls().find(self);
    return it == impls().end() ? nullptr : it->second;
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
    std::shared_ptr<PipedProcessImpl> impl;
    {
        std::lock_guard<std::mutex> lock(implsMutex());
        auto& slot = impls()[this];
        if (slot && slot->started) return false;
        slot = std::make_shared<PipedProcessImpl>();
        impl = slot;
    }

    reproc::options options;
    options.redirect.in.type = reproc::redirect::pipe;
    options.redirect.out.type = reproc::redirect::pipe;
    options.redirect.err.type = reproc::redirect::parent;

    auto argv = buildArgv(executable, args);
    std::lock_guard<std::mutex> guard(impl->ctlMutex);
    std::error_code ec = impl->process.start(argv, options);
    if (ec) return false;
    impl->started = true;
    return true;
}

bool PipedProcess::write(const void* data, size_t len) {
    auto impl = findImpl(this);
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
    auto impl = findImpl(this);
    if (!impl || !impl->started) return -1;
    uint8_t b;
    auto [n, ec] = impl->process.read(reproc::stream::out, &b, 1);
    if (ec || n == 0) return -1;
    return static_cast<int>(b);
}

size_t PipedProcess::read(void* buf, size_t len) {
    auto impl = findImpl(this);
    if (!impl || !impl->started) return 0;
    auto [n, ec] = impl->process.read(reproc::stream::out,
                                      static_cast<uint8_t*>(buf), len);
    if (ec) return 0;
    return n;
}

void PipedProcess::closeStdin() {
    auto impl = findImpl(this);
    if (!impl || !impl->started || impl->stdinClosed) return;
    impl->process.close(reproc::stream::in);
    impl->stdinClosed = true;
}

void PipedProcess::closePipes() {
    auto impl = findImpl(this);
    if (!impl || !impl->started) return;
    if (!impl->stdinClosed) {
        impl->process.close(reproc::stream::in);
        impl->stdinClosed = true;
    }
    impl->process.close(reproc::stream::out);
    impl->process.close(reproc::stream::err);
}

void PipedProcess::stop(int timeoutMs) {
    auto impl = findImpl(this);
    if (!impl || !impl->started) return;

    // Serialize the control transition against a concurrent isRunning(): both
    // call into the same reproc::process and flip `started`. Every reproc call
    // here is time-bounded (wait/terminate/kill all carry deadlines), so this
    // lock never blocks indefinitely — a reader thread blocked in read() holds
    // no lock, so stop() always reaches process.stop() to interrupt it.
    std::lock_guard<std::mutex> guard(impl->ctlMutex);
    if (!impl->started) return;

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
    auto impl = findImpl(this);
    if (!impl || !impl->started) return false;
    // Same control mutex as stop(): the non-blocking wait below and stop()'s
    // bounded stop sequence must not run concurrently on one reproc handle.
    std::lock_guard<std::mutex> guard(impl->ctlMutex);
    if (!impl->started) return false;
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
