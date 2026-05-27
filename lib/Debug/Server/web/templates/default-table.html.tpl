<!-- Plan 45 WP45d default template: 1D value table.
     Data model: { method, site, values: [ {idx, value}, ... ] }
     Refresh the browser to pick up edits — no rebuild. -->
<div class="topo-tpl topo-table">
  <h3>{{method}} <span class="site">@ {{site}}</span></h3>
  <table>
    <thead><tr><th>#</th><th>value</th></tr></thead>
    <tbody>
      {{#each values}}<tr><td>{{idx}}</td><td>{{value}}</td></tr>{{/each}}
    </tbody>
  </table>
</div>
