// Bench page served at http://<cam-ip>/ : test the camera tasks without the S3.
// It refreshes its settings every second (POST /bench); the camera drops them
// BENCH_KEEPALIVE_MS after the page closes, and ignores them while the S3 talks.
#pragma once

static const char kBenchPage[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Eco eco camera</title>
<style>
:root{--bg:#000;--panel:#121008;--line:#3a3010;--fg:#f4e9c8;--dim:#a89a70;--y:#ffc400;--ok:#4ade80;--bad:#f87171}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.4 system-ui,sans-serif}
header{display:flex;gap:12px;align-items:center;padding:10px 16px;background:linear-gradient(90deg,#000,#2a2000);border-bottom:2px solid var(--y)}
h1{margin:0;font-size:18px;color:var(--y);letter-spacing:.08em}#src{padding:2px 10px;border-radius:99px;border:1px solid var(--y);font-weight:600}
main{display:grid;grid-template-columns:minmax(0,1.6fr) minmax(260px,1fr);gap:12px;padding:12px 16px;max-width:1200px;margin:auto}
@media(max-width:760px){main{grid-template-columns:1fr}}
section{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:12px}
h2{margin:0 0 8px;font-size:13px;color:var(--y);text-transform:uppercase;letter-spacing:.1em}
#view{width:100%;aspect-ratio:4/3;background:#050505;border-radius:6px;display:block;object-fit:contain}
.row{display:flex;gap:8px;flex-wrap:wrap;margin:6px 0}
button{background:#1c1704;color:var(--fg);border:1px solid var(--line);border-radius:6px;padding:7px 12px;font:inherit;cursor:pointer}
button.on{background:var(--y);color:#000;border-color:var(--y);font-weight:700}button:disabled{opacity:.4;cursor:default}
dl{display:grid;grid-template-columns:auto 1fr;gap:3px 12px;margin:6px 0 0}dt{color:var(--dim)}dd{margin:0;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
.ok{color:var(--ok)}.bad{color:var(--bad)}input[type=range]{flex:1;accent-color:var(--y)}small{color:var(--dim)}a{color:var(--y)}
</style></head><body>
<header><h1>ECO ECO · CAMERA BENCH</h1><span id="src">…</span><small id="note"></small></header>
<main>
<section><h2>Camera</h2><img id="view" alt="camera stream">
<small>One stream viewer at a time. <a href="/gray.jpg" target="_blank">/gray.jpg</a> (what the QR decoder sees) · <a href="/status" target="_blank">/status</a></small></section>
<div>
<section><h2>QR</h2><div class="row"><button id="qr">QR decode: off</button></div>
<dl><dt>text</dt><dd id="qrText">—</dd><dt>IPv4</dt><dd id="qrIp">—</dd><dt>seen</dt><dd id="qrAge">—</dd><dt>decoded / tries</dt><dd id="qrCount">—</dd></dl></section>
<section style="margin-top:12px"><h2>Motion</h2><div class="row"><button id="mot">Motion detect: off</button></div>
<dl><dt>target</dt><dd id="motDet">—</dd><dt>position cx</dt><dd id="motCx">—</dd><dt>changed area</dt><dd id="motArea">—</dd></dl></section>
<section style="margin-top:12px"><h2>Pan servo</h2><div class="row" id="pm">
<button data-pm="hold">hold</button><button data-pm="goto">goto</button><button data-pm="scan">scan</button><button data-pm="track">track</button></div>
<div class="row"><input id="ang" type="range" min="45" max="135" step="1" value="90"><output id="angOut">90°</output></div>
<dl><dt>state</dt><dd id="panState">—</dd><dt>angle / target</dt><dd id="panAng">—</dd><dt>fps</dt><dd id="fps">—</dd></dl></section>
</div></main>
<script>
const $=id=>document.getElementById(id);
const st={qr:false,mot:false,pm:'hold',ang:90};let s3=false;
function send(){if(s3)return;fetch(`/bench?qr=${st.qr?1:0}&mot=${st.mot?1:0}&pm=${st.pm}&ang=${st.ang}`,{method:'POST'}).catch(()=>{});}
function paint(){$('qr').textContent=`QR decode: ${st.qr?'on':'off'}`;$('qr').classList.toggle('on',st.qr);
$('mot').textContent=`Motion detect: ${st.mot?'on':'off'}`;$('mot').classList.toggle('on',st.mot);
for(const b of document.querySelectorAll('#pm button'))b.classList.toggle('on',b.dataset.pm===st.pm);
$('angOut').textContent=`${st.ang}°`;for(const b of document.querySelectorAll('button,input'))b.disabled=s3;}
$('qr').onclick=()=>{st.qr=!st.qr;paint();send();};$('mot').onclick=()=>{st.mot=!st.mot;paint();send();};
for(const b of document.querySelectorAll('#pm button'))b.onclick=()=>{st.pm=b.dataset.pm;paint();send();};
$('ang').oninput=()=>{st.ang=Number($('ang').value);st.pm='goto';paint();send();};
const put=(id,t,c)=>{const e=$(id);e.textContent=t;e.className=c||'';};
async function poll(){try{const d=await(await fetch('/status',{cache:'no-store'})).json();
s3=d.link.source==='s3';put('src',s3?'S3 in charge':(d.link.source==='bench'?'bench mode':'idle'));
$('note').textContent=s3?'the S3 is connected: use the rover dashboard':(d.camera_ok?'':'camera failed: '+d.camera_error);
const q=d.qr;put('qrText',q.last_text||'—');put('qrIp',q.last_ip||(q.last_err?'rejected: '+q.last_err:'—'),q.last_ip?'ok':(q.last_err?'bad':''));
put('qrAge',q.age_ms==null?'—':`${(q.age_ms/1000).toFixed(1)} s ago`);put('qrCount',`${q.decoded} / ${q.attempts}`);
const m=d.motion;put('motDet',m.detected?'yes':'no',m.detected?'ok':'');put('motCx',m.detected?m.cx.toFixed(2):'—');put('motArea',`${(m.area*100).toFixed(1)} %`);
const p=d.pan;put('panState',p.fitted?`${p.mode} · ${p.state}${p.tracking?' · tracking':''}`:'not fitted');
put('panAng',`${p.angle.toFixed(0)}° / ${p.target.toFixed(0)}°`);put('fps',d.fps.toFixed(1));paint();}
catch(e){put('src','camera not answering','bad');}setTimeout(poll,500);}
$('view').src=`http://${location.hostname}:81/stream`;setInterval(send,1000);paint();poll();
</script></body></html>)HTML";
