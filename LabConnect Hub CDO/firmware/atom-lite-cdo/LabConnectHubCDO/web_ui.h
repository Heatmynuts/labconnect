#pragma once

#include <Arduino.h>

const char LABCONNECT_CDO_WEB_UI[] PROGMEM = R"HTML(
<!doctype html>
<html lang="fr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>LabConnect CDO · Diagnostic</title>
  <style>
    :root{color-scheme:dark;--bg:#071018;--panel:#101d28;--line:#284256;--text:#eff7ff;--muted:#91a8ba;--blue:#62a8ff;--orange:#ffb45c;--green:#63d59a;--red:#ff7272}
    *{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(circle at 15% 0,#153d5f 0,transparent 34%),var(--bg);color:var(--text);font:14px/1.45 ui-monospace,SFMono-Regular,Menlo,monospace}
    main{width:min(1100px,calc(100% - 28px));margin:auto;padding:24px 0}header,.panel{border:1px solid var(--line);border-radius:14px;background:#101d28df}
    header{display:flex;justify-content:space-between;gap:20px;padding:20px}h1{font:750 clamp(24px,4vw,36px)/1 system-ui,sans-serif;margin:0 0 8px}.sub{margin:0;color:var(--muted)}
    .identity{font:800 26px/1 system-ui,sans-serif;color:var(--blue);text-align:right}.identity small{display:block;margin-top:8px;color:var(--muted);font:12px/1.3 ui-monospace,monospace}
    .grid{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin:14px 0}.card{padding:13px;border:1px solid var(--line);border-radius:11px;background:#0b151e}.label{color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:.08em}.value{margin-top:5px;font-weight:700;overflow-wrap:anywhere}
    .panel{overflow:hidden}.toolbar{display:flex;align-items:center;gap:10px;padding:12px 14px;border-bottom:1px solid var(--line)}button{margin-left:auto;padding:7px 11px;border:1px solid var(--line);border-radius:8px;background:#172b3b;color:var(--text);cursor:pointer}
    #events{height:min(57vh,590px);min-height:320px;overflow:auto;background:#050a0f}.empty{display:grid;place-items:center;height:100%;color:#607789}.row{display:grid;grid-template-columns:85px 92px 1fr;gap:12px;padding:8px 12px;border-bottom:1px solid #14232f}.time{color:#698296}.dir{font-weight:800}.dir.rx,.dir.id-rx{color:var(--blue)}.dir.tx,.dir.id-tx{color:var(--orange)}.text{white-space:pre-wrap;overflow-wrap:anywhere}.hex{display:block;color:#607789;font-size:12px;margin-top:2px}
    .ok{color:var(--green)}.bad{color:var(--red)}footer{margin-top:12px;color:var(--muted);font-size:12px}
    @media(max-width:760px){header{display:block}.identity{text-align:left;margin-top:18px}.grid{grid-template-columns:1fr 1fr}.row{grid-template-columns:70px 1fr}.text{grid-column:1/-1}}
  </style>
</head>
<body>
<main>
  <header><div><h1>LabConnect CDO</h1><p class="sub">Diagnostic en lecture seule · aucune donnée n’est modifiée</p></div><div class="identity" id="identity">Détection…<small id="model">Balance non identifiée</small></div></header>
  <section class="grid">
    <div class="card"><div class="label">Bluetooth SPP</div><div class="value" id="bluetooth">En attente</div></div>
    <div class="card"><div class="label">RS-232</div><div class="value" id="uart">9600/8N1 · CRLF</div></div>
    <div class="card"><div class="label">Identification</div><div class="value" id="identification">Démarrage</div></div>
    <div class="card"><div class="label">LED ATOM</div><div class="value" id="led">—</div></div>
  </section>
  <section class="panel">
    <div class="toolbar"><strong>Journal des octets</strong><span class="sub">texte et hexadécimal</span><button id="clear">Effacer l’affichage</button></div>
    <div id="events"><div class="empty">En attente de trafic…</div></div>
  </section>
  <footer>Le journal est conservé uniquement en mémoire vive. Fermer ce diagnostic ou couper le Wi-Fi n’interrompt pas Optimu.</footer>
</main>
<script>
let after=0,rows=0;const box=document.querySelector('#events');
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
function add(e){if(!rows)box.innerHTML='';const row=document.createElement('div');row.className='row';const label={'rx':'BAL → PC','tx':'PC → BAL','id-rx':'ID ← BAL','id-tx':'ID → BAL'}[e.dir]||e.dir;row.innerHTML=`<span class="time">${(e.ms/1000).toFixed(3)} s</span><span class="dir ${esc(e.dir)}">${esc(label)}</span><span class="text">${esc(e.text)}<span class="hex">${esc(e.hex)}</span></span>`;box.appendChild(row);rows++;while(box.children.length>160)box.firstChild.remove();box.scrollTop=box.scrollHeight;after=Math.max(after,e.seq)}
async function poll(){try{const r=await fetch('/api/status?after='+after,{cache:'no-store'});if(!r.ok)throw Error(r.status);const s=await r.json();identity.textContent=s.id||'Détection…';model.textContent=s.model||'Balance non identifiée';bluetooth.textContent=s.btClient?'Optimu connecté':s.btStarted?'Prêt, PC déconnecté':'Non démarré';bluetooth.className='value '+(s.btClient?'ok':'');uart.textContent=s.uart;identification.textContent=s.identification;identification.className='value '+(s.id?'ok':s.identificationError?'bad':'');led.textContent=(s.led||'—')+(s.ledMeaning?' · '+s.ledMeaning:'');led.className='value '+(s.identificationError?'bad':s.btClient?'ok':'');s.events.forEach(add)}catch(e){bluetooth.textContent='Diagnostic indisponible';bluetooth.className='value bad'}finally{setTimeout(poll,700)}}
document.querySelector('#clear').onclick=()=>{box.innerHTML='<div class="empty">Affichage effacé</div>';rows=0};poll();
</script>
</body>
</html>
)HTML";
