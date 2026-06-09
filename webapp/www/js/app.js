/* SmartBracelet — App Controller (app.js) */

const STEPS_GOAL = 10000;
const $ = id => document.getElementById(id);
const ble = new BleService();
const watchData = new WatchData();
const notifyLog = [];
const discoveredDevices = new Map();

const els = {
  connectScreen: $('connectScreen'), dashboard: $('dashboard'),
  btnScan: $('btnScan'), btnDisconnect: $('btnDisconnect'),
  connectStatus: $('connectStatus'), noBtHint: $('noBtHint'),
  deviceList: $('deviceList'), deviceListItems: $('deviceListItems'),
  btnStopScan: $('btnStopScan'),
  stepsValue: $('stepsValue'), stepsBar: $('stepsBar'),
  battPercent: $('battPercent'), battMv: $('battMv'),
  battBadge: $('battBadge'), battBar: $('battBar'),
  actIcon: $('actIcon'), actName: $('actName'), actSub: $('actSub'),
  notifyCard: $('notifyCard'), notifyArrow: $('notifyArrow'), notifyBody: $('notifyBody'),
  inputAppId: $('inputAppId'), inputTitle: $('inputTitle'), inputBody: $('inputBody'),
  btnSend: $('btnSend'), notifyStatus: $('notifyStatus'),
  notifyLog: $('notifyLog'), notifyLogList: $('notifyLogList'),
  debugToggle: $('debugToggle'), debugArrow: $('debugArrow'),
  debugBody: $('debugBody'), debugContent: $('debugContent'),
  lastUpdate: $('lastUpdate'),
};

// Check Bluetooth availability
if (!isNative && !navigator.bluetooth) {
  els.noBtHint.style.display = 'block';
  els.btnScan.disabled = true;
  els.btnScan.style.opacity = '0.4';
}

// ── Scan / Connect Button ──
els.btnScan.addEventListener('click', async () => {
  els.connectStatus.className = 'connect-status';
  els.btnScan.disabled = true;
  if (isNative) await _scanCapacitor();
  else await _connectWebBluetooth();
});

async function _connectWebBluetooth() {
  els.connectStatus.textContent = 'Opening device selector...';
  try {
    await ble.connect();
    const data = await ble.readAll();
    Object.assign(watchData, data);
    showDashboard();
    render();
  } catch (e) {
    const msg = e.message || String(e);
    if (msg.includes('cancel') || msg.includes('user') || msg.includes('cancelled')) {
      els.connectStatus.textContent = 'Selection cancelled. Click Scan to try again.';
    } else {
      els.connectStatus.textContent = `Error: ${msg}`;
    }
    els.connectStatus.className = 'connect-status error';
    els.btnScan.disabled = false;
  }
}

async function _scanCapacitor() {
  els.connectStatus.textContent = 'Requesting permissions...';
  discoveredDevices.clear();
  els.deviceListItems.innerHTML = '';

  try { await CapacitorBLE.requestPermissions(); } catch (e) { /* */ }

  els.connectStatus.textContent = 'Scanning...';
  els.deviceList.style.display = 'block';
  els.btnStopScan.style.display = 'block';

  const listener = await CapacitorBLE.addListener('onScanResult', (result) => {
    const dev = result.device || {};
    const info = {
      deviceId: dev.deviceId || result.deviceId || '',
      name: result.localName || dev.name || 'Unknown',
      rssi: result.rssi || 0,
    };
    if (!info.deviceId) return;
    if (!discoveredDevices.has(info.deviceId)) {
      discoveredDevices.set(info.deviceId, info);
      renderDeviceList();
    } else {
      discoveredDevices.set(info.deviceId, info);
      const r = document.getElementById(`rssi-${info.deviceId}`);
      if (r) r.textContent = `${info.rssi} dBm`;
    }
  });
  ble._capListeners.push(listener);

  try {
    await CapacitorBLE.requestLEScan({ allowDuplicates: false });
  } catch (e) {
    els.connectStatus.textContent = `Scan failed: ${e.message}`;
    els.connectStatus.className = 'connect-status error';
    els.btnScan.disabled = false;
    els.btnStopScan.style.display = 'none';
  }
}

els.btnStopScan.addEventListener('click', async () => {
  try { await CapacitorBLE.stopLEScan(); } catch (e) { /* */ }
  els.btnStopScan.style.display = 'none';
  els.connectStatus.textContent = discoveredDevices.size > 0
    ? `Found ${discoveredDevices.size} device(s). Tap Connect.`
    : 'No devices found. Make sure the watch is nearby.';
  els.btnScan.disabled = false;
});

