"""
report_html.py — Standalone HTML report generator with embedded Plotly.js

Generates a beautiful, self-contained HTML report from a Thread Sentinel
JSON race report. Plotly.js is inlined from the installed `plotly` package, so
the file opens offline with no network access.
"""

from __future__ import annotations
import json
from typing import Dict, Any, List


_SEVERITY_BADGE = {
    "HIGH":   ("bg-red",    "HIGH"),
    "MEDIUM": ("bg-yellow", "MEDIUM"),
    "LOW":    ("bg-blue",   "LOW"),
}

_VERDICT_BADGE = {
    "CONFIRMED": ("bg-red",    "CONFIRMED"),
    "POTENTIAL": ("bg-yellow", "POTENTIAL"),
}


def _plotly_script() -> str:
    try:
        from plotly.offline import get_plotlyjs
    except ImportError:
        # Degrade to the CDN copy rather than fail when plotly is not installed
        return '<script src="https://cdn.plot.ly/plotly-2.26.0.min.js"></script>'
    # Guard against a literal closing tag inside the bundle ending the script element early
    return "<script>" + get_plotlyjs().replace("</script", "<\\/script") + "</script>"


def _badge(cls: str, text: str) -> str:
    return f'<span class="badge {cls}">{text}</span>'


def _races_table(races: List[Dict]) -> str:
    if not races:
        return '<p class="no-races">✓ No races detected.</p>'

    rows = []
    for i, race in enumerate(races, 1):
        verdict = race.get("verdict", "UNKNOWN")
        vc, vt  = _VERDICT_BADGE.get(verdict, ("bg-blue", verdict))
        sev     = race.get("severity", "")
        sc, st  = _SEVERITY_BADGE.get(sev, ("bg-blue", sev))
        fa      = race.get("first_access", {})
        sa      = race.get("second_access", {})

        rows.append(f"""
        <tr>
          <td>{i}</td>
          <td>{_badge(vc, vt)}</td>
          <td>{_badge(sc, st)}</td>
          <td><code>{race.get('variable','?')}</code></td>
          <td><code>{race.get('address','?')}</code></td>
          <td>{fa.get('type','?')} T{fa.get('thread','?')} <em>{fa.get('location','?')}</em></td>
          <td>{sa.get('type','?')} T{sa.get('thread','?')} <em>{sa.get('location','?')}</em></td>
          <td class="reason">{race.get('reason','')}</td>
        </tr>""")

    return f"""
    <table class="race-table">
      <thead>
        <tr>
          <th>#</th><th>Verdict</th><th>Severity</th><th>Variable</th>
          <th>Address</th><th>Access 1</th><th>Access 2</th><th>Reason</th>
        </tr>
      </thead>
      <tbody>{''.join(rows)}</tbody>
    </table>"""


