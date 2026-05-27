# debug metadata emitter, IPC framing, and query engine.
#
# - *.topo-dbg.json emitter (Emitter.cpp).
# - Extract<->Compute IPC framing (Ipc/) + query parser/evaluator
# (Query/). Both ship under TopoDebug so the CLI can link a single library.
# Zero LLVM dependency.

add_library(TopoDebug
    Emitter.cpp
    Ipc/BinaryFrame.cpp
    Ipc/FrameReader.cpp
    Ipc/FrameWriter.cpp
    Query/FrameView.cpp
    Query/Parser.cpp
    Query/Value.cpp
    Query/Evaluator.cpp
    # `topo debug serve` HTTP layer + embedded SPA.
    Server/HttpServer.cpp
    Server/Assets.cpp
    # RFC 6455 WebSocket helpers (handshake + framed query/summary).
    Server/WebSocket.cpp
    # dynamic loader for user-authored render-fns.
    # Loads `libtopo_render_<method>.{so,dylib}` on demand and invokes the
    # `topo_render_<method>` C-ABI export with Compute-layer JSON.
    Server/RenderFnLoader.cpp
    # summary template renderer (`topo debug summary` /
    # GET /summary). Pure-string parser + resolver-driven substitution.
    SummaryRenderer.cpp
    # minimal mustache-subset template engine backing
    # `web/templates/*.html.tpl` (GET /templates/<name>). Zero LLVM.
    TemplateEngine.cpp
    # Pass sidecar reports loader (per-Pass JSON
    # files under <output>.topo-passes/). topo-core never owns the per-Pass
    # detail schema (principle 19); the loader keeps detail as opaque JSON.
    PassReportsLoader.cpp
)
target_include_directories(TopoDebug PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(TopoDebug
    PUBLIC TopoSema TopoAST
    PUBLIC nlohmann_json::nlohmann_json
    # RenderFnLoader uses TopoPlatform's SharedLibrary
    # wrapper (dlopen / LoadLibrary). PUBLIC so downstream consumers that
    # link TopoDebug also pull in the platform layer transitively.
    PUBLIC TopoPlatform
)
topo_set_compiler_flags(TopoDebug)

topo_apply_std_pch(TopoDebug)
# vendor static assets (Chart.js, etc.) consumed by
# the SPA's `renderChartWidget` and friends. We copy the entire directory
# tree (including README.md and any placeholder stubs) to a stable build
# location so `topo debug serve --vendor-path <dir>` can default to that
# path. The copy is performed at configure time; replacing the placeholder
# with the real Chart.js bundle is a manual filesystem op the user does
# AFTER configure (no rebuild required because the server reads files at
# request time, not configure time).
set(TOPO_DEBUG_VENDOR_BUILD_DIR
    "${CMAKE_BINARY_DIR}/topo-debug-vendor"
    CACHE INTERNAL "runtime location of vendored SPA assets")
file(MAKE_DIRECTORY "${TOPO_DEBUG_VENDOR_BUILD_DIR}")
file(GLOB _topo_debug_vendor_files
    "${CMAKE_CURRENT_SOURCE_DIR}/Server/vendor/*")
foreach(_f IN LISTS _topo_debug_vendor_files)
    get_filename_component(_name "${_f}" NAME)
    configure_file("${_f}" "${TOPO_DEBUG_VENDOR_BUILD_DIR}/${_name}" COPYONLY)
endforeach()

# copy the default `*.html.tpl` templates to a stable build
# location so `topo debug serve` can default to it. Like the vendor dir the
# server reads template files at REQUEST time, so a user editing a copied
# template (or pointing --template-path at their own dir) takes effect on
# the next browser refresh with no tool rebuild.
set(TOPO_DEBUG_TEMPLATES_BUILD_DIR
    "${CMAKE_BINARY_DIR}/topo-debug-templates"
    CACHE INTERNAL "runtime location of default templates")
file(MAKE_DIRECTORY "${TOPO_DEBUG_TEMPLATES_BUILD_DIR}")
file(GLOB _topo_debug_tpl_files
    "${CMAKE_CURRENT_SOURCE_DIR}/Server/web/templates/*")
foreach(_f IN LISTS _topo_debug_tpl_files)
    get_filename_component(_name "${_f}" NAME)
    configure_file("${_f}" "${TOPO_DEBUG_TEMPLATES_BUILD_DIR}/${_name}" COPYONLY)
endforeach()

# install component — ship under topo-core-check (debug metadata is
# part of the same "no-LLVM frontend tooling" surface as Analysis/Check).
install(TARGETS TopoDebug
    EXPORT TopoCoreTargets
    COMPONENT topo-core-check
    ARCHIVE DESTINATION lib/topo-core
    LIBRARY DESTINATION lib/topo-core
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/topo/Debug
    DESTINATION include/topo
    COMPONENT topo-core-check
    FILES_MATCHING PATTERN "*.h" PATTERN "*.json"
)
