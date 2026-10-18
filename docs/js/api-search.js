// Search over window.LVS_API (generated from the spec by tools/build_site_data.py).
// Pure: no DOM, so it can be tested in node.

(function (root) {
  'use strict';

  // Every whitespace-separated term must be a substring of name, kind or summary.
  // Best first: exact name, name prefix, name substring, then kind/summary matches;
  // ties keep the entries' own order.
  function searchApi(entries, query, kind) {
    const terms = String(query || '').toLowerCase().split(/\s+/).filter(Boolean);
    const scored = [];
    entries.forEach((e, i) => {
      if (kind && kind !== 'all' && e.kind !== kind) return;
      const name = e.name.toLowerCase();
      const hay = name + ' ' + e.kind + ' ' + (e.summary || '').toLowerCase();
      if (!terms.every(t => hay.includes(t))) return;
      let rank = 3;
      if (terms.length) {
        const first = terms[0];
        if (name === first) rank = 0;
        else if (name.startsWith(first)) rank = 1;
        else if (name.includes(first)) rank = 2;
      }
      scored.push({ e, rank, i });
    });
    scored.sort((a, b) => a.rank - b.rank || a.i - b.i);
    return scored.map(s => s.e);
  }

  root.LVS_searchApi = searchApi;
  if (typeof module !== 'undefined' && module.exports) module.exports = searchApi;
})(typeof window !== 'undefined' ? window : globalThis);
