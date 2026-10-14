#ifndef TOPO_PLATFORM_PROCESS_H
#define TOPO_PLATFORM_PROCESS_H

#include <atomic>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace topo::platform {

// Result of a fire-and-forget process execution.
struct ProcessResult {
    int exitCode = -1;
};

// Result of a process execution with captured stdout and stderr.
struct CapturedProcessResult {
    int exitCode = -1;
    std::string stdoutOutput;
    std::string stderrOutput;
};

// Execute a process and wait for completion.
// Arguments are passed directly (no shell involved), eliminating quoting issues.
// stdout/stderr are inherited from the parent process.
ProcessResult runProcess(const std::string& executable, const std::vector<std::string>& args, bool verbose = false);

// Execute a process, capture its stdout and stderr, and wait for completion.
// Arguments are passed directly (no shell involved).
CapturedProcessResult runProcessCapture(const std::string& executable,
                                        const std::vector<std::string>& args,
                                        bool verbose = false);

// Execute a process in a specific working directory, capture stdout and stderr.
// workingDir: if non-empty, the child process starts in this directory.
CapturedProcessResult runProcessCapture(const std::string& executable,
                                        const std::vector<std::string>& args,
                                        const std::string& workingDir,
                                        bool verbose = false);

// Execute a process, capture its stdout and stderr, and wait with a timeout.
// If the process does not finish within timeoutMs milliseconds, it is
// forcefully terminated and the result has exitCode == -1.
CapturedProcessResult runProcessCaptureWithTimeout(const std::string& executable,
                                                   const std::vector<std::string>& args,
                                                   int timeoutMs,
                                                   bool verbose = false);

// Explicit limits for one-shot analysis tools. Zero bytes permits only an
// empty stream; timeoutMs must be positive. Limits are per stream.
struct ProcessCaptureLimits {
    int timeoutMs = 30000;
    size_t maxStdoutBytes = 32 * 1024 * 1024;
    size_t maxStderrBytes = 1024 * 1024;
    size_t maxStdinBytes = 8 * 1024 * 1024;
    // One-shot input is size-checked and staged in a private temporary
    // file before spawning, so a child that never reads stdin cannot block
    // an input writer. Empty input gives immediate EOF.
    std::string stdinInput = {};
    // nullopt preserves the caller's environment. An engaged map replaces
    // it completely, including an empty map for an empty environment.
    std::optional<std::map<std::string, std::string>> environment = {};
};

enum class ProcessCaptureFailure {
    None,
    InvalidLimits,
    StartFailed,
    TimedOut,
    OutputLimitExceeded,
    IoError,
    CleanupFailed,
};

struct BoundedProcessResult : CapturedProcessResult {
    ProcessCaptureFailure failure = ProcessCaptureFailure::None;
    std::string diagnostic;
};

// Capture through pipes with finite OS buffers, retaining at most the given
// byte limits (no output temp files). A limit, timeout, spawn/I/O or cleanup
// failure sets exitCode=-1 and failure/diagnostic; partial output is retained.
// Normal tool failures retain the child's exit code and failure=None.
// All normal descendants are terminated before returning, including when the
// direct child exits first: POSIX uses a fresh process group; Windows starts
// suspended, assigns a kill-on-close Job Object, then resumes. Failure to
// establish this containment prevents execution. This is process lifecycle
// management, not an OS sandbox: POSIX descendants may escape with setsid or
// setpgid, and filesystem, network, CPU and child memory remain unrestricted.
BoundedProcessResult runProcessCaptureBounded(const std::string& executable,
                                              const std::vector<std::string>& args,
                                              const ProcessCaptureLimits& limits,
                                              bool verbose = false);

// Long-lived subprocess with bidirectional stdin/stdout pipes.
// Used by ClangdBridge for JSON-RPC communication.
//
// Threading contract: once start() has returned true, a single live
// PipedProcess may have its I/O and control methods called concurrently
// from multiple threads — the canonical pattern is a reader thread looping
// in read()/readByte() while a control thread calls stop()/isRunning()/
// closeStdin(). The shared state flags are atomic and the stop()/isRunning()
// control transition is serialized internally, so this is race-free; a
// blocking read is NOT held under that lock, so stop() can always interrupt
// a stalled reader. start() and destruction are NOT internally synchronized
// against concurrent I/O — the owner must ensure the object outlives, and is
// not restarted during, any in-flight method call. exitCode() is read-after-
// stop: it is not meant to be called concurrently with stop()/isRunning().
// Note the synchronization covers the state flags and the stop()/isRunning()
// transition only: closeStdin()/closePipes() touch the underlying pipe fields
// lock-free, so they must not be called while a read()/readByte() is in flight
// on another thread (stop() is the safe way to interrupt a blocked reader).
class PipedProcess {
public:
    PipedProcess() = default;
    ~PipedProcess();

    PipedProcess(const PipedProcess&) = delete;
    PipedProcess& operator=(const PipedProcess&) = delete;

    // Start the subprocess with the given executable and arguments.
    bool start(const std::string& executable, const std::vector<std::string>& args);

    // Write data to the subprocess stdin.
    bool write(const void* data, size_t len);

    // Close the parent's write end of stdin, signaling EOF to the child
    // while leaving stdout readable. Required for one-shot children that
    // read stdin to EOF before producing output (topo-extract-{cpp,rust,
    // java,python}, cat-like filters). Calling stop() closes both pipes
    // at once and is unsuitable when the parent still needs to drain stdout.
    // Safe to call multiple times; second call is a no-op.
    void closeStdin();

    // Read a single byte from the subprocess stdout.
    // Returns -1 on EOF or error.
    int readByte();

    // Read up to `len` bytes from the subprocess stdout.
    // Returns number of bytes actually read, or 0 on EOF/error.
    size_t read(void* buf, size_t len);

    // Gracefully stop the subprocess with a timeout (ms).
    // After the timeout, the process is forcefully terminated.
    void stop(int timeoutMs = 3000);

    // Check if the subprocess is currently running.
    bool isRunning() const;

    // Exit code captured once stop() or isRunning() observed termination.
    // A child that exits on its own within stop()'s grace window reports its
    // own exit code (including nonzero). When stop() escalates to forceful
    // termination (the child failed to exit within timeoutMs), exitCode()
    // is -1 on every platform. -1 is also returned before any termination
    // was observed and on OS-level errors retrieving the status. Only the
    // stop()-initiated kill is normalized: a child that dies abnormally on
    // its own keeps the platform encoding (POSIX 128+signo via reproc,
    // Windows NTSTATUS).
    int exitCode() const { return exitStatus_.load(std::memory_order_relaxed); }

private:
    void closePipes();

    // exitStatus_ may be populated from a const method (isRunning) when it
    // reaps a terminated child, so it is mutable. Atomic so a control thread
    // writing it (stop()/isRunning()) never races a reader observing it.
    mutable std::atomic<int> exitStatus_{-1};

#ifdef _WIN32
    // Stored as void* to avoid including <windows.h> in the header.
    void* processHandle_ = nullptr;
    void* stdinWrite_ = nullptr;
    void* stdoutRead_ = nullptr;
#else
    // childPid_ is cleared to -1 when the child is reaped — that may happen
    // from const isRunning(), hence mutable.
    mutable int childPid_ = -1;
    int stdinPipe_ = -1;
    int stdoutPipe_ = -1;
#endif
};

} // namespace topo::platform

#endif // TOPO_PLATFORM_PROCESS_H
