// The dashboard page. Live values arrive as Turbo Stream messages over the
// WebSocket at /ws; the buttons are plain forms that POST to /cmd and get a
// Turbo Stream back.
#pragma once

const char PAGE_HTML[] = R"rawliteral(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Freddy</title>
<script src="/turbo.js"></script>
<style>
  :root { --bg:#f6f4ef; --card:#fff; --ink:#1d1d1f; --mute:#6b6b70; --line:#e2ded5;
          --ok:#1f8a4c; --bad:#c62828; --accent:#3a5bd9; }
  @media (prefers-color-scheme: dark) {
    :root { --bg:#141416; --card:#1f1f23; --ink:#ececf0; --mute:#9a9aa3; --line:#33333a;
            --ok:#4cc27e; --bad:#ff6b6b; --accent:#7b96ff; }
  }
  * { box-sizing: border-box; }
  body { margin:0; font:15px/1.4 system-ui, -apple-system, sans-serif; background:var(--bg); color:var(--ink); }
  header { display:flex; align-items:center; justify-content:space-between; padding:14px 16px; border-bottom:1px solid var(--line); }
  h1 { font-size:18px; margin:0; }
  h2 { font-size:14px; text-transform:uppercase; letter-spacing:.06em; color:var(--mute); margin:22px 0 8px; }
  main { max-width:900px; margin:0 auto; padding:0 16px 40px; }
  .pill { font-size:12px; padding:3px 10px; border-radius:99px; border:1px solid var(--line); color:var(--mute); }
  .pill.on { color:var(--ok); border-color:var(--ok); }
  .pill.off { color:var(--bad); border-color:var(--bad); }
  .cards { display:grid; grid-template-columns:repeat(auto-fill, minmax(140px, 1fr)); gap:10px; margin-top:16px; }
  .card { background:var(--card); border:1px solid var(--line); border-radius:10px; padding:10px 12px; }
  .card.warn { border-color:var(--bad); }
  .k { font-size:12px; color:var(--mute); }
  .v { font-size:24px; font-weight:650; font-variant-numeric:tabular-nums; }
  .card.warn .s { color:var(--bad); font-weight:600; }
  .s { font-size:12px; color:var(--mute); font-variant-numeric:tabular-nums; }
  .controls { display:flex; flex-wrap:wrap; gap:8px; align-items:center; margin-top:16px; }
  .controls form { margin:0; }
  button { font:inherit; font-weight:600; padding:9px 16px; border-radius:8px; border:1px solid var(--line);
           background:var(--card); color:var(--ink); cursor:pointer; }
  button:hover { border-color:var(--accent); }
  button.stop { background:var(--bad); color:#fff; border-color:var(--bad); padding:9px 26px; }
  .duty { display:flex; gap:8px; align-items:center; }
  .duty input { width:140px; }
  #msg { min-height:20px; margin-top:8px; font-size:13px; color:var(--mute); }
  table { width:100%; border-collapse:collapse; font-variant-numeric:tabular-nums; background:var(--card);
          border:1px solid var(--line); border-radius:10px; overflow:hidden; }
  th, td { text-align:right; padding:6px 10px; border-bottom:1px solid var(--line); font-size:13px; }
  th:first-child, td:first-child { text-align:left; }
  td.bad { color:var(--bad); font-weight:650; }
  td.ok { color:var(--ok); }
  #log { list-style:none; padding:0; margin:0; font:12px/1.5 ui-monospace, monospace; max-height:240px; overflow:auto;
         background:var(--card); border:1px solid var(--line); border-radius:10px; padding:8px 10px; }
</style>
</head>
<body>
<header>
  <h1>Freddy &middot; power test</h1>
  <span id="conn" class="pill">connecting&hellip;</span>
</header>
<main>
  <section id="live" class="cards"><div class="card"><div class="k">Waiting for data&hellip;</div></div></section>

  <section class="controls">
    <form action="/cmd" method="post"><input type="hidden" name="a" value="stop"><button class="stop">STOP</button></form>
    <form action="/cmd" method="post"><input type="hidden" name="a" value="lift"><button>Lift</button></form>
    <form action="/cmd" method="post"><input type="hidden" name="a" value="lower"><button>Lower</button></form>
    <form action="/cmd" method="post"><input type="hidden" name="a" value="cycle"><button>Start cycling</button></form>
  </section>
  <section class="controls">
    <form action="/cmd" method="post" class="duty">
      <label for="jm">Jog (ignores the switch)</label>
      <input id="jm" type="number" name="v" min="50" max="1500" step="50" value="300" style="width:80px"> ms
      <button name="a" value="jogup">Jog up</button>
      <button name="a" value="jogdown">Jog down</button>
    </form>
    <form action="/cmd" method="post" class="duty">
      <input type="hidden" name="a" value="duty">
      <label for="v">Lift duty</label>
      <input id="v" type="range" name="v" min="20" max="100" step="10" value="40"
             oninput="this.nextElementSibling.value = this.value + '%'">
      <output>40%</output>
      <button>Set</button>
    </form>
    <form action="/cmd" method="post"><input type="hidden" name="a" value="elbow"><button>Toggle elbow</button></form>
    <form action="/cmd" method="post"><input type="hidden" name="a" value="clear"><button>Clear counters &amp; peaks</button></form>
  </section>
  <div id="msg"></div>

  <h2>Cycles</h2>
  <table>
    <thead><tr><th>#</th><th>Main rail min</th><th>5V min</th><th>Peak current</th><th>Result</th></tr></thead>
    <tbody id="cycles"></tbody>
  </table>

  <h2>Log</h2>
  <ol id="log"></ol>
</main>
<script>
  // Our own WebSocket (instead of <turbo-stream-source>) so it reconnects
  // after a reset or a WiFi drop. Every message is a Turbo Stream.
  const conn = document.getElementById("conn");
  function connect() {
    const ws = new WebSocket(`ws://${location.host}/ws`);
    ws.onopen = () => { conn.textContent = "connected"; conn.className = "pill on"; };
    ws.onmessage = (e) => Turbo.renderStreamMessage(e.data);
    ws.onclose = () => { conn.textContent = "disconnected"; conn.className = "pill off"; setTimeout(connect, 2000); };
  }
  connect();
</script>
</body>
</html>
)rawliteral";
