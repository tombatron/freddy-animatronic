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
    <div class="card" id="c_eyes"></div><div class="card wide" id="c_pos"></div><div class="card" id="c_cal"></div>
    <div class="card" id="c_led"></div><div class="card" id="c_sys"></div>
  </section>
  <div id="msg"></div>

  <h2>Find center</h2>
  <div class="row">
    <form action="/cmd" method="post" class="jog">
      <button name="a" value="jogr" onclick="this.form.v.value=250">&laquo; 250</button>
      <button name="a" value="jogr" onclick="this.form.v.value=100">100</button>
      <button name="a" value="jogr" onclick="this.form.v.value=50">50</button>
      <button name="a" value="jogr" onclick="this.form.v.value=20">20 &lsaquo;</button>
      <span class="gap"></span>
      <button name="a" value="jogf" onclick="this.form.v.value=20">&rsaquo; 20</button>
      <button name="a" value="jogf" onclick="this.form.v.value=50">50</button>
      <button name="a" value="jogf" onclick="this.form.v.value=100">100</button>
      <button name="a" value="jogf" onclick="this.form.v.value=250">250 &raquo;</button>
      <input type="hidden" name="v" value="50">
    </form>
  </div>
  <div class="row">
    <form action="/cmd" method="post"><button name="a" value="center" class="primary">Set center here</button></form>
    <form action="/cmd" method="post"><input type="hidden" name="a" value="mark">
      <button name="v" value="1">Mark full right</button><button name="v" value="2">Mark full left</button></form>
  </div>
  <p class="hint">Jog pulses are in ms at full speed (&rsaquo; forward, &lsaquo; reverse). Jog until the eyes look dead
    ahead and press Set center here. Then jog to the far right and mark it, and the same for the left.</p>

  <h2>Go to</h2>
  <div class="row">
    <form action="/cmd" method="post"><input type="hidden" name="a" value="goto">
      <button name="v" value="0">Center</button><button name="v" value="1">Right</button><button name="v" value="2">Left</button></form>
    <form action="/cmd" method="post"><input type="hidden" name="a" value="goto"><input type="hidden" name="v" value="3">
      <input type="number" name="t" min="0" max="99" value="25">% of a turn<button>Go</button></form>
    <form action="/cmd" method="post"><label>Dart</label>
      <input type="number" name="v" min="40" max="800" step="10" value="180"> ms<button name="a" value="dart">Dart</button></form>
  </div>
  <p class="hint">Go-to moves run forward only, so the eyes may sweep past other positions on the way.</p>

  <h2>Turn length</h2>
  <div class="row">
    <form action="/cmd" method="post"><input type="hidden" name="a" value="spin"><label>Spin</label>
      <input type="number" name="v" min="1" max="50" value="10"> turns<button>Spin</button></form>
    <form action="/cmd" method="post"><button name="a" value="learn" class="primary">Back at center: learn turn</button></form>
  </div>
  <div class="row">
    <form action="/cmd" method="post"><input type="hidden" name="a" value="turn"><label>Turn</label>
      <span id="f_turn"><input type="number" name="v" min="1000" max="20000" value="4224"></span> ms<button>Set</button></form>
    <form action="/cmd" method="post"><input type="hidden" name="a" value="lead"><label>Aim lead</label>
      <span id="f_lead"><input type="number" name="v" min="0" max="500" step="5" value="0"></span> ms<button>Set</button></form>
  </div>
  <p class="hint">From center, spin 10 turns. The eyes should end centered; jog them back to dead center if not, then
    press Back at center. It divides the whole trip by the number of turns, so a few ms of jog error barely counts.
    If go-to moves keep landing a little past the target, raise the aim lead.</p>

  <h2>Free run</h2>
  <div class="row">
    <form action="/cmd" method="post"><button name="a" value="fwd">Run &rarr;</button><button name="a" value="rev">Run &larr;</button></form>
    <form action="/cmd" method="post">
      <input type="hidden" name="a" value="speed"><label>Speed</label>
      <span id="f_speed"><input type="range" name="v" min="5" max="100" step="5" value="100"
             oninput="this.nextElementSibling.value=this.value+'%'" onchange="this.form.requestSubmit()"><output>100%</output></span>
    </form>
  </div>
  <p class="hint">Below 100% the position isn't tracked: set center again afterwards.</p>

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
