/* SmartBracelet — Xiaozhi AI Chat (chat.js)
 * Replaces DeepSeek text chat with Xiaozhi WebSocket protocol.
 * Voice input via Web Speech API, communication via WebSocket,
 * results forwarded to watch via BLE.
 */

const xzClient = new XiaozhiClient();
let recognition = null;
let isRecording = false;
const voiceChatLog = [];
let lastSTTText = '';
let lastTTSText = '';

const $ = id => document.getElementById(id);

// ── Event Bindings ──
function initChat() {
  $('voiceCard').addEventListener('click', e => {
    if (e.target.closest('.input-field') || e.target.closest('.btn-send')) return;
    $('voiceBody').classList.toggle('open');
    $('voiceArrow').classList.toggle('open');
    updateBleHint();
  });

  $('inputChatText').addEventListener('keydown', e => {
    if (e.key === 'Enter') { e.preventDefault(); sendTextChat(); }
  });
  $('btnSendChat').addEventListener('click', () => sendTextChat());
  $('btnRecord').addEventListener('click', () => {
    if (isRecording) stopVoiceInput();
    else startVoiceInput();
  });
  $('btnXzConnect').addEventListener('click', toggleXzConnection);

  // Restore saved config
  const savedUrl = localStorage.getItem('xz_ws_url');
  const savedToken = localStorage.getItem('xz_token');
  if (savedUrl) $('inputWsUrl').value = savedUrl;
  if (savedToken) $('inputToken').value = savedToken;

  // Setup xiaozhi callbacks
  xzClient.onConnected = () => {
    setXzStatus('Connected', 'success');
    $('btnXzConnect').textContent = 'Disconnect';
    $('btnXzConnect').style.background = 'var(--red)';
    setVoiceStatus('Ready - Xiaozhi connected', 'success');
  };
  xzClient.onDisconnected = () => {
    setXzStatus('Disconnected', '');
    $('btnXzConnect').textContent = 'Connect';
    $('btnXzConnect').style.background = 'var(--green)';
    setVoiceStatus('Disconnected', '');
  };
  xzClient.onSTT = (text) => {
    lastSTTText = text;
    $('voiceTransText').textContent = text;
    $('voiceTransBox').style.display = 'block';
    setVoiceStatus('Heard: ' + text, 'pending');
  };
  xzClient.onTTSState = (state) => {
    if (state === 'start') {
      setVoiceStatus('AI speaking...', 'pending');
    } else if (state === 'stop') {
      setVoiceStatus('Done', 'success');
      // Send results to watch
      sendToWatch(lastSTTText, lastTTSText);
      updateVoiceChatLog(lastSTTText, lastTTSText);
      lastTTSText = '';
    }
  };
  xzClient.onTTSText = (text) => {
    lastTTSText = text;
    $('voiceRespText').textContent = text;
    $('voiceRespBox').style.display = 'block';
  };
  xzClient.onError = (msg) => {
    setVoiceStatus('Error: ' + msg, 'error');
    setXzStatus('Error', 'error');
  };
  xzClient.onLog = (msg) => {
    console.log('[XZ]', msg);
  };
}

function updateBleHint() {
  const hint = $('bleStatusHint');
  if (ble.isConnected) {
    hint.textContent = '\u{1F7E2} Watch connected';
    hint.style.color = 'var(--green)';
  } else {
    hint.textContent = '\u26A0\uFE0F Watch not connected';
    hint.style.color = 'var(--amber)';
  }
  hint.style.display = 'block';
}

// ── Xiaozhi Connection ──
async function toggleXzConnection() {
  if (xzClient.connected) {
    xzClient.disconnect();
    return;
  }
  const url = $('inputWsUrl').value.trim();
  const token = $('inputToken').value.trim();
  if (!url) { setXzStatus('Enter WebSocket URL', 'error'); return; }

  // Save config
  localStorage.setItem('xz_ws_url', url);
  localStorage.setItem('xz_token', token);

  setXzStatus('Connecting...', 'pending');
  $('btnXzConnect').disabled = true;

  try {
    await xzClient.connect(url, token, 2);
  } catch (e) {
    setXzStatus('Failed: ' + e.message, 'error');
  }
  $('btnXzConnect').disabled = false;
}

