#ifndef TOPO_PROFILE_FRAMERESOLVER_H
#define TOPO_PROFILE_FRAMERESOLVER_H

// Abstract frame-resolution interface used by the sampling
// converters in topo-core/lib/Profile/ to keep the converter
// host-agnostic.
//
// `topo-core` must not depend on host-specific packages (topo-v8 etc.),
// so the concrete resolver implementation lives in
// `topo-v8/lib/Debug/SourceMapResolver`. That class implements
// `FrameResolver` by adapting its Source Map v3 result into the
// host-agnostic `ResolvedFrame` shape. The CLI (`topo-profile`) wires
// the concrete subclass and passes a base-class pointer into
// `convertCpuProfileStream`.

#include <optional>
#include <string>

namespace topo::profile {

struct ResolvedFrame {
    // Original source file path (caller-supplied verbatim — the
    // converter takes a basename for display).
    std::string source_path;
    // 1-indexed source line.
    int line_1indexed = 0;
    // 1-indexed source column.
    int column_1indexed = 0;
    // Optional name from the underlying mapping (Source Map v3 `names`
    // field for the V8 backend; future hosts may leave this empty).
    std::optional<std::string> name;
};

// Implementations are stateful (typically cache parsed maps) and
// expected to be called from a single thread per instance. Returning
// std::nullopt means "no mapping" — the converter falls back to the
// raw V8 location and never logs.
class FrameResolver {
public:
    virtual ~FrameResolver() = default;
    virtual std::optional<ResolvedFrame> resolve(const std::string& source_url,
                                                 int line_1indexed,
                                                 int column_1indexed) = 0;
};

} // namespace topo::profile

#endif // TOPO_PROFILE_FRAMERESOLVER_H
