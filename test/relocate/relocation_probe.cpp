// relocation_probe — prints the LLVM toolchain the BYO-priority resolver
// (topo::platform::resolveLLVMToolchain) selects, so a shell harness can
// assert relocation behaviour across env settings. One scenario per process
// run (the resolver caches its result process-wide).
//
// Output (stdout, machine-parseable):
//   valid=<0|1> source=<name> root=<path> resourceDir=<path> version=<x.y.z>
//   clang++=<path-or-empty>
// Exit code: 0 when a toolchain resolved, 3 when none did.

#include "topo/Platform/ToolResolution.h"

#include <iostream>

int main() {
    const auto& tc = topo::platform::resolveLLVMToolchain();
    const char* src[] = {"None",          "CliOverride",   "EnvVar",
                         "TopoCache",      "PathDiscovery", "WellKnownPrefix",
                         "CompileTimeDefault"};
    std::cout << "valid=" << (tc.valid() ? 1 : 0)
              << " source=" << src[static_cast<int>(tc.source)]
              << " root=" << tc.root
              << " resourceDir=" << tc.resourceDir
              << " version=" << tc.version << "\n";
    std::cout << "clang++=" << topo::platform::llvmClangxx() << "\n";
    return tc.valid() ? 0 : 3;
}
