// Embedded HTML + JS for `topo debug serve`.
//
// The SPA is intentionally vanilla (no framework, no bundler, no build step)
// so an LLM or a curious user can read the source straight out of the
// compiled binary's strings table, fork it, and serve a customised UI from
// the same dbg.json stream. Two files, raw string literals, no escaping.

#include "topo/Debug/Server/Assets.h"

namespace topo::debug_server {

extern const char kIndexHtml[] = R"HTML(<!doctype html>
<html lang=en>
<head>
<meta charset="utf-8">
<title>topo debug</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
html, body { height: 100%; }
body {
  font: 13px/1.5 ui-monospace, "SF Mono", "JetBrains Mono", Menlo, monospace;
  color: #1c1f23;
  background: #f6f7f9;
  display: grid;
  grid-template-rows: 40px 1fr 260px;
}
header {
  background: #1c1f23;
  color: #e8eaed;
  padding: 0 16px;
  display: flex;
  align-items: center;
  gap: 14px;
}
header h1 { font-size: 13px; font-weight: 600; letter-spacing: 0.04em; }
header .src { font-size: 11px; opacity: 0.7; }
header .status { margin-left: auto; font-size: 11px; }
header .status.ok { color: #62d27c; }
header .status.err { color: #ff8a8a; }
main {
  display: grid;
  grid-template-columns: 280px 1fr;
  overflow: hidden;
  min-height: 0;
}
aside {
  border-right: 1px solid #d6d9df;
  background: #fff;
  overflow-y: auto;
  min-height: 0;
}
aside .group {
  padding: 8px 14px;
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: #6b7280;
  background: #fafbfc;
  border-bottom: 1px solid #eef0f3;
  position: sticky;
  top: 0;
}
aside ul { list-style: none; }
aside li {
  padding: 7px 14px;
  cursor: pointer;
  border-bottom: 1px solid #f1f3f6;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}
aside li:hover { background: #f6f7f9; }
aside li.sel { background: #e7eefb; color: #1f4eae; font-weight: 600; }
aside li .k {
  font-size: 10px;
  opacity: 0.65;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  font-weight: 500;
}
section.detail {
  overflow-y: auto;
  padding: 16px 20px;
  min-height: 0;
}
section.detail .empty { color: #98a2af; font-style: italic; }
section.detail h2 {
  font-size: 10px;
  text-transform: uppercase;
  color: #6b7280;
  margin-top: 18px;
  margin-bottom: 8px;
  letter-spacing: 0.08em;
}
section.detail h2:first-child { margin-top: 0; }
section.detail dl {
  display: grid;
  grid-template-columns: max-content 1fr;
  gap: 4px 14px;
}
section.detail dt { color: #6b7280; }
section.detail dd { color: #1c1f23; word-break: break-all; }
section.detail .card {
  border: 1px solid #e6e8ec;
  border-radius: 4px;
  padding: 9px 12px;
  margin-bottom: 6px;
  background: #fff;
}
section.detail .card b { color: #1f4eae; }
section.detail pre {
  background: #0f1115;
  color: #d3d7de;
  padding: 9px 12px;
  border-radius: 4px;
  overflow-x: auto;
  font-size: 12px;
  margin-top: 6px;
  white-space: pre-wrap;
  word-break: break-all;
}
footer {
  border-top: 1px solid #d6d9df;
  background: #fff;
  padding: 12px 20px;
  display: grid;
  grid-template-rows: max-content max-content 1fr;
  gap: 8px;
  min-height: 0;
}
footer h2 {
  font-size: 10px;
  text-transform: uppercase;
  color: #6b7280;
  letter-spacing: 0.08em;
}
footer .row { display: flex; gap: 8px; }
footer input {
  font: inherit;
  padding: 6px 9px;
  border: 1px solid #c8ccd2;
  border-radius: 3px;
  flex: 1;
  min-width: 0;
}
footer input.site { flex: 0 0 220px; }
footer button {
  font: inherit;
  font-weight: 600;
  padding: 6px 16px;
  background: #1f4eae;
  color: #fff;
  border: 0;
  border-radius: 3px;
  cursor: pointer;
}
footer button:disabled { opacity: 0.5; cursor: not-allowed; }
footer .out {
  overflow: auto;
  background: #0f1115;
  color: #d3d7de;
  padding: 10px 12px;
  border-radius: 3px;
  font-size: 12px;
  white-space: pre;
  min-height: 0;
}
footer .out.err { color: #ff9a9a; }
footer .out.dim { color: #6e7681; font-style: italic; }
</style>
</head>
<body>
<header>
  <h1>topo debug</h1>
  <span class=src id=src>(loading...)</span>
  <span class=status id=status>...</span>
</header>
<main>
  <aside>
    <div class=group id=symHeader>Symbols</div>
    <ul id=symList></ul>
  </aside>
  <section class=detail id=detail><span class=empty>(no symbol selected)</span></section>
</main>
<footer>
  <h2>Query  &middot;  POST /query  &rarr;  Compute layer</h2>
  <div class=row>
    <input id=qExpr placeholder='expression  e.g.  (max(matrix) - min(matrix)) / count(matrix)' autocomplete=off />
    <input id=qSite class=site placeholder='--break site (required)' autocomplete=off />
    <button id=qRun>Run</button>
  </div>
  <pre class='out dim' id=qOut>(no result yet)</pre>
</footer>
<script src="/app.js"></script>
</body>
</html>
)HTML";

extern const char kAppJs[] = R"JS(// topo debug SPA — vanilla JS, no framework. Designed to be readable
// (and rewritable) by humans and LLMs alike. The data model is whatever
// `*.topo-dbg.json` carries — see the debug session schema for details.

const $ = id => document.getElementById(id);

let dbg = null;
let selectedIdx = -1;

function escapeHtml(s) {
  if (s == null) return '';
  return String(s).replace(/[&<>"']/g, c => ({
    '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'
  })[c]);
}

// WebSocket-backed query/summary. We open one ws:// connection
// on load and multiplex every request through it (correlated by `id`). On
// any failure (server lacks /ws, network blip, browser blocked) the helpers
// fall back to the HTTP POST endpoints — keeping the SPA functional against
// older servers and during temporary disconnects.
const wsClient = (() => {
  let ws = null;
  let nextId = 1;
  const pending = new Map();
  let connecting = null;

  function url() {
    const proto = (location.protocol === 'https:') ? 'wss:' : 'ws:';
    return proto + '//' + location.host + '/ws';
  }

  function connect() {
    if (ws && ws.readyState === 1) return Promise.resolve(ws);
    if (connecting) return connecting;
    connecting = new Promise((resolve, reject) => {
      let sock;
      try { sock = new WebSocket(url()); }
      catch (e) { connecting = null; reject(e); return; }
      sock.onopen = () => {
        ws = sock;
        connecting = null;
        const dot = document.getElementById('wsDot');
        if (dot) { dot.style.background = '#4caf50'; dot.title = 'ws connected'; }
        resolve(sock);
      };
      sock.onmessage = ev => {
        let m; try { m = JSON.parse(ev.data); } catch (_) { return; }
        const id = m.id;
        const cb = pending.get(id);
        if (cb) { pending.delete(id); cb.resolve(m); }
      };
      sock.onerror = () => {};
      sock.onclose = () => {
        const dot = document.getElementById('wsDot');
        if (dot) { dot.style.background = '#999'; dot.title = 'ws disconnected'; }
        ws = null;
        connecting = null;
        for (const cb of pending.values()) cb.reject(new Error('ws closed'));
        pending.clear();
      };
      setTimeout(() => {
        if (!ws) { connecting = null; reject(new Error('ws connect timeout')); }
      }, 1500);
    });
    return connecting;
  }

  function call(payload) {
    return connect().then(sock => new Promise((resolve, reject) => {
      const id = 'r' + (nextId++);
      pending.set(id, {resolve, reject});
      sock.send(JSON.stringify(Object.assign({id}, payload)));
      setTimeout(() => {
        if (pending.has(id)) {
          pending.delete(id);
          reject(new Error('ws timeout'));
        }
      }, 30000);
    }));
  }

  // Kick the connect attempt at load so the latency hides behind /dbg.json.
  setTimeout(() => { connect().catch(() => {}); }, 0);
  return {call};
})();

// query(expr, site) → server reply object. Tries WS, falls back to POST.
async function callQuery(expr, site) {
  try {
    return await wsClient.call({op: 'query', expr, site});
  } catch (_) {
    const r = await fetch('/query', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({expr, site}),
    });
    const body = await r.json();
    if (!r.ok && body.ok !== false) body.ok = false;
    return body;
  }
}

// summary(symbol, site) → server reply object. Same fallback contract.
async function callSummary(symbol, site) {
  try {
    return await wsClient.call({op: 'summary', symbol, site});
  } catch (_) {
    const r = await fetch('/summary', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({symbol, site}),
    });
    const body = await r.json();
    if (!r.ok && body.ok !== false) body.ok = false;
    return body;
  }
}

async function loadDbg() {
  try {
    const r = await fetch('/dbg.json', {cache: 'no-store'});
    if (!r.ok) throw new Error('HTTP ' + r.status);
    dbg = await r.json();
    const files = (dbg.source && dbg.source.topo_files) || [];
    $('src').textContent = files.length ? files.join(', ') : '(no source manifest)';
    $('status').className = 'status ok';
    $('status').textContent = 'schema v' + (dbg.schema_version || '?');
    renderList();
  } catch (e) {
    $('status').className = 'status err';
    $('status').textContent = 'load failed: ' + e.message;
    $('src').textContent = '';
  }
}

function renderList() {
  const ul = $('symList');
  ul.innerHTML = '';
  const syms = dbg.symbols || [];
  $('symHeader').textContent = `Symbols (${syms.length})`;
  syms.forEach((s, i) => {
    const li = document.createElement('li');
    li.dataset.idx = String(i);
    li.innerHTML =
      `<span>${escapeHtml(s.topo_name)}</span>` +
      `<span class=k>${escapeHtml(s.kind)}</span>`;
    li.onclick = () => select(i);
    ul.appendChild(li);
  });
  if (syms.length) select(0);
  else $('detail').innerHTML = '<span class=empty>(no symbols declared)</span>';
}

function select(i) {
  selectedIdx = i;
  Array.from($('symList').children).forEach((el, j) => {
    el.classList.toggle('sel', j === i);
  });
  renderDetail(dbg.symbols[i]);
}

function renderDetail(s) {
  const parts = [];
  parts.push('<h2>Identity</h2><dl>');
  parts.push(`<dt>topo_name</dt><dd>${escapeHtml(s.topo_name)}</dd>`);
  parts.push(`<dt>host_symbol</dt><dd>${escapeHtml(s.host_symbol)}</dd>`);
  parts.push(`<dt>kind</dt><dd>${escapeHtml(s.kind)}</dd>`);
  parts.push('</dl>');

  if (s.summary_template) {
    parts.push('<h2>Summary template  &middot;  POST /summary</h2>');
    parts.push(`<div class=card><pre style="margin:0">${escapeHtml(s.summary_template)}</pre></div>`);
    parts.push('<div class=row style="display:flex;gap:8px;margin:6px 0 4px 0">');
    parts.push(`<input id=sumSite class=site placeholder="--break site (required)"`);
    parts.push(` value="${escapeHtml(($('qSite') && $('qSite').value) || '')}"`);
    parts.push(` style="flex:1;font:inherit;padding:5px 8px;border:1px solid #c8ccd2;border-radius:3px"/>`);
    parts.push(`<button id=sumRun style="font:inherit;font-weight:600;padding:5px 14px;background:#1f4eae;color:#fff;border:0;border-radius:3px;cursor:pointer">Render summary</button>`);
    parts.push('</div>');
    parts.push(`<pre id=sumOut style="background:#0f1115;color:#6e7681;font-style:italic;padding:9px 12px;border-radius:4px;margin-top:4px;font-size:12px;white-space:pre-wrap;word-break:break-all">(not yet rendered)</pre>`);
  }

  const views = s.views || [];
  if (views.length) {
    parts.push(`<h2>Views (${views.length})</h2>`);
    for (const v of views) {
      const e = v.expr || {};
      let desc;
      if (e.kind === 'slice') {
        const lo = (e.start !== undefined) ? e.start : '';
        const hi = (e.end !== undefined) ? e.end : '';
        desc = `slice(${escapeHtml(e.container)}, ${lo}, ${hi})`;
      } else {
        desc = `field(${escapeHtml(e.container || '')})`;
      }
      parts.push(
        `<div class=card><b>${escapeHtml(v.name)}</b>` +
        ` &middot; <span style=opacity:.7>${desc}</span></div>`
      );
    }
  }

  const renders = s.render_decls || [];
  if (renders.length) {
    parts.push(`<h2>Render decls (${renders.length})</h2>`);
    renders.forEach((r, idx) => {
      parts.push(
        `<div class=card><b>method=${escapeHtml(r.method)}</b>` +
        `<pre>${escapeHtml(r.raw_body)}</pre>` +
        `<div class=row style="display:flex;gap:8px;margin:6px 0 4px 0">` +
        `<button id=rd_run_${idx} style="font:inherit;font-weight:600;` +
        `padding:5px 14px;background:#1f4eae;color:#fff;border:0;` +
        `border-radius:3px;cursor:pointer">Render</button>` +
        `</div>` +
        `<div id=rd_out_${idx} style="background:#0f1115;color:#6e7681;` +
        `font-style:italic;padding:9px 12px;border-radius:4px;margin-top:4px;` +
        `font-size:12px;white-space:pre-wrap;word-break:break-all">` +
        `(not yet rendered)</div>` +
        `</div>`
      );
    });
  }

  const inactive = s.inactive_regions || [];
  if (inactive.length) {
    parts.push(`<h2>Inactive regions (${inactive.length})</h2>`);
    for (const ir of inactive) {
      parts.push(
        `<div class=card>mode=<b>${escapeHtml(ir.mode)}</b>` +
        `<pre>${escapeHtml(JSON.stringify(ir.expr, null, 2))}</pre></div>`
      );
    }
  }

  const ext = s.backend_ext || {};
  if (Object.keys(ext).length) {
    parts.push('<h2>Backend ext (raw)</h2>');
    parts.push(`<pre>${escapeHtml(JSON.stringify(ext, null, 2))}</pre>`);
  }

  $('detail').innerHTML = parts.join('\n');

  // Bind summary panel widgets after innerHTML has been
  // replaced. The panel only renders when the selected symbol carries a
  // `summary_template`; otherwise these getElementById lookups return null
  // and the bindings are skipped harmlessly.
  if ($('sumRun')) {
    $('sumRun').addEventListener('click', () => runSummary(s.topo_name));
    $('sumSite').addEventListener('keydown', e => {
      if (e.key === 'Enter') runSummary(s.topo_name);
    });
  }

  // Bind render_decl widget buttons. The qSite input at
  // the bottom of the page is the shared site source; reading at click time
  // lets the user change site between renders without rebinding.
  (s.render_decls || []).forEach((r, idx) => {
    const btn = $(`rd_run_${idx}`);
    const out = $(`rd_out_${idx}`);
    if (!btn || !out) return;
    btn.addEventListener('click', () => {
      const site = ($('qSite') && $('qSite').value || '').trim();
      if (!site) {
        out.style.color = '#ff9a9a';
        out.style.fontStyle = 'italic';
        out.textContent = 'site required (use the bottom query box\'s site input)';
        return;
      }
      const args = parseRenderArgs(r.raw_body);
      if (args === null && r.method !== 'summary') {
        out.style.color = '#ff9a9a';
        out.style.fontStyle = 'italic';
        out.textContent =
          'could not parse render body as `key: value; ...`; raw body shown above';
        return;
      }
      if (r.method === 'table') {
        renderTableWidget(args, site, out);
      } else if (r.method === 'histogram') {
        renderHistogramWidget(args, site, out);
      } else if (r.method === 'summary') {
        renderSummaryWidget(args || {}, site, out);
      } else {
        // Unknown method names dispatch to the
        // server-side render-fn loader. The server consults its
        // render-decl registry (allow-list of methods declared in the
        // .topo source) and dlopen's libtopo_render_<method>.{so,dylib}.
        // For Chart.js (`chart_demo`) the server emits a Chart.js config
        // object that renderChartWidget feeds into `new Chart(...)`. For
        // other methods we fall back to a generic JSON dump.
        renderServerSideWidget(r.method, site, out);
      }
    });
  });
}

// render_decl helpers.
//
// parseRenderArgs takes the raw body the user wrote between `render method=<m>
// { ... }` and returns a plain object keyed by trimmed-string keys with
// trimmed-string values. Grammar is intentionally minimal:
//
//   body  ::= pair (';' pair)* ';'?
//   pair  ::= key ':' value
//
// Whitespace (including newlines) around keys, values, and separators is
// stripped. Empty pairs (consecutive semicolons or a trailing one) are
// ignored. If any non-empty pair is missing the colon or has an empty key,
// the whole parse is rejected with `null` and the caller falls back to the
// raw <pre> display.
function parseRenderArgs(rawBody) {
  if (rawBody == null) return null;
  // The .topo parser captures the outer `{` and `}` as part of raw_body
  // (Parser.cpp's depth-counted token capture). Strip one wrapping pair
  // before splitting on `;` so the trailing `}` does not become a junk
  // fragment without a `:`.
  let body = String(rawBody).trim();
  if (body.length >= 2 && body.charAt(0) === '{'
      && body.charAt(body.length - 1) === '}') {
    body = body.slice(1, -1).trim();
  }
  const out = {};
  const parts = body.split(';');
  let sawAny = false;
  for (const part of parts) {
    const trimmed = part.trim();
    if (!trimmed) continue;
    const colon = trimmed.indexOf(':');
    if (colon < 0) return null;
    const key = trimmed.slice(0, colon).trim();
    const value = trimmed.slice(colon + 1).trim();
    if (!key) return null;
    out[key] = value;
    sawAny = true;
  }
  return sawAny ? out : null;
}

// POST /query with the parsed `input` expression at `site`, and render the
// result into outEl as a <table>. 1D number lists become a single-row
// table; 2D number lists become nested rows; scalars become a 1x1 cell.
async function renderTableWidget(args, site, outEl) {
  if (!args || !args.input) {
    outEl.style.color = '#ff9a9a';
    outEl.style.fontStyle = 'italic';
    outEl.textContent = "render(table): missing required 'input' key";
    return;
  }
  outEl.style.color = '#6e7681';
  outEl.style.fontStyle = 'italic';
  outEl.textContent = 'running ...';
  try {
    const body = await callQuery(args.input, site);
    if (body.ok === false) {
      outEl.style.color = '#ff9a9a';
      outEl.style.fontStyle = 'italic';
      outEl.textContent = body.error || JSON.stringify(body, null, 2);
      return;
    }
    const val = body.result;
    let rows;
    if (Array.isArray(val) && val.length && Array.isArray(val[0])) {
      rows = val;
    } else if (Array.isArray(val)) {
      rows = [val];
    } else {
      rows = [[val]];
    }
    const cellStyle =
      'border:1px solid #2a2f38;padding:3px 8px;color:#d3d7de;' +
      'font:inherit;text-align:right;min-width:32px';
    const tbl = ['<table style="border-collapse:collapse;font-size:12px">'];
    for (const row of rows) {
      tbl.push('<tr>');
      for (const cell of row) {
        tbl.push(`<td style="${cellStyle}">${escapeHtml(cell)}</td>`);
      }
      tbl.push('</tr>');
    }
    tbl.push('</table>');
    outEl.style.color = '#d3d7de';
    outEl.style.fontStyle = 'normal';
    outEl.innerHTML = tbl.join('');
  } catch (e) {
    outEl.style.color = '#ff9a9a';
    outEl.style.fontStyle = 'italic';
    outEl.textContent = 'request failed: ' + e.message;
  }
}

// POST /query with `input`, expect a 1D number list, render as styled <div>
// flex bars. Max bar height is ~80px, scaled relative to the maximum
// absolute value in the list.
async function renderHistogramWidget(args, site, outEl) {
  if (!args || !args.input) {
    outEl.style.color = '#ff9a9a';
    outEl.style.fontStyle = 'italic';
    outEl.textContent = "render(histogram): missing required 'input' key";
    return;
  }
  outEl.style.color = '#6e7681';
  outEl.style.fontStyle = 'italic';
  outEl.textContent = 'running ...';
  try {
    const body = await callQuery(args.input, site);
    if (body.ok === false) {
      outEl.style.color = '#ff9a9a';
      outEl.style.fontStyle = 'italic';
      outEl.textContent = body.error || JSON.stringify(body, null, 2);
      return;
    }
    const val = body.result;
    if (!Array.isArray(val) || (val.length && Array.isArray(val[0]))) {
      outEl.style.color = '#ff9a9a';
      outEl.style.fontStyle = 'italic';
      outEl.textContent =
        'render(histogram): expected a 1D number list, got ' +
        JSON.stringify(val);
      return;
    }
    const nums = val.map(v => Number(v));
    const peak = nums.reduce((m, n) => Math.max(m, Math.abs(n)), 0);
    const maxPx = 80;
    const bars = ['<div style="display:flex;align-items:flex-end;gap:2px;' +
                  'height:' + (maxPx + 18) + 'px;padding:4px 0">'];
    for (const n of nums) {
      const h = peak > 0 ? Math.max(1, Math.round(Math.abs(n) / peak * maxPx)) : 1;
      const color = n < 0 ? '#ae3636' : '#1f4eae';
      bars.push(
        '<div style="display:flex;flex-direction:column;align-items:center;' +
        'gap:2px;min-width:14px">' +
        `<div style="width:14px;height:${h}px;background:${color}"></div>` +
        `<div style="font-size:9px;color:#9aa3af">${escapeHtml(n)}</div>` +
        '</div>'
      );
    }
    bars.push('</div>');
    outEl.style.color = '#d3d7de';
    outEl.style.fontStyle = 'normal';
    outEl.innerHTML = bars.join('');
  } catch (e) {
    outEl.style.color = '#ff9a9a';
    outEl.style.fontStyle = 'italic';
    outEl.textContent = 'request failed: ' + e.message;
  }
}

// POST /summary for the currently selected symbol. Note: the server-side
// /summary endpoint uses the *symbol's own* `summary_template` field, not
// this render_decl's raw body — args are accepted for parity but ignored.
async function renderSummaryWidget(args, site, outEl) {
  const sym = (selectedIdx >= 0 && dbg && dbg.symbols)
    ? dbg.symbols[selectedIdx] : null;
  if (!sym) {
    outEl.style.color = '#ff9a9a';
    outEl.style.fontStyle = 'italic';
    outEl.textContent = 'render(summary): no symbol selected';
    return;
  }
  outEl.style.color = '#6e7681';
  outEl.style.fontStyle = 'italic';
  outEl.textContent = 'running ...';
  try {
    const body = await callSummary(sym.topo_name, site);
    if (body.ok) {
      outEl.style.color = '#d3d7de';
      outEl.style.fontStyle = 'normal';
      outEl.textContent = body.rendered;
    } else {
      outEl.style.color = '#ff9a9a';
      outEl.style.fontStyle = 'italic';
      outEl.textContent = body.error || JSON.stringify(body, null, 2);
    }
  } catch (e) {
    outEl.style.color = '#ff9a9a';
    outEl.style.fontStyle = 'italic';
    outEl.textContent = 'request failed: ' + e.message;
  }
}

async function runSummary(symbol) {
  const site = $('sumSite').value.trim();
  if (!site) {
    $('sumOut').style.color = '#ff9a9a';
    $('sumOut').textContent = 'site is required';
    return;
  }
  $('sumRun').disabled = true;
  $('sumOut').style.color = '#6e7681';
  $('sumOut').style.fontStyle = 'italic';
  $('sumOut').textContent = 'running ...';
  try {
    const body = await callSummary(symbol, site);
    $('sumOut').style.fontStyle = 'normal';
    if (body.ok) {
      $('sumOut').style.color = '#d3d7de';
      $('sumOut').textContent = body.rendered;
    } else {
      $('sumOut').style.color = '#ff9a9a';
      $('sumOut').textContent = body.error || JSON.stringify(body, null, 2);
    }
  } catch (e) {
    $('sumOut').style.color = '#ff9a9a';
    $('sumOut').textContent = 'request failed: ' + e.message;
  } finally {
    $('sumRun').disabled = false;
  }
}

// Server-side render-fn dispatcher.
//
// POST /render with the method name (server resolves declared inputs from
// the .topo render_decl, evaluates each via /query, then feeds the merged
// JSON object to the user's libtopo_render_<method>.{so,dylib}). On
// success the response carries an `output` JSON value — the render-fn's
// product. For `chart_demo` we feed that straight into Chart.js; for any
// other method we drop the JSON in a <pre> so the user can see what their
// render-fn produced (a useful default while iterating on a new method).
async function renderServerSideWidget(method, site, outEl) {
  outEl.style.color = '#6e7681';
  outEl.style.fontStyle = 'italic';
  outEl.textContent = 'running ' + method + ' (server) ...';
  try {
    const r = await fetch('/render', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({method, site}),
    });
    const body = await r.json();
    if (!r.ok || body.ok === false) {
      outEl.style.color = '#ff9a9a';
      outEl.style.fontStyle = 'italic';
      outEl.textContent = body.error || JSON.stringify(body, null, 2);
      return;
    }
    if (method === 'chart_demo') {
      await renderChartWidget(body.output, outEl);
      return;
    }
    // A render-fn opts into an advanced visualiser by
    // tagging its output object with `__topo_widget`: one of
    // 'chart' | 'speedscope' | 'mesh' | 'plotly' | 'd3' | 'markdown'.
    // Method-name suffix conventions (*_chart, *_mesh, ...) are a fallback
    // so a method can pick a widget without changing its JSON shape.
    const out = body.output;
    const hint = (out && typeof out === 'object' && out.__topo_widget) ||
      (/_chart$/.test(method) ? 'chart' :
       /_(flame|speedscope|trace)$/.test(method) ? 'speedscope' :
       /_(mesh|gltf|glb)$/.test(method) ? 'mesh' :
       /_(plot|plotly)$/.test(method) ? 'plotly' :
       /_d3$/.test(method) ? 'd3' :
       /_(md|markdown|doc)$/.test(method) ? 'markdown' : '');
    if (hint === 'chart')      { await renderChartWidget(out, outEl); return; }
    if (hint === 'speedscope') { renderSpeedscopeWidget(out, outEl); return; }
    if (hint === 'mesh')       { await renderMeshWidget(out, outEl); return; }
    if (hint === 'plotly')     { await renderPlotlyWidget(out, outEl); return; }
    if (hint === 'd3')         { await renderD3Widget(out, outEl); return; }
    if (hint === 'markdown')   { await renderMarkdownWidget(out, outEl); return; }
    // Generic fallback for any other declared method: pretty-print the
    // server-produced JSON so the user can see what their render-fn made.
    outEl.style.color = '#d3d7de';
    outEl.style.fontStyle = 'normal';
    const pre = document.createElement('pre');
    pre.style.margin = '0';
    pre.style.whiteSpace = 'pre-wrap';
    pre.style.wordBreak = 'break-all';
    pre.textContent = JSON.stringify(body.output, null, 2);
    outEl.innerHTML = '';
    outEl.appendChild(pre);
  } catch (e) {
    outEl.style.color = '#ff9a9a';
    outEl.style.fontStyle = 'italic';
    outEl.textContent = 'request failed: ' + e.message;
  }
}

// Chart.js widget.
//
// `config` is the JSON object the server-side render-fn returned (typically
// `{type: 'line', data: {...}, options: {...}}` — i.e. the exact shape
// Chart.js expects as its second `new Chart(canvas, ...)` argument).
//
// Chart.js is loaded lazily from `/vendor/chart.umd.min.js` because the
// real bundle is ~200KB and many sessions never trigger a chart render.
// If the vendor stub is in place (no real Chart.js), `new Chart` still
// returns an object — the placeholder warns to the console but does not
// draw anything. The e2e CTest asserts the server JSON response, not the
// canvas pixels, so the test passes under both stub and real-bundle modes.
let __topoChartLoadingPromise = null;
function ensureChartJsLoaded() {
  if (typeof window.Chart === 'function') return Promise.resolve(window.Chart);
  if (__topoChartLoadingPromise) return __topoChartLoadingPromise;
  __topoChartLoadingPromise = new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = '/vendor/chart.umd.min.js';
    s.onload = () => resolve(window.Chart);
    s.onerror = () => reject(new Error('failed to fetch /vendor/chart.umd.min.js'));
    document.head.appendChild(s);
  });
  return __topoChartLoadingPromise;
}

async function renderChartWidget(config, outEl) {
  if (!config || typeof config !== 'object') {
    outEl.style.color = '#ff9a9a';
    outEl.style.fontStyle = 'italic';
    outEl.textContent = 'chart_demo render-fn returned a non-object output';
    return;
  }
  try {
    await ensureChartJsLoaded();
  } catch (e) {
    outEl.style.color = '#ff9a9a';
    outEl.style.fontStyle = 'italic';
    outEl.textContent = 'Chart.js load failed: ' + e.message;
    return;
  }
  outEl.style.color = '#d3d7de';
  outEl.style.fontStyle = 'normal';
  outEl.innerHTML = '';
  const canvas = document.createElement('canvas');
  canvas.style.maxHeight = '240px';
  outEl.appendChild(canvas);
  try {
    /* global Chart */
    // eslint-disable-next-line no-new
    new Chart(canvas, config);
  } catch (e) {
    outEl.style.color = '#ff9a9a';
    outEl.style.fontStyle = 'italic';
    outEl.textContent = 'new Chart() threw: ' + e.message;
  }
}

// Advanced visualisation vendor integrations.
//
// Each helper lazy-loads its single-file vendor bundle from /vendor/ on
// first use (so a session that never opens a 3D mesh pays nothing) and
// renders the server-side render-fn `output` into outEl. The vendor files
// ship as placeholder stubs (see vendor/README.md); the stub path still
// exercises the loader + DOM wiring, the real bundle path draws pixels.

const __topoVendorLoaders = {};
function loadVendorScript(file) {
  if (__topoVendorLoaders[file]) return __topoVendorLoaders[file];
  __topoVendorLoaders[file] = new Promise((resolve, reject) => {
    const s = document.createElement('script');
    s.src = '/vendor/' + file;
    s.onload = () => resolve();
    s.onerror = () => reject(new Error('failed to fetch /vendor/' + file));
    document.head.appendChild(s);
  });
  return __topoVendorLoaders[file];
}

// speedscope: embed the single-file SPA in an <iframe>; the render-fn
// output is expected to be a speedscope-format profile JSON which we hand
// over via a blob URL on the ?profileURL= query param.
function renderSpeedscopeWidget(profile, outEl) {
  outEl.style.fontStyle = 'normal';
  outEl.style.color = '#d3d7de';
  outEl.innerHTML = '';
  let url;
  try {
    const blob = new Blob([JSON.stringify(profile)],
                          {type: 'application/json'});
    url = URL.createObjectURL(blob);
  } catch (e) {
    outEl.textContent = 'speedscope: could not build profile blob: ' +
      e.message;
    return;
  }
  const f = document.createElement('iframe');
  f.src = '/vendor/speedscope.html#profileURL=' +
    encodeURIComponent(url);
  f.style.width = '100%';
  f.style.height = '320px';
  f.style.border = '1px solid #2a2f38';
  outEl.appendChild(f);
}

// model-viewer: the render-fn output is a glTF/GLB document (or a data URL
// / object describing one). We embed a <model-viewer src=...> element.
async function renderMeshWidget(output, outEl) {
  try { await loadVendorScript('model-viewer.min.js'); }
  catch (e) {
    outEl.style.color = '#ff9a9a';
    outEl.style.fontStyle = 'italic';
    outEl.textContent = 'model-viewer load failed: ' + e.message;
    return;
  }
  outEl.style.fontStyle = 'normal';
  outEl.style.color = '#d3d7de';
  outEl.innerHTML = '';
  let src = output;
  if (output && typeof output === 'object') {
    src = output.src || output.uri ||
      ('data:model/gltf+json;base64,' + btoa(JSON.stringify(output)));
  }
  const mv = document.createElement('model-viewer');
  mv.setAttribute('src', src);
  mv.setAttribute('camera-controls', '');
  mv.style.width = '100%';
  mv.style.height = '320px';
  outEl.appendChild(mv);
}

// Plotly: render-fn output is { data, layout } as Plotly.newPlot expects.
async function renderPlotlyWidget(output, outEl) {
  try { await loadVendorScript('plotly.min.js'); }
  catch (e) {
    outEl.style.color = '#ff9a9a';
    outEl.style.fontStyle = 'italic';
    outEl.textContent = 'Plotly load failed: ' + e.message;
    return;
  }
  outEl.style.fontStyle = 'normal';
  outEl.style.color = '#d3d7de';
  outEl.innerHTML = '';
  const div = document.createElement('div');
  div.style.width = '100%';
  outEl.appendChild(div);
  try {
    /* global Plotly */
    await window.Plotly.newPlot(div, (output && output.data) || [],
                                (output && output.layout) || {});
  } catch (e) {
    outEl.style.color = '#ff9a9a';
    outEl.textContent = 'Plotly.newPlot threw: ' + e.message;
  }
}

// d3: render-fn output carries `{ snippet: "<d3 code string>", ... }`.
// The snippet runs with `d3`, `data`, and `el` in scope. CSP permitting;
// this is the user/LLM free-visualisation escape hatch.
async function renderD3Widget(output, outEl) {
  try { await loadVendorScript('d3.min.js'); }
  catch (e) {
    outEl.style.color = '#ff9a9a';
    outEl.style.fontStyle = 'italic';
    outEl.textContent = 'd3 load failed: ' + e.message;
    return;
  }
  outEl.style.fontStyle = 'normal';
  outEl.style.color = '#d3d7de';
  outEl.innerHTML = '';
  const el = document.createElement('div');
  outEl.appendChild(el);
  const snippet = (output && output.snippet) || '';
  if (!snippet) {
    el.textContent = 'd3 render-fn output has no `snippet` string';
    return;
  }
  try {
    /* global d3 */
    // eslint-disable-next-line no-new-func
    new Function('d3', 'data', 'el', snippet)(
      window.d3, (output && output.data), el);
  } catch (e) {
    outEl.style.color = '#ff9a9a';
    outEl.textContent = 'd3 snippet threw: ' + e.message;
  }
}

// marked: render-fn output is a Markdown string (or { markdown: "..." }).
async function renderMarkdownWidget(output, outEl) {
  try { await loadVendorScript('marked.min.js'); }
  catch (e) {
    outEl.style.color = '#ff9a9a';
    outEl.style.fontStyle = 'italic';
    outEl.textContent = 'marked load failed: ' + e.message;
    return;
  }
  outEl.style.fontStyle = 'normal';
  outEl.style.color = '#d3d7de';
  const md = (typeof output === 'string')
    ? output : ((output && output.markdown) || '');
  try {
    /* global marked */
    outEl.innerHTML = window.marked.parse(md);
  } catch (e) {
    outEl.style.color = '#ff9a9a';
    outEl.textContent = 'marked.parse threw: ' + e.message;
  }
}

async function runQuery() {
  const expr = $('qExpr').value.trim();
  const site = $('qSite').value.trim();
  if (!expr || !site) {
    $('qOut').className = 'out err';
    $('qOut').textContent = 'expr and site are both required';
    return;
  }
  $('qRun').disabled = true;
  $('qOut').className = 'out dim';
  $('qOut').textContent = 'running ...';
  try {
    const body = await callQuery(expr, site);
    const pretty = JSON.stringify(body, null, 2);
    $('qOut').className = (body.ok === false) ? 'out err' : 'out';
    $('qOut').textContent = pretty;
  } catch (e) {
    $('qOut').className = 'out err';
    $('qOut').textContent = 'request failed: ' + e.message;
  } finally {
    $('qRun').disabled = false;
  }
}

$('qRun').addEventListener('click', runQuery);
for (const id of ['qExpr', 'qSite']) {
  $(id).addEventListener('keydown', e => { if (e.key === 'Enter') runQuery(); });
}

loadDbg();
)JS";

} // namespace topo::debug_server
