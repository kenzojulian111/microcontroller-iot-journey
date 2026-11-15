#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>IoT Smart Switch Panel</title>
  <style>
    :root {
      --bg: #0f172a;
      --card: #1e293b;
      --text: #f8fafc;
      --muted: #94a3b8;
      --accent: #38bdf8;
      --on: #22c55e;
      --off: #475569;
      --border: #334155;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      background-color: var(--bg);
      color: var(--text);
      min-height: 100vh;
      display: flex;
      flex-direction: column;
      align-items: center;
      padding: 24px 16px;
    }
    .container {
      width: 100%;
      max-width: 480px;
    }
    header {
      text-align: center;
      margin-bottom: 24px;
    }
    header h1 {
      font-size: 1.5rem;
      font-weight: 700;
      color: var(--accent);
      margin-bottom: 6px;
    }
    header p {
      font-size: 0.875rem;
      color: var(--muted);
    }
    .card {
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 14px;
      padding: 18px 20px;
      margin-bottom: 16px;
      box-shadow: 0 4px 12px rgba(0, 0, 0, 0.25);
    }
    .switch-row {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 12px 0;
    }
    .switch-row + .switch-row {
      border-top: 1px solid var(--border);
    }
    .switch-info h3 {
      font-size: 1.05rem;
      margin-bottom: 4px;
    }
    .badge {
      display: inline-block;
      font-size: 0.75rem;
      font-weight: 600;
      padding: 2px 8px;
      border-radius: 999px;
      background: var(--off);
      color: #fff;
    }
    .badge.active {
      background: var(--on);
    }
    .btn-toggle {
      cursor: pointer;
      border: none;
      border-radius: 10px;
      padding: 10px 20px;
      font-size: 0.9rem;
      font-weight: 600;
      color: #fff;
      background: var(--off);
      transition: all 0.2s ease;
      min-width: 96px;
    }
    .btn-toggle.active {
      background: var(--on);
      box-shadow: 0 0 12px rgba(34, 197, 94, 0.4);
    }
    .btn-toggle:active {
      transform: scale(0.97);
    }
    .actions-row {
      display: flex;
      gap: 10px;
      margin-top: 14px;
    }
    .btn-secondary {
      flex: 1;
      cursor: pointer;
      border: 1px solid var(--border);
      background: transparent;
      color: var(--text);
      padding: 9px;
      border-radius: 8px;
      font-size: 0.85rem;
      font-weight: 600;
      transition: background 0.2s;
    }
    .btn-secondary:hover {
      background: rgba(255, 255, 255, 0.06);
    }
    .telemetry-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 12px;
      margin-top: 8px;
    }
    .telemetry-item {
      background: rgba(15, 23, 42, 0.5);
      padding: 10px 12px;
      border-radius: 8px;
      border: 1px solid var(--border);
    }
    .telemetry-label {
      font-size: 0.75rem;
      color: var(--muted);
      margin-bottom: 4px;
    }
    .telemetry-value {
      font-size: 0.95rem;
      font-weight: 600;
      color: var(--text);
    }
  </style>
</head>
<body>
  <div class="container">
    <header>
      <h1>IoT Smart Switch</h1>
      <p id="board-name">Local Web Server Control Panel</p>
    </header>

    <div class="card">
      <div class="switch-row">
        <div class="switch-info">
          <h3>Relay Channel 1 (Lampu Utama)</h3>
          <span id="badge-1" class="badge">OFF</span>
        </div>
        <button id="btn-1" class="btn-toggle" onclick="toggleRelay(1)">Turn ON</button>
      </div>

      <div class="switch-row">
        <div class="switch-info">
          <h3>Relay Channel 2 (Kipas / Exhaust)</h3>
          <span id="badge-2" class="badge">OFF</span>
        </div>
        <button id="btn-2" class="btn-toggle" onclick="toggleRelay(2)">Turn ON</button>
      </div>

      <div class="actions-row">
        <button class="btn-secondary" onclick="setAll(1)">Nyalakan Semua</button>
        <button class="btn-secondary" onclick="setAll(0)">Matikan Semua</button>
      </div>
    </div>

    <div class="card">
      <h3 style="font-size: 0.95rem; margin-bottom: 8px; color: var(--muted);">Informasi Perangkat</h3>
      <div class="telemetry-grid">
        <div class="telemetry-item">
          <div class="telemetry-label">Alamat IP</div>
          <div id="val-ip" class="telemetry-value">-</div>
        </div>
        <div class="telemetry-item">
          <div class="telemetry-label">Sinyal Wi-Fi (RSSI)</div>
          <div id="val-rssi" class="telemetry-value">- dBm</div>
        </div>
        <div class="telemetry-item">
          <div class="telemetry-label">Uptime</div>
          <div id="val-uptime" class="telemetry-value">- s</div>
        </div>
        <div class="telemetry-item">
          <div class="telemetry-label">Free Heap RAM</div>
          <div id="val-heap" class="telemetry-value">- KB</div>
        </div>
      </div>
    </div>
  </div>

  <script>
    function updateUI(data) {
      document.getElementById('board-name').textContent = data.board + ' • Local Web Server';
      const states = [data.relay1, data.relay2];
      states.forEach((isOn, idx) => {
        const ch = idx + 1;
        const badge = document.getElementById('badge-' + ch);
        const btn = document.getElementById('btn-' + ch);
        if (isOn) {
          badge.textContent = 'ON';
          badge.classList.add('active');
          btn.textContent = 'Turn OFF';
          btn.classList.add('active');
        } else {
          badge.textContent = 'OFF';
          badge.classList.remove('active');
          btn.textContent = 'Turn ON';
          btn.classList.remove('active');
        }
      });
      document.getElementById('val-ip').textContent = data.ip;
      document.getElementById('val-rssi').textContent = data.rssi + ' dBm';
      document.getElementById('val-uptime').textContent = data.uptime + ' dtk';
      document.getElementById('val-heap').textContent = (data.heap / 1024).toFixed(1) + ' KB';
    }

    async function fetchStatus() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();
        updateUI(data);
      } catch (e) {
        console.error('Gagal mengambil status:', e);
      }
    }

    async function toggleRelay(ch) {
      try {
        const res = await fetch('/api/toggle?ch=' + ch, { method: 'POST' });
        const data = await res.json();
        updateUI(data);
      } catch (e) {
        console.error('Gagal mengubah relay:', e);
      }
    }

    async function setAll(state) {
      try {
        const res = await fetch('/api/all?state=' + state, { method: 'POST' });
        const data = await res.json();
        updateUI(data);
      } catch (e) {
        console.error('Gagal mengatur semua relay:', e);
      }
    }

    fetchStatus();
    setInterval(fetchStatus, 2000);
  </script>
</body>
</html>
)rawliteral";

#endif // WEBPAGE_H
