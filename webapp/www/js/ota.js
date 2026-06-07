/* SmartBracelet — OTA Update (ota.js) */
// BLE firmware push from phone/browser

const $ = id => document.getElementById(id);
const CHUNK_SIZE = 480; // bytes per BLE write (MTU ~512 - overhead)

let otaInProgress = false;

function initOTA() {
  const btn = $('btnOtaSend');
  const fileInput = $('otaFile');
  if (!btn || !fileInput) return;

  btn.addEventListener('click', async () => {
    const file = fileInput.files[0];
    if (!file) {
      setOtaStatus('Select a .bin file first', 'error');
      return;
    }
    if (!file.name.endsWith('.bin')) {
      setOtaStatus('Only .bin firmware files allowed', 'error');
      return;
    }
    if (!ble.isConnected) {
      setOtaStatus('Connect to watch first', 'error');
      return;
    }

    await startBleOTA(file);
  });
}

async function startBleOTA(file) {
  if (otaInProgress) {
    setOtaStatus('OTA already in progress', 'error');
    return;
  }
  otaInProgress = true;
  const totalSize = file.size;
  const btn = $('btnOtaSend');
  btn.disabled = true;

  setOtaStatus(`Starting OTA: ${(totalSize/1024).toFixed(0)} KB...`, 'pending');
  setOtaProgress(0);

  try {
    // Send start command as text: "ota_ble|<size>"
    const startCmd = new TextEncoder().encode(`ota_ble|${totalSize}`);
    await ble.writeRaw(startCmd);

    // Wait a bit for watch to prepare
    await sleep(500);

    // Read file and send chunks
    const arrayBuffer = await file.arrayBuffer();
    const data = new Uint8Array(arrayBuffer);
    let offset = 0;
    let chunkNum = 0;
    const totalChunks = Math.ceil(totalSize / CHUNK_SIZE);

    while (offset < totalSize) {
      const end = Math.min(offset + CHUNK_SIZE, totalSize);
      const chunk = data.slice(offset, end);

      // Send chunk as raw bytes via BLE
      await ble.writeRaw(chunk);

      offset = end;
      chunkNum++;
      const pct = Math.round((offset / totalSize) * 100);
      setOtaProgress(pct);
      setOtaStatus(`Sending: ${pct}% (${chunkNum}/${totalChunks} chunks)`, 'pending');

      // Throttle to avoid overwhelming BLE buffer
      if (chunkNum % 10 === 0) await sleep(50);
    }

    // Send end command
    await sleep(200);
    const endCmd = new TextEncoder().encode('ota_ble_end');
    await ble.writeRaw(endCmd);

    setOtaStatus('Firmware sent! Watch will reboot in 3s...', 'success');
    setOtaProgress(100);

  } catch (e) {
    setOtaStatus(`OTA failed: ${e.message}`, 'error');
  } finally {
    otaInProgress = false;
    btn.disabled = false;
  }
}

function setOtaStatus(text, cls) {
  const el = $('otaStatus');
  if (el) {
    el.textContent = text;
    el.className = `notify-status ${cls || ''}`;
  }
}

function setOtaProgress(pct) {
  const bar = $('otaProgress');
  if (bar) bar.style.width = pct + '%';
  const lbl = $('otaProgressLabel');
  if (lbl) lbl.textContent = pct + '%';
}

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }
