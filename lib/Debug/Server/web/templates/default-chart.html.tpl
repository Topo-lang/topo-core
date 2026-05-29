<!-- Default template: chart caption + raw config.
     Data model: { method, site, output (Chart.js config JSON) }
     The actual canvas is drawn by the SPA's renderChartWidget; this
     template is the human/LLM-editable caption + config inspector.
     Refresh the browser to pick up edits — no rebuild. -->
<div class="topo-tpl topo-chart">
  <h3>{{method}} <span class="site">@ {{site}}</span></h3>
  <p>Chart.js config produced by the render-fn:</p>
  <pre class="chart-config">{{output}}</pre>
</div>
