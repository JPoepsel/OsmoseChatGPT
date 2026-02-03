/* ============================================================
   GLOBALS
   ============================================================ */

let ws;


/* ============================================================
   TAB SWITCHING
   ============================================================ */

function show(id)
{
  document.querySelectorAll("section")
    .forEach(s => s.hidden = true);

  const el = document.getElementById(id);
  if(el) el.hidden = false;

  if(id === "hist" && !chart)
    initHistory();
}


/* ============================================================
   WEBSOCKET
   ============================================================ */

function connectWS()
{
  ws = new WebSocket(`ws://${location.host}/ws`);

  ws.onopen = () => {
    console.log("[WS] connected");
  };

  ws.onclose = () => {
    console.log("[WS] reconnect...");
    setTimeout(connectWS, 1500);
  };

  ws.onmessage = (ev) => {

    const d = JSON.parse(ev.data);

    if(d.state !== undefined) {
      state.innerText = d.error ? (d.state + " : " + d.error) : d.state;
      updateButtons(d.state);   // <<< ADD
    }


    if(d.tds !== undefined)    tds.innerText    = Number(d.tds).toFixed(1);
    if(d.liters !== undefined) liters.innerText = Number(d.liters).toFixed(2);
    if(d.flow !== undefined)   flow.innerText   = Number(d.flow).toFixed(2);
    if(d.left !== undefined)   left.innerText   = Number(d.left).toFixed(2);
  };
}


/* ============================================================
   START / STOP COMMANDS (robust!)
   ============================================================ */

function startCmd()
{
  if(ws && ws.readyState === WebSocket.OPEN)
    ws.send("start");
}

function stopCmd()
{
  if(ws && ws.readyState === WebSocket.OPEN)
    ws.send("stop");
}

/* ============================================================
   BUTTON STATE CONTROL (ADD)
   ============================================================ */

function updateButtons(stateName)
{
  const start = document.getElementById("btnStart");
  const stop  = document.getElementById("btnStop");

  if(!start || !stop) return;

  if(stateName === "IDLE" || stateName === "ERROR"){
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
    "flushTimeSec",
    "maxRuntimeSec",
    "maxProductionLiters",
    "autoStart",
    "mqttHost",
    "mqttPort",
    "mDNSName",
    "APPassWord"
  ];

  const data = {};

  ids.forEach(id => {

    const el = document.getElementById(id);
    if(!el) return;

    if(el.type === "checkbox")
      data[id] = el.checked;
    else
      data[id] = el.value;
  });

  fetch("/api/settings", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(data)
  })
  .then(() => alert("Settings gespeichert"))
  .catch(() => alert("Fehler beim Speichern"));
}


/* ============================================================
   SETTINGS LOAD (auto fill UI)
   ============================================================ */

function loadSettings()
{
  fetch("/api/settings")
    .then(r => {

      if(!r.ok) throw new Error("HTTP error");

      return r.text();   // erstmal als Text holen
    })
    .then(txt => {

      if(!txt) return {};   // leere Antwort = leeres Objekt

      return JSON.parse(txt);
    })
    .then(cfg => {

      if(!cfg) return;

      Object.keys(cfg).forEach(k => {

        const el = document.getElementById(k);
        if(!el) return;

        if(el.type === "checkbox")
          el.checked = cfg[k];
        else
          el.value = cfg[k];
      });
    })
    .catch(err => {
      console.log("[CFG] no config yet (normal on first boot)");
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


/* ============================================================
   HISTORY PAGE
   ============================================================ */

let chart;
let histTimer;

function initHistory()
{
  const ctx = document.getElementById("chart");

  chart = new Chart(ctx, {
    type: "line",
    data: {
      labels: [],
      datasets: [
        { label: "TDS",  data: [], yAxisID: "y1" },
        { label: "Flow", data: [], yAxisID: "y2" },
        { label: "Liter",data: [], yAxisID: "y2" }
      ]
    },
    options: {
      animation:false,
      responsive:true,
      scales:{
        y1:{ type:"linear", position:"left" },
        y2:{ type:"linear", position:"right" }
      }
    }
  });

  loadHistory();

  histTimer = setInterval(loadHistory, 30000);

  document.getElementById("rangeSel").onchange = loadHistory;
}


/* ================== SERIES ================== */

function loadHistory()
{
  const sec = document.getElementById("rangeSel").value;

  fetch("/api/history/series?range="+sec)
    .then(r => r.json())
    .then(d => {

      const len = d.tds.length;

      chart.data.labels = Array(len).fill("");

      chart.data.datasets[0].data = d.tds;
      chart.data.datasets[1].data = d.flow;
      chart.data.datasets[2].data = d.prod;

      chart.update();

      const last = len-1;

      document.getElementById("lastValues").innerText =
        `TDS: ${d.tds[last]?.toFixed(1)}  |  Flow: ${d.flow[last]?.toFixed(2)}  |  Liter: ${d.prod[last]?.toFixed(2)}`;

      loadHistoryTable();
    });
}


/* ================== TABLE ================== */

function loadHistoryTable()
{
  fetch("/api/history/table")
    .then(r => r.json())
    .then(rows => {

      const body = document.querySelector("#histTable tbody");
      body.innerHTML = "";

      rows.forEach(r => {

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
