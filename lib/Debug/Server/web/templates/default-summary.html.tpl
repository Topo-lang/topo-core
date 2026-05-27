<!-- Plan 45 WP45d default template: summary card.
     Data model: { symbol, site, rendered, template, placeholders: [..] }
     Edit this file and refresh the browser — no tool rebuild needed. -->
<div class="topo-tpl topo-summary">
  <h3>{{symbol}} <span class="site">@ {{site}}</span></h3>
  {{#if rendered}}<p class="rendered">{{rendered}}</p>{{/if}}
  {{#if template}}<pre class="tpl-src">{{template}}</pre>{{/if}}
  {{#if placeholders}}
  <ul class="placeholders">
    {{#each placeholders}}<li>{{.}}</li>{{/each}}
  </ul>
  {{/if}}
</div>
