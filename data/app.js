'use strict';
// Eco eco rover dashboard. Talks to the S3 main controller:
//   WS   /ws      telemetry (status + sensors) every ~75 ms, events, acks, pong
//   HTTP /api/*   mode, mission, E-stop, servo and task commands
// The firmware enforces every safety rule (AUTO lock, teleop timeout, E-stop,
// reverse interlock). This page mirrors them so the operator sees why a
// control is disabled; it never relies on itself for safety.

const $ = (id) => document.getElementById(id);

// ---------------------------------------------------------------- rover address
const params = new URLSearchParams(location.search);
const servedByRover = location.protocol === 'http:' &&
  !['localhost', '127.0.0.1', '[::1]', ''].includes(location.hostname);

function storageGet(key, fallback) {
  try { return localStorage.getItem(key) ?? fallback; } catch { return fallback; }
}
function storageSet(key, value) {
  try { localStorage.setItem(key, value); } catch { /* private mode: not remembered */ }
}

let host = params.get('rover') || (servedByRover ? location.host : storageGet('roverHost', ''));
const api = (path) => `http://${host}${path}`;

// ---------------------------------------------------------------- state
let tel = null;                // last {status, sensors}
let ws = null;
let wsRetryMs = 1000;
let pollTimer = null;
let lastTelemetryAt = 0;
let rttMs = null;
const arrivals = [];           // performance.now() of each telemetry frame, last 60 s
let selectedMission = storageGet('mission', 'proximity');
let seenMode = null;
let modeSeenAt = null;
let toastTimer = null;

// ---------------------------------------------------------------- helpers
function put(id, text, tone = '') {
  const e = $(id);
  if (!e) return;
  const t = String(text);
  if (e.textContent !== t) e.textContent = t;
  for (const c of ['good', 'warnc', 'badc', 'dim']) e.classList.toggle(c, c === tone);
}

function toast(msg, isErr = false) {
  const t = $('toast');
  t.textContent = msg;
  t.classList.toggle('err', isErr);
  t.classList.remove('hidden');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.add('hidden'), isErr ? 4000 : 2500);
}

function clock(ms) {
  if (ms == null) return '—';
  const s = ms / 1000;
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = (s % 60).toFixed(1).padStart(4, '0');
  return h ? `${h}:${String(m).padStart(2, '0')}:${sec}` : `${String(m).padStart(2, '0')}:${sec}`;
}

function ago(ms) {
  if (ms == null) return '—';
  if (ms < 10000) return `${Math.max(0, Math.round(ms))} ms ago`;
  if (ms < 120000) return `${Math.round(ms / 1000)} s ago`;
  return `${Math.round(ms / 60000)} min ago`;
}

// Rover timestamps are ms since rover boot; age is relative to the same snapshot.
function stamp(tMs) {
  if (tMs == null || !tel) return '—';
  return `T+${clock(tMs)} · ${ago(tel.status.uptime_ms - tMs)}`;
}

function setBar(id, v) {
  const e = $(id);
  if (!e) return;
  const x = Math.max(-1, Math.min(1, v || 0));
  e.style.left = x >= 0 ? '50%' : `${50 + x * 50}%`;
  e.style.width = `${Math.abs(x) * 50}%`;
}

function median(values) {
  if (!values.length) return null;
  const s = [...values].sort((a, b) => a - b);
  const mid = s.length >> 1;
  return s.length % 2 ? s[mid] : (s[mid - 1] + s[mid]) / 2;
}

function enable(id, on) {
  const e = $(id);
  if (e) e.disabled = !on;
}

// ---------------------------------------------------------------- command log
const cmdItems = [];
function logCmd(name, args, ok, note) {
  // Repeats of one command (a dragged pan slider) stay one line.
  if (cmdItems[0] && cmdItems[0].name === name) cmdItems.shift();
  cmdItems.unshift({ at: new Date().toLocaleTimeString([], { hour12: false }), name, args, ok, note });
  cmdItems.length = Math.min(cmdItems.length, 5);
  const ol = $('cmdLog');
  ol.innerHTML = '';
  for (const c of cmdItems) {
    const li = document.createElement('li');
    li.title = c.note || '';
    const t = document.createElement('span');
    t.className = 't';
    t.textContent = c.at;
    const n = document.createElement('span');
    n.className = 'name';
    n.textContent = c.args ? `${c.name} ${c.args}` : c.name;
    const res = document.createElement('span');
    res.className = `res ${c.ok ? 'good' : 'badc'}`;
    res.textContent = c.ok ? 'OK' : 'REFUSED';
    li.append(t, n, res);
    ol.append(li);
  }
}

// ---------------------------------------------------------------- commands
async function post(path, body, quiet = false) {
  if (!host) {
    toast('set the rover address first', true);
    return { ok: false };
  }
  const name = path.replace('/api/', '');
  const args = Object.values(body || {}).join(' ');
  const log = (ok, note) => { if (name !== 'drive') logCmd(name, args, ok, note); };
  try {
    const r = await fetch(api(path), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body || {}),
      cache: 'no-store',
    });
    const j = await r.json().catch(() => ({ ok: false, error: `HTTP ${r.status}` }));
    log(j.ok, j.ok ? j.message : j.error);
    if (!quiet) toast(j.ok ? j.message : j.error, !j.ok);
    return j;
  } catch (e) {
    log(false, e.message);
    if (!quiet) toast(`rover unreachable (${e.message})`, true);
    return { ok: false };
  }
}

