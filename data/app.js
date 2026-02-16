const SPIFF_VERSION = "WEB v3.3.1";

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

function clearHistory()
{
  if(!confirm("Bezüge wirklich löschen?"))
    return;

  fetch("/api/history/clearProd", { method: "POST" })
    .then(() => loadHistoryTable());   // Tabelle sofort neu laden
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
      const label = (d.mode ? (d.mode + " • ") : "") + d.state;
      state.innerText = d.error ? (label + " : " + d.error) : label;


      updateButtons(d.state, d.mode, d.autoBlocked===true);


      if(d.state !== lastState && historyVisible())
        loadHistory(true);

      lastState = d.state;
    }
    if(d.espVersion !== undefined) { 
      const el = document.getElementById("espVersion"); 
      if(el) el.innerText = d.espVersion;
    }

    if(d.tds !== undefined)    tds.innerText    = Number(d.tds).toFixed(1);
    if(d.liters !== undefined) liters.innerText = Number(d.liters).toFixed(2);
    if(d.flow !== undefined)   flow.innerText   = Number(d.flow).toFixed(2);
    if(d.flowIn !== undefined && d.flow !== undefined) {
      const fin = Number(d.flowIn);
      const fout = Number(d.flow);

      let eff = 0;
      if(fin > 0)
        eff = (fout / fin) * 100.0;

      const el = document.getElementById("flowInEff");
      if(el)
        el.innerText =
          `IN: ${fin.toFixed(2)} · Eff: ${eff.toFixed(0)} %`;
    }

    if(d.left !== undefined)   left.innerText   = Number(d.left).toFixed(2);
    if(d.timeLeft !== undefined) {
      const sec = Math.max(0, Math.floor(d.timeLeft));
      const m = Math.floor(sec/60);
      const s = sec%60;
      timeLeft.innerText = `${m}m ${s}s`;
    }
    if(d.status !== undefined) {
      const el = document.getElementById("statusLine");
      if(el) el.innerText = d.status;
    }

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
function updateButtons(state, mode, autoBlocked)
{
  const start = document.getElementById("btnStart");
  const stop  = document.getElementById("btnStop");

  if(!start || !stop) return;

  const isOff      = (mode === "OFF");
  const isIdle     = (state === "IDLE");
  const isError    = (state === "ERROR");
  const autoLocked = (mode === "AUTO" && autoBlocked);

  // START: erlaubt bei IDLE oder ERROR
  start.disabled = !((isIdle || isError) && !isOff && !autoLocked);

  // STOP: bei IDLE und ERROR deaktiviert (Error springt automatisch auf start enebeled)
  stop.disabled = (isIdle || isError);
}



/* ============================================================
   SETTINGS SAVE
   ============================================================ */
/* ============================================================
   SETTINGS HELPER (NEU)
   sammelt alle Werte einmal zentral
   ============================================================ */

function collectSettingsData()
{
  const data = {};

  document.querySelectorAll("#settings input, #settings select")
    .forEach(el =>
    {
      if(!el.id) return;

      if(el.type === "checkbox")
        data[el.id] = el.checked;

      else if(el.type === "number") {
        let v = Number(el.value);

        // Stunden → Sekunden
        if(el.id === "serviceFlushIntervalSec")
          v *= 3600.0;

        data[el.id] = v;
      }
      else
        data[el.id] = el.value;
    });

  return data;
}

function saveSettings()
{
  const data = collectSettingsData();

  fetch("/api/settings", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(data)
  })
  .then(() => toast("Settings gespeichert"))
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
        
          let v = cfg[k];
        
          // ⭐⭐ Sekunden → Stunden
          if(k === "serviceFlushIntervalSec")
            v = v / 3600.0;
        
          if(el.type === "checkbox")
            el.checked = v;
          else
            el.value = v;
        });
        

      lastMdnsName = cfg.mDNSName || "";
   
     /* ===== aktuelle SSID sofort anzeigen ===== */
     const sel = document.getElementById("wifiSSID");
     if(sel && cfg.wifiSSID) {
       sel.innerHTML = `<option value="${cfg.wifiSSID}">
                          ${cfg.wifiSSID}
                       </option>`;
     }

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
        {
          label: "TDS (ppm)",
          data: [],
          yAxisID: "yTds",
          borderColor: "#00bcd4",
          borderWidth: 1,
          pointRadius: 0,
          tension: 0.25
        },
        {
          label: "Flow (L/min)",
          data: [],
          yAxisID: "yFlow",
          borderColor: "#4caf50",
          borderWidth: 1,
          pointRadius: 0,
          tension: 0.25
        },
        {
          label: "Liter (L)",
          data: [],
          yAxisID: "yProd",
          borderColor: "#ff9800",
          borderWidth: 1,
          pointRadius: 0,
          tension: 0.25
        }
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
      
        const durFmt = s => {
          const m = Math.floor(s/60);
          const sec = s%60;
          return `${m}m ${sec}s`;
        };
      
        tr.innerHTML =
          `<td>${r.mode || ""}</td>
           <td>${fmt(r.start)}</td>
           <td>${fmt(r.end)}</td>
           <td>${durFmt(r.duration||0)}</td>
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
  document.getElementById("spiffVersion").innerText = SPIFF_VERSION;
 };

function scanWifi()
{
  const results = document.getElementById("wifiResults");
  const input   = document.getElementById("wifiSSID");

  results.hidden = true;
  results.replaceChildren();

  toast("Scan läuft…");

  function poll()
  {
    fetch("/api/wifi/scan")
      .then(r =>
      {
        if(r.status !== 200){
          setTimeout(poll, 600);
          return null;
        }
        return r.json();
      })
      .then(list =>
      {
        if(!list) return;

        results.hidden = false;

        list.forEach(n =>
        {
          const row = document.createElement("div");
          row.className = "wifiItem";

          const name = document.createElement("span");
          name.textContent = n.ssid;

          const rssi = document.createElement("span");
          rssi.textContent = `${n.rssi} dBm`;
          rssi.style.color = "#aaa";

          const btn = document.createElement("button");
          btn.textContent = "Use";
          btn.onclick = () =>
          {
            input.value = n.ssid;   // ⭐ nur SSID übernehmen
            results.hidden = true;
          };

          row.appendChild(name);
          row.appendChild(rssi);
          row.appendChild(btn);

          results.appendChild(row);
        });

        toast("Scan fertig");
      })
      .catch(()=>toast("Scan Fehler"));
  }

  poll();
}


function saveAndReboot()
{
  const data = collectSettingsData();

  fetch("/api/settings", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(data)
  })
  .then(() => {
    toast("Settings gespeichert – reboot…");
    return fetch("/api/reboot", { method:"POST" });
  })
  .catch(() => toast("Speichern fehlgeschlagen"));
}

function togglePw(id)
{
  const el = document.getElementById(id);

  if(el.type === "password")
    el.type = "text";
  else
    el.type = "password";
}



