/* SmartBracelet — Chat (chat.js) */
// Voice input + DeepSeek LLM integration

let recognition = null;
let isRecording = false;
const voiceChatLog = [];
const chatHistory = [];  // multi-turn conversation

const $ = id => document.getElementById(id);

// ── Event Bindings (called after DOM ready) ──
function initChat() {
  $('voiceCard').addEventListener('click', e => {
    if (e.target.closest('.input-field') || e.target.closest('.btn-send')) return;
    $('voiceBody').classList.toggle('open');
    $('voiceArrow').classList.toggle('open');
    const hint = $('bleStatusHint');
    if (ble.isConnected) {
      hint.textContent = '\u{1F7E2} Watch connected';
      hint.style.color = 'var(--green)';
    } else {
      hint.textContent = '\u26A0\uFE0F Watch not connected \u2014 use "Scan for Devices" first';
      hint.style.color = 'var(--amber)';
    }
    hint.style.display = 'block';
  });

  $('inputChatText').addEventListener('keydown', e => {
    if (e.key === 'Enter') { e.preventDefault(); sendTextChat(); }
  });
  $('btnSendChat').addEventListener('click', () => sendTextChat());
  $('btnRecord').addEventListener('click', () => {
    if (isRecording) stopVoiceInput();
    else startVoiceInput();
  });

  // Restore API key
  const savedKey = localStorage.getItem('deepseek_api_key');
  if (savedKey) $('inputApiKey').value = savedKey;
}

// ── Voice Input ──
function startVoiceInput() {
  const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
  if (!SpeechRecognition) {
    setVoiceStatus('Speech recognition not supported in this browser', 'error');
    return;
  }
  recognition = new SpeechRecognition();
  recognition.lang = 'zh-CN';
  recognition.interimResults = false;
  recognition.maxAlternatives = 1;

  recognition.onresult = (event) => {
    const text = event.results[0][0].transcript;
    $('inputChatText').value = text;
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

// ── Text Chat ──
async function sendTextChat() {
  const text = $('inputChatText').value.trim();
  if (!text) return;
  const apiKey = $('inputApiKey').value.trim();
  if (!apiKey) { setVoiceStatus('Enter API key first', 'error'); return; }
  localStorage.setItem('deepseek_api_key', apiKey);

  $('inputChatText').value = '';
  $('voiceTransText').textContent = text;
  $('voiceTransBox').style.display = 'block';
  setVoiceStatus('Thinking...', 'pending');

  try {
    chatHistory.push({ role: 'user', content: text });
    const messages = [
      { role: 'system', content: 'You are a smartwatch assistant. Reply shortly (under 80 words). Match the user language.' },
      ...chatHistory.slice(-10),
    ];

    const resp = await fetch('https://api.deepseek.com/v1/chat/completions', {
      method: 'POST',
      headers: { 'Authorization': `Bearer ${apiKey}`, 'Content-Type': 'application/json' },
      body: JSON.stringify({ model: 'deepseek-chat', messages }),
    });
    if (!resp.ok) throw new Error(`API ${resp.status}: ${await resp.text()}`);
    const data = await resp.json();
    const reply = data.choices[0].message.content;

    chatHistory.push({ role: 'assistant', content: reply });
    $('voiceRespText').textContent = reply;
    $('voiceRespBox').style.display = 'block';
    setVoiceStatus('Done', 'success');
    updateVoiceChatLog(text, reply);

    // Send to watch via BLE
    const bleHint = $('bleStatusHint');
    if (ble.isConnected) {
      try {
        await ble.sendNotification('voice', 'result', `${text}|${reply}`);
        bleHint.textContent = '\u2705 Sent to watch';
        bleHint.style.color = 'var(--green)';
        bleHint.style.display = 'block';
      } catch (e) {
        bleHint.textContent = `\u274C Watch send failed: ${e.message}`;
        bleHint.style.color = 'var(--red)';
        bleHint.style.display = 'block';
      }
    } else {
      bleHint.textContent = '\u26A0\uFE0F Watch not connected. Use "Scan for Devices" to connect first.';
      bleHint.style.color = 'var(--amber)';
      bleHint.style.display = 'block';
    }
  } catch (e) {
    console.error('Chat error:', e);
    setVoiceStatus(`Error: ${e.message}`, 'error');
    if (ble.isConnected) {
      try { await ble.sendNotification('voice', 'error', e.message); } catch (e2) {}
    }
  }
}

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