function estop(source) {
  // Both paths on purpose: whichever arrives first latches the E-stop.
  if (ws && ws.readyState === WebSocket.OPEN) ws.send('{"type":"estop"}');
  if (host) {
    fetch(api('/api/estop'), { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{}' })
      .catch(() => {});
  }
  disarmGamepad(null);
  clearManual();
  logCmd('E-STOP', source, true, 'sent over WebSocket and HTTP');
  toast(`E-STOP sent (${source})`, true);
}

// ---------------------------------------------------------------- link
function connect() {
  if (ws) { ws.onclose = null; ws.close(); ws = null; }
  if (!host) {
    put('linkPill', 'no rover address');
    $('linkPill').className = 'pill bad';
    return;
  }
  for (const id of ['apiStatus', 'apiSensors', 'apiEvents']) {
    const a = $(id);
    a.href = api(a.textContent);
  }
  try {
    ws = new WebSocket(`ws://${host}/ws`);
  } catch {
    scheduleReconnect();
    return;
  }
  ws.onopen = () => {
    wsRetryMs = 1000;
    stopPolling();
    toast(`connected to ${host}`);
  };
  ws.onclose = () => {
    ws = null;
    disarmGamepad('link lost');
    clearManual();
    startPolling();
    scheduleReconnect();
  };
  ws.onerror = () => {};  // onclose follows
  ws.onmessage = (ev) => {
    let m;
    try { m = JSON.parse(ev.data); } catch { return; }
    if (m.type === 'telemetry') {
      const now = performance.now();
      arrivals.push(now);
      while (arrivals.length && now - arrivals[0] > 60000) arrivals.shift();
      lastTelemetryAt = now;
      render(m.status, m.sensors);
    } else if (m.type === 'events') {
      renderEvents(m.items || []);
    } else if (m.type === 'ack') {
      if (!m.ok) toast(`${m.cmd}: ${m.error}`, true);
    } else if (m.type === 'pong') {
      rttMs = performance.now() - m.c;
    }
  };
}

function scheduleReconnect() {
  setTimeout(connect, wsRetryMs);
  wsRetryMs = Math.min(wsRetryMs * 2, 5000);
}

// HTTP fallback so the page still shows the rover while the socket reconnects.
function startPolling() {
  if (pollTimer || !host) return;
  pollTimer = setInterval(async () => {
    try {
      const [s, n] = await Promise.all([
        fetch(api('/api/status'), { cache: 'no-store' }).then((r) => r.json()),
        fetch(api('/api/sensors'), { cache: 'no-store' }).then((r) => r.json()),
      ]);
      if (s.ok && n.ok) {
        lastTelemetryAt = performance.now();
        render(s, n);
      }
    } catch { /* rover unreachable: the link pill says so */ }
  }, 1000);
}
function stopPolling() {
  clearInterval(pollTimer);
  pollTimer = null;
}

// ---------------------------------------------------------------- render
const MISSIONS = ['traverse', 'proximity', 'qr_ping', 'motion'];

function render(st, se) {
  tel = { status: st, sensors: se };
  const teleop = isTeleop();
  const auto = st.mode === 'auto';
  const running = st.state === 'AUTO_RUNNING' || st.state === 'AUTO_BLOCKED';
  const es = st.estop;

  // top bar: the status strip turns gold in AUTO and red on E-stop
  put('modePill', auto ? `AUTO · ${st.mission}` : st.mode.toUpperCase());
  $('modePill').className = `pill ${auto ? 'auto' : es.active ? 'bad' : teleop ? 'ok' : ''}`;
  $('strip').classList.toggle('auto', auto && !es.active);
  $('strip').classList.toggle('alarm', es.active);
  $('estopBtn').classList.toggle('latched', es.active);
  const stateTone = { ESTOP: 'badc', SAFE_STOP: 'badc', AUTO_FAILED: 'warnc', AUTO_BLOCKED: 'warnc',
    AUTO_COMPLETE: 'good', TELEOP: 'good' }[st.state] || '';
  put('stateBig', st.state.replace('_', ' '), stateTone);
  put('missionChip', `mission ${st.mission}`);
  let detail = st.detail;
  if (es.active) {
    detail = es.hw_pressed
      ? 'E-STOP: twist the red mushroom button to release it, then RESET E-STOP'
      : `E-STOP (${es.source}): press RESET E-STOP when safe`;
  } else if (auto) {
    detail = `CONTROLS LOCKED · ${st.detail}`;
  }
  put('stateDetail', detail);
  $('stateDetail').title = st.detail;

  // control card
  put('modeVal', st.mode);
  if (st.mode !== seenMode) {
    if (seenMode !== null) modeSeenAt = new Date();
    seenMode = st.mode;
  }
  const wall = modeSeenAt ? ` · ${modeSeenAt.toLocaleTimeString()}` : '';
  put('modeChanged', `${stamp(st.mode_changed_ms)}${wall}`);
  put('stateChanged', stamp(st.state_changed_ms));
  put('controlsVal', st.controls_locked ? 'LOCKED' : teleop ? 'active' : 'off',
    st.controls_locked ? 'warnc' : teleop ? 'good' : 'dim');

  // mode buttons
  for (const b of document.querySelectorAll('[data-mode]')) {
    b.classList.toggle('active', st.mode === b.dataset.mode);
    b.disabled = es.active || (b.dataset.mode === 'teleop' && st.state === 'SAFE_STOP');
  }
  const shownMission = auto ? st.mission : selectedMission;
  for (const b of document.querySelectorAll('[data-mission]')) {
    b.classList.toggle('on', b.dataset.mission === shownMission);
    b.setAttribute('aria-checked', b.dataset.mission === shownMission);
    b.disabled = running;
  }
  enable('armAutoBtn', !es.active && st.state !== 'SAFE_STOP' && !running);
  enable('startBtn', auto && ['AUTO_READY', 'AUTO_COMPLETE', 'AUTO_FAILED'].includes(st.state));
  enable('abortBtn', running);
  enable('resetEstopBtn', es.reset_allowed);
  enable('ackBtn', st.state === 'SAFE_STOP');

  // drive
  put('driveLock', teleop ? 'teleop' : auto ? 'locked · auto' : 'locked');
  $('driveLock').classList.toggle('on', teleop);
  for (const b of document.querySelectorAll('.pad-btn')) b.disabled = !teleop;
  const m = st.motors;
  setBar('outL', m.left);
  setBar('outR', m.right);
  put('outLv', m.left.toFixed(2));
  put('outRv', m.right.toFixed(2));
  put('teleopMax', `${Math.round(m.teleop_max * 100)} %`);

  // power + E-stop
  put('esActive', es.active ? 'ACTIVE' : 'released', es.active ? 'badc' : 'good');
  put('esSource', es.source);
  put('esHw', es.hw_sense === 'not_fitted' ? 'sense wire not fitted' : es.hw_pressed ? 'PRESSED' : 'released',
    es.hw_pressed ? 'badc' : '');
  const b = st.battery;
  put('batt', !b.wired ? 'not wired' : b.v == null ? 'no reading' : `${b.v.toFixed(2)} V${b.pct != null ? ` · ${b.pct} %` : ''}${b.low ? ' · LOW' : ''}`,
    !b.wired ? 'dim' : b.low ? 'badc' : 'good');
  const mb = st.motor_bus;
  put('bus', !mb.wired ? 'not wired' : `${mb.v.toFixed(2)} V · ${mb.present ? 'present' : 'OFF'}`, !mb.wired ? 'dim' : mb.present ? 'good' : 'warnc');
  put('armed', m.armed ? 'ARMED' : 'disabled', m.armed ? 'warnc' : 'dim');
  put('driver', m.driver);

  // connection
  const c = st.connection;
  put('wifi', `${c.wifi}${c.ssid ? ` · ${c.ssid}` : ''}`, c.wifi === 'ap' ? 'warnc' : '');
  put('ip', c.ip);
  $('ip').title = c.hostname;
  put('rssi', c.rssi == null ? '—' : `${c.rssi} dBm`);
  put('uptime', clock(st.uptime_ms));
  put('wsClients', c.ws_clients);
  put('fw', st.fw);
  put('heap', `${Math.round(st.free_heap / 1024)} KB`);
  put('pinmap', st.pinmap, st.pinmap === 'PROVISIONAL' ? 'warnc' : 'good');

  renderSensors(se, teleop, auto, es.active);
}

let sonarBuilt = false;
const SONAR_LABEL = { front_center: 'Front C', front_left: 'Front L', front_right: 'Front R', rear: 'Rear' };
function renderSensors(se, teleop, auto, estopActive) {
  // sonar tiles
  const tiles = $('sonarTiles');
  if (!sonarBuilt) {
    tiles.innerHTML = se.sonar.map((s, i) => `
      <div class="tile" id="sonar${i}" title="${s.id}">
        <div class="name">${SONAR_LABEL[s.id] || s.id}</div>
        <div class="mm" id="sonarMm${i}">—</div>
        <div class="st" id="sonarSt${i}">—</div>
        <div class="dist"><i id="sonarBar${i}"></i></div>
      </div>`).join('');
    sonarBuilt = true;
  }
  se.sonar.forEach((s, i) => {
    $(`sonar${i}`).classList.toggle('off', !s.fitted);
    const mmEl = $(`sonarMm${i}`);
    const html = !s.fitted ? 'not fitted' : s.mm != null ? `${s.mm}<small> mm</small>` : s.status.replace(/_/g, ' ');
    if (mmEl.innerHTML !== html) mmEl.innerHTML = html;
    mmEl.classList.toggle('na', s.mm == null);
    put(`sonarSt${i}`, s.fitted ? (s.age_ms != null ? `${s.age_ms} ms old` : 'no data') : '—');
    $(`sonarBar${i}`).style.width = s.mm != null ? `${Math.min(100, s.mm / 10)}%` : '0';
  });
  const p = se.proximity;
  put('proxFront', p.front_mm != null ? `${p.front_mm} mm` : '—');
  put('proxWindow', p.in_window == null ? '—' : p.in_window ? 'IN WINDOW' : 'outside', p.in_window ? 'good' : 'dim');
  put('proxT', stamp(p.t_ms || null));
  const ri = se.rear_interlock;
  put('revAllowed', ri.reverse_allowed ? 'allowed' : `blocked, ${ri.reason.replace('rear ', '')}`, ri.reverse_allowed ? 'good' : 'warnc');

  // camera
  const cam = se.camera;
  put('camLink', `link ${cam.link}`, cam.link === 'ok' ? 'good' : 'badc');
  put('camSensor', cam.sensor ? `${cam.sensor} (${cam.pid})` : '—');
  put('camLinkVal', cam.link, cam.link === 'ok' ? 'good' : 'badc');
  put('camAge', cam.hb_age_ms != null ? `${cam.hb_age_ms} ms` : '—');
  put('camRx', `${cam.rx_ok} ok · ${cam.rx_bad} bad`);
  put('camIp', cam.ip || '—');
  put('camFps', `${cam.fps.toFixed(1)} fps · hb ${cam.hb_age_ms ?? '—'} ms`);
  // The video comes straight from the camera over Wi-Fi; the UART link only told
  // us its address. Keep the picture through link blips (stream_url stays set).
  setStream(cam.stream_url || null);

  // The pan servo is wired to the camera board, so it needs the camera link.
  const pan = se.pan;
  const panWhy = cam.link === 'ok' ? 'no servo on camera' : 'camera link down';
  put('panState', pan.present ? `${pan.mode} · ${pan.state}${pan.tracking ? ' · tracking' : ''}` : panWhy,
    pan.present ? '' : 'warnc');
  put('panAngle', pan.present ? `${pan.angle.toFixed(0)}° / ${pan.target.toFixed(0)}°` : '—');
  const slider = $('panSlider');
  slider.min = pan.soft_min;
  slider.max = pan.soft_max;
  if (!panDragging && pan.present) {
    slider.value = pan.target;
    put('panOut', `${Math.round(pan.target)}°`);
  }
  const panOk = pan.present && !auto && !estopActive;
  for (const id of ['panSlider', 'panLeftBtn', 'panCenterBtn', 'panRightBtn']) enable(id, panOk);

  // QR + ping
  const q = se.qr;
  put('qrText', q.text || '—');
  put('qrIp', q.ip || '—');
  put('qrValid', q.t_ms == null ? '—' : q.valid ? 'valid IPv4' : `rejected: ${q.error || 'invalid'}`, q.valid ? 'good' : q.t_ms == null ? '' : 'warnc');
  put('qrT', stamp(q.t_ms));
  put('qrLast', q.last_valid_ip || '—');
  put('qrArmed', q.requested ? (q.camera_armed ? 'scanning…' : 'requested, camera not armed') : 'idle', q.requested ? 'warnc' : 'dim');
  const pg = se.ping;
  const pingTone = { reply: 'good', timeout: 'warnc', error: 'badc', running: 'warnc' }[pg.state] || 'dim';
  put('pingState', pg.state + (pg.error ? ` · ${pg.error}` : ''), pingTone);
  put('pingTarget', pg.target || '—');
  put('pingCount', `${pg.sent} / ${pg.received}`);
  put('pingRtt', pg.rtt_ms != null ? `${pg.rtt_ms} ms` : '—');
  put('pingT', stamp(pg.t_ms));
  enable('qrBtn', teleop && cam.link === 'ok' && !q.requested && pg.state !== 'running');

  // motion
  const mo = se.motion;
  put('motEnabled', mo.enabled ? (mo.camera_armed ? 'on' : 'waiting') : 'off', mo.enabled ? 'good' : 'dim');
  put('motDetected', mo.detected ? `yes ${mo.cx.toFixed(2)}` : 'no', mo.detected ? 'good' : 'dim');
  put('motTracking', mo.tracking ? 'yes' : 'no', mo.tracking ? 'good' : 'dim');
  put('motArea', `${(mo.area * 100).toFixed(1)} %`);
  put('motT', stamp(mo.t_ms));
  const dot = $('motDot');
  dot.style.left = `${((mo.cx + 1) / 2) * 100}%`;
  dot.classList.toggle('live', mo.detected);
  const frac = Math.min(1, mo.tracked_ms / mo.goal_ms);
  $('motProg').style.width = `${frac * 100}%`;
  put('motProgTxt', `${(mo.tracked_ms / 1000).toFixed(1)} / ${(mo.goal_ms / 1000).toFixed(1)} s`);
  enable('motOnBtn', teleop && cam.link === 'ok');
  enable('motOffBtn', teleop);

  // audio
  const au = se.audio;
  put('auOnline', au.online ? `online · ${au.ip}` : 'offline', au.online ? 'good' : 'dim');
  $('auLevel').style.width = `${au.online ? au.mic_level : 0}%`;
  put('auClients', au.online ? au.clients : '—');
  put('auSpk', au.online ? (au.speaker_active ? 'playing' : 'idle') : '—');
  put('auT', stamp(au.t_ms));
  enable('listenBtn', au.online);
  enable('talkBtn', au.online);
}

function renderEvents(items) {
  const ol = $('events');
  ol.innerHTML = '';
  for (const e of [...items].reverse()) {
    const li = document.createElement('li');
    const t = document.createElement('span');
    t.className = 't';
    t.textContent = clock(e.t_ms);
    const lv = document.createElement('span');
    lv.className = `lv ${e.level}`;
    lv.textContent = e.level.toUpperCase();
    const tx = document.createElement('span');
    tx.textContent = e.text;  // textContent: event text can contain QR payloads
    li.append(t, lv, tx);
    ol.append(li);
  }
}

// ---------------------------------------------------------------- camera stream
let streamUrl = null;
let streamRetry = null;
function setStream(url) {
  if (url === streamUrl) return;
  streamUrl = url;
  const img = $('camImg');
  clearTimeout(streamRetry);
  if (!url) {
    img.removeAttribute('src');
    put('camOverlay', 'camera link down');
    return;
  }
  put('camOverlay', 'connecting to stream…');
  img.src = `${url}?t=${Date.now()}`;
}
$('camImg').addEventListener('error', () => {
  if (!streamUrl) return;
  put('camOverlay', 'stream unavailable, retrying… (only one viewer at a time)');
  clearTimeout(streamRetry);
  streamRetry = setTimeout(() => { if (streamUrl) $('camImg').src = `${streamUrl}?t=${Date.now()}`; }, 3000);
});

// ---------------------------------------------------------------- drive input
function isTeleop() {
  return !!tel && tel.status.mode === 'teleop' && tel.status.state === 'TELEOP' && !tel.status.estop.active;
}
const maxCmd = () => Number($('speed').value) / 100;

// On-screen pad and keyboard: [throttle, turn-left] while held.
let padVec = null;
const keys = new Set();
function clearManual() {
  padVec = null;
  keys.clear();
  for (const b of document.querySelectorAll('.pad-btn.held')) b.classList.remove('held');
}

function mix(y, w) {
  let l = y - w;
  let r = y + w;
  const k = Math.max(1, Math.abs(l), Math.abs(r));
  return [(l / k) * maxCmd(), (r / k) * maxCmd()];
}

function manualCommand() {
  if (padVec) return mix(padVec[0], padVec[1]);
  if (!keys.size) return null;
  let y = 0;
  let w = 0;
  if (keys.has('w') || keys.has('arrowup')) y += 1;
  if (keys.has('s') || keys.has('arrowdown')) y -= 1;
  if (keys.has('a') || keys.has('arrowleft')) w += 1;
  if (keys.has('d') || keys.has('arrowright')) w -= 1;
  if (y !== 0) w *= 0.5;  // gentle curve while moving, full spin when standing
  return mix(y, w);
}

for (const b of document.querySelectorAll('.pad-btn')) {
  const release = () => {
    b.classList.remove('held');
    padVec = null;
  };
  b.addEventListener('pointerdown', (e) => {
    if (b.disabled) return;
    e.preventDefault();
    b.setPointerCapture(e.pointerId);
    b.classList.add('held');
    padVec = b.dataset.v.split(',').map(Number);
  });
  for (const ev of ['pointerup', 'pointercancel', 'lostpointercapture']) b.addEventListener(ev, release);
  b.addEventListener('contextmenu', (e) => e.preventDefault());
}

const DRIVE_KEYS = ['w', 'a', 's', 'd', 'arrowup', 'arrowdown', 'arrowleft', 'arrowright'];
window.addEventListener('keydown', (e) => {
  if (e.target.matches('input, textarea')) return;
  const k = e.key.toLowerCase();
  if (k === ' ' || k === 'escape') {
    e.preventDefault();
    estop('keyboard');
    return;
  }
  if (DRIVE_KEYS.includes(k) && isTeleop()) {
    e.preventDefault();
    keys.add(k);
  }
});
window.addEventListener('keyup', (e) => keys.delete(e.key.toLowerCase()));

// ---------------------------------------------------------------- gamepad
const gp = { index: null, armed: false, mode: 'arcade', lastB: false };

window.addEventListener('gamepadconnected', (e) => {
  if (gp.index === null) gp.index = e.gamepad.index;
  toast(`controller connected: ${e.gamepad.id}`);
});
window.addEventListener('gamepaddisconnected', (e) => {
  if (e.gamepad.index !== gp.index) return;
  gp.index = null;
  disarmGamepad('controller disconnected');
});

function readGamepad() {
  const pads = navigator.getGamepads ? navigator.getGamepads() : [];
  let p = gp.index !== null ? pads[gp.index] : null;
  if (!p) {
    p = [...pads].find((x) => x && x.connected) || null;
    if (p) gp.index = p.index;
  }
  return p;
}

function disarmGamepad(reason) {
  if (gp.armed && reason) toast(`gamepad disarmed: ${reason}`, true);
  gp.armed = false;
}

function stickCommand(p) {
  const dz = Number($('deadzone').value);
  const axis = (v) => (Math.abs(v) < dz ? 0 : (Math.sign(v) * (Math.abs(v) - dz)) / (1 - dz));
  if (gp.mode === 'tank') {
    return [axis(-p.axes[1]) * maxCmd(), axis(-p.axes[3]) * maxCmd()];
  }
  let x = p.axes[0];
  let y = -p.axes[1];
  const mag = Math.hypot(x, y);
  if (mag < dz) return [0, 0];
  const s = Math.min(1, (mag - dz) / (1 - dz)) / mag;
  x *= s;
  y *= s;
  // stick right -> left side faster -> rover turns right
  let l = y + x;
  let r = y - x;
  const k = Math.max(1, Math.abs(l), Math.abs(r));
  return [(l / k) * maxCmd(), (r / k) * maxCmd()];
}

// ---------------------------------------------------------------- drive loop (20 Hz)
let sending = false;
function sendDrive(l, r) {
  const left = Math.round(l * 100) / 100;
  const right = Math.round(r * 100) / 100;
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: 'drive', left, right }));
  } else {
    post('/api/drive', { left, right }, true);
  }
}

function driveTick() {
  const teleop = isTeleop();
  const p = readGamepad();
  const standard = !!p && p.mapping === 'standard';

  // B / Circle: software E-stop, whenever a controller is present and this tab has focus.
  const bNow = !!(p && p.buttons[1] && p.buttons[1].pressed);
  if (bNow && !gp.lastB && document.hasFocus()) estop('gamepad');
  gp.lastB = bNow;

  if (gp.armed) {
    if (!p) disarmGamepad('controller disconnected');
    else if (!teleop) disarmGamepad(tel && tel.status.mode === 'auto' ? 'AUTO mode' : 'not in teleoperation');
  }

  const rt = p && p.buttons[7] ? p.buttons[7].value : 0;
  const deadman = gp.armed && rt > 0.5;
  let cmd = null;
  if (deadman) cmd = stickCommand(p);
  if (!cmd && teleop) cmd = manualCommand();
  if (!teleop) cmd = null;

  if (cmd) {
    sendDrive(cmd[0], cmd[1]);
    sending = true;
  } else if (sending) {
    sendDrive(0, 0);  // one explicit stop; the firmware also stops after 300 ms of silence
    sending = false;
  }
  const shown = cmd || [0, 0];
  setBar('cmdL', shown[0]);
  setBar('cmdR', shown[1]);
  put('cmdLv', shown[0].toFixed(2));
  put('cmdRv', shown[1].toFixed(2));

  // gamepad panel
  put('gpName', p ? p.id.slice(0, 60) : 'none — press a button on it');
  put('gpMapping', p ? (standard ? 'standard' : 'NOT standard') : '—', p && !standard ? 'badc' : '');
  $('gpName').title = p ? `${p.id} (mapping: "${p.mapping || 'none'}")` : '';
  put('gpDeadman', p ? (rt > 0.5 ? `held (${rt.toFixed(2)})` : `released (${rt.toFixed(2)})`) : '—', rt > 0.5 ? 'good' : 'dim');
  put('gpState', gp.armed ? (deadman ? 'DRIVING' : 'armed · hold RT/R2') : 'disarmed', gp.armed ? 'good' : 'dim');
  enable('gpEnableBtn', teleop && standard && !gp.armed);
  enable('gpDisableBtn', gp.armed);
}
setInterval(driveTick, 50);

// Leaving the tab or window disarms everything.
window.addEventListener('blur', () => {
  disarmGamepad('dashboard lost focus');
  clearManual();
});
document.addEventListener('visibilitychange', () => {
  if (document.hidden) {
    disarmGamepad('dashboard hidden');
    clearManual();
  }
});

// ---------------------------------------------------------------- UI timer (4 Hz)
setInterval(() => {
  const pill = $('linkPill');
  const now = performance.now();
  const intervals = [];
  for (let i = 1; i < arrivals.length; i++) intervals.push(arrivals[i] - arrivals[i - 1]);
  const med = median(intervals);
  put('pollMedian', med != null ? `${med.toFixed(0)} ms (n=${intervals.length})` : '—', med != null && med <= 100 ? 'good' : med != null ? 'warnc' : '');
  put('rtt', rttMs != null ? `${rttMs.toFixed(0)} ms` : '—');
  const live = ws && ws.readyState === WebSocket.OPEN;
  const stale = now - lastTelemetryAt > 1000;
  put('linkKind', live ? 'WebSocket' : pollTimer ? 'HTTP polling, 1 s' : 'down', live ? 'good' : 'warnc');
  if (live && !stale) {
    put('linkPill', `live · ${med != null ? med.toFixed(0) : '—'} ms`);
    pill.className = 'pill ok';
  } else if (live) {
    put('linkPill', 'stale');
    pill.className = 'pill warn';
    disarmGamepad('telemetry stale');
    clearManual();
  } else if (pollTimer && !stale) {
    put('linkPill', 'polling 1 s');
    pill.className = 'pill warn';
  } else {
    put('linkPill', host ? 'offline' : 'no rover address');
    pill.className = 'pill bad';
  }
  // Hide the overlay once the MJPEG image has produced a frame.
  const img = $('camImg');
  const showing = !!streamUrl && img.naturalWidth > 0;
  $('camOverlay').classList.toggle('hidden', showing);
  img.style.visibility = showing ? 'visible' : 'hidden';  // no broken-image icon
}, 250);

