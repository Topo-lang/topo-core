# `topo debug serve` vendor directory

This directory holds **third-party JavaScript bundles** served by
`topo debug serve` under the route `GET /vendor/<file>`. They are not part
of the Topo source tree's intellectual property and are deliberately kept
out of the binary (the SPA fetches them with a runtime `<script src>` tag,
not as embedded string literals).

## Files

| File | Purpose | License | Source |
|------|---------|---------|--------|
| `chart.umd.min.js` | Chart.js v4 UMD single-file bundle, consumed by `renderChartWidget` in the SPA | MIT | https://github.com/chartjs/Chart.js/releases (download `chart.umd.js` from a tagged release) |
| `speedscope.html` | speedscope single-file flamegraph SPA, embedded via `<iframe>` by `renderSpeedscopeWidget` | MIT | https://github.com/jlfwong/speedscope (the `dist/release/index.html` single-file build) |
| `model-viewer.min.js` | Google `<model-viewer>` web component for glTF/GLB meshes, used by `renderMeshWidget` | Apache-2.0 | https://cdn.jsdelivr.net/npm/@google/model-viewer/dist/model-viewer.min.js |
| `plotly.min.js` | Plotly.js scientific plotting bundle, used by `renderPlotlyWidget` | MIT | https://cdn.plot.ly/plotly-latest.min.js |
| `d3.min.js` | d3 v7 bundle, used by `renderD3Widget` (free-form user/LLM snippets) | ISC | https://cdn.jsdelivr.net/npm/d3@7/dist/d3.min.js |
| `marked.min.js` | marked Markdown→HTML, used by `renderMarkdownWidget` (summary / AI explanation text) | MIT | https://cdn.jsdelivr.net/npm/marked/marked.min.js |

A render-fn selects which widget renders its output by tagging the output
object with `__topo_widget` (`chart` / `speedscope` / `mesh` / `plotly` /
`d3` / `markdown`), or by a method-name suffix convention (`*_chart`,
`*_mesh`, `*_plot`, `*_d3`, `*_md`, `*_flame`).

## Placeholder mode

To keep the build hermetic and the test suite runnable without network
access, every file in this directory ships as a **small placeholder stub**
that satisfies the SPA's load-time `<script>` tag without doing anything
useful. The placeholder always emits a `console.warn` so a developer who
forgot to fetch the real bundle sees an obvious diagnostic in DevTools.

The e2e test suite detects placeholder mode (the SPA's render widget tests
look for a magic comment string) and:

- Always exercises the **server-produced render-fn output** (the C++ slice
  e2e asserts the JSON config returned by `POST /render` directly — no
  browser involvement).
- **Skips with an explicit reason** any test that needs the real Chart.js
  to be present in the DOM.

## Enabling real charts

1. Fetch the upstream UMD bundle (size ~200 KB). E.g. for v4.x:

   ```sh
   curl -L -o topo-core/lib/Debug/Server/vendor/chart.umd.min.js \
        https://cdn.jsdelivr.net/npm/chart.js@4/dist/chart.umd.js
   ```

2. Re-run `cmake --build build` (no reconfigure needed — the file is read
   at request time, not configure time).

3. Re-run the render e2e tests; the "placeholder; skipped" entries should
   then exercise the real path.

## Why not bundle Chart.js?

- It is ~200 KB compressed; embedding it in every Topo distribution
  inflates the binary noticeably for a feature most users will not enable.
- License compatibility (MIT) is permissive but adding a vendored copy to
  Topo's git history mixes ownership; the SPA's load-on-demand model lets
  us upgrade Chart.js without coordinated commits.
- Hermetic builds: the tests should not fail offline. Placeholder + skip
  is the contract.
