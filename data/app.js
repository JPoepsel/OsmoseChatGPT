/* ============================================================
   GLOBALS
   ============================================================ */

let ws;
let chart;
let histTimer;

let lastMdnsName = "";
let lastState    = "";


/* ============================================================
   SMALL TOAST
   ============================================================ */

function toast(msg, ms=2200)
{
  let t = document.getElementById("toast");

  if(!t){
    t = document.createElement("div");
    t.id = "toast";
    t.style.cssText = `
      position:fixed;
      bottom:20px;
      left:50%;
      transform:translateX(-50%);
      background:#333;
      color:#fff;
      padding:10px 18px;
      border-radius:8px;
      font-size:14px;
      opacity:0;
      transition:opacity .2s;
      z-index:9999;
    `;
    document.body.appendChild(t);
  }

  t.innerText = msg;
  t.style.opacity = 1;

  setTimeout(()=> t.style.opacity = 0, ms);
}


/* ============================================================
   HELPERS
   ============================================================ */

function historyVisible()
{
  const el = document.getElementById("hist");
  return el && !el.hidden;
}


/* ============================================================
   TAB SWITCHING
   ============================================================ */

function show(id)
{
  document.querySelectorAll("section")
    .forEach(s => s.hidden = true);

  const el = document.getElementById(id);
  if(el) el.hidden = false;

  if(id === "hist")
  {
    if(!chart) initHistory();
    loadHistory(true);
  }
}


/* ============================================================
   WEBSOCKET
   ============================================================ */

function connectWS()
{
  ws = new WebSocket(`ws://${location.host}/ws`);

  ws.onopen = () => console.log("[WS] connected");

  ws.onclose = () => {
    console.log("[WS] reconnect...");
    setTimeout(connectWS, 1500);
  };

  ws.onmessage = (ev) =>
  {
    const d = JSON.parse(ev.data);

    if(d.state !== undefined)
    {
      state.innerText = d.error ? (d.state + " : " + d.error) : d.state;

      updateButtons(d.state);

      if(d.state !== lastState && historyVisible())
        loadHistory(true);

      lastState = d.state;
    }

    if(d.tds !== undefined)    tds.innerText    = Number(d.tds).toFixed(1);
    if(d.liters !== undefined) liters.innerText = Number(d.liters).toFixed(2);
    if(d.flow !== undefined)   flow.innerText   = Number(d.flow).toFixed(2);
    if(d.left !== undefined)   left.innerText   = Number(d.left).toFixed(2);
  };
}


/* ============================================================
   START / STOP
   ============================================================ */

function startCmd()
{
  if(ws?.readyState === WebSocket.OPEN)
    ws.send("start");
}

function stopCmd()
{
  if(ws?.readyState === WebSocket.OPEN)
    ws.send("stop");
}


/* ============================================================
   BUTTON STATE
   ============================================================ */

function updateButtons(stateName)
{
  const start = document.getElementById("btnStart");
  const stop  = document.getElementById("btnStop");

  if(!start || !stop) return;

  /* ⭐ INFO wie IDLE/ERROR behandeln */
  if(stateName === "IDLE" || stateName === "ERROR" || stateName === "INFO"){
    start.disabled = false;
    stop.disabled  = true;
  }
  else{
    start.disabled = true;
    stop.disabled  = false;
  }
}


/* ============================================================
   SETTINGS SAVE
   ============================================================ */

function saveSettings()
{
  const ids = [
    "pulsesPerLiterIn",
    "pulsesPerLiterOut",
    "tdsLimit",
    "maxFlushTimeSec",
    "maxRuntimeSec",
    "maxProductionLiters",
    "autoStart",
    "mqttHost",
    "mqttPort",
    "mDNSName",
    "APPassWord"
  ];

  const data = {};

  ids.forEach(id =>
  {
    const el = document.getElementById(id);
    if(!el) return;

    if(el.type === "checkbox")
      data[id] = el.checked;
    else if(el.type === "number")
      data[id] = Number(el.value);
    else
      data[id] = el.value;
  });

  fetch("/api/settings", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(data)
  })
  .then(() =>
  {
    if(data.mDNSName !== lastMdnsName)
      toast("Settings gespeichert – Neustart erforderlich");
    else
      toast("Settings gespeichert");

    lastMdnsName = data.mDNSName;
  })
  .catch(() => toast("Fehler beim Speichern"));
}


/* ============================================================
   SETTINGS LOAD
   ============================================================ */

function loadSettings()
{
  fetch("/api/settings")
    .then(r => r.json())
    .then(cfg =>
    {
      Object.keys(cfg).forEach(k =>
      {
        const el = document.getElementById(k);
        if(!el) return;

        if(el.type === "checkbox")
          el.checked = cfg[k];
        else
          el.value = cfg[k];
      });

      lastMdnsName = cfg.mDNSName || "";
    });
}


/* ============================================================
   HISTORY PAGE
   ============================================================ */

function initHistory()
{
  const ctx = document.getElementById("chart");

  chart = new Chart(ctx, {
    type: "line",
    data: {
      labels: [],
      datasets: [
        { label: "TDS (ppm)",  data: [], yAxisID: "yTds",  borderColor:"#00bcd4" },
        { label: "Flow (L/min)", data: [], yAxisID: "yFlow", borderColor:"#4caf50" },
        { label: "Liter (L)", data: [], yAxisID: "yProd", borderColor:"#ff9800" }
      ]
    },
    options: {
      animation:false,
      responsive:true,
      scales:{
        yTds:{  type:"linear", position:"left",  title:{display:true,text:"ppm"},  ticks:{color:"#00bcd4"} },
        yFlow:{ type:"linear", position:"right", title:{display:true,text:"L/min"},ticks:{color:"#4caf50"} },
        yProd:{ type:"linear", position:"right", offset:true, title:{display:true,text:"Liter"}, ticks:{color:"#ff9800"} }
      }
    }
  });

  histTimer = setInterval(loadHistory, 30000);

  document.getElementById("rangeSel").onchange = () => loadHistory(true);
}



/* ================== SERIES ================== */

function loadHistory(force=false)
{
  if(!historyVisible() && !force) return;

  const sec = document.getElementById("rangeSel").value;

  fetch("/api/history/series?range="+sec)
    .then(r => r.json())
    .then(d =>
    {
      const len = d.tds.length;

      chart.data.labels = Array(len).fill("");
      chart.data.datasets[0].data = d.tds;
      chart.data.datasets[1].data = d.flow;
      chart.data.datasets[2].data = d.prod;

      chart.update();

      loadHistoryTable();
    });
}


/* ================== TABLE ================== */

function loadHistoryTable()
{
  fetch("/api/history/table")
    .then(r => r.json())
    .then(rows =>
    {
      const body = document.querySelector("#histTable tbody");
      body.innerHTML = "";

      rows.forEach(r =>
      {
        const tr = document.createElement("tr");
        const fmt = t => t ? new Date(t*1000).toLocaleString() : "";

        tr.innerHTML =
          `<td>${fmt(r.start)}</td>
           <td>${fmt(r.end)}</td>
           <td>${Number(r.liters).toFixed(2)}</td>
           <td>${r.reason||""}</td>`;

        body.appendChild(tr);
      });
    });
}


/* ============================================================
   INIT
   ============================================================ */

window.onload = () =>
{
  show("home");
  connectWS();
  loadSettings();
};
