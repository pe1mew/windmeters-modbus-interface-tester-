'use strict';

// ── WebSocket connection ─────────────────────────────────────────────────
// Same connect/reconnect skeleton and type-keyed router as the template's
// app.js — new sections just add a case instead of a second dispatch
// mechanism (design/scratchbook.md §7).
let ws = null;

function wsConnect() {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  ws = new WebSocket(proto + '//' + location.host + '/ws');

  ws.onopen = function () {
    setBadge('ws-badge', 'Online', 'online');
  };

  ws.onclose = function () {
    setBadge('ws-badge', 'Offline', 'offline');
    setTimeout(wsConnect, 3000);
  };

  ws.onerror = function () { ws.close(); };

  ws.onmessage = function (evt) {
    try {
      const msg = JSON.parse(evt.data);
      if      (msg.type === 'status')    handleStatus(msg);
      else if (msg.type === 'scan')      handleScan(msg);
      else if (msg.type === 'wind')      handleWind(msg);
      else if (msg.type === 'log')       appendLog(msg);
      else if (msg.type === 'log_clear') clearLogTable();
    } catch (_) {}
  };
}

// ── HTTP POST helper ─────────────────────────────────────────────────────
function post(url, body) {
  return fetch(url, {
    method:  'POST',
    headers: { 'Content-Type': 'application/json' },
    body:    JSON.stringify(body),
  }).then(r => r.ok ? r.json() : null).catch(() => null);
}

// ── Status ────────────────────────────────────────────────────────────────
function handleStatus(s) {
  setText('st-wifi-mode', s.wifi_mode || '—');
  setText('st-wifi-ssid', s.wifi_ssid || '—');
  setText('st-wifi-ip',   s.wifi_ip   || '—');
  setText('st-wifi-rssi', s.wifi_rssi !== undefined ? s.wifi_rssi : '—');

  setText('st-time', s.local_time ? s.local_time.replace('T', ' ') : '—');
  const ntp = document.getElementById('st-ntp');
  if (ntp) {
    if (s.ntp_synced) {
      ntp.textContent = 'NTP synced';
      ntp.className   = 'badge ntp-on';
    } else {
      ntp.textContent = 'NTP pending';
      ntp.className   = 'badge ntp-off';
    }
  }

  if (s.bus) {
    setText('st-bus-crc',      s.bus.crc_errors !== undefined ? s.bus.crc_errors : '—');
    setText('st-bus-timeouts', s.bus.timeouts   !== undefined ? s.bus.timeouts   : '—');
    setText('st-bus-exc',      (s.bus.last_exception === null || s.bus.last_exception === undefined) ? 'none' : s.bus.last_exception);
  }

  if (s.uptime_s !== undefined) {
    setText('st-uptime', fmtElapsed(s.uptime_s));
  }
}

// ── Bus Scanner ──────────────────────────────────────────────────────────
function setScanRange(start, end) {
  document.getElementById('scan-start').value = start;
  document.getElementById('scan-end').value   = end;
}

function postScanStart() {
  const start = parseInt(document.getElementById('scan-start').value, 10);
  const end   = parseInt(document.getElementById('scan-end').value, 10);
  post('/scan/start', { start, end });
}

function postScanStop() {
  post('/scan/stop', {});
}

function handleScan(s) {
  const running = (s.state === 'running');
  const btnStart = document.getElementById('btn-scan-start');
  const btnStop  = document.getElementById('btn-scan-stop');
  if (btnStart) btnStart.disabled = running;
  if (btnStop)  btnStop.disabled  = !running;

  const p = document.getElementById('scan-progress');
  if (p) {
    if (s.state === 'running') {
      p.textContent = 'Scanning address ' + s.current_addr + ' / ' + s.range_end
        + '  (' + (s.found ? s.found.length : 0) + ' found so far)';
    } else if (s.state === 'complete') {
      p.textContent = 'Complete — ' + (s.found ? s.found.length : 0) + ' address(es) found.';
    } else if (s.state === 'cancelled') {
      p.textContent = 'Cancelled.';
    } else {
      p.textContent = 'Idle.';
    }
  }

  const tbody = document.getElementById('scan-body');
  if (tbody && s.found) {
    tbody.innerHTML = '';
    s.found.forEach(addr => {
      const tr = document.createElement('tr');
      tr.innerHTML = '<td>' + addr + '</td>';
      tbody.appendChild(tr);
    });
  }
}

