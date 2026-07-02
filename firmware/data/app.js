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

// ── Tabs ─────────────────────────────────────────────────────────────────
function switchTab(id) {
  document.querySelectorAll('.tab-panel').forEach(el => el.classList.remove('active'));
  document.querySelectorAll('.tab-btn').forEach(el => el.classList.remove('active'));
  const panel = document.getElementById('tab-' + id);
  if (panel) panel.classList.add('active');
  const btn = document.querySelector('.tab-btn[data-tab="' + id + '"]');
  if (btn) btn.classList.add('active');
}

// ── Status ────────────────────────────────────────────────────────────────
let mbSettingsPopulated = false;

function handleStatus(s) {
  setText('fw-version', s.fw_version ? ('v' + s.fw_version) : '');

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

  // Populate once from the live (NVS-backed) values so the fields never
  // load empty — after that, leave them alone so a client mid-edit doesn't
  // get overwritten by the next status tick.
  if (!mbSettingsPopulated && s.mb_timeout_ms !== undefined && s.mb_retries !== undefined) {
    const timeoutEl = document.getElementById('mb-timeout');
    const retriesEl = document.getElementById('mb-retries');
    if (timeoutEl) timeoutEl.value = s.mb_timeout_ms;
    if (retriesEl) retriesEl.value = s.mb_retries;
    mbSettingsPopulated = true;
  }
}

// ── Bus Scanner ──────────────────────────────────────────────────────────
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
    s.found.forEach(dev => {
      // dev is { slave, functions_ok, round_trip_ms } — same shape as
      // GET/POST /api/v1/scan's found[] (design/api.md §5.3), so the GUI
      // and the machine API show the same information for the same scan.
      const tr = document.createElement('tr');
      tr.innerHTML = '<td>' + dev.slave + '</td>'
        + '<td>' + dev.functions_ok.map(fc => 'FC' + fc).join(', ') + '</td>'
        + '<td>' + dev.round_trip_ms + ' ms</td>';
      tbody.appendChild(tr);
    });
  }
}

// ── Wind Speed / Wind Direction ─────────────────────────────────────────
// Wind speed and wind direction are separate physical units (wind_poll.h)
// with their own tab, but only one can poll at a time (wind_poll_task's
// single s_active_type) — every helper here takes a 'speed'|'direction'
// type and maps it to that tab's 'wspd-'/'wdir-' element prefix.
function windPrefix(type) {
  return (type === 'speed') ? 'wspd' : 'wdir';
}

function setWindButtons(prefix, active) {
  const btnStart = document.getElementById('btn-' + prefix + '-start');
  const btnStop  = document.getElementById('btn-' + prefix + '-stop');
  if (btnStart) btnStart.disabled = active;
  if (btnStop)  btnStop.disabled  = !active;
}

function postWindStart(type) {
  const prefix   = windPrefix(type);
  const addr     = parseInt(document.getElementById(prefix + '-addr').value, 10);
  const interval = parseInt(document.getElementById(prefix + '-interval').value, 10);
  post('/wind/start', { type, addr, interval_ms: interval });
  setWindButtons(prefix, true);
}

function postWindStop(type) {
  post('/wind/stop', {});
  setWindButtons(windPrefix(type), false);
}

function handleWind(w) {
  function fmt(v) { return (v === undefined || v === null) ? '—' : v.toFixed(1); }

  const activePrefix = windPrefix(w.sensor_type);
  const otherPrefix   = (activePrefix === 'wspd') ? 'wdir' : 'wspd';

  if (w.sensor_type === 'speed') {
    setText('wspd-instant',     fmt(w.speed_instant_ms));
    setText('wspd-avg',         fmt(w.speed_avg_ms));
    setText('wspd-pulses',      w.raw_pulses           !== undefined ? w.raw_pulses           : '—');
    setText('wspd-gust',        fmt(w.gust_ms));
    setText('wspd-since-pulse', w.seconds_since_pulse  !== undefined ? w.seconds_since_pulse  : '—');
    setText('wspd-age',         w.age_ms               !== undefined ? w.age_ms               : '—');
  } else {
    setText('wdir-instant', fmt(w.dir_instant_deg));
    setText('wdir-avg',     fmt(w.dir_avg_deg));
    setText('wdir-adc',     w.raw_adc !== undefined ? w.raw_adc : '—');
    setText('wdir-age',     w.age_ms  !== undefined ? w.age_ms  : '—');
    const faultEl = document.getElementById('wdir-fault');
    if (faultEl) faultEl.style.display = w.dir_fault ? 'inline-block' : 'none';
  }

  // Only one sensor is ever actually polling — receiving an update for one
  // type is proof the other tab's Start/Stop buttons are stale if they
  // still claim to be running (e.g. this client started Speed while
  // Direction's Stop button was left enabled from an earlier session).
  setWindButtons(activePrefix, true);
  setWindButtons(otherPrefix, false);
}

function postWindConfigRead(type) {
  const prefix   = windPrefix(type);
  const addr     = parseInt(document.getElementById(prefix + '-addr').value, 10);
  const statusEl = document.getElementById(prefix + '-config-status');
  post('/wind/config/read', { type, addr }).then(r => {
    if (!r || !r.ok) {
      if (statusEl) statusEl.textContent = 'Read failed.';
      return;
    }
    // All 4 holding registers exist identically on both builds (TDS §2.7
    // FR-MB27) — the response always carries all 4; each tab just picks
    // out the ones it shows.
    if (type === 'speed') {
      document.getElementById('wspd-cfg-meas').value   = r.measurement_window_ms;
      document.getElementById('wspd-cfg-cutoff').value = r.low_speed_cutoff_ms;
    } else {
      document.getElementById('wdir-cfg-offset').value = r.dir_offset_deg;
    }
    document.getElementById(prefix + '-cfg-avg').value = r.averaging_window_s;
    if (statusEl) statusEl.textContent = 'Config read OK.';
  });
}

function postWindConfigWrite(type, field) {
  const prefix = windPrefix(type);
  const addr   = parseInt(document.getElementById(prefix + '-addr').value, 10);
  // No device_addr case — that register no longer exists (TDS v0.6,
  // FR-MB07/FR-MB26); the Modbus address is hardware-jumper only.
  const fieldInputMap = {
    dir_offset:           'wdir-cfg-offset',
    measurement_window:   'wspd-cfg-meas',
    averaging_window:     prefix + '-cfg-avg',
    low_speed_cutoff:     'wspd-cfg-cutoff',
  };
  const inputId  = fieldInputMap[field];
  const value    = parseFloat(document.getElementById(inputId).value);
  const statusEl = document.getElementById(prefix + '-config-status');
  post('/wind/config/write', { type, addr, field, value }).then(r => {
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
const LOG_MAX = 50;

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