setInterval(() => {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({ type: 'ping', c: Math.round(performance.now()) }));
}, 2000);

// ---------------------------------------------------------------- audio (optional board)
const savedVol = localStorage.getItem('eco_au_vol');
const audio = {
  ctx: null,
  ws: null,
  listening: false,
  talking: false,
  next: 0,
  mic: null,
  nodes: [],
  gainNode: null,
  filterNode: null,
  volume: savedVol !== null ? Math.max(0, Math.min(1, parseFloat(savedVol))) : 0.8
};

function audioCtx() {
  if (!audio.ctx) {
    audio.ctx = new (window.AudioContext || window.webkitAudioContext)();

    // 120 Hz high-pass filter: eliminates DC bias and low-frequency rover motor rumble
    audio.filterNode = audio.ctx.createBiquadFilter();
    audio.filterNode.type = 'highpass';
    audio.filterNode.frequency.value = 120;
    audio.filterNode.Q.value = 0.707;

    // Gain node for listening volume control & talk-ducking
    audio.gainNode = audio.ctx.createGain();
    audio.gainNode.gain.value = audio.volume;

    audio.filterNode.connect(audio.gainNode);
    audio.gainNode.connect(audio.ctx.destination);
  }
  if (audio.ctx.state === 'suspended') audio.ctx.resume();
  return audio.ctx;
}

