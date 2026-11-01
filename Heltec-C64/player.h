// The C64 itself, in the browser: http://<board-ip>/
// Screen + sound stream over a WebSocket on port 81.
#pragma once
static const char PLAYER_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover,user-scalable=no">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="theme-color" content="#1b1a4a">
<title>Heltec-C64</title>
<style>
:root{--bg:#121133;--bar:#1b1a4a;--key:#3a3870;--key2:#4b4890;--ink:#eceaff;--dim:#a9a6e6;--acc:#a9ff9f;--warn:#edf171}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;height:100%;background:var(--bg);color:var(--ink);font:14px/1.3 ui-monospace,"Cascadia Mono",Menlo,Consolas,monospace;overscroll-behavior:none}
body{display:flex;flex-direction:column;height:100dvh;user-select:none;-webkit-user-select:none}
header{display:flex;align-items:center;gap:6px;padding:6px 8px;background:var(--bar);flex-wrap:wrap;padding-top:max(6px,env(safe-area-inset-top))}
header h1{font-size:14px;margin:0 6px 0 2px;letter-spacing:.08em;white-space:nowrap}
.tb{font:inherit;color:var(--ink);background:var(--key);border:0;border-radius:4px;padding:6px 9px;cursor:pointer;white-space:nowrap}
.tb.on{background:var(--acc);color:#14301a}
.tb:active{filter:brightness(1.3)}
#status{margin-left:auto;color:var(--dim);font-size:12px;white-space:nowrap}
#status i{display:inline-block;width:8px;height:8px;border-radius:50%;background:#e66;margin-right:5px;vertical-align:middle}
#status.ok i{background:var(--acc)}
#stage{flex:1;min-height:0;display:flex;align-items:center;justify-content:center;position:relative;padding:6px}
canvas{image-rendering:pixelated;image-rendering:crisp-edges;background:#000;box-shadow:0 0 0 1px #000,0 8px 30px rgba(0,0,0,.5);touch-action:none}
#overlay{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;background:rgba(18,17,51,.72);cursor:pointer;text-align:center}
#overlay div{background:var(--bar);padding:18px 22px;border-radius:6px;max-width:320px}
#overlay b{display:block;font-size:18px;margin-bottom:6px}
#overlay span{color:var(--dim)}
/* on-screen keyboard */
#kbd{display:none;background:var(--bar);padding:6px 4px calc(6px + env(safe-area-inset-bottom));}
#kbd .r{display:flex;gap:3px;margin:3px 0;justify-content:center}
.k{flex:1 1 0;min-width:0;height:38px;border:0;border-radius:4px;background:var(--key);color:var(--ink);font:inherit;font-size:13px;padding:0;touch-action:none}
.k.w15{flex-grow:1.5}.k.w2{flex-grow:2}.k.w8{flex-grow:8}
.k.f{background:#5a4a20}.k.m{background:#2a2958}
.k.dn,.k.lat{background:var(--acc);color:#14301a}
/* joystick */
#joy{display:none;position:absolute;inset:auto 0 0 0;pointer-events:none;justify-content:space-between;align-items:flex-end;padding:14px 18px calc(14px + env(safe-area-inset-bottom))}
#pad,#fire{pointer-events:auto;touch-action:none;border-radius:50%;background:rgba(255,255,255,.10);border:2px solid rgba(255,255,255,.25)}
#pad{width:150px;height:150px;position:relative}
#pad::after{content:"";position:absolute;left:50%;top:50%;width:54px;height:54px;margin:-27px 0 0 -27px;border-radius:50%;background:rgba(255,255,255,.18);transform:translate(var(--dx,0),var(--dy,0))}
#fire{width:96px;height:96px;display:flex;align-items:center;justify-content:center;font-weight:bold;color:var(--ink)}
#fire.dn{background:rgba(169,255,159,.45)}
dialog{background:var(--bar);color:var(--ink);border:0;border-radius:6px;width:min(520px,92vw)}
dialog textarea{width:100%;height:140px;font:inherit;background:#0d0c26;color:var(--ink);border:1px solid var(--key2);border-radius:4px;padding:8px}
dialog::backdrop{background:rgba(0,0,0,.6)}
.row{display:flex;gap:8px;justify-content:flex-end;margin-top:8px;flex-wrap:wrap;align-items:center}
.note{color:var(--dim);font-size:12px;margin:6px 0 0}
@media (max-width:600px){header h1{display:none}.lbl{display:none}}
</style></head><body>
<header>
 <h1>HELTEC-C64</h1>
 <button class="tb" id="bSound" title="Sound">&#128264;<span class="lbl"> Sound</span></button>
 <button class="tb" id="bKbd" title="On-screen keyboard">&#9000;<span class="lbl"> Keys</span></button>
 <button class="tb" id="bJoy" title="Joystick">&#127918;<span class="lbl"> Joystick</span></button>
 <button class="tb" id="bPort" title="Joystick port">Port 2</button>
 <button class="tb" id="bPaste" title="Type or paste text">&#128203;<span class="lbl"> Type</span></button>
 <button class="tb" id="bReset" title="Reset the C64">&#8635;<span class="lbl"> Reset</span></button>
 <button class="tb" id="bFull" title="Full screen">&#9974;</button>
 <a class="tb" href="/settings" title="ROMs, disk, Wi-Fi" style="text-decoration:none">&#9881;<span class="lbl"> Settings</span></a>
 <span id="status"><i></i><span id="stxt">connecting…</span></span>
</header>
<div id="stage">
 <canvas id="scr" width="384" height="272"></canvas>
 <div id="overlay"><div><b>READY.</b><span>Tap or click to start the C64 with sound</span></div></div>
 <div id="joy"><div id="pad"></div><div id="fire">FIRE</div></div>
</div>
<div id="kbd"></div>
<dialog id="dlg"><b>Type into the C64</b>
 <p class="note">Pasted text is typed on the C64 keyboard. Ctrl+V / Cmd+V on the screen does the same.</p>
 <textarea id="txt" placeholder='10 PRINT "HELLO MESH"&#10;20 GOTO 10&#10;RUN'></textarea>
 <div class="row"><label class="note"><input type="checkbox" id="arrowsJoy"> Arrow keys + Space = joystick</label>
 <button class="tb" onclick="dlg.close()">Cancel</button><button class="tb on" id="bTypeGo">Type it</button></div>
</dialog>
<script>
"use strict";
const $=id=>document.getElementById(id);
const W=384,H=272;
const PAL=[0x000000,0xFFFFFF,0x813338,0x75CEC8,0x8E3C97,0x56AC4D,0x2E2C9B,0xEDF171,0x8E5029,0x553800,0xC46C71,0x4A4A4A,0x7B7B7B,0xA9FF9F,0x706DEB,0xB2B2B2]
 .map(c=>0xFF000000|((c&255)<<16)|(c&0xFF00)|(c>>16));   // RGBA bytes as little-endian uint32
const cv=$('scr'),ctx=cv.getContext('2d',{alpha:false});
const img=ctx.createImageData(W,H),px=new Uint32Array(img.data.buffer);
px.fill(PAL[14]);ctx.putImageData(img,0,0);

/*DECODE-START*/
// Apply one 0x01 tile message to a W-wide Uint32 pixel array
function applyTiles(u8,px,pal,W){
  let n=u8[1]|(u8[2]<<8),p=3;
  for(let t=0;t<n;t++){
    const tx=u8[p++],ty=u8[p++],mode=u8[p++],x0=tx*16,y0=ty*16;
    if(mode===0){const c=pal[u8[p++]];for(let y=0;y<16;y++){const r=(y0+y)*W+x0;for(let x=0;x<16;x++)px[r+x]=c;}}
    else if(mode===1){const c0=pal[u8[p]&15],c1=pal[u8[p]>>4];p++;
      for(let y=0;y<16;y++){const bits=(u8[p]<<8)|u8[p+1];p+=2;const r=(y0+y)*W+x0;
        for(let x=0;x<16;x++)px[r+x]=(bits&(0x8000>>x))?c1:c0;}}
    else{for(let y=0;y<16;y++){const r=(y0+y)*W+x0;for(let x=0;x<8;x++){const b=u8[p++];px[r+x*2]=pal[b&15];px[r+x*2+1]=pal[b>>4];}}}
  }
  return p;
}
/*DECODE-END*/

// ---------------- connection
let ws=null,started=false,soundOn=true,joyPort=2,frames=0,lastFpsT=performance.now(),noReconnect=false;
function setStatus(ok,t){$('status').className=ok?'ok':'';$('stxt').textContent=t}
function send(bytes){if(ws&&ws.readyState===1)ws.send(bytes)}
function connect(){
  ws=new WebSocket('ws://'+location.hostname+':81/');
  ws.binaryType='arraybuffer';
  ws.onopen=()=>{setStatus(true,'connected');send(new Uint8Array([0x12]));send(new Uint8Array([0x14,soundOn&&started?1:0]));send(new Uint8Array([0x15,joyPort]))};
  ws.onclose=()=>{setStatus(false,noReconnect?'another viewer took over':'reconnecting…');ws=null;if(!noReconnect)setTimeout(connect,1500)};
  ws.onerror=()=>{};
  ws.onmessage=e=>{
    const u8=new Uint8Array(e.data);
    switch(u8[0]){
      case 1:applyTiles(u8,px,PAL,W);break;
      case 3:ctx.putImageData(img,0,0);send(new Uint8Array([0x13]));frames++;break;
      case 2:playAudio(u8);break;
      case 4:noReconnect=true;setStatus(false,new TextDecoder().decode(u8.subarray(1)));break;
    }
  };
}
setInterval(()=>{const t=performance.now();if(ws&&ws.readyState===1)setStatus(true,'connected · '+Math.round(frames*1000/(t-lastFpsT))+' fps');frames=0;lastFpsT=t},2000);

// ---------------- sound (scheduled buffers: works on plain http and on iPhone)
let ac=null,nextT=0,RATE=22050;
function startAudio(){
  if(!ac){try{if(navigator.audioSession)navigator.audioSession.type='playback'}catch(e){}
    ac=new (window.AudioContext||window.webkitAudioContext)();}
  ac.resume();
}
function playAudio(u8){
  if(!ac||!soundOn)return;
  const n=(u8.length-1)>>1;if(!n)return;
  const dv=new DataView(u8.buffer,u8.byteOffset+1,n*2);
  const buf=ac.createBuffer(1,n,RATE),ch=buf.getChannelData(0);
  for(let i=0;i<n;i++)ch[i]=dv.getInt16(i*2,true)/32768;
  const now=ac.currentTime;
  if(nextT<now+0.02)nextT=now+0.09;        // underrun: rebuild a small cushion
  if(nextT>now+0.45)return;                // too far behind: drop to catch up
  const s=ac.createBufferSource();s.buffer=buf;s.connect(ac.destination);s.start(nextT);nextT+=buf.duration;
}
function setSound(on){soundOn=on;$('bSound').classList.toggle('on',on);$('bSound').firstChild.textContent=on?'\u{1F50A}':'\u{1F508}';
  if(on&&started)startAudio();send(new Uint8Array([0x14,on&&started?1:0]))}

// ---------------- keys (X11 keysyms, same table the board understands)
function key(down,sym){const b=new Uint8Array(6);b[0]=0x10;b[1]=down?1:0;b[2]=sym&255;b[3]=(sym>>8)&255;b[4]=(sym>>16)&255;b[5]=(sym>>>24)&255;send(b)}
const NAMED={Enter:0xff0d,Backspace:0xff08,Delete:0xffff,Insert:0xff63,Home:0xff50,ArrowLeft:0xff51,ArrowUp:0xff52,ArrowRight:0xff53,ArrowDown:0xff54,
 PageUp:0xff55,Escape:0xff1b,Tab:0xff09,Alt:0xffe9,AltGraph:0xffea,Meta:0xffeb,OS:0xffeb,F1:0xffbe,F2:0xffbf,F3:0xffc0,F4:0xffc1,F5:0xffc2,F6:0xffc3,F7:0xffc4,F8:0xffc5};
const JOYARROWS={ArrowUp:0xffb8,ArrowDown:0xffb2,ArrowLeft:0xffb4,ArrowRight:0xffb6,Space:0xffb0};
function symFor(e){
  if($('arrowsJoy').checked&&JOYARROWS[e.code])return JOYARROWS[e.code];
  if(e.code==='ShiftLeft')return 0xffe1; if(e.code==='ShiftRight')return 0xffe2;
  if(e.code==='ControlLeft')return 0xffe3; if(e.code==='ControlRight')return 0xffe4;
  const m=/^Numpad(\d)$/.exec(e.code); if(m)return 0xffb0+ +m[1];
  if(e.code==='NumpadEnter')return 0xff8d;
  if(NAMED[e.key])return NAMED[e.key];
  if(e.key.length===1){const c=e.key.codePointAt(0);if(c<0x100)return c}
  return 0;
}
const held=new Map();
document.addEventListener('keydown',e=>{
  if(dlg.open)return;
  if((e.ctrlKey||e.metaKey)&&(e.key==='v'||e.key==='V'))return;   // let paste through
  const s=symFor(e);if(!s)return;
  e.preventDefault();if(!started)begin();
  if(e.repeat||held.has(e.code))return;
  held.set(e.code,s);key(true,s);
});
document.addEventListener('keyup',e=>{const s=held.get(e.code);if(s===undefined)return;e.preventDefault();held.delete(e.code);key(false,s)});
window.addEventListener('blur',()=>{for(const s of held.values())key(false,s);held.clear()});
document.addEventListener('paste',e=>{if(dlg.open)return;const t=e.clipboardData.getData('text');if(t){e.preventDefault();typeText(t)}});
function typeText(t){const enc=new TextEncoder().encode(t.replace(/\r\n/g,'\n'));const b=new Uint8Array(enc.length+1);b[0]=0x11;b.set(enc,1);send(b)}

// ---------------- on-screen C64 keyboard (raw matrix keys: 0xC64000 + row*8 + col)
const MX={DEL:[0,0],RETURN:[0,1],'→':[0,2],F7:[0,3],F1:[0,4],F3:[0,5],F5:[0,6],'↓':[0,7],'3':[1,0],W:[1,1],A:[1,2],'4':[1,3],Z:[1,4],S:[1,5],E:[1,6],SHIFT:[1,7],
 '5':[2,0],R:[2,1],D:[2,2],'6':[2,3],C:[2,4],F:[2,5],T:[2,6],X:[2,7],'7':[3,0],Y:[3,1],G:[3,2],'8':[3,3],B:[3,4],H:[3,5],U:[3,6],V:[3,7],
 '9':[4,0],I:[4,1],J:[4,2],'0':[4,3],M:[4,4],K:[4,5],O:[4,6],N:[4,7],'+':[5,0],P:[5,1],L:[5,2],'-':[5,3],'.':[5,4],':':[5,5],'@':[5,6],',':[5,7],
 '£':[6,0],'*':[6,1],';':[6,2],HOME:[6,3],RSHIFT:[6,4],'=':[6,5],'↑':[6,6],'/':[6,7],'1':[7,0],'←':[7,1],CTRL:[7,2],'2':[7,3],SPACE:[7,4],'C=':[7,5],Q:[7,6],STOP:[7,7]};
const KROWS=[
 ['←','1','2','3','4','5','6','7','8','9','0','+','-','£','HOME','DEL'],
 ['CTRL','Q','W','E','R','T','Y','U','I','O','P','@','*','↑','RESTORE'],
 ['STOP','A','S','D','F','G','H','J','K','L',':',';','=','RETURN'],
 ['C=','SHIFT','Z','X','C','V','B','N','M',',','.','/','↓','→'],
 ['F1','F3','F5','F7','SPACE','RSHIFT']];
const MODS=new Set(['SHIFT','RSHIFT','C=','CTRL']);
const latched=new Set();
function msym(l){if(l==='RESTORE')return 0xff55;const p=MX[l];return 0xC64000+p[0]*8+p[1]}
function buildKbd(){
  const k=$('kbd');
  KROWS.forEach(row=>{const r=document.createElement('div');r.className='r';
    row.forEach(l=>{const b=document.createElement('button');b.className='k';b.textContent=l==='STOP'?'RUN/STOP':l==='RSHIFT'?'SHIFT':l;
      if(l==='SPACE')b.classList.add('w8');else if(l==='RETURN'||l==='RESTORE'||l==='HOME'||l==='STOP')b.classList.add('w15');
      if(/^F\d$/.test(l))b.classList.add('f');if(MODS.has(l))b.classList.add('m');
      const s=msym(l);
      if(MODS.has(l)){
        b.onpointerdown=e=>{e.preventDefault();if(!started)begin();
          if(latched.has(l)){latched.delete(l);b.classList.remove('lat');key(false,s)}else{latched.add(l);b.classList.add('lat');key(true,s)}};
      }else{
        let down=false;
        const up=()=>{if(!down)return;down=false;b.classList.remove('dn');key(false,s);
          setTimeout(()=>{for(const m of latched){key(false,msym(m))}latched.clear();document.querySelectorAll('.k.lat').forEach(x=>x.classList.remove('lat'))},60)};
        b.onpointerdown=e=>{e.preventDefault();if(!started)begin();b.setPointerCapture(e.pointerId);down=true;b.classList.add('dn');key(true,s)};
        b.onpointerup=up;b.onpointercancel=up;
      }
      r.appendChild(b)});
    k.appendChild(r)});
}
buildKbd();

// ---------------- on-screen joystick (keypad keysyms: 8 up, 2 down, 4 left, 6 right, 0 fire)
const JU=0xffb8,JD=0xffb2,JL=0xffb4,JR=0xffb6,JF=0xffb0;
let jdirs=new Set();
function setDirs(n){for(const d of jdirs)if(!n.has(d))key(false,d);for(const d of n)if(!jdirs.has(d))key(true,d);jdirs=n}
const pad=$('pad');
function padMove(e){const r=pad.getBoundingClientRect(),dx=e.clientX-r.left-r.width/2,dy=e.clientY-r.top-r.height/2,dead=r.width*0.14,n=new Set();
  if(Math.hypot(dx,dy)>dead){const a=Math.atan2(dy,dx)*180/Math.PI;
    if(a>-157.5&&a<-22.5)n.add(JU);if(a>22.5&&a<157.5)n.add(JD);if(a>112.5||a<-112.5)n.add(JL);if(a>-67.5&&a<67.5)n.add(JR);}
  const lim=r.width*0.3,k=Math.min(1,lim/Math.max(1,Math.hypot(dx,dy)));pad.style.setProperty('--dx',dx*k+'px');pad.style.setProperty('--dy',dy*k+'px');setDirs(n)}
pad.onpointerdown=e=>{e.preventDefault();if(!started)begin();pad.setPointerCapture(e.pointerId);padMove(e)};
pad.onpointermove=e=>{if(pad.hasPointerCapture(e.pointerId))padMove(e)};
pad.onpointerup=pad.onpointercancel=()=>{setDirs(new Set());pad.style.setProperty('--dx','0px');pad.style.setProperty('--dy','0px')};
const fire=$('fire');
fire.onpointerdown=e=>{e.preventDefault();if(!started)begin();fire.setPointerCapture(e.pointerId);fire.classList.add('dn');key(true,JF)};
fire.onpointerup=fire.onpointercancel=()=>{fire.classList.remove('dn');key(false,JF)};

// ---------------- toolbar
function toggle(btn,el,disp){const on=el.style.display!==disp;el.style.display=on?disp:'none';btn.classList.toggle('on',on);fit();try{localStorage.setItem(el.id,on?1:0)}catch(e){}}
$('bSound').onclick=()=>setSound(!soundOn);
$('bKbd').onclick=()=>toggle($('bKbd'),$('kbd'),'block');
$('bJoy').onclick=()=>toggle($('bJoy'),$('joy'),'flex');
$('bPort').onclick=()=>{joyPort=joyPort===2?1:2;$('bPort').textContent='Port '+joyPort;send(new Uint8Array([0x15,joyPort]));try{localStorage.setItem('port',joyPort)}catch(e){}};
$('bPaste').onclick=()=>{dlg.showModal();$('txt').focus()};
$('bTypeGo').onclick=()=>{typeText($('txt').value);dlg.close()};
$('bReset').onclick=()=>{if(confirm('Reset the C64?'))fetch('/api/reset',{method:'POST'})};
$('bFull').onclick=()=>{const d=document.documentElement;if(!document.fullscreenElement&&d.requestFullscreen)d.requestFullscreen().catch(()=>{});else if(document.exitFullscreen)document.exitFullscreen()};
const dlg=$('dlg');
function begin(){if(started)return;started=true;$('overlay').style.display='none';if(soundOn)startAudio();send(new Uint8Array([0x14,soundOn?1:0]))}
$('overlay').onclick=begin;

// ---------------- layout
function fit(){const st=$('stage'),aw=st.clientWidth-12,ah=st.clientHeight-12;let w=aw,h=w*H/W;if(h>ah){h=ah;w=h*W/H}
  if(w>=W*2&&h>=H*2){const s=Math.floor(Math.min(aw/W,ah/H)*2)/2;w=W*s;h=H*s}  // tidy scale on big screens
  cv.style.width=Math.max(120,w)+'px';cv.style.height=Math.max(85,h)+'px'}
window.addEventListener('resize',fit);
const touch=matchMedia('(pointer:coarse)').matches;
let pref=k=>{try{return localStorage.getItem(k)}catch(e){return null}};
if(pref('kbd')==='1'||(pref('kbd')===null&&touch))toggle($('bKbd'),$('kbd'),'block');
if(pref('joy')==='1')toggle($('bJoy'),$('joy'),'flex');
if(pref('port')==='1'){joyPort=1;$('bPort').textContent='Port 1'}
setSound(true);fit();connect();
</script></body></html>)HTML";
