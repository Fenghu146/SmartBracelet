/* SmartBracelet — BLE Service (ble.js) */

// ═══════════════════════════════════════════════════════════════
// Platform detection
// ═══════════════════════════════════════════════════════════════
const isNative = !!(window.Capacitor && window.Capacitor.isNativePlatform && window.Capacitor.isNativePlatform());
const CapacitorBLE = isNative ? window.Capacitor?.Plugins?.BLE : null;

// ═══════════════════════════════════════════════════════════════
// BLE UUIDs — must match firmware (src/service/ble_srv.cpp)
// ═══════════════════════════════════════════════════════════════
const UUID = {
  DATA_SERVICE:      'abcd1000-0000-1000-8000-00805f9b34fb',
  STEPS_CHAR:        'abcd1001-0000-1000-8000-00805f9b34fb',
  BATT_RAW_CHAR:     'abcd1002-0000-1000-8000-00805f9b34fb',
  ACTIVITY_CHAR:     'abcd1003-0000-1000-8000-00805f9b34fb',
  NOTIFY_SERVICE:    'abcd0001-0000-1000-8000-00805f9b34fb',
  NOTIFY_RX_CHAR:    'abcd0002-0000-1000-8000-00805f9b34fb',
  NOTIFY_TX_CHAR:    'abcd0003-0000-1000-8000-00805f9b34fb',
  BATTERY_SERVICE:   '0000180f-0000-1000-8000-00805f9b34fb',
  BATTERY_LEVEL:     '00002a19-0000-1000-8000-00805f9b34fb',
  TIME_SERVICE:      '00001805-0000-1000-8000-00805f9b34fb',
  CURRENT_TIME:      '00002a2b-0000-1000-8000-00805f9b34fb',
};

const WATCH_NAME = 'SmartBracelet';

// Initialize Capacitor BLE plugin
if (isNative && CapacitorBLE) {
  CapacitorBLE.initialize({ displayStrings: { scanning: 'Scanning...' } })
    .then(() => console.log('[BLE] Capacitor plugin initialized'))
    .catch(e => console.warn('[BLE] Init warning:', e.message));
}