async function connectToDevice(deviceId) {
  try { await CapacitorBLE.stopLEScan(); } catch (e) { /* */ }
  els.btnStopScan.style.display = 'none';
  els.connectStatus.textContent = 'Connecting...';
  els.deviceList.style.display = 'none';
  try {
    await ble.connect(deviceId);
    const data = await ble.readAll();
    Object.assign(watchData, data);
    showDashboard();
    render();
  } catch (e) {
    els.connectStatus.textContent = `Connection failed: ${e.message || String(e)}`;
    els.connectStatus.className = 'connect-status error';
    els.btnScan.disabled = false;
    els.deviceList.style.display = 'none';
  }
}

function renderDeviceList() {
  els.deviceListItems.innerHTML = '';
  for (const [id, dev] of discoveredDevices) {
    const isWatch = dev.name.toLowerCase().includes(WATCH_NAME.toLowerCase());
    const item = document.createElement('div');
    item.className = 'device-item' + (isWatch ? ' highlight' : '');
    item.id = `dev-${id}`;
    item.innerHTML = `
      <div>
        <div class="device-name">${escapeHtml(dev.name)}</div>
        <div class="device-id">${id}</div>
      </div>
      <div style="display:flex;align-items:center;gap:8px">
        <span id="rssi-${id}" class="device-rssi">${dev.rssi} dBm</span>
        <button class="btn-connect-device" data-id="${id}">Connect</button>
      </div>`;
    item.querySelector('.btn-connect-device').addEventListener('click', e => {
      e.stopPropagation();
      connectToDevice(id);
    });
    els.deviceListItems.appendChild(item);
  }
  const watchFound = [...discoveredDevices.values()].some(d =>
    d.name.toLowerCase().includes(WATCH_NAME.toLowerCase()));
  els.connectStatus.textContent = watchFound
    ? 'Found SmartBracelet! Tap Connect.'
    : `Scanning... (${discoveredDevices.size} devices)`;
}

// ── Disconnect ──
els.btnDisconnect.addEventListener('click', () => ble.disconnect());
ble.onDisconnected = () => showConnectScreen();

// ── BLE Data Callbacks ──
ble.onStepsChanged = val => { watchData.steps = val; watchData.lastUpdate = new Date(); render(); };
ble.onActivityChanged = val => { watchData.activity = val; watchData.lastUpdate = new Date(); render(); };
ble.onBatteryChanged = val => { watchData.batteryPercent = val; watchData.lastUpdate = new Date(); render(); };
ble.onAckReceived = text => {
  if (text.startsWith('ack:')) { setNotifyStatus('Delivered', 'success'); updateLog(text.slice(4), 'delivered'); }
};

// ── View State ──
function showConnectScreen() {
  els.connectScreen.classList.remove('hidden');
  els.dashboard.classList.remove('active');
  els.connectStatus.textContent = '';
  els.connectStatus.className = 'connect-status';
  els.btnScan.disabled = false;
  els.deviceList.style.display = 'none';
  els.btnStopScan.style.display = 'none';
}
function showDashboard() {
  els.connectScreen.classList.add('hidden');
  els.dashboard.classList.add('active');
  // Init charts when dashboard first shows
  if (typeof initCharts === 'function') setTimeout(initCharts, 100);
  // Send GPS location to watch for weather
  sendLocationToWatch();
}

// Send phone GPS location to watch via BLE
function sendLocationToWatch() {
  if (!navigator.geolocation || !ble.isConnected) return;
  navigator.geolocation.getCurrentPosition(
    async (pos) => {
      try {
        const lat = pos.coords.latitude.toFixed(4);
        const lon = pos.coords.longitude.toFixed(4);
        await ble.sendLocation(lat, lon);
        console.log(`Location sent: ${lat}, ${lon}`);
      } catch (e) {
        console.warn('Failed to send location:', e.message);
      }
    },
    (err) => { console.warn('Geolocation error:', err.message); },
    { timeout: 10000, maximumAge: 300000 }  // 5 min cache
  );
}

