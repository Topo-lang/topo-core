#ifndef TOPO_PLATFORM_PROCESS_H
#define TOPO_PLATFORM_PROCESS_H

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

// Long-lived subprocess with bidirectional stdin/stdout pipes.
// Used by ClangdBridge for JSON-RPC communication.
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

    // Exit code captured by stop(). Valid only after stop() has observed the
    // child terminating on its own (before the timeout). Returns -1 when the
    // child was force-killed, when stop() has not been called yet, or on any
    // OS-level error retrieving the status.
    int exitCode() const { return exitStatus_; }

private:
    void closePipes();

    // exitStatus_ may be populated from a const method (isRunning) when it
    // reaps a terminated child, so it is mutable.
    mutable int exitStatus_ = -1;

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
