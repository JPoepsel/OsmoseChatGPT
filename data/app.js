let ws = new WebSocket("ws://"+location.host+"/ws");

let chart;
let data=[];

ws.onmessage = e => {
  let d = JSON.parse(e.data);

  state.innerText=d.state;
  tds.innerText=d.tds.toFixed(1);
  liters.innerText=d.liters.toFixed(2);
  flow.innerText=d.flow.toFixed(2);
  left.innerText=d.left.toFixed(2);

  addChart(d.tds);
};

function addChart(v){
  data.push(v);
  if(data.length>200) data.shift();
  chart.update();
}

function show(id){
  home.hidden=hist.hidden=set.hidden=true;
  document.getElementById(id).hidden=false;
}

window.onload=()=>{
  chart=new Chart(document.getElementById("chart"),{
    type:"line",
    data:{labels:data,datasets:[{data:data}]},
    options:{animation:false}
  });
};
