#include "topo/Platform/Process.h"
#include "topo/Platform/TempFile.h"
#include "topo/Platform/ToolResolution.h"

#include <reproc++/reproc.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
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

// POSIX backend wraps reproc++ (third_party/reproc, MIT); on Windows EVERY
// spawn path (runProcess / runProcessCapture / PipedProcess) uses raw Win32
// instead — reproc's Windows backend detects child exit and pipe EOF over
// loopback sockets, and that notification is not reliably delivered for
// MinGW children on GHA windows-2022, hanging even instant-exit processes.
// Public Process.h surface unchanged; the Windows-/POSIX-specific private
// fields on PipedProcess are vestigial after this refactor (kept for ABI,
// retire in a future break).
// Behavior preserved (covered by ctest):
//   - closeStdin() closes ONLY stdin; stdout remains drainable.
//   - stop(timeoutMs) waits, then terminates, then kills the child.
//   - runProcessCaptureWithTimeout returns exitCode == -1 on timeout.
//   - capture redirects stdout/stderr to temp files (not pipes) so the Windows
//     loopback-socket EOF that fails to arrive for MinGW children can't hang
//     the call; wait is bounded by a process deadline (exitCode == -1 on hit).
//   - isRunning() reaps a terminated child so exitCode() reflects the status.
//   - argv passed without a shell — no quoting, no env substitution. The one
//     deliberate exception: on Windows a program that resolves to a
//     `.cmd`/`.bat` launcher is run through `cmd.exe /d /s /c` (CreateProcessW
//     cannot execute a batch script); see buildSpawnCommandLineW for the
//     resolution + quoting contract.

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

