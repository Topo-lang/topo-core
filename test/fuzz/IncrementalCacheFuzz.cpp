// Fuzz target for IncrementalCache manifest loading.
//
// Invariant under test: any bytes fed to loadManifest() must result in a
// graceful "cache miss" (return false) rather than a crash or hang. The
// build system relies on this to degrade to a full rebuild when the
// on-disk manifest is corrupted (e.g. partial write, external tampering,
// version skew, bit-flip on disk).
//
// The loader takes a path (not an in-memory buffer), so we materialize
// the fuzzer input to a temp file under std::filesystem::temp_directory_path()
// with a per-process subdirectory, then invoke loadManifest().

#include "topo/Build/IncrementalCache.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

// One-time initialization: create a unique project directory under temp.
static fs::path makeFuzzProjectDir() {
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    if (ec) base = "/tmp";
    fs::path dir = base / ("topo-fuzz-cache-" + std::to_string(::getpid()));
    fs::create_directories(dir / ".topo-cache", ec);
    return dir;
}

static const fs::path& fuzzProjectDir() {
    static const fs::path dir = makeFuzzProjectDir();
    return dir;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Write fuzzer input as the manifest file. The loader looks at
    // <projectDir>/.topo-cache/manifest.json.
    const fs::path& projectDir = fuzzProjectDir();
    fs::path manifestPath = projectDir / ".topo-cache" / "manifest.json";

    {
        std::ofstream ofs(manifestPath, std::ios::binary | std::ios::trunc);
        if (!ofs) return 0;
        if (size > 0) ofs.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }

    // Invariant: loadManifest returns true or false, but must never throw or crash
    // on arbitrary bytes. A false return means "cache invalid -> full rebuild".
    topo::build::IncrementalCache cache(projectDir);
    topo::build::CacheManifest manifest;
    (void)cache.loadManifest(manifest);

    return 0;
}