// ── Wind Test ────────────────────────────────────────────────────────────
function postWindStart() {
  const addr     = parseInt(document.getElementById('wind-addr').value, 10);
  const interval = parseInt(document.getElementById('wind-interval').value, 10);
  post('/wind/start', { addr, interval_ms: interval });
  const btnStart = document.getElementById('btn-wind-start');
  const btnStop  = document.getElementById('btn-wind-stop');
  if (btnStart) btnStart.disabled = true;
  if (btnStop)  btnStop.disabled  = false;
}

function postWindStop() {
  post('/wind/stop', {});
  const btnStart = document.getElementById('btn-wind-start');
  const btnStop  = document.getElementById('btn-wind-stop');
  if (btnStart) btnStart.disabled = false;
  if (btnStop)  btnStop.disabled  = true;
}

function handleWind(w) {
  function fmt(v) { return (v === undefined || v === null) ? '—' : v.toFixed(1); }
  setText('wind-dir-instant',   fmt(w.dir_instant_deg));
  setText('wind-dir-avg',       fmt(w.dir_avg_deg));
  setText('wind-speed-instant', fmt(w.speed_instant_ms));
  setText('wind-speed-avg',     fmt(w.speed_avg_ms));
  setText('wind-pulses',        w.raw_pulses !== undefined ? w.raw_pulses : '—');
  setText('wind-age',           w.age_ms     !== undefined ? w.age_ms     : '—');
}

function postWindConfigRead() {
  const addr = parseInt(document.getElementById('wind-addr').value, 10);
  const statusEl = document.getElementById('wind-config-status');
  post('/wind/config/read', { addr }).then(r => {
    if (!r || !r.ok) {
      if (statusEl) statusEl.textContent = 'Read failed.';
      return;
    }
    document.getElementById('wind-cfg-addr').value   = r.device_addr;
    document.getElementById('wind-cfg-offset').value = r.dir_offset_deg;
    document.getElementById('wind-cfg-meas').value   = r.measurement_window_ms;
    document.getElementById('wind-cfg-avg').value    = r.averaging_window_s;
    if (statusEl) statusEl.textContent = 'Config read OK.';
  });
}

function postWindConfigWrite(field) {
  const addr = parseInt(document.getElementById('wind-addr').value, 10);
  const fieldInputMap = {
    device_addr:         'wind-cfg-addr',
    dir_offset:           'wind-cfg-offset',
    measurement_window:   'wind-cfg-meas',
    averaging_window:     'wind-cfg-avg',
  };
  const inputId = fieldInputMap[field];
  const value = parseFloat(document.getElementById(inputId).value);
  const statusEl = document.getElementById('wind-config-status');
  post('/wind/config/write', { addr, field, value }).then(r => {
    if (statusEl) statusEl.textContent = (r && r.ok) ? ('Wrote ' + field + ' OK.') : ('Write of ' + field + ' failed.');
  });
}

// ── Register Explorer ───────────────────────────────────────────────────
function postExplorerQuery() {
  const addr   = parseInt(document.getElementById('exp-addr').value, 10);
  const fc     = parseInt(document.getElementById('exp-fc').value, 10);
  const fmt    = document.querySelector('input[name="exp-fmt"]:checked').value;
  const regStr = document.getElementById('exp-reg').value.trim();
  const count  = parseInt(document.getElementById('exp-count').value, 10);
  const valuesStr = document.getElementById('exp-values').value.trim();
  const values = valuesStr ? valuesStr.split(',').map(v => parseInt(v.trim(), 10)) : [];

  const resultEl = document.getElementById('exp-result');
  post('/explorer/query', { addr, fc, register: regStr, format: fmt, count, values }).then(r => {
    if (!r) { if (resultEl) resultEl.textContent = 'Request failed.'; return; }
    if (!r.ok) { if (resultEl) resultEl.textContent = 'Error: ' + (r.status || 'unknown'); return; }
    let txt = 'OK (raw addr ' + r.raw_register + ')';
    if (r.registers) txt += ' — values: ' + r.registers.join(', ');
    if (r.raw_hex)   txt += ' — hex: ' + r.raw_hex;
    if (resultEl) resultEl.textContent = txt;
  });
}

