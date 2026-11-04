const ROLE = 'control-panel';

const $ = (s) => document.getElementById(s);

const el = {
  micBtn:    $('micBtn'),
  spkBtn:    $('spkBtn'),
  micLbl:    $('micLbl'),
  spkLbl:    $('spkLbl'),
  volSlider: $('volSlider'),
  connBadge: $('connBadge'),
  connDot:   $('connDot'),
  connText:  $('connText'),
  peerBanner:    $('peerBanner'),
  peerMuteNotice:$('peerMuteNotice'),
  sLatency:  $('sLatency'),
  sState:    $('sState'),
  cvIn:      $('cvIn'),
  cvOut:     $('cvOut'),
};

let ws, pc, localStream, audioCtx, localAnalyser, remoteAnalyser, gainNode;
let micMuted = false, spkMuted = false;

// ── WebSocket ──────────────────────────────────────────────

function connect() {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(`${proto}//${location.host}`);

  ws.onopen = () => {
    setConn(true);
    ws.send(JSON.stringify({ type: 'register', role: ROLE }));
  };

  ws.onmessage = async ({ data }) => {
    const msg = JSON.parse(data);

    if (msg.type === 'peer-connected')    { setPeer(true); await startCall(); }
    if (msg.type === 'peer-disconnected') { setPeer(false); cleanup(); }

    if (msg.type === 'answer' && pc) {
      await pc.setRemoteDescription(new RTCSessionDescription(msg.sdp));
    }

    if (msg.type === 'ice-candidate' && pc && msg.candidate) {
      await pc.addIceCandidate(new RTCIceCandidate(msg.candidate));
    }

    if (msg.type === 'peer-mute-status' && msg.from === 'rover') {
      el.peerMuteNotice.classList.toggle('show', msg.muted);
    }
  };

  ws.onclose = () => { setConn(false); setPeer(false); setTimeout(connect, 3000); };
}

// ── WebRTC ─────────────────────────────────────────────────

async function startCall() {
  localStream = await navigator.mediaDevices.getUserMedia({
    audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true }
  });

  audioCtx = new AudioContext();
  const src = audioCtx.createMediaStreamSource(localStream);
  localAnalyser = audioCtx.createAnalyser();
  localAnalyser.fftSize = 256;
  src.connect(localAnalyser);
  visualize(el.cvOut, localAnalyser, '#22c55e');

  pc = new RTCPeerConnection({ iceServers: [{ urls: 'stun:stun.l.google.com:19302' }] });

  localStream.getTracks().forEach(t => pc.addTrack(t, localStream));

  pc.ontrack = ({ streams }) => attachRemote(streams[0]);

  pc.onicecandidate = ({ candidate }) => {
    if (candidate) ws.send(JSON.stringify({ type: 'ice-candidate', candidate }));
  };

  pc.onconnectionstatechange = () => {
    el.sState.textContent = pc.connectionState;
    if (pc.connectionState === 'connected') monitorLatency();
  };

  const offer = await pc.createOffer();
  await pc.setLocalDescription(offer);
  ws.send(JSON.stringify({ type: 'offer', sdp: pc.localDescription }));
}

function cleanup() {
  if (pc) { pc.close(); pc = null; }
  el.sState.textContent = '--';
  el.sLatency.textContent = '--';
}

// ── Audio ──────────────────────────────────────────────────

function attachRemote(stream) {
  const audio = document.createElement('audio');
  audio.srcObject = stream;
  audio.autoplay = true;
  audio.volume = el.volSlider.value / 100;
  document.body.appendChild(audio);

  if (audioCtx) {
    const src = audioCtx.createMediaStreamSource(stream);
    remoteAnalyser = audioCtx.createAnalyser();
    remoteAnalyser.fftSize = 256;
    gainNode = audioCtx.createGain();
    gainNode.gain.value = el.volSlider.value / 100;
    src.connect(remoteAnalyser);
    src.connect(gainNode);
    visualize(el.cvIn, remoteAnalyser, '#3b82f6');
  }

  el.volSlider.addEventListener('input', () => {
    audio.volume = el.volSlider.value / 100;
    if (gainNode) gainNode.gain.value = el.volSlider.value / 100;
  });
}

// ── Visualizer ─────────────────────────────────────────────

function visualize(canvas, analyser, color) {
  const ctx = canvas.getContext('2d');
  const buf = new Uint8Array(analyser.frequencyBinCount);

  (function draw() {
    requestAnimationFrame(draw);
    analyser.getByteFrequencyData(buf);

    const w = canvas.width  = canvas.offsetWidth * 2;
    const h = canvas.height = canvas.offsetHeight * 2;
    ctx.clearRect(0, 0, w, h);

    const barW = (w / buf.length) * 2;
    let x = 0;
    for (let i = 0; i < buf.length; i++) {
      const barH = (buf[i] / 255) * h;
      ctx.fillStyle = color;
      ctx.globalAlpha = 0.25 + (buf[i] / 255) * 0.75;
      ctx.fillRect(x, h - barH, barW - 1, barH);
      x += barW;
    }
    ctx.globalAlpha = 1;
  })();
}

// ── Latency ────────────────────────────────────────────────

function monitorLatency() {
  setInterval(async () => {
    if (!pc || pc.connectionState !== 'connected') return;
    const stats = await pc.getStats();
    stats.forEach(r => {
      if (r.type === 'candidate-pair' && r.state === 'succeeded' && r.currentRoundTripTime != null) {
        el.sLatency.textContent = Math.round(r.currentRoundTripTime * 1000) + 'ms';
      }
    });
  }, 2000);
}

// ── UI ─────────────────────────────────────────────────────

el.micBtn.onclick = () => {
  micMuted = !micMuted;
  if (localStream) localStream.getAudioTracks().forEach(t => t.enabled = !micMuted);
  el.micBtn.classList.toggle('off', micMuted);
  el.micBtn.textContent = micMuted ? 'MIC' : 'MIC';
  el.micLbl.textContent = micMuted ? 'Off' : 'On';
  if (ws?.readyState === WebSocket.OPEN) ws.send(JSON.stringify({ type: 'mute-status', muted: micMuted }));
};

el.spkBtn.onclick = () => {
  spkMuted = !spkMuted;
  document.querySelectorAll('audio').forEach(a => a.muted = spkMuted);
  el.spkBtn.classList.toggle('off', spkMuted);
  el.spkLbl.textContent = spkMuted ? 'Off' : 'On';
};

function setConn(on) {
  el.connBadge.className = `badge ${on ? 'on' : 'off'}`;
  el.connDot.className   = `dot ${on ? 'green' : 'red'}`;
  el.connText.textContent = on ? 'Server connected' : 'Disconnected';
}

function setPeer(on) {
  el.peerBanner.className = `peer-banner ${on ? 'active' : 'waiting'}`;
  el.peerBanner.textContent = on ? 'Rover connected — audio link active' : 'Waiting for rover...';
  if (!on) el.peerMuteNotice.classList.remove('show');
}

connect();