function audioSocket() {
  const au = tel && tel.sensors.audio;
  if (!au || !au.online) {
    toast('audio board offline', true);
    return null;
  }
  if (audio.ws && audio.ws.readyState <= WebSocket.OPEN) return audio.ws;
  const s = new WebSocket(`ws://${au.ip}/audio`);
  s.binaryType = 'arraybuffer';
  s.onopen = () => {
    s.send(JSON.stringify({ listen: audio.listening }));
  };
  s.onmessage = (e) => {
    if (audio.listening && e.data instanceof ArrayBuffer) playPcm(e.data);
  };
  s.onclose = () => {
    audio.ws = null;
    audio.listening = false;
    stopTalk();
    updateAudioButtons();
  };
  audio.ws = s;
  return s;
}

function playPcm(buf) {
  const ctx = audio.ctx;
  const pcm = new Int16Array(buf);
  if (!ctx || !pcm.length) return;

  // Real-time level calculation for immediate responsive visual feedback
  let maxAmp = 0;
  const ab = ctx.createBuffer(1, pcm.length, 16000);  // the context resamples
  const ch = ab.getChannelData(0);
  for (let i = 0; i < pcm.length; i++) {
    const v = pcm[i] / 32768;
    ch[i] = v;
    const abs = Math.abs(v);
    if (abs > maxAmp) maxAmp = abs;
  }

  if (audio.listening) {
    const pct = Math.min(100, Math.round(maxAmp * 125));
    $('auLevel').style.width = `${pct}%`;
  }

  const src = ctx.createBufferSource();
  src.buffer = ab;
  src.connect(audio.filterNode || ctx.destination);
  const now = ctx.currentTime;
  if (audio.next < now + 0.02 || audio.next > now + 0.5) audio.next = now + 0.06;
  src.start(audio.next);
  audio.next += ab.duration;
}