#ifndef _WIN32
// Build the argv vector reproc expects (argv[0] = executable, then args).
// POSIX-only: every Windows path spawns via raw Win32 below.
std::vector<std::string> buildArgv(const std::string& exe,
                                   const std::vector<std::string>& args) {
    std::vector<std::string> argv;
    argv.reserve(args.size() + 1);
    argv.push_back(exe);
    for (const auto& a : args) argv.push_back(a);
    return argv;
}
#endif // !_WIN32

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
    // topo::platform::tempDirectory() honours TMPDIR / XDG_RUNTIME_DIR / TEMP /
    // TMP, then the std temp dir, then cwd — the portable route the
    // no-hardcoded-tmpdir audit gate requires (the raw standard-library temp
    // lookup ignores TMPDIR on some libstdc++/MSVC configs). TempFile.cpp is
    // the one sanctioned wrapper around that raw call.
    std::filesystem::path dir = topo::platform::tempDirectory();
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
// or at the end of a quoted token). `forceQuote` wraps even a simple token in
// quotes — required inside a cmd.exe command line, where an unquoted token
// would expose & | < > ^ to the shell parser (quoting is transparent to
// CommandLineToArgvW, so forcing it never changes what the target argv sees).
void appendQuotedArg(std::wstring& cmd, const std::wstring& arg,
                     bool forceQuote = false) {
    if (!forceQuote && !arg.empty() &&
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

// ---- Windows spawn-target resolution (.exe / .cmd / .bat launchers) ----
// CreateProcessW's own search appends only ".exe" to a bare program name, so
// script launchers staged as `<tool>.cmd` / `<tool>.bat` (npm shims, jdtls,
// the staged topo-extract-{typescript,python,java}) are never found — and a
// batch file is not a valid executable image even when named explicitly.
// Every spawn path in this file therefore builds its command line through
// buildSpawnCommandLineW():
//   1. resolve the program to an ABSOLUTE on-disk path via
//      findSpawnableOnPath() (per PATH directory: `<name>.exe`, `<name>.cmd`,
//      `<name>.bat`, then the bare name);
//   2. a resolved `.cmd`/`.bat` is wrapped as
//        "<ComSpec>" /d /s /c ""<absolute launcher>" "<arg>"..."
//      because cmd.exe is the only way to execute a batch script;
//   3. anything else — including an unresolved name, for which
//      CreateProcessW's native search (app dir, system dirs, PATH + ".exe")
//      remains the final fallback — uses the plain argv command line.
//
// Quoting contract for the cmd.exe wrapper:
//   * the launcher path and EVERY argument are force-quoted with the same
//     CommandLineToArgvW/MSVC rules used for direct spawns; the batch shim's
//     `%*` forwards them verbatim to the real tool, whose CRT reconstructs
//     the original argv;
//   * `/s` pins cmd's quote handling to "strip exactly the one outer quote
//     pair", and `/d` skips AutoRun registry commands, so parsing is
//     deterministic;
//   * force-quoting makes whitespace and the cmd metacharacters & | < > ^
//     inert (cmd does not interpret them inside double quotes);
//   * accepted limitations, by design: a literal `%` may still undergo cmd
//     variable expansion, and a literal `"` inside an argument can break
//     cmd's quote pairing — there is no general escape for either inside a
//     quoted cmd region. Batch launchers receive tool modes, file paths and
//     plain flags (none contain `%` or `"`); direct .exe spawns are not
//     affected because no shell is involved.

// Serializes inheritable-handle creation against CreateProcessW across every
// spawn path in this TU. CreateProcessW(bInheritHandles=TRUE) inherits ALL
// currently-inheritable handles in the process, so two concurrent spawns
// could leak each other's child-side pipe/file handles into the wrong child —
// and a stray copy of a pipe's write end keeps EOF from ever reaching the
// reader (exactly the hang class this raw backend exists to avoid). Handles
// are made inheritable, passed to CreateProcessW, and closed again only while
// this mutex is held.
std::mutex& spawnMutex() {
    static std::mutex m;
    return m;
}

std::string lowercaseExtension(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

bool isBatchLauncher(const std::string& path) {
    const std::string ext = lowercaseExtension(path);
    return ext == ".cmd" || ext == ".bat";
}

// The command interpreter for batch launchers. %COMSPEC% is set by Windows
// itself; the bare-name fallback lets CreateProcessW's native search find
// System32\cmd.exe if the environment was scrubbed.
std::string comspec() {
    if (const char* v = std::getenv("COMSPEC"); v && *v) return v;
    return "cmd.exe";
}

// Build the final CreateProcessW command line for (exe, args), applying the
// launcher resolution + cmd.exe wrapping documented above.
std::wstring buildSpawnCommandLineW(const std::string& exe,
                                    const std::vector<std::string>& args) {
    std::string resolved = findSpawnableOnPath(exe);
    const std::string& target = resolved.empty() ? exe : resolved;
    if (!isBatchLauncher(target)) {
        return buildCommandLineW(target, args);
    }
    std::wstring cmd;
    appendQuotedArg(cmd, utf8ToWide(comspec()));
    cmd += L" /d /s /c \"";
    appendQuotedArg(cmd, utf8ToWide(target), /*forceQuote=*/true);
    for (const auto& a : args) {
        cmd += L' ';
        appendQuotedArg(cmd, utf8ToWide(a), /*forceQuote=*/true);
    }
    cmd += L'"';
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

// Duplicate one of the parent's std handles into an inheritable copy for a
// child (the reproc redirect::parent equivalent), falling back to NUL when
// the parent has no usable handle (GUI host / detached service). Duplication
// — rather than SetHandleInformation on the original — avoids flipping the
// inherit bit on the parent's own handle, which would be a process-wide side
// effect visible to every other concurrent spawn.
HANDLE inheritableStdHandle(DWORD stdWhich, DWORD nulAccess) {
    HANDLE h = GetStdHandle(stdWhich);
    if (h != nullptr && h != INVALID_HANDLE_VALUE) {
        HANDLE dup = nullptr;
        if (DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup,
                            0, /*bInheritHandle=*/TRUE,
                            DUPLICATE_SAME_ACCESS)) {
            return dup;
        }
    }
    return openInheritableFile("NUL", nulAccess, OPEN_EXISTING);
}

CapturedProcessResult runCaptureWindows(const std::string& exe,
                                        const std::vector<std::string>& args,
                                        const std::string& workingDir,
                                        int timeoutMs) {
    CapturedProcessResult result;
    const std::string outPath = makeCaptureTempPath("out");
    const std::string errPath = makeCaptureTempPath("err");

    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = buildSpawnCommandLineW(exe, args);
    std::wstring wcwd =
        workingDir.empty() ? std::wstring() : utf8ToWide(workingDir);

    BOOL ok = FALSE;
    DWORD spawnError = 0;
    {
        // Inheritable handles live only inside the spawn lock — see
        // spawnMutex() for why.
        std::lock_guard<std::mutex> spawnLock(spawnMutex());

        HANDLE hOut =
            openInheritableFile(outPath, GENERIC_WRITE, CREATE_ALWAYS);
        HANDLE hErr =
            openInheritableFile(errPath, GENERIC_WRITE, CREATE_ALWAYS);
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

        ok = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr,
                            /*bInheritHandles=*/TRUE, /*flags=*/0, nullptr,
                            wcwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
        if (!ok) spawnError = GetLastError();

        // Drop our copies of the child's std handles; the child holds its own
        // and is the sole writer once we've closed ours.
        CloseHandle(hOut);
        CloseHandle(hErr);
        CloseHandle(hIn);
    }

    if (!ok) {
        std::cerr << "error: failed to start process '" << exe
                  << "': CreateProcess failed (" << spawnError << ")\n";
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

// ---- Windows raw-Win32 fire-and-forget path (runProcess) ----
// Same rationale as runCaptureWindows: bypass reproc, wait on the real
// process HANDLE. The child shares the parent's stdio via inheritable
// duplicates of the parent's std handles (reproc redirect::parent parity);
// runProcess's contract is "wait for completion", so the wait is unbounded.
ProcessResult runProcessWindows(const std::string& exe,
                                const std::vector<std::string>& args) {
    std::wstring cmdLine = buildSpawnCommandLineW(exe, args);

    PROCESS_INFORMATION pi{};
    BOOL ok = FALSE;
    DWORD spawnError = 0;
    {
        std::lock_guard<std::mutex> spawnLock(spawnMutex());

        HANDLE hIn = inheritableStdHandle(STD_INPUT_HANDLE, GENERIC_READ);
        HANDLE hOut = inheritableStdHandle(STD_OUTPUT_HANDLE, GENERIC_WRITE);
        HANDLE hErr = inheritableStdHandle(STD_ERROR_HANDLE, GENERIC_WRITE);
        if (hIn == INVALID_HANDLE_VALUE || hOut == INVALID_HANDLE_VALUE ||
            hErr == INVALID_HANDLE_VALUE) {
            std::cerr << "error: failed to prepare std handles for '" << exe
                      << "'\n";
            if (hIn != INVALID_HANDLE_VALUE) CloseHandle(hIn);
            if (hOut != INVALID_HANDLE_VALUE) CloseHandle(hOut);
            if (hErr != INVALID_HANDLE_VALUE) CloseHandle(hErr);
            return ProcessResult{-1};
        }

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = hIn;
        si.hStdOutput = hOut;
        si.hStdError = hErr;

        // With a console present the child shares it (progress output stays
        // interactive; Ctrl+C reaches the whole console group). Without one
        // (editor-hosted topo-lsp, services), CREATE_NO_WINDOW keeps a
        // console child from flashing a new console window — the explicit
        // std handles above carry its output either way.
        const DWORD flags =
            GetConsoleWindow() != nullptr ? 0 : CREATE_NO_WINDOW;

        ok = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr,
                            /*bInheritHandles=*/TRUE, flags, nullptr, nullptr,
                            &si, &pi);
        if (!ok) spawnError = GetLastError();

        CloseHandle(hIn);
        CloseHandle(hOut);
        CloseHandle(hErr);
    }

    if (!ok) {
        std::cerr << "error: failed to start process '" << exe
                  << "': CreateProcess failed (" << spawnError << ")\n";
        return ProcessResult{-1};
    }

    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    const int exitCode =
        GetExitCodeProcess(pi.hProcess, &code) ? static_cast<int>(code) : -1;
    CloseHandle(pi.hProcess);
    return ProcessResult{exitCode};
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

#ifdef _WIN32
    return runProcessWindows(executable, args);
#else
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
#endif // _WIN32
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

// PipedProcess — bidirectional stdin/stdout pipes. POSIX rides
// reproc::process; Windows uses raw anonymous pipes + CreateProcessW (same
// reproc-bypass rationale as the capture/runProcess paths above — reproc's
// socket-based EOF/exit detection hangs for MinGW children).
// Per-instance state lives in a side table keyed by `this` because we
// can't add a real member without breaking the public-header ABI (57
// downstream TUs). The map mutex guards only insert/erase/lookup; concrete
// use is single-digit live PipedProcess instances at a time.
// The impl is single-use, so each PipedProcess::start() creates a
// fresh PipedProcessImpl — the old one is discarded so a stop()+start()
// cycle on the same PipedProcess works (rust-analyzer restart tests rely
// on this).

struct PipedProcessImpl {
#ifdef _WIN32
    // Raw-Win32 child state. The parent-side pipe ends deliberately stay
    // open until this impl is destroyed: a reader thread may be blocked in
    // ReadFile on stdoutRead, and closing a handle out from under a blocked
    // thread is undefined — stop() instead terminates the child, which
    // breaks the pipe and unblocks the read with EOF. The shared_ptr side
    // table guarantees this destructor only runs once no call is in flight.
    HANDLE child = nullptr;
    HANDLE stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr;
    ~PipedProcessImpl() {
        if (stdinWrite != nullptr) CloseHandle(stdinWrite);
        if (stdoutRead != nullptr) CloseHandle(stdoutRead);
        if (child != nullptr) CloseHandle(child);
    }
#else
    reproc::process process;
#endif
    // Flags are atomic because the reader thread (read/readByte) observes
    // `started` while the control thread (stop/isRunning) flips it, and
    // `stdinClosed` is likewise touched from both sides. ctlMutex serializes
    // the stop()/isRunning() control transition (concurrent control calls on
    // one process handle are not guaranteed safe); it is deliberately NOT
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
    // The impl is single-use: after stop() it cannot be restarted.
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

#ifdef _WIN32
    std::wstring cmdLine = buildSpawnCommandLineW(executable, args);

    HANDLE stdinWrite = nullptr;
    HANDLE stdoutRead = nullptr;
    PROCESS_INFORMATION pi{};

    std::lock_guard<std::mutex> guard(impl->ctlMutex);
    {
        std::lock_guard<std::mutex> spawnLock(spawnMutex());

        // Both pipe ends are created inheritable, then the PARENT-side ends
        // (stdinWrite / stdoutRead) are stripped of the inherit bit: a child
        // holding a copy of stdoutRead's peer or our stdinWrite would keep
        // the pipe alive after we close it, suppressing the EOF the reader
        // relies on. The buffer size is advisory; 64 KiB matches typical
        // LSP/extractor message sizes so neither side stalls on small
        // request/response exchanges.
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        HANDLE stdinRead = nullptr;
        HANDLE stdoutWrite = nullptr;
        if (!CreatePipe(&stdinRead, &stdinWrite, &sa, 64 * 1024)) {
            return false;
        }
        if (!CreatePipe(&stdoutRead, &stdoutWrite, &sa, 64 * 1024)) {
            CloseHandle(stdinRead);
            CloseHandle(stdinWrite);
            return false;
        }
        SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);

        // stderr passes through to the parent (reproc redirect::parent
        // parity) so server/extractor diagnostics stay visible.
        HANDLE childErr = inheritableStdHandle(STD_ERROR_HANDLE, GENERIC_WRITE);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = stdinRead;
        si.hStdOutput = stdoutWrite;
        si.hStdError = childErr;

        // PipedProcess children are background servers/extractors, never
        // interactive — CREATE_NO_WINDOW keeps them from opening a console
        // window when the parent has none (editor-hosted topo-lsp).
        BOOL ok = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr,
                                 /*bInheritHandles=*/TRUE, CREATE_NO_WINDOW,
                                 nullptr, nullptr, &si, &pi);

        // The parent must not keep the child-side ends: a surviving
        // stdoutWrite copy in this process would suppress EOF for our own
        // reader, and an open stdinRead would mask the child's stdin close.
        CloseHandle(stdinRead);
        CloseHandle(stdoutWrite);
        if (childErr != INVALID_HANDLE_VALUE) CloseHandle(childErr);

        if (!ok) {
            CloseHandle(stdinWrite);
            CloseHandle(stdoutRead);
            return false;
        }
    }

    CloseHandle(pi.hThread);
    impl->child = pi.hProcess;
    impl->stdinWrite = stdinWrite;
    impl->stdoutRead = stdoutRead;
    impl->started = true;
    return true;
#else
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
#endif // _WIN32
}

bool PipedProcess::write(const void* data, size_t len) {
    auto impl = findImpl(this);
    if (!impl || !impl->started || impl->stdinClosed) return false;
    const auto* buf = static_cast<const uint8_t*>(data);
    size_t remaining = len;
#ifdef _WIN32
    while (remaining > 0) {
        const DWORD chunk = static_cast<DWORD>(
            std::min<size_t>(remaining, 1u << 30));
        DWORD written = 0;
        // Synchronous pipe write; fails with ERROR_BROKEN_PIPE /
        // ERROR_NO_DATA once the child exits or closes its stdin.
        if (!WriteFile(impl->stdinWrite, buf, chunk, &written, nullptr) ||
            written == 0) {
            return false;
        }
        buf += written;
        remaining -= written;
    }
    return true;
#else
    while (remaining > 0) {
        auto [n, ec] = impl->process.write(buf, remaining);
        if (ec || n == 0) return false;
        buf += n;
        remaining -= n;
    }
    return true;
#endif // _WIN32
}

int PipedProcess::readByte() {
    auto impl = findImpl(this);
    if (!impl || !impl->started) return -1;
#ifdef _WIN32
    uint8_t b = 0;
    DWORD n = 0;
    // Blocks until at least one byte or EOF; ERROR_BROKEN_PIPE after the
    // child exits (or closes stdout) is the EOF signal.
    if (!ReadFile(impl->stdoutRead, &b, 1, &n, nullptr) || n == 0) return -1;
    return static_cast<int>(b);
#else
    uint8_t b;
    auto [n, ec] = impl->process.read(reproc::stream::out, &b, 1);
    if (ec || n == 0) return -1;
    return static_cast<int>(b);
#endif // _WIN32
}

size_t PipedProcess::read(void* buf, size_t len) {
    auto impl = findImpl(this);
    if (!impl || !impl->started) return 0;
#ifdef _WIN32
    const DWORD want = static_cast<DWORD>(std::min<size_t>(len, 1u << 30));
    DWORD n = 0;
    // Returns with whatever is available (POSIX read semantics); FALSE with
    // ERROR_BROKEN_PIPE once the child is gone and the buffer is drained.
    if (!ReadFile(impl->stdoutRead, buf, want, &n, nullptr)) return 0;
    return static_cast<size_t>(n);
#else
    auto [n, ec] = impl->process.read(reproc::stream::out,
                                      static_cast<uint8_t*>(buf), len);
    if (ec) return 0;
    return n;
#endif // _WIN32
}

void PipedProcess::closeStdin() {
    auto impl = findImpl(this);
    if (!impl || !impl->started || impl->stdinClosed) return;
#ifdef _WIN32
    impl->stdinClosed = true;
    if (impl->stdinWrite != nullptr) {
        CloseHandle(impl->stdinWrite);
        impl->stdinWrite = nullptr;
    }
#else
    impl->process.close(reproc::stream::in);
    impl->stdinClosed = true;
#endif // _WIN32
}

void PipedProcess::closePipes() {
    auto impl = findImpl(this);
    if (!impl || !impl->started) return;
#ifdef _WIN32
    if (!impl->stdinClosed) {
        impl->stdinClosed = true;
        if (impl->stdinWrite != nullptr) {
            CloseHandle(impl->stdinWrite);
            impl->stdinWrite = nullptr;
        }
    }
    if (impl->stdoutRead != nullptr) {
        CloseHandle(impl->stdoutRead);
        impl->stdoutRead = nullptr;
    }
#else
    if (!impl->stdinClosed) {
        impl->process.close(reproc::stream::in);
        impl->stdinClosed = true;
    }
    impl->process.close(reproc::stream::out);
    impl->process.close(reproc::stream::err);
#endif // _WIN32
}

void PipedProcess::stop(int timeoutMs) {
    auto impl = findImpl(this);
    if (!impl || !impl->started) return;

    // Serialize the control transition against a concurrent isRunning(): both
    // inspect the same child handle and flip `started`. Every wait here is
    // time-bounded, so this lock never blocks indefinitely — a reader thread
    // blocked in read() holds no lock, so stop() always proceeds and (by
    // ending the child) interrupts the stalled read.
    std::lock_guard<std::mutex> guard(impl->ctlMutex);
    if (!impl->started) return;

#ifdef _WIN32
    // wait → TerminateProcess → bounded reap. No CTRL_BREAK escalation —
    // consistent with the capture path's timeout handling; protocol-level
    // consumers (LSP bridges) ask the child to exit before calling stop().
    const DWORD waitMs =
        timeoutMs < 0 ? 0 : static_cast<DWORD>(timeoutMs);
    DWORD wr = WaitForSingleObject(impl->child, waitMs);
    if (wr == WAIT_OBJECT_0) {
        DWORD code = 0;
        exitStatus_ = GetExitCodeProcess(impl->child, &code)
                          ? static_cast<int>(code)
                          : -1;
    } else {
        // Timeout (or wait failure): force-killed children report -1, per
        // the exitCode() contract in the header.
        TerminateProcess(impl->child, 1);
        WaitForSingleObject(impl->child, 5000);
        exitStatus_ = -1;
    }
    // The parent-side pipe ends deliberately stay open here — a reader may
    // still be draining buffered output or blocked in ReadFile (the child's
    // death delivers its EOF). ~PipedProcessImpl closes them once no call
    // holds the impl.
    impl->started = false;
#else
    // wait → terminate → kill. Matches the previous SIGTERM/TerminateProcess
    // + blocking waitpid behavior.
    auto [status, ec] = impl->process.stop(
        {{reproc::stop::wait, reproc::milliseconds(timeoutMs)},
         {reproc::stop::terminate, reproc::milliseconds(timeoutMs)},
         {reproc::stop::kill, reproc::milliseconds(5000)}});

    exitStatus_ = ec ? -1 : status;
    impl->started = false;
#endif // _WIN32
}

bool PipedProcess::isRunning() const {
    auto impl = findImpl(this);
    if (!impl || !impl->started) return false;
    // Same control mutex as stop(): the non-blocking wait below and stop()'s
    // bounded stop sequence must not run concurrently on one process handle.
    std::lock_guard<std::mutex> guard(impl->ctlMutex);
    if (!impl->started) return false;
#ifdef _WIN32
    // Non-blocking wait — still alive → true; signaled → capture the status
    // so a subsequent exitCode() call returns it (mirrors the old
    // waitpid(WNOHANG) reap path). The process handle stays valid until the
    // impl is destroyed, so no explicit reap step is needed.
    DWORD wr = WaitForSingleObject(impl->child, 0);
    if (wr == WAIT_TIMEOUT) return true;
    if (wr == WAIT_OBJECT_0) {
        DWORD code = 0;
        exitStatus_ = GetExitCodeProcess(impl->child, &code)
                          ? static_cast<int>(code)
                          : -1;
    } else {
        exitStatus_ = -1; // wait failure — treat as terminated
    }
    impl->started = false;
    return false;
#else
    // Non-blocking wait — timed_out → still alive; success → reap + capture
    // status so a subsequent exitCode() call returns it (mirrors the old
    // waitpid(WNOHANG) reap path).
    auto [status, ec] = impl->process.wait(reproc::milliseconds(0));
    if (ec == std::errc::timed_out) return true;
    if (!ec) exitStatus_ = status;
    impl->started = false;
    return false;
#endif // _WIN32
}

} // namespace topo::platform
