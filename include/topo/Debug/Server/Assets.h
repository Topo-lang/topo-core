#ifndef TOPO_DEBUG_SERVER_ASSETS_H
#define TOPO_DEBUG_SERVER_ASSETS_H

// Static assets for the `topo debug serve` SPA.
//
// The HTML+JS+CSS bundle is compiled into the binary as raw string literals.
// Two assets only: a self-contained HTML shell and the app's JS. CSS is
// inlined inside the HTML to keep the route table small.

namespace topo::debug_server {

extern const char kIndexHtml[];
extern const char kAppJs[];

} // namespace topo::debug_server

#endif // TOPO_DEBUG_SERVER_ASSETS_H
