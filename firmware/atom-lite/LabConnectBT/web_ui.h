#pragma once

#include <Arduino.h>

const char LABCONNECT_WEB_UI[] PROGMEM = R"HTML(
<!doctype html>
<html lang="fr">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>LabConnect BT · Moniteur série</title>
  <style>
    :root{color-scheme:light dark;--bg:#05070a;--panel:#0d1218;--glass:#111a24cc;--line:#26384a;--muted:#91a0ae;--text:#f6f9fc;--blue:#3389ff;--cyan:#1ec9e6;--accent:#1ec9e6;--rx:#62a8ff;--tx:#ffb45c;--web:#d98cff}
    *{box-sizing:border-box}
    body{margin:0;background:radial-gradient(circle at 12% -10%,#12345a 0,transparent 34%),radial-gradient(circle at 90% 0,#073b48 0,transparent 30%),var(--bg);color:var(--text);font:14px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;min-height:100vh}
    button,input,select{font:inherit}
    .shell{width:min(1180px,calc(100% - 28px));margin:0 auto;padding:24px 0 32px}
    header{display:flex;align-items:flex-start;justify-content:space-between;gap:20px;padding:18px;border:1px solid #6abfff35;border-radius:14px;background:var(--glass);backdrop-filter:blur(22px) saturate(145%)}
    .brand{font:800 30px/1 system-ui,sans-serif;letter-spacing:-.035em;margin-bottom:18px}.brand-lab{color:#fff}.brand-connect{display:inline-block;color:var(--cyan);background:linear-gradient(90deg,var(--blue),var(--cyan));background-clip:text;-webkit-background-clip:text;-webkit-text-fill-color:transparent}
    h1{font:700 clamp(22px,4vw,34px)/1.05 system-ui,sans-serif;letter-spacing:-.04em;margin:3px 0 8px}
    .subtitle{color:var(--muted);margin:0}
    .status{display:grid;grid-template-columns:auto auto;gap:6px 18px;min-width:250px;padding:13px 15px;border-radius:10px;background:#02050980}
    .status span:nth-child(odd){color:var(--muted)}
    .dot{display:inline-block;width:7px;height:7px;margin-right:7px;border-radius:50%;background:#536068}
    .dot.on{background:var(--accent);box-shadow:0 0 10px #45d48388}
    .toolbar{display:flex;flex-wrap:wrap;align-items:center;gap:10px;margin:18px 0 10px}
    .toolbar label{display:flex;align-items:center;gap:7px;color:var(--muted)}
    button,select,input{border:1px solid var(--line);background:#111a23;color:var(--text);border-radius:9px}
    button{cursor:pointer;padding:9px 13px}
    button:hover{border-color:#55b9e8}
    button:active{transform:translateY(1px)}
    button.primary{border-color:var(--blue);background:var(--blue);color:#fff;font-weight:700}
    button:disabled{cursor:not-allowed;opacity:.55}
    select{padding:8px 30px 8px 10px}
    .spacer{flex:1}
    .terminal{height:min(57vh,610px);min-height:320px;overflow:auto;border:1px solid var(--line);border-radius:12px;background:#030609}
    .empty{display:grid;place-items:center;height:100%;color:#66727a;text-align:center;padding:30px}
    .row{display:grid;grid-template-columns:88px 126px 1fr;gap:12px;padding:7px 12px;border-bottom:1px solid #181c1f}
    .row:hover{background:#101417}
    .time{color:#68747b}
    .dir{font-weight:700}
    .dir.rx{color:var(--rx)}.dir.tx{color:var(--tx)}.dir.web{color:var(--web)}
    .payload{min-width:0;overflow-wrap:anywhere;white-space:pre-wrap}
    .hex{display:block;color:#68747b;font-size:12px;margin-top:2px}
    .composer{margin-top:14px;padding:16px;border:1px solid #6abfff35;border-radius:14px;background:var(--glass);backdrop-filter:blur(18px) saturate(135%)}
    .composer h2{font:650 16px/1.2 system-ui,sans-serif;margin:0 0 13px}
    .compose-grid{display:grid;grid-template-columns:1fr auto auto auto;gap:9px}
    input{width:100%;padding:10px 12px;outline:none}
    input:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 2px #45d48322}
    .hint{min-height:20px;margin:9px 0 0;color:var(--muted);font-size:12px}
    .hint.error{color:#ff7b72}.hint.ok{color:var(--accent)}
    dialog{width:min(680px,calc(100% - 28px));max-height:90vh;overflow:auto;padding:0;border:1px solid #6abfff55;border-radius:14px;background:#0b1118ee;color:var(--text);backdrop-filter:blur(26px) saturate(140%)}
    dialog::backdrop{background:#000a}
    .settings-head{display:flex;align-items:center;justify-content:space-between;gap:20px;padding:18px 20px;border-bottom:1px solid var(--line)}
    .settings-head h2{font:650 20px/1.2 system-ui,sans-serif;margin:0}
    .icon-button{padding:7px 11px;font-size:18px}
    .settings-body{padding:20px}
    .settings-grid{display:grid;grid-template-columns:1fr 1fr;gap:15px}
    .field{display:grid;gap:6px;color:var(--muted)}
    .field.wide{grid-column:1/-1}
    .field input,.field select{color:var(--text)}
    .serial-grid{display:grid;grid-template-columns:2fr repeat(3,1fr);gap:10px;grid-column:1/-1}
    .check{display:flex;align-items:center;gap:9px;color:var(--text);grid-column:1/-1}
    .check input{width:auto}
    .notice{margin:17px 0 0;padding:12px;border:1px solid #8a6e45;background:#1b1712;color:#e3cfb1;font-size:12px;border-radius:9px}
    .settings-actions{display:flex;flex-wrap:wrap;gap:9px;align-items:center;margin-top:18px}
    .settings-actions .spacer{flex:1}
    @media(max-width:760px){
      header{display:block}.status{margin-top:18px;min-width:0}
      .row{grid-template-columns:72px 1fr}.payload{grid-column:1/-1}
      .compose-grid{grid-template-columns:1fr 1fr}.compose-grid input{grid-column:1/-1}.compose-grid button{grid-column:1/-1}
      .settings-grid,.serial-grid{grid-template-columns:1fr}.field.wide,.serial-grid,.check{grid-column:auto}
      .terminal{height:52vh}
    }
    @media(prefers-color-scheme:light){
      :root{--bg:#eef5fb;--panel:#fff;--glass:#ffffffcc;--line:#b9cfdf;--muted:#526574;--text:#101820;--rx:#1769cf;--tx:#9a5700;--web:#8b3cab}
      body{background:radial-gradient(circle at 12% -10%,#b7daf9 0,transparent 34%),radial-gradient(circle at 90% 0,#c4f1f3 0,transparent 30%),var(--bg)}
      header,.composer{background:var(--glass);border-color:#478fc455}
      .status{background:#e7f1f880}
      button,select,input{background:#f8fbfd;color:var(--text)}
      .terminal{background:#fff}
      .row{border-bottom-color:#dce8f0}.row:hover{background:#edf6fb}
      .time,.hex{color:#647784}
      dialog{background:#f8fbfdee;color:var(--text)}
      dialog::backdrop{background:#26374666}
      .notice{background:#fff8e9;color:#68491c}
      .brand-lab{color:#101820}
    }
    @media(prefers-reduced-motion:reduce){*{scroll-behavior:auto!important}}
  </style>
</head>
<body>
  <main class="shell">
    <header>
      <div>
        <div class="brand" aria-label="LabConnect"><span class="brand-lab">Lab</span><span class="brand-connect">Connect</span></div>
        <h1>Moniteur série</h1>
        <p class="subtitle">Mesures brutes et commandes de la balance, sans transformation.</p>
      </div>
      <div class="status" aria-live="polite">
        <span>Bluetooth</span><b><i id="btDot" class="dot"></i><span id="bt">—</span></b>
        <span>UART</span><b id="uart">—</b>
        <span>Wi‑Fi</span><b id="ssid">—</b>
      </div>
    </header>

    <div class="toolbar">
      <label><input id="autoScroll" type="checkbox" checked> Défilement auto</label>
      <label>Filtre
        <select id="filter">
          <option value="all">Tout le trafic</option>
          <option value="rx">Balance → PC</option>
          <option value="tx">PC → balance</option>
          <option value="web">Interface → balance</option>
        </select>
      </label>
      <span class="spacer"></span>
      <button id="pause">Pause</button>
      <button id="clear">Effacer l’écran</button>
      <button id="openSettings">Paramètres</button>
    </div>

    <section id="terminal" class="terminal" aria-label="Trafic série">
      <div id="empty" class="empty">En attente de données série…</div>
    </section>

    <form id="composer" class="composer">
      <h2>Envoyer une commande à la balance</h2>
      <div class="compose-grid">
        <input id="command" maxlength="256" autocomplete="off" spellcheck="false" aria-label="Commande" placeholder="Exemple : Q">
        <select id="format" aria-label="Format">
          <option value="text">Texte ASCII</option>
          <option value="hex">Hexadécimal</option>
        </select>
        <select id="ending" aria-label="Terminaison">
          <option value="crlf">+ CRLF</option>
          <option value="cr">+ CR</option>
          <option value="lf">+ LF</option>
          <option value="none">Aucune</option>
        </select>
        <button id="send" class="primary" type="submit">Envoyer</button>
      </div>
      <p id="hint" class="hint">La commande est injectée directement sur l’UART, sans passer par RsCom.</p>
    </form>
  </main>

  <dialog id="settings">
    <div class="settings-head">
      <h2>Paramètres de l’adaptateur</h2>
      <button id="closeSettings" class="icon-button" type="button" aria-label="Fermer">×</button>
    </div>
    <form id="settingsForm" class="settings-body">
      <div class="settings-grid">
        <label class="field">Nom Bluetooth
          <input id="bluetoothName" name="bluetoothName" maxlength="28" required>
        </label>
        <label class="field">Nom du Wi‑Fi
          <input id="wifiName" name="wifiName" maxlength="32" required>
        </label>
        <label class="field wide">Mot de passe Wi‑Fi
          <input id="wifiPassword" name="wifiPassword" type="password" minlength="8" maxlength="63" required>
        </label>
        <div class="serial-grid">
          <label class="field">Débit
            <select id="baud" name="baud">
              <option>300</option><option>600</option><option>1200</option><option selected>2400</option>
              <option>4800</option><option>9600</option><option>19200</option><option>38400</option>
              <option>57600</option><option>115200</option>
            </select>
          </label>
          <label class="field">Bits
            <select id="dataBits" name="dataBits"><option>7</option><option>8</option></select>
          </label>
          <label class="field">Parité
            <select id="parity" name="parity"><option value="0">Aucune</option><option value="1">Paire</option><option value="2">Impaire</option></select>
          </label>
          <label class="field">Arrêt
            <select id="stopBits" name="stopBits"><option>1</option><option>2</option></select>
          </label>
        </div>
        <label class="check"><input id="swapRxTx" name="swapRxTx" type="checkbox"> Inverser RX et TX</label>
        <p id="pins" class="hint field wide"></p>
      </div>
      <p class="notice">L’enregistrement redémarre l’adaptateur. Si le nom Bluetooth change, supprimez l’ancien appareil dans Windows, refaites l’appairage puis vérifiez le nouveau port COM. Si le Wi‑Fi change, reconnectez-vous avec les nouveaux identifiants.</p>
      <p id="settingsHint" class="hint"></p>
      <div class="settings-actions">
        <select id="profile" aria-label="Profil constructeur">
          <option value="and">A&amp;D — 2400 / 7E1</option>
          <option value="mettler">Mettler Toledo — 9600 / 8N1</option>
          <option value="sartorius">Sartorius — 9600 / 8O1</option>
          <option value="kern">KERN — 9600 / 8N1</option>
          <option value="precisa">Precisa — 9600 / 7E1</option>
          <option value="radwag">Radwag — 9600 / 8N1</option>
          <option value="ohaus">Ohaus — 9600 / 8N1</option>
        </select>
        <button id="applyProfile" type="button">Appliquer le profil</button>
        <button id="resetSettings" type="button">Valeurs d’origine</button>
        <span class="spacer"></span>
        <button class="primary" type="submit">Enregistrer et redémarrer</button>
      </div>
    </form>
  </dialog>
  <script>
    const terminal=document.querySelector('#terminal'),empty=document.querySelector('#empty');
    const pauseBtn=document.querySelector('#pause'),filter=document.querySelector('#filter');
    let after=0,paused=false,busy=false;
    const esc=s=>{const e=document.createElement('span');e.textContent=s;return e.innerHTML};
    function addEvent(e){
      empty?.remove();
      const map={rx:['rx','BAL → PC'],tx:['tx','PC → BAL'],web:['web','WEB → BAL']};
      const meta=map[e.dir]||['','INCONNU'];
      const row=document.createElement('div');
      row.className='row'; row.dataset.dir=meta[0];
      row.innerHTML=`<span class="time">${(e.ms/1000).toFixed(3)} s</span><span class="dir ${meta[0]}">${meta[1]}</span><span class="payload">${esc(e.text)}<small class="hex">${esc(e.hex)}</small></span>`;
      row.hidden=filter.value!=='all'&&filter.value!==meta[0];
      terminal.append(row);
      while(terminal.children.length>300) terminal.firstElementChild.remove();
    }
    async function poll(){
      if(paused||busy)return;
      busy=true;
      try{
        const r=await fetch(`/api/events?after=${after}`,{cache:'no-store'});
        if(!r.ok)throw Error();
        const data=await r.json();
        document.querySelector('#bt').textContent=data.bt?'connecté':'en attente';
        document.querySelector('#btDot').classList.toggle('on',data.bt);
        document.querySelector('#ssid').textContent=data.ssid;
        document.querySelector('#uart').textContent=data.uart;
        data.events.forEach(e=>{after=Math.max(after,e.seq);addEvent(e)});
        if(document.querySelector('#autoScroll').checked&&data.events.length)terminal.scrollTop=terminal.scrollHeight;
      }catch(_){}
      finally{busy=false}
    }
    pauseBtn.onclick=()=>{paused=!paused;pauseBtn.textContent=paused?'Reprendre':'Pause'};
    document.querySelector('#clear').onclick=()=>{terminal.innerHTML='<div id="empty" class="empty">Écran effacé. En attente de nouvelles données…</div>'};
    filter.onchange=()=>document.querySelectorAll('.row').forEach(r=>r.hidden=filter.value!=='all'&&filter.value!==r.dataset.dir);
    document.querySelector('#composer').onsubmit=async e=>{
      e.preventDefault();
      const send=document.querySelector('#send'),hint=document.querySelector('#hint'),command=document.querySelector('#command');
      if(!command.value){hint.className='hint error';hint.textContent='Saisis une commande.';return}
      send.disabled=true;hint.className='hint';hint.textContent='Envoi…';
      const body=new URLSearchParams({command:command.value,format:document.querySelector('#format').value,ending:document.querySelector('#ending').value});
      try{
        const r=await fetch('/api/send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
        const data=await r.json();
        if(!r.ok)throw Error(data.error||'Échec de l’envoi');
        hint.className='hint ok';hint.textContent=`${data.bytes} octet(s) envoyé(s) à la balance.`;
        command.select(); poll();
      }catch(err){hint.className='hint error';hint.textContent=err.message}
      finally{send.disabled=false}
    };
    const settings=document.querySelector('#settings'),settingsForm=document.querySelector('#settingsForm');
    const settingsHint=document.querySelector('#settingsHint');
    function setField(id,value){document.querySelector('#'+id).value=String(value)}
    async function loadSettings(){
      settingsHint.className='hint';settingsHint.textContent='Chargement…';
      try{
        const r=await fetch('/api/config',{cache:'no-store'}),data=await r.json();
        if(!r.ok)throw Error(data.error||'Impossible de lire les paramètres');
        ['bluetoothName','wifiName','wifiPassword','baud','dataBits','parity','stopBits'].forEach(k=>setField(k,data[k]));
        document.querySelector('#swapRxTx').checked=data.swapRxTx;
        document.querySelector('#pins').textContent=`Broches physiques fixes : RX GPIO ${data.physicalRx}, TX GPIO ${data.physicalTx}.`;
        settingsHint.textContent='';
      }catch(err){settingsHint.className='hint error';settingsHint.textContent=err.message}
    }
    document.querySelector('#openSettings').onclick=()=>{settings.showModal();loadSettings()};
    document.querySelector('#closeSettings').onclick=()=>settings.close();
    settings.onclick=e=>{if(e.target===settings)settings.close()};
    const profiles={
      and:{label:'A&D',baud:2400,bits:7,parity:1,stop:1},
      mettler:{label:'Mettler Toledo',baud:9600,bits:8,parity:0,stop:1},
      sartorius:{label:'Sartorius',baud:9600,bits:8,parity:2,stop:1},
      kern:{label:'KERN',baud:9600,bits:8,parity:0,stop:1},
      precisa:{label:'Precisa',baud:9600,bits:7,parity:1,stop:1},
      radwag:{label:'Radwag',baud:9600,bits:8,parity:0,stop:1},
      ohaus:{label:'Ohaus',baud:9600,bits:8,parity:0,stop:1}
    };
    document.querySelector('#applyProfile').onclick=()=>{
      const p=profiles[document.querySelector('#profile').value];
      setField('baud',p.baud);setField('dataBits',p.bits);setField('parity',p.parity);setField('stopBits',p.stop);
      settingsHint.className='hint ok';settingsHint.textContent=`Profil ${p.label} préparé. Vérifiez les réglages de la balance puis cliquez sur Enregistrer.`;
    };
    settingsForm.onsubmit=async e=>{
      e.preventDefault();
      const submit=settingsForm.querySelector('[type=submit]');
      submit.disabled=true;settingsHint.className='hint';settingsHint.textContent='Enregistrement…';
      const body=new URLSearchParams(new FormData(settingsForm));
      body.set('swapRxTx',document.querySelector('#swapRxTx').checked?'1':'0');
      try{
        const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
        const data=await r.json();
        if(!r.ok)throw Error(data.error||'Échec de l’enregistrement');
        settingsHint.className='hint ok';settingsHint.textContent='Enregistré. Redémarrage en cours…';
      }catch(err){settingsHint.className='hint error';settingsHint.textContent=err.message;submit.disabled=false}
    };
    document.querySelector('#resetSettings').onclick=async()=>{
      if(!confirm('Rétablir tous les paramètres d’origine et redémarrer ?'))return;
      settingsHint.className='hint';settingsHint.textContent='Réinitialisation…';
      try{
        const r=await fetch('/api/config/reset',{method:'POST'}),data=await r.json();
        if(!r.ok)throw Error(data.error||'Échec de la réinitialisation');
        settingsHint.className='hint ok';settingsHint.textContent='Valeurs d’origine restaurées. Redémarrage…';
      }catch(err){settingsHint.className='hint error';settingsHint.textContent=err.message}
    };
    setInterval(poll,350);poll();
  </script>
</body>
</html>
)HTML";