function setXzStatus(text, cls) {
  $('xzStatus').textContent = text;
  $('xzStatus').className = `notify-status ${cls || ''}`;
}

// ── Voice Input (Web Speech API) ──
function startVoiceInput() {
  if (!xzClient.connected) {
    setVoiceStatus('Connect to Xiaozhi first', 'error');
    return;
  }
  const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
  if (!SpeechRecognition) {
    setVoiceStatus('Speech recognition not supported', 'error');
    return;
  }
  recognition = new SpeechRecognition();
  recognition.lang = 'zh-CN';
  recognition.interimResults = false;
  recognition.maxAlternatives = 1;

  recognition.onresult = (event) => {
    const text = event.results[0][0].transcript;
    $('inputChatText').value = text;
    // Send via xiaozhi protocol
    sendTextChat();
  };
  recognition.onerror = (e) => {
    if (e.error !== 'no-speech') setVoiceStatus(`Voice error: ${e.error}`, 'error');
    resetRecordBtn();
  };
  recognition.onend = () => { resetRecordBtn(); };

  recognition.start();
  isRecording = true;
  $('btnRecord').textContent = '\u23F9 Listening...';
  $('btnRecord').style.background = 'var(--red)';
  setVoiceStatus('Listening... speak now', 'pending');
}

function stopVoiceInput() {
  if (recognition) recognition.stop();
  resetRecordBtn();
}

function resetRecordBtn() {
  isRecording = false;
  $('btnRecord').textContent = '\u{1F3A4} Voice Input';
  $('btnRecord').style.background = 'var(--green)';
}

// ── Text Chat via Xiaozhi ──
function sendTextChat() {
  const text = $('inputChatText').value.trim();
  if (!text) return;
  if (!xzClient.connected) {
    setVoiceStatus('Connect to Xiaozhi first', 'error');
    return;
  }

  $('inputChatText').value = '';
  $('voiceTransText').textContent = text;
  $('voiceTransBox').style.display = 'block';
  lastSTTText = text;
  lastTTSText = '';
  setVoiceStatus('Thinking...', 'pending');

  // Send text via xiaozhi protocol
  const ok = xzClient.sendTextMessage(text);
  if (!ok) {
    setVoiceStatus('Send failed - reconnect and try again', 'error');
  }
}

// ── Send results to watch via BLE ──
async function sendToWatch(transcription, response) {
  if (!ble.isConnected) return;
  const bleHint = $('bleStatusHint');
  try {
    const text = `${transcription}|${response}`;
    await ble.sendNotification('voice', 'result', text);
    bleHint.textContent = '\u2705 Sent to watch';
    bleHint.style.color = 'var(--green)';
    bleHint.style.display = 'block';
  } catch (e) {
    bleHint.textContent = `\u274C Watch: ${e.message}`;
    bleHint.style.color = 'var(--red)';
    bleHint.style.display = 'block';
  }
}

// ── UI Helpers ──
function setVoiceStatus(text, cls) {
  $('voiceStatus').textContent = text;
  $('voiceStatus').className = `notify-status ${cls || ''}`;
}

function updateVoiceChatLog(trans, resp) {
  const now = new Date().toLocaleTimeString();
  voiceChatLog.unshift({ time: now, trans, resp });
  if (voiceChatLog.length > 5) voiceChatLog.pop();
  $('voiceLogList').innerHTML = voiceChatLog.map(i =>
    `<div class="notify-log-item"><span class="log-msg">${escapeHtml(i.trans)}</span><span class="log-time">${i.time}</span></div>`
  ).join('');
  $('voiceLog').style.display = 'block';
}
