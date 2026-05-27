// topo-build/include/topo/Build/AutoLink.h
#pragma once
#include <string>
#include <vector>
#include <algorithm>

namespace topo::build {

/// Inject runtime libraries required by enabled features into linkLibs/linkDirs.
/// Called by topo-build (with BuildConfig) and backend tools (with BackendConfig).
/// Both config types expose the same parallelCfg/adaptiveCfg/observabilityCfg/
/// lifetimeCfg members, so this is a template to accept either.
template <typename Config>
void injectAutoLinkLibs(const Config& cfg, std::vector<std::string>& linkLibs, std::vector<std::string>& linkDirs) {
    auto addLib = [&](const std::string& lib) {
        if (std::find(linkLibs.begin(), linkLibs.end(), lib) == linkLibs.end()) linkLibs.push_back(lib);
    };
    auto addDir = [&](const std::string& dir) {
        if (std::find(linkDirs.begin(), linkDirs.end(), dir) == linkDirs.end()) linkDirs.push_back(dir);
    };

    if (cfg.parallelCfg.isEnabled()) addLib("topo-parallel");
    if (cfg.adaptiveCfg.isEnabled()) {
        addLib("topo-adaptive");
        addLib("topo-jit");
        addLib("topo-parallel");
        addLib("TopoPlatform");
    }
    if (cfg.observabilityCfg.isEnabled()) addLib("topo-observe");
    if (cfg.lifetimeCfg.isEnabled()) addLib("topo-arena");
    if (cfg.containmentCfg.isEnabled()) addLib("topo-containment");

    // Transitive: topo-jit requires topo-parallel + TopoPlatform
    if (std::find(linkLibs.begin(), linkLibs.end(), "topo-jit") != linkLibs.end()) {
        addLib("topo-parallel");
        addLib("TopoPlatform");
    }

    // libtopo-adaptive unconditionally references
    // topo_pass_event_emit (the adaptive monitor emits a variant-switch
    // pass-event at every jitPtr flip — see topo_adaptive.cpp
    // emitVariantSwitch), independent of whether [adaptive] is enabled.
    // So topo-pass-event is a hard dependency of topo-adaptive whenever
    // the latter is linked — including when it is named explicitly via
    // [build].link_libs (e.g. the benchmark tomls) rather than injected
    // by the adaptiveCfg branch above. Resolve it transitively, the
    // same way topo-jit pulls topo-parallel. Static-lib order: callee
    // (topo-pass-event) must follow its caller (topo-adaptive); addLib
    // appends, and topo-adaptive is already present here, so the
    // append lands after it on the link line.
    if (std::find(linkLibs.begin(), linkLibs.end(), "topo-adaptive") != linkLibs.end()) {
        addLib("topo-pass-event");
    }

    // LifetimeArenaPass injects a
    // topo_pass_event_emit_sized() call at every arena open/close site
    // (see topo-llvm/lib/Transforms/LifetimeArenaPass.cpp). The symbol
    // lives in libtopo-pass-event, so it is a hard dependency of
    // topo-arena whenever the latter is linked — including when named
    // explicitly via [build].link_libs (benchmark tomls) rather than
    // injected by the lifetimeCfg branch above. Resolved transitively,
    // identically to the topo-adaptive case; static-lib order: callee
    // (topo-pass-event) appends after its caller (topo-arena).
    if (std::find(linkLibs.begin(), linkLibs.end(), "topo-arena") != linkLibs.end()) {
        addLib("topo-pass-event");
    }

    // TopoParallelPass injects a topo_pass_event_emit()
    // call at every task spawn/join site (see
    // topo-llvm/lib/Transforms/TopoParallelPass.cpp). The symbol lives in
    // libtopo-pass-event, so it is a hard dependency of topo-parallel
    // whenever the latter is linked — including when named explicitly via
    // [build].link_libs (benchmark tomls) rather than injected by the
    // parallelCfg branch above. Resolved transitively, identically to the
    // topo-adaptive / topo-arena cases; static-lib order: callee
    // (topo-pass-event) appends after its caller (topo-parallel). NOTE:
    // topo-adaptive's branch above also pulls topo-parallel; addLib
    // dedups so a single -ltopo-pass-event lands after whichever owner is
    // present.
    if (std::find(linkLibs.begin(), linkLibs.end(), "topo-parallel") != linkLibs.end()) {
        addLib("topo-pass-event");
    }

    if (!linkLibs.empty()) {
#ifdef TOPO_BUILD_LIBDIR_SDK
        addDir(TOPO_BUILD_LIBDIR_SDK);
#endif
#ifdef TOPO_BUILD_LIBDIR_PLATFORM
        addDir(TOPO_BUILD_LIBDIR_PLATFORM);
#endif
    }
}

} // namespace topo::build