function updateAudioButtons() {
  put('listenBtn', audio.listening ? 'STOP LISTENING' : 'LISTEN');
  $('listenBtn').classList.toggle('active', audio.listening);
  $('talkBtn').classList.toggle('active', audio.talking);
}

$('listenBtn').addEventListener('click', () => {
  audioCtx();
  const s = audioSocket();
  if (!s) return;
  audio.listening = !audio.listening;
  if (s.readyState === WebSocket.OPEN) s.send(JSON.stringify({ listen: audio.listening }));
  updateAudioButtons();
});

async function startTalk() {
  if (!window.isSecureContext || !navigator.mediaDevices) {
    toast('microphone needs a secure page: open the dashboard from http://localhost (docs/04_dashboard.md)', true);
    return;
  }
  const ctx = audioCtx();
  if (!audioSocket()) return;

  // Acoustic echo / feedback prevention: duck incoming audio while operator talks
  if (audio.gainNode) {
    audio.gainNode.gain.setTargetAtTime(0, ctx.currentTime, 0.03);
  }

  try {
    if (!audio.mic) {
      audio.mic = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: true, noiseSuppression: true, channelCount: 1 } });
    }
  } catch (e) {
    toast(`microphone blocked: ${e.message}`, true);
    if (audio.gainNode) audio.gainNode.gain.setTargetAtTime(audio.volume, ctx.currentTime, 0.05);
    return;
  }
  audio.talking = true;
  const src = ctx.createMediaStreamSource(audio.mic);
  const proc = ctx.createScriptProcessor(2048, 1, 1);
  const mute = ctx.createGain();
  mute.gain.value = 0;
  const ratio = ctx.sampleRate / 16000;
  proc.onaudioprocess = (e) => {
    if (!audio.talking || !audio.ws || audio.ws.readyState !== WebSocket.OPEN) return;
    const f = e.inputBuffer.getChannelData(0);
    const n = Math.floor(f.length / ratio);
    const out = new Int16Array(n);
    for (let i = 0; i < n; i++) {
      // average each window: a cheap low-pass before decimating to 16 kHz
      const a = Math.floor(i * ratio);
      const b = Math.max(a + 1, Math.floor((i + 1) * ratio));
      let sum = 0;
      for (let j = a; j < b; j++) sum += f[j];
      out[i] = Math.max(-1, Math.min(1, sum / (b - a))) * 32767;
    }
    try {
      audio.ws.send(out.buffer);
    } catch (err) {}
  };
  src.connect(proc);
  proc.connect(mute);
  mute.connect(ctx.destination);
  audio.nodes = [src, proc, mute];
  updateAudioButtons();
}

function stopTalk() {
  audio.talking = false;
  for (const n of audio.nodes) {
    try { n.disconnect(); } catch (e) {}
  }
  audio.nodes = [];

  // Smoothly restore listening volume
  if (audio.gainNode && audio.ctx) {
    audio.gainNode.gain.setTargetAtTime(audio.volume, audio.ctx.currentTime, 0.08);
  }
  updateAudioButtons();
}

