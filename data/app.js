// ===== Settings handling =====

function saveSettings(){
  const cfg = {
    p_in: +p_in.value,
    p_out: +p_out.value,
    t_prepare: +t_prepare.value,
    t_flush: +t_flush.value,
    max_l: +max_l.value
  };

  ws.send("cfg:"+JSON.stringify(cfg));
}
