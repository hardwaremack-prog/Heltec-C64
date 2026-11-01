// Web control page served at http://<board-ip>/
#pragma once
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>C64 Mesh</title>
<style>
:root{--blue:#2e2c9b;--lblue:#706deb;--ink:#e8e7ff;--dim:#b3b1f5;--card:#3a38b0;--ok:#a9ff9f;--warn:#edf171;--bad:#ff9a9f}
*{box-sizing:border-box}
body{margin:0;background:var(--lblue);color:var(--ink);font:15px/1.45 ui-monospace,"Cascadia Mono",Menlo,Consolas,monospace}
.frame{max-width:860px;margin:16px auto;background:var(--blue);padding:18px 16px 28px;border-radius:4px}
h1{font-size:20px;letter-spacing:.06em;margin:0 0 2px;text-transform:uppercase}
.sub{color:var(--dim);margin:0 0 16px}
h2{font-size:14px;text-transform:uppercase;letter-spacing:.08em;margin:0 0 10px;color:#fff}
section{background:var(--card);padding:14px;margin:12px 0;border-radius:4px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px}
.stat{background:rgba(0,0,0,.18);padding:8px 10px;border-radius:3px}
.stat b{display:block;font-size:12px;color:var(--dim);font-weight:normal;text-transform:uppercase}
.stat span{font-size:16px}
table{width:100%;border-collapse:collapse}
td,th{text-align:left;padding:6px 4px;border-bottom:1px solid rgba(255,255,255,.12);vertical-align:middle}
th{font-size:12px;color:var(--dim);font-weight:normal;text-transform:uppercase}
button,select,input[type=text],input[type=password],textarea{font:inherit;color:#111;background:#e8e7ff;border:0;border-radius:3px;padding:6px 10px}
button{cursor:pointer;background:#fff;color:var(--blue);font-weight:bold}
button.alt{background:transparent;color:#fff;border:1px solid rgba(255,255,255,.5)}
button:disabled{opacity:.5;cursor:default}
input[type=file]{color:var(--dim);max-width:100%}
textarea{width:100%;min-height:70px}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:8px}
.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}
.note{color:var(--dim);font-size:13px;margin:8px 0 0}
code{background:rgba(0,0,0,.25);padding:1px 5px;border-radius:2px}
#toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:#fff;color:var(--blue);padding:8px 14px;border-radius:3px;font-weight:bold;display:none}
@media (max-width:560px){.hide-s{display:none}}
</style></head><body><div class="frame">
<h1>**** C64 Mesh ****</h1>
<p class="sub">Commodore 64 on a Heltec V3 &middot; <span id="ready">READY.</span></p>

<section><h2>Status</h2><div class="grid">
<div class="stat"><b>VNC address</b><span id="vncaddr">-</span></div>
<div class="stat"><b>VNC viewer</b><span id="vnccli">-</span></div>
<div class="stat"><b>Speed</b><span id="speed">-</span></div>
<div class="stat"><b>Wi-Fi</b><span id="wifi">-</span></div>
</div>
<div class="row"><button onclick="act('reset')">Reset C64</button><button class="alt" onclick="act('restore')">RESTORE key</button>
<label><input type="checkbox" id="warp" onchange="act('warp?on='+(this.checked?1:0))"> Warp speed</label></div>
<p class="note">Open any VNC viewer (RealVNC, TigerVNC, TightVNC) to the address above, no password. Keys: Esc = RUN/STOP, Page Up = RESTORE, Tab/Ctrl = CTRL, Alt = C=, Home = CLR/HOME, F1&ndash;F8 as on the C64. Numeric keypad 8/2/4/6 + 0 = joystick in port 2. Paste text into the viewer to type it.</p>
</section>

<section><h2>ROMs</h2>
<table><thead><tr><th>Slot</th><th>Boot from</th><th class="hide-s">Your file</th><th>Upload</th></tr></thead><tbody id="roms"></tbody></table>
<div class="row"><button onclick="saveRoms()">Apply &amp; restart C64</button><span id="rommsg" class="note"></span></div>
<p class="note">Upload your own BASIC (8&nbsp;KB), KERNAL (8&nbsp;KB) and CHARACTER (4&nbsp;KB) ROM dumps. A 16&nbsp;KB BASIC+KERNAL combo file is also accepted in either slot. Files stay on the board's flash; built-in fallback is the free Open ROMs, whose BASIC is still incomplete (no maths), so real ROMs give the proper experience.</p>
</section>

<section><h2>Virtual disk (device 8)</h2>
<table><thead><tr><th>File</th><th>Blocks</th><th></th></tr></thead><tbody id="disk"></tbody></table>
<div class="row"><input type="file" id="prgfile" accept=".prg,.PRG"><label><input type="checkbox" id="prgrun" checked> load &amp; run now</label><button onclick="upPrg()">Upload .PRG</button></div>
<p class="note">On the C64: <code>LOAD"$",8</code> then <code>LIST</code> for the directory, <code>LOAD"NAME",8,1</code> to load, <code>SAVE"NAME",8</code> to save. <span id="diskfree"></span></p>
</section>

<section><h2>Type into the C64</h2>
<textarea id="typed" placeholder='10 PRINT "HELLO MESH"&#10;20 GOTO 10&#10;RUN'></textarea>
<div class="row"><button onclick="typeIt()">Type it</button></div>
</section>

<section><h2>Wi-Fi</h2>
<div class="row"><select id="ssidsel" onchange="if(this.value)ssid.value=this.value"><option value="">Scan for networks&hellip;</option></select><button class="alt" onclick="scan()">Scan</button></div>
<div class="row"><input type="text" id="ssid" placeholder="Network name"><input type="password" id="pass" placeholder="Password"><button onclick="saveWifi()">Save &amp; reboot</button></div>
<p class="note">If the board can't join your network it starts its own hotspot <code>C64-Setup</code> (password <code>commodore64</code>); this page is then at <code>http://192.168.4.1</code>.</p>
</section>
</div><div id="toast"></div>
<script>
const $=id=>document.getElementById(id);
function toast(t){const e=$('toast');e.textContent=t;e.style.display='block';clearTimeout(e._t);e._t=setTimeout(()=>e.style.display='none',2600)}
async function act(a){const r=await fetch('/api/'+a,{method:'POST'});toast(r.ok?'OK':'Failed');setTimeout(()=>load(true),400)}
const SLOTS=[['basic','BASIC'],['kernal','KERNAL'],['chargen','CHARACTER']];
let st={};
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
async function load(full){
 try{st=await (await fetch('/api/status')).json()}catch(e){return}
 $('vncaddr').textContent=st.ip+':5900';
 $('vnccli').innerHTML=st.vnc?'<span class="ok">connected</span>':'none';
 $('speed').textContent=st.speed+'%'+(st.warp?' (warp)':'');
 $('warp').checked=!!st.warp;
 $('wifi').textContent=st.mode=='ap'?'Hotspot C64-Setup':(st.ssid+' '+st.rssi+'dBm');
 if(full){
 $('roms').innerHTML=SLOTS.map(([k,n])=>{const r=st.roms[k];
  return `<tr><td>${n}</td><td><select id="sel_${k}"><option value="0">Built-in Open ROMs</option><option value="1" ${r.custom?'':'disabled'}>My ROM file</option></select></td>
  <td class="hide-s">${r.custom?esc(r.label)+' <small>'+r.crc+'</small> <a href="#" onclick="delRom(\''+k+'\');return false" style="color:#ff9a9f">remove</a>':'<span class="note">none uploaded</span>'}</td>
  <td><input type="file" id="f_${k}"> <button class="alt" onclick="upRom('${k}')">Upload</button></td></tr>`}).join('');
 SLOTS.forEach(([k])=>$('sel_'+k).value=st.roms[k].use?'1':'0');
 const m=st.roms.basic.use+st.roms.kernal.use;
 $('rommsg').innerHTML=st.romsPending?'<span class="warn">Changes not applied yet</span>':(m==1?'<span class="warn">Mixing a real KERNAL/BASIC with Open ROMs may not work well</span>':'Running: '+esc(st.romName));
 }
 $('disk').innerHTML=st.disk.length?st.disk.map(f=>`<tr><td>${esc(f.n)}</td><td>${Math.ceil(f.s/254)}</td><td style="text-align:right"><button class="alt" onclick="runPrg('${esc(f.n)}')">Run</button> <a href="/api/disk/${encodeURIComponent(f.n)}" download="${esc(f.n)}.prg" style="color:#fff">&darr;</a> <a href="#" onclick="delPrg('${esc(f.n)}');return false" style="color:#ff9a9f">&times;</a></td></tr>`).join(''):'<tr><td colspan=3 class="note">Empty disk</td></tr>';
 $('diskfree').textContent='Flash free: '+Math.round(st.fsfree/1024)+' KB.';
}
async function up(url,file){const fd=new FormData();fd.append('file',file,file.name);const r=await fetch(url,{method:'POST',body:fd});const t=await r.text();toast(r.ok?'Uploaded':t);return r.ok}
async function upRom(k){const f=$('f_'+k).files[0];if(!f)return toast('Pick a file first');await up('/api/rom?slot='+k,f);load(true)}
async function delRom(k){if(!confirm('Remove your '+k+' ROM file?'))return;await fetch('/api/romdel?slot='+k,{method:'POST'});load(true)}
async function saveRoms(){const q=SLOTS.map(([k])=>k+'='+$('sel_'+k).value).join('&');const r=await fetch('/api/romsel?'+q,{method:'POST'});toast(r.ok?'Restarting C64 with selected ROMs':'Failed');setTimeout(()=>load(true),800)}
async function upPrg(){const f=$('prgfile').files[0];if(!f)return toast('Pick a .PRG first');await up('/api/prg?run='+($('prgrun').checked?1:0),f);load()}
async function runPrg(n){await fetch('/api/run?name='+encodeURIComponent(n),{method:'POST'});toast('Loading '+n)}
async function delPrg(n){if(!confirm('Delete '+n+'?'))return;await fetch('/api/diskdel?name='+encodeURIComponent(n),{method:'POST'});load()}
async function typeIt(){const r=await fetch('/api/type',{method:'POST',body:$('typed').value});toast(r.ok?'Typing…':'Failed')}
async function scan(){toast('Scanning…');const l=await (await fetch('/api/scan')).json();$('ssidsel').innerHTML='<option value="">'+l.length+' networks found</option>'+l.map(n=>`<option value="${esc(n.s)}">${esc(n.s)} (${n.r}dBm)</option>`).join('')}
async function saveWifi(){if(!$('ssid').value)return toast('Enter a network name');const b=new URLSearchParams({ssid:$('ssid').value,pass:$('pass').value});await fetch('/api/wifi',{method:'POST',body:b});toast('Saved, rebooting…')}
load(true);setInterval(()=>load(false),3000);
</script></body></html>)HTML";