const talk = $('talkBtn');
talk.addEventListener('pointerdown', (e) => {
  e.preventDefault();
  talk.setPointerCapture(e.pointerId);
  startTalk();
});
for (const ev of ['pointerup', 'pointercancel', 'lostpointercapture']) talk.addEventListener(ev, stopTalk);

// Volume slider handling
const auVolSlider = $('auVol');
if (auVolSlider) {
  auVolSlider.value = Math.round(audio.volume * 100);
  auVolSlider.addEventListener('input', (e) => {
    audio.volume = e.target.value / 100;
    try { localStorage.setItem('eco_au_vol', audio.volume.toString()); } catch (err) {}
    if (audio.gainNode && !audio.talking && audio.ctx) {
      audio.gainNode.gain.setValueAtTime(audio.volume, audio.ctx.currentTime);
    }
  });
}

if (!window.isSecureContext) $('auNote').classList.add('warnc');

// ---------------------------------------------------------------- buttons
$('estopBtn').addEventListener('click', () => estop('button'));
for (const b of document.querySelectorAll('[data-mode]')) {
  b.addEventListener('click', () => post('/api/mode', { mode: b.dataset.mode }));
}
for (const b of document.querySelectorAll('[data-mission]')) {
  b.addEventListener('click', () => {
    selectedMission = b.dataset.mission;
    storageSet('mission', selectedMission);
    for (const x of document.querySelectorAll('[data-mission]')) x.classList.toggle('on', x === b);
  });
}
$('armAutoBtn').addEventListener('click', () => {
  if (!MISSIONS.includes(selectedMission)) return;
  disarmGamepad(null);
  clearManual();
  post('/api/mode', { mode: 'auto', mission: selectedMission });
});
$('startBtn').addEventListener('click', () => post('/api/mission/start'));
$('abortBtn').addEventListener('click', () => post('/api/mission/abort'));
$('resetEstopBtn').addEventListener('click', () => post('/api/estop/reset'));
$('ackBtn').addEventListener('click', () => post('/api/fault/ack'));
$('qrBtn').addEventListener('click', () => post('/api/task/qr_ping'));
$('motOnBtn').addEventListener('click', () => post('/api/motion_detect', { enabled: true }));
$('motOffBtn').addEventListener('click', () => post('/api/motion_detect', { enabled: false }));

$('gpEnableBtn').addEventListener('click', () => {
  if (!isTeleop()) return toast('switch to TELEOP first', true);
  gp.armed = true;
  toast('gamepad armed: hold RT/R2 to drive');
});
$('gpDisableBtn').addEventListener('click', () => disarmGamepad(null));
for (const b of document.querySelectorAll('[data-gpmode]')) {
  b.addEventListener('click', () => {
    gp.mode = b.dataset.gpmode;
    for (const x of document.querySelectorAll('[data-gpmode]')) x.classList.toggle('on', x === b);
  });
}

$('speed').addEventListener('input', () => put('speedOut', `${$('speed').value} %`));
$('deadzone').addEventListener('input', () => put('dzOut', Number($('deadzone').value).toFixed(2)));

let panDragging = false;
let panSendTimer = null;
function sendPan(deg) {
  clearTimeout(panSendTimer);
  panSendTimer = setTimeout(() => post('/api/servo', { angle: deg }, true), 80);
}
const panSlider = $('panSlider');
panSlider.addEventListener('pointerdown', () => { panDragging = true; });
panSlider.addEventListener('pointerup', () => { panDragging = false; });
panSlider.addEventListener('input', () => {
  put('panOut', `${panSlider.value}°`);
  sendPan(Number(panSlider.value));
});
const nudgePan = (d) => {
  const target = tel ? tel.sensors.pan.target + d : 90;
  post('/api/servo', { angle: target }, true);
};
$('panLeftBtn').addEventListener('click', () => nudgePan(-10));
$('panRightBtn').addEventListener('click', () => nudgePan(10));
$('panCenterBtn').addEventListener('click', () => post('/api/servo', { angle: 90 }, true));

// ---------------------------------------------------------------- theme
function setTheme(theme) {
  document.documentElement.dataset.theme = theme;
  document.querySelector('meta[name="theme-color"]').content = theme === 'light' ? '#f6f7f2' : '#000000';
  const next = theme === 'light' ? 'dark' : 'light';
  put('themeIcon', next === 'light' ? '☀' : '☾');
  put('themeLabel', next.toUpperCase());
  $('themeBtn').title = `Switch to the ${next} theme`;
  $('themeBtn').setAttribute('aria-label', $('themeBtn').title);
}
setTheme(document.documentElement.dataset.theme);
$('themeBtn').addEventListener('click', () => {
  const theme = document.documentElement.dataset.theme === 'light' ? 'dark' : 'light';
  setTheme(theme);
  storageSet('theme', theme);
  $('themeBtn').blur();  // Space must stay the E-stop key, with no focus left on this button
});

$('hostInput').value = host;
$('hostBtn').addEventListener('click', () => {
  const v = $('hostInput').value.trim().replace(/^https?:\/\//, '').replace(/\/.*$/, '');
  if (!v) return;
  host = v;
  storageSet('roverHost', host);
  sonarBuilt = false;
  stopPolling();
  connect();
});

connect();
