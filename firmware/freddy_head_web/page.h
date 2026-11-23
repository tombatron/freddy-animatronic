// The head test page. Live values arrive as Turbo Stream messages over the
// WebSocket at /ws; the controls are plain forms that POST to /cmd.
// Turbo comes from a CDN; without internet (Freddy's access point) the small
// script at the bottom does the same two jobs: apply streams and submit forms.
#pragma once

const char PAGE_HTML[] = R"rawliteral(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Freddy head</title>
<script async src="https://cdn.jsdelivr.net/npm/@hotwired/turbo@8.0.23/dist/turbo.es2017-umd.js"></script>
<style>
  :root { --bg:#f6f4ef; --card:#fff; --ink:#1d1d1f; --mute:#6b6b70; --line:#e2ded5;
          --ok:#1f8a4c; --bad:#c62828; --accent:#3a5bd9; }
  @media (prefers-color-scheme: dark) {
    :root { --bg:#141416; --card:#1f1f23; --ink:#ececf0; --mute:#9a9aa3; --line:#33333a;
            --ok:#4cc27e; --bad:#ff6b6b; --accent:#7b96ff; }
  }
  * { box-sizing: border-box; }
  body { margin:0; font:15px/1.4 system-ui, -apple-system, sans-serif; background:var(--bg); color:var(--ink); }
  header { position:sticky; top:0; z-index:2; display:flex; align-items:center; gap:12px; justify-content:space-between;
           padding:10px 16px; border-bottom:1px solid var(--line); background:var(--bg); }
  h1 { font-size:18px; margin:0; }
  h2 { font-size:13px; text-transform:uppercase; letter-spacing:.06em; color:var(--mute); margin:22px 0 8px; }
  main { max-width:920px; margin:0 auto; padding:0 16px 40px; }
  .pill { font-size:12px; padding:3px 10px; border-radius:99px; border:1px solid var(--line); color:var(--mute); }
  .pill.on { color:var(--ok); border-color:var(--ok); }
  .pill.off { color:var(--bad); border-color:var(--bad); }
  .cards { display:grid; grid-template-columns:repeat(auto-fill, minmax(150px, 1fr)); gap:10px; margin-top:16px; }
  .card { background:var(--card); border:1px solid var(--line); border-radius:10px; padding:10px 12px; min-height:78px; }
  .k { font-size:12px; color:var(--mute); }
  .v { font-size:22px; font-weight:650; font-variant-numeric:tabular-nums; }
  .s { font-size:12px; color:var(--mute); font-variant-numeric:tabular-nums; }
  .bar { position:relative; height:10px; margin-top:6px; border-radius:5px; background:var(--line); }
  .bar b { position:absolute; top:0; bottom:0; width:1px; background:var(--mute); }
  .bar b.c { background:var(--ok); width:2px; }
  .bar b.r, .bar b.l { background:var(--accent); width:2px; }
  .card.wide { grid-column:span 2; }
  .jog button { min-width:56px; }
  .gap { width:14px; }
  button.primary { background:var(--accent); color:#fff; border-color:var(--accent); }
  .wiz { margin-top:16px; background:var(--card); border:2px solid var(--accent); border-radius:12px; padding:12px 16px; }
  .wiz h3 { margin:0 0 4px; font-size:16px; }
  .wiz p { margin:0 0 10px; color:var(--mute); }
  .wiz form { margin-top:8px; }
  .wbtns button { margin-right:4px; }
  details.manual { margin-top:22px; }
  details.manual summary { cursor:pointer; font-size:13px; text-transform:uppercase; letter-spacing:.06em; color:var(--mute); }
  .enc { display:flex; gap:16px; align-items:flex-start; }
  .enc p { margin:4px 0 8px; color:var(--mute); font-size:13px; }
  .enc table { border:none; background:none; }
  .enc th, .enc td { border:none; padding:2px 10px 2px 0; }
  .dial { width:110px; height:110px; flex:none; }
  .dial circle { fill:none; stroke:var(--line); stroke-width:4; }
  .dial line { stroke:var(--accent); stroke-width:5; stroke-linecap:round; }
  .verdict { font-size:20px; font-weight:700; }
  .verdict.ok { color:var(--ok); } .verdict.warn { color:#d08a00; } .verdict.bad { color:var(--bad); }
  input[type=range].wide { width:260px; }
  .lr { font-weight:700; color:var(--mute); }
  b.ok { color:var(--ok); } b.warn { color:#d08a00; } b.bad { color:var(--bad); }
  .v.ok { color:var(--ok); } .v.warn { color:#d08a00; } .v.bad { color:var(--bad); }
  .split { display:grid; grid-template-columns:minmax(240px, 1fr) 2fr; gap:12px; margin-top:16px; align-items:start; }
  .split .wiz { margin-top:0; }
  .movebox { margin-top:12px; }
  .gotos { display:flex; flex-wrap:wrap; gap:8px; }
  .movebox form { margin-top:8px; }
  .movebox label { min-width:70px; display:inline-block; }
  .movebox .jog button { min-width:0; padding:6px 9px; }
  .movebox .gap { width:8px; }
  .caltab { border:none; background:none; width:auto; }
  .caltab th, .caltab td { border:none; padding:3px 12px 3px 0; font-size:14px; }
  .caltab form { margin:0; }
  .mute { color:var(--mute); }
  .headsvg .hint { stroke:var(--mute); stroke-width:1.5; stroke-dasharray:1 3; }
  @media (max-width: 640px) { .split { grid-template-columns:1fr; } }
  .headsvg { width:100%; max-width:320px; display:block; margin:4px auto 0; }
  .headsvg .ear { fill:#8a5a2b; } .headsvg .skull { fill:#a06a35; } .headsvg .snout { fill:#d8b48a; }
  .headsvg .nose { fill:#2a1a10; } .headsvg .eye { fill:#fff; stroke:#2a1a10; stroke-width:1.5; }
  .headsvg .range { fill:var(--accent); opacity:.10; } .headsvg .usable { fill:var(--accent); opacity:.18; }
  .headsvg .edge { stroke:var(--mute); stroke-width:1; }
  .headsvg .center { stroke:var(--ok); stroke-width:1.5; stroke-dasharray:4 3; }
  .headsvg .target { stroke:#d08a00; stroke-width:2; stroke-dasharray:6 4; }
  .headsvg .gaze { stroke:var(--accent); stroke-width:3; stroke-linecap:round; }
  .headsvg .lbl { fill:var(--mute); font-size:12px; font-weight:700; }
  .headsvg .read { fill:var(--ink); font-size:13px; font-weight:600; text-anchor:middle; }
  .bar i { position:absolute; top:-3px; width:16px; height:16px; margin-left:-8px; border-radius:50%; background:var(--accent); }
  .row { display:flex; flex-wrap:wrap; gap:8px; align-items:center; margin-top:10px; }
  form { margin:0; display:flex; flex-wrap:wrap; gap:8px; align-items:center; }
  button { font:inherit; font-weight:600; padding:8px 14px; border-radius:8px; border:1px solid var(--line);
           background:var(--card); color:var(--ink); cursor:pointer; }
  button:hover { border-color:var(--accent); }
  button.stop { background:var(--bad); color:#fff; border-color:var(--bad); padding:8px 24px; }
  input[type=number] { width:78px; font:inherit; padding:6px; border-radius:6px; border:1px solid var(--line);
                       background:var(--card); color:var(--ink); }
  input[type=range] { width:160px; }
  output { min-width:42px; font-variant-numeric:tabular-nums; }
  .hint { font-size:12px; color:var(--mute); margin:4px 0 0; }
  #msg { min-height:18px; font-size:13px; color:var(--bad); }
  table { width:100%; border-collapse:collapse; font-variant-numeric:tabular-nums; background:var(--card);
          border:1px solid var(--line); border-radius:10px; overflow:hidden; }
  th, td { text-align:left; padding:6px 10px; border-bottom:1px solid var(--line); font-size:13px; }
  th { color:var(--mute); font-weight:600; }
  .right { display:flex; gap:6px; align-items:center; }
  .loghead { display:flex; align-items:center; justify-content:space-between; }
  button.small { font-size:12px; padding:4px 10px; margin-top:14px; }
  #log span { color:var(--mute); }
  #log { list-style:none; margin:0; font:12px/1.5 ui-monospace, monospace; max-height:260px; overflow:auto;
         background:var(--card); border:1px solid var(--line); border-radius:10px; padding:8px 10px; }
</style>
</head>
<body>
<header>
  <h1>Freddy &middot; head</h1>
  <form action="/cmd" method="post"><input type="hidden" name="a" value="stop"><button class="stop">STOP</button></form>
  <span class="right"><span id="fw" class="pill">&hellip;</span> <span id="conn" class="pill">connecting&hellip;</span></span>
</header>
<main>
  <section class="cards">
    <div class="card" id="c_eyes"></div><div class="card wide" id="c_pos"></div><div class="card" id="c_sens"></div>
    <div class="card" id="c_led"></div><div class="card" id="c_sys"></div>
  </section>
  <div id="msg"></div>

  <section class="split">
    <div class="card headcard"><div class="k">Where the software thinks the eyes point (seen from above, Freddy faces up)</div><div id="head"></div></div>
    <div>
      <section id="cal" class="wiz"></section>
      <div class="card movebox">
        <div class="k">Move the eyes</div>
        <form action="/cmd" method="post" class="jog"><input type="hidden" name="a" value="nudge"><label>Eyes</label>
          <button name="v" value="-100">&laquo; 10&deg;</button><button name="v" value="-20">2&deg;</button><button name="v" value="-5">0.5&deg; &lsaquo;</button><span class="gap"></span>
          <button name="v" value="5">&rsaquo; 0.5&deg;</button><button name="v" value="20">2&deg;</button><button name="v" value="100">10&deg; &raquo;</button></form>

        <form action="/cmd" method="post"><button name="a" value="clearcal" class="small">Clear calibration</button></form>
        <p class="hint">Steps in degrees: &lsaquo; left / right &rsaquo; once both ends are set (before that, just the sensor's
          &minus; and + directions).</p>
      </div>
    </div>
  </section>

  <h2>Go to</h2>
  <div class="row">
    <div id="gotos" class="gotos"></div>
  </div>
  <div class="row">
    <form action="/cmd" method="post"><input type="hidden" name="a" value="goto"><label>Look</label>
      <span class="lr">L</span><input type="range" name="v" min="-100" max="100" step="5" value="0" class="wide"
        oninput="this.nextElementSibling.nextElementSibling.value=this.value+'%'" onchange="this.form.requestSubmit()"><span class="lr">R</span>
      <output>0%</output></form>
  </div>
  <div class="row">
    <form action="/cmd" method="post"><button name="a" value="dart">Glance left &amp; right</button><button name="a" value="tour">Tour</button></form>
  </div>
  <p class="hint">Center is your calibrated center; Left and Right are halfway to the ends; Full left and Full right are the ends
    (in symmetric mode, the shorter side's travel on both sides). The firmware works out how to drive the motor.
    The control loop drives the cam disc whichever way gets there and stops within the tolerance.</p>

  <h2>Lights</h2>
  <div class="row">
    <form action="/cmd" method="post"><input type="hidden" name="a" value="ledmode">
      <button name="v" value="0">Off</button><button name="v" value="1">On</button><button name="v" value="2">Breathe</button>
      <button name="v" value="3">Flicker</button><button name="v" value="4">Blink</button></form>
    <form action="/cmd" method="post"><button name="a" value="ledsteps">LED steps</button></form>
  </div>
  <div class="row">
    <form action="/cmd" method="post"><input type="hidden" name="a" value="bright"><label>Brightness</label>
      <span id="f_bright"><input type="range" name="v" min="0" max="100" value="60"
             oninput="this.nextElementSibling.value=this.value+'%'" onchange="this.form.requestSubmit()"><output>60%</output></span></form>
    <form action="/cmd" method="post"><input type="hidden" name="a" value="period"><label>Period</label>
      <span id="f_period"><input type="number" name="v" min="200" max="10000" step="100" value="3000"></span> ms<button>Set</button></form>
  </div>

  <details class="manual" open><summary>Tuning</summary>
    <div class="row">
      <form action="/cmd" method="post"><input type="hidden" name="a" value="sym"><label>Range</label>
        <button name="v" value="1">Symmetric</button><button name="v" value="0">Full range</button></form>
    </div>
    <p class="hint">Symmetric limits both sides to the shorter side's travel, so left and right are mirror images
      (the Eye position card shows which mode is on). Full range uses each side's whole travel. Far left / Far right (shown only in
      symmetric mode) go to the true extremes.</p>
    <div class="row">
      <form action="/cmd" method="post"><input type="hidden" name="a" value="tol"><label>Tolerance</label>
        <span id="f_tol"><input type="number" name="v" min="0.3" max="5" step="0.1" value="1.0"></span>&deg;<button>Set</button></form>
      
    </div>
    <p class="hint">Tolerance: how close a go-to has to land. The eyes move at full power and brake early by how far they
      coast, then finish with short taps; both are learned as they go.</p>
  </details>

  <details class="manual"><summary>Debug: sensor details</summary>
    <div class="card" id="enc"></div>
    <div class="row"><form action="/cmd" method="post"><button name="a" value="encreset">Reset stats</button></form></div>
    <p class="hint">Field swing: reset, then turn the cam disc a full circle (Run &rarr; below). A small swing means the magnet is
      centered over the chip. Noise: hold still.</p>
  </details>

  <details class="manual"><summary>Debug: free run</summary>
    <div class="row"><form action="/cmd" method="post"><input type="hidden" name="a" value="trace"><label>Trace moves</label>
      <button name="v" value="1">On</button><button name="v" value="0">Off</button></form></div>
    <p class="hint">Trace: each go-to and nudge logs its path when it ends: ms since start : degrees moved : motor power
      (+/&minus; = disc direction). Paste those lines to Claude to tune the control loop.</p>
    <div class="row">
      <form action="/cmd" method="post"><button name="a" value="fwd">Run &rarr;</button><button name="a" value="rev">Run &larr;</button></form>
      <form action="/cmd" method="post">
        <input type="hidden" name="a" value="speed"><label>Speed</label>
        <span id="f_speed"><input type="range" name="v" min="5" max="100" step="5" value="100"
               oninput="this.nextElementSibling.value=this.value+'%'" onchange="this.form.requestSubmit()"><output>100%</output></span>
      </form>
    </div>
    <p class="hint">Free running, for debugging. The cam disc direction (&rarr;/&larr;) isn't the eye direction: the eyes
      swing both ways as the disc turns.</p>
  </details>

  <h2>Results</h2>
  <table><thead><tr><th>What</th><th>Result</th><th>Notes</th></tr></thead><tbody id="results"></tbody></table>

  <div class="loghead"><h2>Log</h2>
    <form action="/cmd" method="post"><button name="a" value="clearlog" class="small">Clear log &amp; results</button></form></div>
  <ol id="log"></ol>
</main>
<script>
  // Every WebSocket message is a Turbo Stream. Turbo applies them if it loaded from the CDN;
  // otherwise this does (update/prepend only, which is all the sketch sends).
  function applyStreams(html) {
    if (window.Turbo) return Turbo.renderStreamMessage(html);
    const t = document.createElement("template");
    t.innerHTML = html;
    for (const s of t.content.querySelectorAll("turbo-stream")) {
      const el = document.getElementById(s.getAttribute("target"));
      const c = s.querySelector("template");
      if (!el || !c) continue;
      if (s.getAttribute("action") === "prepend") el.prepend(c.content.cloneNode(true));
      else el.replaceChildren(c.content.cloneNode(true));
    }
  }
  // Without Turbo, forms would navigate away: post them in the background instead.
  document.addEventListener("submit", (e) => {
    if (window.Turbo) return;
    e.preventDefault();
    const fd = new FormData(e.target, e.submitter);
    fetch(e.target.action, { method: "POST", body: new URLSearchParams(fd) }).then((r) => r.text()).then(applyStreams);
  });
  // Keep the log and results from growing forever.
  function trim() {
    for (const [id, n] of [["log", 200], ["results", 100]]) {
      const el = document.getElementById(id);
      while (el.children.length > n) el.lastElementChild.remove();
    }
  }
  const conn = document.getElementById("conn");
  function connect() {
    const ws = new WebSocket(`ws://${location.host}/ws`);
    ws.onopen = () => { conn.textContent = "connected"; conn.className = "pill on"; };
    ws.onmessage = (e) => { applyStreams(e.data); setTimeout(trim, 100); };
    ws.onclose = () => { conn.textContent = "disconnected"; conn.className = "pill off"; setTimeout(connect, 2000); };
  }
  connect();
</script>
</body>
</html>
)rawliteral";