def generate(data: Dict[str, Any], output_path: str) -> None:
    meta    = data.get("metadata", {})
    summary = data.get("summary", {})
    races   = data.get("races", [])
    prot    = data.get("protected_items", [])

    confirmed = summary.get("confirmed_races", 0)
    potential = summary.get("potential_races", 0)
    mode      = meta.get("mode", "unknown")
    detector  = meta.get("detector", "unknown")
    runtime   = meta.get("runtime_ms", 0)

    # Plotly pie data
    pie_labels = ["Confirmed", "Potential", "Safe"]
    pie_values = [confirmed, potential, max(0, 1 - confirmed - potential)]
    if confirmed == 0 and potential == 0:
        pie_values = [0, 0, 1]
    pie_colours = ["#ef4444", "#f59e0b", "#22c55e"]

    import json as _json
    pie_json = _json.dumps({
        "labels": pie_labels,
        "values": pie_values,
        "colors": pie_colours,
    })

    races_html = _races_table(races)

    prot_rows = ""
    for p in prot:
        prot_rows += f"<tr><td><code>{p.get('variable','?')}</code></td><td>{p.get('address','?')}</td><td>{p.get('location','?')}</td></tr>"
    prot_section = ""
    if prot:
        prot_section = f"""
        <section class="card">
          <h2>Protected Items (Prevent Mode)</h2>
          <table class="race-table">
            <thead><tr><th>Variable</th><th>Address</th><th>Location</th></tr></thead>
            <tbody>{prot_rows}</tbody>
          </table>
        </section>"""

    deadlock_section = ""
    deadlocks = data.get("deadlocks", [])
    if deadlocks:
        rows = ""
        for i, d in enumerate(deadlocks, 1):
            cycle = " → ".join(d.get("cycle", []) + d.get("cycle", [])[:1])
            for e in d.get("edges", []):
                rows += (f"<tr><td>#{i}</td><td><code>{cycle}</code></td><td>T{e.get('thread','?')}</td>"
                         f"<td>takes <code>{e.get('acquired','?')}</code> holding <code>{e.get('held','?')}</code></td>"
                         f"<td>{e.get('location','?')}</td></tr>")
        deadlock_section = f"""
        <section class="card">
          <h2>Potential Deadlocks (lock-order cycles)</h2>
          <table class="race-table">
            <thead><tr><th>#</th><th>Cycle</th><th>Thread</th><th>Acquisition</th><th>Location</th></tr></thead>
            <tbody>{rows}</tbody>
          </table>
        </section>"""

    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Thread Sentinel Race Report</title>
  {_plotly_script()}
  <style>
    *, *::before, *::after {{ box-sizing: border-box; margin: 0; padding: 0; }}
    body {{
      font-family: 'Segoe UI', system-ui, sans-serif;
      background: #0f172a; color: #e2e8f0;
      min-height: 100vh; padding: 2rem;
    }}
    h1 {{ font-size: 1.8rem; font-weight: 700; color: #38bdf8; margin-bottom: .25rem; }}
    h2 {{ font-size: 1.1rem; font-weight: 600; color: #94a3b8; margin-bottom: 1rem; }}
    .subtitle {{ color: #64748b; margin-bottom: 2rem; font-size: .9rem; }}
    .grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(160px,1fr)); gap:1rem; margin-bottom:2rem; }}
    .stat-card {{
      background: #1e293b; border-radius: 12px; padding: 1.2rem;
      border: 1px solid #334155; text-align: center;
    }}
    .stat-card .val {{ font-size: 2rem; font-weight: 800; }}
    .stat-card .lbl {{ font-size: .78rem; color: #94a3b8; margin-top:.25rem; }}
    .val.red {{ color: #ef4444; }}
    .val.yellow {{ color: #f59e0b; }}
    .val.green {{ color: #22c55e; }}
    .val.blue {{ color: #38bdf8; }}
    .card {{ background: #1e293b; border-radius:12px; padding:1.5rem; margin-bottom:1.5rem; border:1px solid #334155; }}
    .race-table {{ width:100%; border-collapse:collapse; font-size:.85rem; }}
    .race-table th {{ background:#0f172a; color:#94a3b8; padding:.6rem .8rem; text-align:left; font-weight:600; }}
    .race-table td {{ padding:.55rem .8rem; border-top:1px solid #334155; vertical-align:top; }}
    .race-table tr:hover td {{ background: #243044; }}
    .badge {{ display:inline-block; padding:.2rem .55rem; border-radius:6px; font-size:.75rem; font-weight:700; }}
    .bg-red    {{ background:#7f1d1d; color:#fca5a5; }}
    .bg-yellow {{ background:#78350f; color:#fcd34d; }}
    .bg-blue   {{ background:#1e3a5f; color:#93c5fd; }}
    .bg-green  {{ background:#14532d; color:#86efac; }}
    .no-races {{ color:#22c55e; font-weight:600; padding:1rem; }}
    .reason {{ color:#94a3b8; font-size:.8rem; }}
    code {{ font-family:monospace; background:#0f172a; padding:.1rem .3rem; border-radius:4px; }}
    #chart {{ width:100%; height:320px; }}
    em {{ color:#94a3b8; font-style:normal; }}
  </style>
</head>
<body>
  <h1>🔍 Thread Sentinel — Race Report</h1>
  <p class="subtitle">
    Mode: <strong>{mode}</strong> &nbsp;|&nbsp;
    Detector: <strong>{detector}</strong> &nbsp;|&nbsp;
    Runtime: <strong>{runtime:.1f} ms</strong>
  </p>

  <div class="grid">
    <div class="stat-card">
      <div class="val {'red' if confirmed else 'green'}">{confirmed}</div>
      <div class="lbl">Confirmed Races</div>
    </div>
    <div class="stat-card">
      <div class="val {'yellow' if potential else 'green'}">{potential}</div>
      <div class="lbl">Potential Races</div>
    </div>
    <div class="stat-card">
      <div class="val blue">{summary.get('total_unique_races',0)}</div>
      <div class="lbl">Unique Race Sites</div>
    </div>
    <div class="stat-card">
      <div class="val blue">{runtime:.0f}</div>
      <div class="lbl">Runtime (ms)</div>
    </div>
  </div>

  <div class="card">
    <h2>Race Verdict Distribution</h2>
    <div id="chart"></div>
  </div>

  <div class="card">
    <h2>Detected Races</h2>
    {races_html}
  </div>

  {prot_section}

  {deadlock_section}

  <script>
  (function() {{
    var d = {pie_json};
    Plotly.newPlot('chart', [{{
      type: 'pie',
      labels: d.labels,
      values: d.values,
      marker: {{ colors: d.colors }},
      hole: 0.45,
      textinfo: 'label+value',
      hovertemplate: '%{{label}}: %{{value}}<extra></extra>',
    }}], {{
      paper_bgcolor: 'transparent',
      plot_bgcolor:  'transparent',
      font: {{ color: '#e2e8f0', family: 'Segoe UI, system-ui, sans-serif' }},
      margin: {{ t: 20, b: 20, l: 20, r: 20 }},
      showlegend: true,
      legend: {{ orientation: 'h' }},
    }}, {{ responsive: true }});
  }})();
  </script>
</body>
</html>
"""

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(html)
