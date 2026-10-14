#ifndef TOPO_PLATFORM_ANALYSISPROCESS_H
#define TOPO_PLATFORM_ANALYSISPROCESS_H

#include "topo/Platform/Process.h"
#include <filesystem>
#include <map>

namespace topo::platform {

// Expanded in arguments and fixed environment values to the private 0700
// temporary directory created for this invocation. Callers cannot add other
// writable paths. Cleanup failures are reported before the synchronous call returns.
inline constexpr const char* AnalysisTemporaryDirectoryToken = "@TOPO_ANALYSIS_TMPDIR@";

struct AnalysisProcessPolicy {
    std::vector<std::filesystem::path> readOnlyFiles;
    std::vector<std::filesystem::path> readOnlyDirectories;
    // Trusted runtime/library roots may also map executable images. Keep
    // project input trees in readOnlyDirectories, which grants data access.
    std::vector<std::filesystem::path> toolchainDirectories;
    std::vector<std::filesystem::path> toolchainFiles;
    std::vector<std::filesystem::path> allowedExecutables;
    // Exact caller-supplied values after clearing the inherited environment.
    // HOME/TMPDIR/TMP/TEMP are reserved for the invocation's private directory.
    std::map<std::string, std::string> environment;
};

struct AnalysisIsolationCapability {
    bool supported = false;
    std::string backend;
    std::string diagnostic;
    // Actual canonical executables in the isolation startup chain. Callers
    // bind their bytes before execution and compare the invocation result.
    std::vector<std::filesystem::path> executionTools;
    // Kernel OS build, process architecture, and fixed policy revision.
    std::string platformIdentity;
};

struct AnalysisProcessResult : BoundedProcessResult {
    bool isolationEstablished = false;
    std::string isolation;
    std::vector<std::filesystem::path> executionTools;
    std::string platformIdentity;
};

// Availability is a platform preflight, not proof of successful application.
// runAnalysisProcess reports the actual invocation's establishment/failure.
// Linux is unsupported here even when the separate experimental resource API
// is enabled; that API does not satisfy the strict audit execution contract.
AnalysisIsolationCapability analysisProcessCapability();

// Native tools only. The macOS profile denies network, fork, session/group
// escape, unlisted execs, input contents outside listed input/runtime roots,
// and writes outside private temporary storage (apart from /dev/null). System
// metadata and startup directory/device reads remain available. Threads and
// in-process code generation, including JVM JIT, remain available. This API
// provides neither a CPU/memory quota nor immutable input snapshots.
// Linux remains unsupported here, including in experimental-enabled builds:
// the exact executable-image guarantee is not established by that backend.
// Unsupported platforms and failed profile application return failure
// without unisolated fallback.
// Permissions do not make inputs immutable; callers must separately bind
// trusted tool/input bytes and hold their snapshots as their contract needs.
AnalysisProcessResult runAnalysisProcess(const std::string& executable,
                                         const std::vector<std::string>& args,
                                         const AnalysisProcessPolicy& policy,
                                         const ProcessCaptureLimits& limits,
                                         bool verbose = false);

namespace experimental {
// Low-level resource-isolation experiment; never a strict audit guarantee.
// Disabled unless built with TOPO_ENABLE_EXPERIMENTAL_LINUX_ANALYSIS. Enabling
// it does not change analysisProcessCapability() or runAnalysisProcess().
// Requires Linux Landlock ABI >= 3, close_range and seccomp. Filesystem,
// network and process authority is confined. The bound dynamic ELF loader
// can run other readable ELF images, and readable files can be mapped
// executable; this API does not establish an exact executable-image boundary.
// Threads and in-process JIT remain available. chmod/chown/timestamp/xattr
// changes are denied globally, including in private temporary storage.
// Creation/read/write/removal in that temporary storage remain available.
// Capability availability is a preflight; each invocation must still establish
// its trusted startup handshake. No unsupported/failed operation falls back.
AnalysisIsolationCapability linuxAnalysisProcessCapability();
AnalysisProcessResult runLinuxAnalysisProcess(const std::string& executable,
                                              const std::vector<std::string>& args,
                                              const AnalysisProcessPolicy& policy,
                                              const ProcessCaptureLimits& limits,
                                              bool verbose = false);
} // namespace experimental

} // namespace topo::platform
#endif
