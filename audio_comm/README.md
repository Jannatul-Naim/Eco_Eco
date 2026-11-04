# Rover 2-Way Audio Communication System

A low-latency, real-time 2-way audio communication link between a Control Panel and a Rover built with WebRTC, WebSockets, and Web Audio API.

## Features

- **Full-Duplex 2-Way Audio**: Control Panel to Rover and Rover to Control Panel.
- **WebRTC Peer-to-Peer**: Ultra-low latency audio streaming using the Opus codec.
- **Audio Device Selection**: Select external microphones and speakers dynamically on the Rover client.
- **Real-Time Visualizer**: Audio waveform visualizer for incoming and outgoing streams.
- **Volume & Mute Controls**: Hardware mic mute, speaker mute, and volume adjustments with state sync across peers.
- **HTTPS & LAN Support**: Self-signed SSL certificates for mobile phone and local network access (browser microphone permissions requirement).

## Project Structure

```
rover-audio-comm/
├── package.json
├── server.js              # Signaling server (HTTP + HTTPS + WebSocket)
├── generate-cert.js       # SSL certificate generator for LAN/mobile access
├── .gitignore
└── public/
    ├── control-panel.html # Operator Control Panel interface
    ├── rover.html         # Rover onboard interface
    └── js/
        ├── control-panel.js
        └── rover.js
```

## Getting Started

### 1. Prerequisites

- [Node.js](https://nodejs.org/) (v16 or higher)

### 2. Installation

Clone the repository and install dependencies:

```bash
git clone <repository-url>
cd rover-audio-comm
npm install
```

### 3. Generate SSL Certificates (Optional, for LAN / Mobile access)

Browsers restrict microphone access over plain HTTP when accessed outside `localhost`. To access the Rover from a mobile phone or another device on your local Wi-Fi:

```bash
node generate-cert.js
```

### 4. Run the Server

```bash
npm start
```

### 5. Open in Browser

- **Control Panel**:
  - Local: `http://localhost:3000/control-panel.html`
  - Over LAN: `https://<YOUR-IP>:3443/control-panel.html`
- **Rover Interface**:
  - Local: `http://localhost:3000/rover.html`
  - Over LAN: `https://<YOUR-IP>:3443/rover.html`

> Grant microphone permissions when prompted by your browser on both devices.