// ── WiFi / NTP / time / Modbus settings ─────────────────────────────────
function postWifi() {
  const ssid = document.getElementById('wifi-ssid').value;
  const pass = document.getElementById('wifi-pass').value;
  post('/config/wifi', { ssid, pass });
}

function postNtp() {
  const server = document.getElementById('ntp-server').value;
  post('/config/ntp', { server });
}

function postTime() {
  const val = document.getElementById('manual-time').value; // "YYYY-MM-DDTHH:MM"
  if (!val) return;
  const [datePart, timePart] = val.split('T');
  const [year, month, day]   = datePart.split('-').map(Number);
  const [hour, minute]       = timePart.split(':').map(Number);
  post('/config/time', { year, month, day, hour, minute, second: 0 });
}

function postModbusSettings() {
  const timeout_ms = parseInt(document.getElementById('mb-timeout').value, 10);
  const retries    = parseInt(document.getElementById('mb-retries').value, 10);
  post('/config/modbus', { timeout_ms, retries });
}

// ── Modbus log ───────────────────────────────────────────────────────────
const LOG_MAX = 30;

function appendLog(entry) {
  const tbody = document.getElementById('log-body');
  if (!tbody) return;
  const tr = document.createElement('tr');
  tr.innerHTML =
    '<td>' + esc(entry.ts      || '') + '</td>' +
    '<td>' + esc(entry.dir     || '') + '</td>' +
    '<td><code>' + esc(entry.hex || '') + '</code></td>' +
    '<td>' + esc(entry.summary || '') + '</td>';
  tbody.insertBefore(tr, tbody.firstChild);
  while (tbody.rows.length > LOG_MAX) tbody.deleteRow(tbody.rows.length - 1);
}

function clearLogTable() {
  const tbody = document.getElementById('log-body');
  if (tbody) tbody.innerHTML = '';
}

function postLogClear() {
  post('/log/clear', {});
}

// ── Utilities ────────────────────────────────────────────────────────────
function setText(id, val) {
  const el = document.getElementById(id);
  if (el) el.textContent = val;
}

function setBadge(id, text, cls) {
  const el = document.getElementById(id);
  if (!el) return;
  el.textContent = text;
  el.className   = 'badge ' + cls;
}

function fmtElapsed(sec) {
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = Math.floor(sec % 60);
  return String(h).padStart(2,'0') + ':' + String(m).padStart(2,'0') + ':' + String(s).padStart(2,'0');
}

// Minimal XSS-safe text escaping for log table content.
function esc(str) {
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

// ── Resizable log columns ────────────────────────────────────────────────
function initResizableCols() {
  const table = document.getElementById('log-tbl');
  if (!table) return;

  table.style.tableLayout = 'fixed';

  const defaults = [70, 45, 300, 200];
  const ths = table.querySelectorAll('thead th');
  const FIXED_COLS = 2;

  ths.forEach(function (th, i) {
    th.style.width = (th.offsetWidth || defaults[i]) + 'px';
    if (i < FIXED_COLS) return;

    const handle = document.createElement('div');
    handle.className = 'col-resizer';
    th.appendChild(handle);

    var startX, startW;

    handle.addEventListener('mousedown', function (e) {
      startX = e.clientX;
      startW = th.offsetWidth;
      handle.classList.add('dragging');
      e.preventDefault();

      function onMove(e) {
        th.style.width = Math.max(40, startW + e.clientX - startX) + 'px';
      }
      function onUp() {
        handle.classList.remove('dragging');
        document.removeEventListener('mousemove', onMove);
        document.removeEventListener('mouseup',   onUp);
      }
      document.addEventListener('mousemove', onMove);
      document.addEventListener('mouseup',   onUp);
    });
  });
}

// ── Boot ─────────────────────────────────────────────────────────────────
wsConnect();
initResizableCols();