// ── Render ──
function render() {
  const stepsStr = watchData.steps.toLocaleString();
  if (els.stepsValue.textContent !== stepsStr) { els.stepsValue.textContent = stepsStr; flashElement(els.stepsValue); }
  els.stepsBar.style.width = Math.min(100, (watchData.steps / STEPS_GOAL) * 100) + '%';

  if (watchData.batteryPercent >= 0) {
    els.battPercent.textContent = watchData.batteryPercent;
    els.battBar.style.width = watchData.batteryPercent + '%';
    let c = 'var(--accent)';
    if (watchData.batteryPercent <= 20) c = 'var(--red)';
    else if (watchData.batteryPercent <= 50) c = 'var(--amber)';
    els.battPercent.style.color = c;
    els.battBar.style.background = c;
  }
  if (watchData.isUsbPowered) {
    els.battMv.textContent = ''; els.battBadge.textContent = 'USB Powered';
    els.battBadge.className = 'battery-badge usb'; els.battBadge.style.display = 'inline-block';
  } else if (watchData.batteryRawMv > 0) {
    els.battMv.textContent = `${watchData.batteryRawMv} mV`; els.battBadge.style.display = 'none';
  }

  const ac = watchData.activityClass;
  els.actIcon.className = `activity-icon ${ac}`; els.actIcon.innerHTML = watchData.activityIcon;
  if (els.actName.textContent !== watchData.activityName) { els.actName.textContent = watchData.activityName; flashElement(els.actName); }
  els.actSub.textContent = ac === 'idle' ? 'No movement detected' : `${ac === 'run' ? 'High' : 'Moderate'} intensity`;

  if (watchData.lastUpdate) els.lastUpdate.textContent = `Last update: ${watchData.lastUpdate.toLocaleTimeString()}`;
  updateDebug();
  els.btnSend.disabled = !ble.isConnected;
  // Update charts with new data
  if (typeof updateCharts === 'function') {
    updateCharts(watchData.steps, watchData.batteryPercent, watchData.activity);
  }
}
function flashElement(el) { el.classList.remove('value-changed'); void el.offsetWidth; el.classList.add('value-changed'); }
function updateDebug() {
  els.debugContent.textContent = [
    `Steps:      ${watchData.steps} (0x${watchData.steps.toString(16).padStart(8,'0')})`,
    `Battery:    ${watchData.batteryPercent}%  (${watchData.batteryRawMv} mV)`,
    `Activity:   ${watchData.activity} (${watchData.activityName})`,
    `USB Power:  ${watchData.isUsbPowered ? 'Yes' : 'No'}`,
  ].join('\n');
}

// ── Notify Panel ──
els.notifyCard.addEventListener('click', e => {
  if (e.target.closest('.input-field') || e.target.closest('.btn-send')) return;
  els.notifyBody.classList.toggle('open'); els.notifyArrow.classList.toggle('open');
});

// ── History Panel ──
const historyCard = $('historyCard');
if (historyCard) {
  historyCard.addEventListener('click', e => {
    if (e.target.closest('canvas')) return;
    $('historyBody').classList.toggle('open');
    $('historyArrow').classList.toggle('open');
  });
}

// ── OTA Panel ──
const otaCard = $('otaCard');
if (otaCard) {
  otaCard.addEventListener('click', e => {
    if (e.target.closest('.input-field') || e.target.closest('.btn-send')) return;
    $('otaBody').classList.toggle('open');
    $('otaArrow').classList.toggle('open');
  });
}

els.debugToggle.addEventListener('click', () => { els.debugBody.classList.toggle('open'); els.debugArrow.classList.toggle('open'); });
els.btnSend.addEventListener('click', async () => {
  const appId = els.inputAppId.value.trim(), title = els.inputTitle.value.trim(), body = els.inputBody.value.trim();
  if (!appId || !title || !body) { setNotifyStatus('Fill in all fields', 'error'); return; }
  els.btnSend.disabled = true; setNotifyStatus('Sending...', 'pending');
  try {
    await ble.sendNotification(appId, title, body);
    setNotifyStatus('Sent, waiting for ACK...', 'pending'); updateLog(appId, 'sent');
    setTimeout(() => { if (els.notifyStatus.textContent.includes('waiting')) setNotifyStatus('Sent (no ACK)', 'success'); }, 3000);
  } catch (e) { setNotifyStatus(`Failed: ${e.message}`, 'error'); }
  finally { els.btnSend.disabled = false; }
});
function setNotifyStatus(t, c) { els.notifyStatus.textContent = t; els.notifyStatus.className = `notify-status ${c}`; }
function updateLog(id, st) {
  const now = new Date().toLocaleTimeString(), title = els.inputTitle.value.trim() || id;
  notifyLog.unshift({ time: now, title, status: st });
  if (notifyLog.length > 5) notifyLog.pop();
  els.notifyLogList.innerHTML = notifyLog.map(i => {
    const icon = i.status === 'delivered' ? '&#x2705;' : '&#x1F4E4;';
    return `<div class="notify-log-item"><span class="log-msg">${icon} ${escapeHtml(i.title)}</span><span class="log-time">${i.time}</span></div>`;
  }).join('');
  els.notifyLog.style.display = 'block';
}
function escapeHtml(s) { const d = document.createElement('div'); d.textContent = s; return d.innerHTML; }

// ── Init ──
initChat();
if (typeof initOTA === 'function') initOTA();