// ═══════════════════════════════════════════════════════════════
// Base64 <-> Uint8Array helpers
// ═══════════════════════════════════════════════════════════════
function b64ToBytes(b64) {
  const bin = atob(b64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  return bytes;
}
function bytesToB64(bytes) {
  let bin = '';
  for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
  return btoa(bin);
}

// ═══════════════════════════════════════════════════════════════
// WatchData Model
// ═══════════════════════════════════════════════════════════════
class WatchData {
  constructor() {
    this.steps = 0; this.batteryPercent = -1; this.batteryRawMv = 0;
    this.activity = 2; this.lastUpdate = null;
  }
  get activityName() { return ['Walk', 'Run', 'Idle'][this.activity] || 'Unknown'; }
  get activityIcon() { return ['&#x1F6B6;', '&#x1F3C3;', '&#x23F8;'][this.activity] || '?'; }
  get activityClass() { return ['walk', 'run', 'idle'][this.activity] || 'idle'; }
  get isUsbPowered() { return this.batteryRawMv === 0xFFFF; }
}

// ═══════════════════════════════════════════════════════════════
// BLE Service — unified API for Web Bluetooth + Capacitor
// ═══════════════════════════════════════════════════════════════
class BleService {
  constructor() {
    this.device = null;        // Web Bluetooth device object
    this.deviceId = null;      // Capacitor device ID (MAC)
    this.server = null;        // Web Bluetooth GATT server
    this.chars = {};           // Web Bluetooth characteristics
    this.connected = false;
    this.onStepsChanged = null;
    this.onActivityChanged = null;
    this.onBatteryChanged = null;
    this.onAckReceived = null;
    this.onDisconnected = null;
    this._capListeners = [];
  }

  // ── Web Bluetooth: requestDevice shows system picker ──
  async connectWeb() {
    console.log('[BLE] Web: requesting device (no filter, shows all nearby BLE devices)...');
    this.device = await navigator.bluetooth.requestDevice({
      acceptAllDevices: true,
      optionalServices: [
        UUID.DATA_SERVICE, UUID.NOTIFY_SERVICE,
        UUID.BATTERY_SERVICE, UUID.TIME_SERVICE,
      ],
    });
    console.log('[BLE] Web: selected', this.device.name, this.device.id);

    this.device.addEventListener('gattserverdisconnected', () => {
      this.connected = false;
      this.chars = {};
      this.server = null;
      if (this.onDisconnected) this.onDisconnected();
    });

    this.server = await this.device.gatt.connect();
    this.connected = true;
    this.deviceId = this.device.id;

    // Discover characteristics
    await this._webDiscover(UUID.DATA_SERVICE, UUID.STEPS_CHAR, 'steps');
    await this._webDiscover(UUID.DATA_SERVICE, UUID.BATT_RAW_CHAR, 'battRaw');
    await this._webDiscover(UUID.DATA_SERVICE, UUID.ACTIVITY_CHAR, 'activity');
    await this._webDiscover(UUID.BATTERY_SERVICE, UUID.BATTERY_LEVEL, 'battLevel');
    await this._webDiscover(UUID.NOTIFY_SERVICE, UUID.NOTIFY_RX_CHAR, 'notifyRx');
    await this._webDiscover(UUID.NOTIFY_SERVICE, UUID.NOTIFY_TX_CHAR, 'notifyTx');

    // Subscribe to notifications
    await this._webNotify('steps', dv => {
      if (this.onStepsChanged) this.onStepsChanged(dv.getUint32(0, true));
    });
    await this._webNotify('activity', dv => {
      if (this.onActivityChanged) this.onActivityChanged(dv.getUint8(0));
    });
    await this._webNotify('battLevel', dv => {
      if (this.onBatteryChanged) this.onBatteryChanged(dv.getUint8(0));
    });
    await this._webNotify('notifyTx', dv => {
      const text = new TextDecoder().decode(dv.buffer);
      if (this.onAckReceived) this.onAckReceived(text);
    });

    await this._syncTimeWeb();
  }

  async _webDiscover(svcUUID, charUUID, name) {
    try {
      const svc = await this.server.getPrimaryService(svcUUID);
      this.chars[name] = await svc.getCharacteristic(charUUID);
    } catch (e) { console.warn(`[BLE] Char ${name} not found:`, e.message); }
  }

  async _webNotify(name, cb) {
    const ch = this.chars[name];
    if (!ch) return;
    ch.addEventListener('characteristicvaluechanged', e => cb(e.target.value));
    try { await ch.startNotifications(); } catch (e) { console.warn(`[BLE] Notify ${name} fail:`, e.message); }
  }

  async readAllWeb() {
    const data = new WatchData();
    try {
      if (this.chars.steps) { const dv = await this.chars.steps.readValue(); data.steps = dv.getUint32(0, true); }
      if (this.chars.battLevel) { const dv = await this.chars.battLevel.readValue(); data.batteryPercent = dv.getUint8(0); }
      if (this.chars.battRaw) { const dv = await this.chars.battRaw.readValue(); data.batteryRawMv = dv.getUint16(0, true); }
      if (this.chars.activity) { const dv = await this.chars.activity.readValue(); data.activity = dv.getUint8(0); }
    } catch (e) { console.warn('[BLE] readAll error:', e.message); }
    data.lastUpdate = new Date();
    return data;
  }

  async sendNotificationWeb(appId, title, body) {
    const ch = this.chars.notifyRx;
    if (!ch) throw new Error('Notify characteristic not available');
    await ch.writeValue(new TextEncoder().encode(`${appId}|${title}|${body}`));
  }

  async _syncTimeWeb() {
    try {
      const svc = await this.server.getPrimaryService(UUID.TIME_SERVICE);
      const ch = await svc.getCharacteristic(UUID.CURRENT_TIME);
      const now = new Date();
      const dow = now.getDay() === 0 ? 7 : now.getDay();
      await ch.writeValue(new Uint8Array([
        now.getFullYear() % 100, Math.floor(now.getFullYear() / 100),
        now.getMonth() + 1, now.getDate(), now.getHours(), now.getMinutes(), now.getSeconds(),
        dow, 0, 0,
      ]));
    } catch (e) { console.warn('[BLE] Time sync failed:', e.message); }
  }

  disconnectWeb() {
    if (this.device && this.device.gatt.connected) this.device.gatt.disconnect();
  }

  // ── Capacitor: scan + connect ──
  async connectCapacitor(deviceId) {
    this.deviceId = deviceId;
    await CapacitorBLE.connect({ deviceId });
    this.connected = true;

    const d1 = await CapacitorBLE.addListener('onDisconnect', ({ deviceId: did }) => {
      if (did === this.deviceId) { this.connected = false; if (this.onDisconnected) this.onDisconnected(); }
    });
    this._capListeners.push(d1);

    const d2 = await CapacitorBLE.addListener('onNotifications', ({ deviceId: did, characteristic, value }) => {
      if (did !== this.deviceId) return;
      const bytes = b64ToBytes(value);
      if (characteristic === UUID.STEPS_CHAR && bytes.length >= 4 && this.onStepsChanged)
        this.onStepsChanged(new DataView(bytes.buffer).getUint32(0, true));
      else if (characteristic === UUID.ACTIVITY_CHAR && bytes.length >= 1 && this.onActivityChanged)
        this.onActivityChanged(bytes[0]);
      else if (characteristic === UUID.BATTERY_LEVEL && bytes.length >= 1 && this.onBatteryChanged)
        this.onBatteryChanged(bytes[0]);
      else if (characteristic === UUID.NOTIFY_TX_CHAR) {
        const text = new TextDecoder().decode(bytes);
        if (this.onAckReceived) this.onAckReceived(text);
      }
    });
    this._capListeners.push(d2);

    for (const [svc, chr] of [
      [UUID.DATA_SERVICE, UUID.STEPS_CHAR], [UUID.DATA_SERVICE, UUID.ACTIVITY_CHAR],
      [UUID.BATTERY_SERVICE, UUID.BATTERY_LEVEL], [UUID.NOTIFY_SERVICE, UUID.NOTIFY_TX_CHAR],
    ]) {
      try { await CapacitorBLE.startNotifications({ deviceId, service: svc, characteristic: chr }); } catch (e) { /* */ }
    }
    await this._syncTimeCap();
  }

  async readAllCap() {
    const data = new WatchData();
    try {
      const s = b64ToBytes((await CapacitorBLE.read({ deviceId: this.deviceId, service: UUID.DATA_SERVICE, characteristic: UUID.STEPS_CHAR })).value);
      if (s.length >= 4) data.steps = new DataView(s.buffer).getUint32(0, true);
      const b = b64ToBytes((await CapacitorBLE.read({ deviceId: this.deviceId, service: UUID.BATTERY_SERVICE, characteristic: UUID.BATTERY_LEVEL })).value);
      if (b.length >= 1) data.batteryPercent = b[0];
      const r = b64ToBytes((await CapacitorBLE.read({ deviceId: this.deviceId, service: UUID.DATA_SERVICE, characteristic: UUID.BATT_RAW_CHAR })).value);
      if (r.length >= 2) data.batteryRawMv = new DataView(r.buffer).getUint16(0, true);
      const a = b64ToBytes((await CapacitorBLE.read({ deviceId: this.deviceId, service: UUID.DATA_SERVICE, characteristic: UUID.ACTIVITY_CHAR })).value);
      if (a.length >= 1) data.activity = a[0];
    } catch (e) { console.warn('[BLE] readAll error:', e.message); }
    data.lastUpdate = new Date();
    return data;
  }

  async sendNotificationCap(appId, title, body) {
    const payload = (appId === 'voice') ? `voice:${title}|${body}` : `${appId}|${title}|${body}`;
    await CapacitorBLE.write({
      deviceId: this.deviceId, service: UUID.NOTIFY_SERVICE,
      characteristic: UUID.NOTIFY_RX_CHAR,
      value: bytesToB64(new TextEncoder().encode(payload)),
    });
  }

  async _syncTimeCap() {
    try {
      const now = new Date(); const dow = now.getDay() === 0 ? 7 : now.getDay();
      await CapacitorBLE.write({
        deviceId: this.deviceId, service: UUID.TIME_SERVICE, characteristic: UUID.CURRENT_TIME,
        value: bytesToB64(new Uint8Array([now.getFullYear()%100,Math.floor(now.getFullYear()/100),
          now.getMonth()+1,now.getDate(),now.getHours(),now.getMinutes(),now.getSeconds(),dow,0,0])),
      });
    } catch (e) { console.warn('[BLE] Time sync failed:', e.message); }
  }

  disconnectCap() {
    if (this.deviceId) { CapacitorBLE?.disconnect({ deviceId: this.deviceId }).catch(()=>{}); }
    this.connected = false; this.deviceId = null;
  }

  // ── Unified API ──
  get isConnected() { return this.connected; }

  async connect(deviceId) {
    if (isNative) return this.connectCapacitor(deviceId);
    return this.connectWeb();
  }
  async readAll() { return isNative ? this.readAllCap() : this.readAllWeb(); }
  async sendNotification(a, t, b) { return isNative ? this.sendNotificationCap(a,t,b) : this.sendNotificationWeb(a,t,b); }
  disconnect() { isNative ? this.disconnectCap() : this.disconnectWeb(); }

  // Write raw bytes to notify RX (for BLE OTA chunks)
  async writeRaw(bytes) {
    if (isNative) {
      await CapacitorBLE.write({
        deviceId: this.deviceId, service: UUID.NOTIFY_SERVICE,
        characteristic: UUID.NOTIFY_RX_CHAR,
        value: bytesToB64(bytes),
      });
    } else {
      const ch = this.chars.notifyRx;
      if (!ch) throw new Error('Notify RX not available');
      await ch.writeValue(bytes);
    }
  }

  // Send GPS location to watch for weather
  async sendLocation(lat, lon) {
    const cmd = `loc:${lat}|${lon}`;
    await this.writeRaw(new TextEncoder().encode(cmd));
    console.log(`[BLE] Sent location: ${lat}, ${lon}`);
  }
}
