const express = require('express');
const fs = require('fs');
const http = require('http');
const https = require('https');
const WebSocket = require('ws');
const path = require('path');
const os = require('os');

const app = express();
app.use(express.static(path.join(__dirname, 'public')));

// Create both HTTP (localhost) and HTTPS (LAN / phone) servers
const httpServer = http.createServer(app);

const certPath = path.join(__dirname, 'cert.pem');
const keyPath = path.join(__dirname, 'key.pem');
let httpsServer = null;

if (fs.existsSync(certPath) && fs.existsSync(keyPath)) {
  httpsServer = https.createServer({
    cert: fs.readFileSync(certPath),
    key: fs.readFileSync(keyPath),
  }, app);
}

// WebSocket on both servers
function setupWS(server) {
  const wss = new WebSocket.Server({ server });

  wss.on('connection', (ws) => {
    ws.on('message', (raw) => {
      let msg;
      try { msg = JSON.parse(raw); } catch { return; }

      if (msg.type === 'register') {
        clients[msg.role] = ws;
        ws.role = msg.role;
        const other = msg.role === 'control-panel' ? 'rover' : 'control-panel';
        const peer = clients[other];
        if (peer && peer.readyState === WebSocket.OPEN) {
          peer.send(JSON.stringify({ type: 'peer-connected', role: msg.role }));
          ws.send(JSON.stringify({ type: 'peer-connected', role: other }));
        }
        return;
      }

      if (['offer', 'answer', 'ice-candidate', 'mute-status'].includes(msg.type)) {
        const target = ws.role === 'control-panel' ? 'rover' : 'control-panel';
        const peer = clients[target];
        if (!peer || peer.readyState !== WebSocket.OPEN) return;

        if (msg.type === 'mute-status') {
          peer.send(JSON.stringify({ type: 'peer-mute-status', muted: msg.muted, from: ws.role }));
        } else {
          peer.send(JSON.stringify(msg));
        }
      }
    });

    ws.on('close', () => {
      if (ws.role) {
        clients[ws.role] = null;
        const other = ws.role === 'control-panel' ? 'rover' : 'control-panel';
        const peer = clients[other];
        if (peer && peer.readyState === WebSocket.OPEN) {
          peer.send(JSON.stringify({ type: 'peer-disconnected', role: ws.role }));
        }
      }
    });
  });
}

const clients = { 'control-panel': null, 'rover': null };

setupWS(httpServer);
if (httpsServer) setupWS(httpsServer);

// Get LAN IP
function getLanIP() {
  const nets = os.networkInterfaces();
  for (const name of Object.keys(nets)) {
    for (const net of nets[name]) {
      if (net.family === 'IPv4' && !net.internal) return net.address;
    }
  }
  return null;
}

const HTTP_PORT = 3000;
const HTTPS_PORT = 3443;

httpServer.listen(HTTP_PORT, () => {
  console.log(`\nRover Audio Server`);
  console.log(`------------------`);
  console.log(`HTTP  : http://localhost:${HTTP_PORT}`);
});

if (httpsServer) {
  httpsServer.listen(HTTPS_PORT, '0.0.0.0', () => {
    const ip = getLanIP();
    console.log(`HTTPS : https://localhost:${HTTPS_PORT}`);
    if (ip) {
      console.log(`\nLAN access (phone/other devices):`);
      console.log(`  https://${ip}:${HTTPS_PORT}/rover.html`);
      console.log(`  https://${ip}:${HTTPS_PORT}/control-panel.html`);
    }
    console.log(`\nControl Panel : /control-panel.html`);
    console.log(`Rover         : /rover.html`);
  });
} else {
  console.log(`\nHTTPS disabled (cert.pem / key.pem not found)`);
  console.log(`Run: node generate-cert.js  to enable LAN/phone access`);
}
