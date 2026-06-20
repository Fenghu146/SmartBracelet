/* SmartBracelet — Xiaozhi AI WebSocket Client (xiaozhi.js)
 * Protocol reference: xiaozhi-esp32 v2.2.4 websocket_protocol.cc
 * 
 * Flow:
 *   1. Connect WebSocket with Bearer token
 *   2. Send client hello → receive server hello (session_id)
 *   3. Send listen start → stream audio / text → send listen stop
 *   4. Receive STT results + TTS audio (OPUS) + state events
 */

class XiaozhiClient {
  constructor() {
    this.ws = null;
    this.sessionId = null;
    this.connected = false;
    this.serverSampleRate = 24000;
    this.serverFrameDuration = 60;
    this.version = 2;
    this.audioCtx = null;
    this.audioQueue = [];
    this.isPlaying = false;

    // Callbacks
    this.onConnected = null;     // () => {}
    this.onDisconnected = null;  // () => {}
    this.onSTT = null;           // (text) => {}  - speech-to-text result
    this.onTTSState = null;      // (state) => {} - "start" | "stop"
    this.onTTSText = null;       // (text) => {}  - server text response (if any)
    this.onError = null;         // (msg) => {}
    this.onLog = null;           // (msg) => {}
  }

  _log(msg) {
    console.log(`[Xiaozhi] ${msg}`);
    if (this.onLog) this.onLog(msg);
  }

  /** Connect to xiaozhi WebSocket server */
  async connect(url, token, version) {
    if (this.ws) this.disconnect();
    this.version = version || 2;

    return new Promise((resolve, reject) => {
      try {
        this.ws = new WebSocket(url);
        this.ws.binaryType = 'arraybuffer';
      } catch (e) {
        reject(new Error(`WebSocket create failed: ${e.message}`));
        return;
      }

      const timeout = setTimeout(() => {
        this.ws?.close();
        reject(new Error('Connection timeout (10s)'));
      }, 10000);

      this.ws.onopen = () => {
        this._log(`Connected to ${url}`);
        // Set headers via subprotocol (browser WebSocket can't set custom headers)
        // Instead we send auth in hello message
        this._sendHello(token);
      };

      this.ws.onmessage = (event) => {
        if (typeof event.data === 'string') {
          this._handleJsonMessage(event.data, resolve, timeout);
        } else if (event.data instanceof ArrayBuffer) {
          this._handleBinaryAudio(event.data);
        }
      };

      this.ws.onerror = (e) => {
        clearTimeout(timeout);
        const msg = 'WebSocket error';
        this._log(msg);
        if (this.onError) this.onError(msg);
        reject(new Error(msg));
      };

      this.ws.onclose = (e) => {
        clearTimeout(timeout);
        this.connected = false;
        this.sessionId = null;
        this._log(`Disconnected (code=${e.code})`);
        if (this.onDisconnected) this.onDisconnected();
      };
    });
  }

  /** Disconnect from server */
  disconnect() {
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    this.connected = false;
    this.sessionId = null;
    this._stopAudioPlayback();
  }

  /** Send client hello message */
  _sendHello(token) {
    const hello = {
      type: 'hello',
      version: this.version,
      transport: 'websocket',
      audio_params: {
        format: 'opus',
        sample_rate: 16000,
        channels: 1,
        frame_duration: 60
      }
    };
    // Include token in hello for auth (since browser WebSocket can't set headers)
    if (token) {
      hello.token = token;
    }
    this._sendJson(hello);
    this._log('Sent client hello');
  }

  /** Handle incoming JSON message */
  _handleJsonMessage(data, connectResolve, connectTimeout) {
    let msg;
    try { msg = JSON.parse(data); } catch (e) { return; }

    const type = msg.type;
    this._log(`RX json: type=${type}`);

    if (type === 'hello') {
      // Server hello - extract session_id and audio params
      this.sessionId = msg.session_id || '';
      if (msg.audio_params) {
        if (msg.audio_params.sample_rate) this.serverSampleRate = msg.audio_params.sample_rate;
        if (msg.audio_params.frame_duration) this.serverFrameDuration = msg.audio_params.frame_duration;
      }
      this.connected = true;
      this._log(`Session: ${this.sessionId}, server SR=${this.serverSampleRate}`);
      if (connectTimeout) clearTimeout(connectTimeout);
      if (connectResolve) connectResolve(true);
      if (this.onConnected) this.onConnected();
      return;
    }

    if (type === 'stt') {
      // Speech-to-text result
      const text = msg.text || '';
      this._log(`STT: ${text}`);
      if (this.onSTT) this.onSTT(text);
      return;
    }

    if (type === 'tts') {
      // TTS state event
      const state = msg.state || '';
      this._log(`TTS state: ${state}`);
      if (this.onTTSState) this.onTTSState(state);
      // Some servers also include text
      if (msg.text && this.onTTSText) this.onTTSText(msg.text);
      return;
    }

    if (type === 'llm') {
      // LLM text response (some server versions)
      const text = msg.text || msg.content || '';
      if (text && this.onTTSText) this.onTTSText(text);
      return;
    }

    if (type === 'error') {
      const errMsg = msg.message || msg.error || 'Server error';
      this._log(`Error: ${errMsg}`);
      if (this.onError) this.onError(errMsg);
      return;
    }
  }

  /** Handle incoming binary audio (OPUS frames) */
  _handleBinaryAudio(data) {
    if (!data || data.byteLength === 0) return;

    if (this.version === 2 && data.byteLength > 16) {
      // BinaryProtocol2: 16-byte header + payload
      const view = new DataView(data);
      const headerType = view.getUint16(2);  // type field at offset 2
      if (headerType === 0) {
        // OPUS audio frame
        const payloadSize = view.getUint32(12);  // payload_size at offset 12
        const payload = new Uint8Array(data, 16, payloadSize);
        this._enqueueOpusFrame(payload);
        return;
      }
      // type === 1 would be JSON, handled elsewhere
    } else if (this.version === 3 && data.byteLength > 4) {
      // BinaryProtocol3: 4-byte header + payload
      const view = new DataView(data);
      const headerType = view.getUint8(0);
      if (headerType === 0) {
        const payloadSize = view.getUint16(2);
        const payload = new Uint8Array(data, 4, payloadSize);
        this._enqueueOpusFrame(payload);
        return;
      }
    } else {
      // Version 1: raw OPUS data
      this._enqueueOpusFrame(new Uint8Array(data));
    }
  }

  /** Queue OPUS frame for playback */
  _enqueueOpusFrame(frame) {
    this.audioQueue.push(frame);
    if (!this.isPlaying) this._startAudioPlayback();
  }

  /** Start playing queued OPUS audio via AudioContext */
  async _startAudioPlayback() {
    if (this.isPlaying) return;
    this.isPlaying = true;

    if (!this.audioCtx) {
      this.audioCtx = new (window.AudioContext || window.webkitAudioContext)({
        sampleRate: this.serverSampleRate
      });
    }

    // Try to decode OPUS using AudioDecoder if available (Chrome 94+)
    const hasDecoder = typeof AudioDecoder !== 'undefined';

    while (this.audioQueue.length > 0) {
      const frame = this.audioQueue.shift();
      try {
        if (hasDecoder) {
          await this._decodeAndPlayOpus(frame);
        } else {
          // Fallback: try playing as raw PCM (won't sound right for OPUS, but keeps pipeline alive)
          this._log('AudioDecoder not available, skipping audio playback');
        }
      } catch (e) {
        this._log(`Audio play error: ${e.message}`);
      }
    }
    this.isPlaying = false;
  }

  /** Decode OPUS frame and play via AudioContext */
  async _decodeAndPlayOpus(frame) {
    // Use WebCodecs AudioDecoder for OPUS
    const decoder = new AudioDecoder({
      output: (audioData) => {
        const samples = audioData.numberOfFrames;
        const buf = this.audioCtx.createBuffer(audioData.numberOfChannels, samples, audioData.sampleRate);
        for (let ch = 0; ch < audioData.numberOfChannels; ch++) {
          buf.copyToChannel(audioData.getChannelData(ch) || new Float32Array(samples), ch);
        }
        const source = this.audioCtx.createBufferSource();
        source.buffer = buf;
        source.connect(this.audioCtx.destination);
        source.start();
        audioData.close();
      },
      error: (e) => { this._log(`Decoder error: ${e.message}`); }
    });

    decoder.configure({
      codec: 'opus',
      sampleRate: this.serverSampleRate,
      numberOfChannels: 1,
    });

    const chunk = new EncodedAudioChunk({
      type: 'key',
      timestamp: performance.now() * 1000,
      data: frame,
    });
    decoder.decode(chunk);
    await decoder.flush();
    decoder.close();
  }

  _stopAudioPlayback() {
    this.audioQueue = [];
    this.isPlaying = false;
  }

  /** Start listening session */
  startListening(mode) {
    if (!this.connected || !this.sessionId) return false;
    this._sendJson({
      session_id: this.sessionId,
      type: 'listen',
      state: 'start',
      mode: mode || 'auto'  // "auto" | "manual" | "realtime"
    });
    this._log('Sent listen start');
    return true;
  }

  /** Stop listening session */
  stopListening() {
    if (!this.connected || !this.sessionId) return false;
    this._sendJson({
      session_id: this.sessionId,
      type: 'listen',
      state: 'stop'
    });
    this._log('Sent listen stop');
    return true;
  }

  /** Send text message (for text-based chat without audio) */
  sendTextMessage(text) {
    if (!this.connected || !this.sessionId) return false;
    // Use listen protocol: start -> send text as STT result -> stop
    this.startListening('auto');
    // Some servers accept a "text" field in the listen message
    this._sendJson({
      session_id: this.sessionId,
      type: 'listen',
      state: 'detect',
      text: text
    });
    this.stopListening();
    this._log(`Sent text: ${text.substring(0, 50)}...`);
    return true;
  }

  /** Abort current TTS playback */
  abortSpeaking() {
    if (!this.connected || !this.sessionId) return false;
    this._sendJson({
      session_id: this.sessionId,
      type: 'abort'
    });
    this._stopAudioPlayback();
    this._log('Sent abort');
    return true;
  }

  /** Send JSON via WebSocket */
  _sendJson(obj) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return false;
    this.ws.send(JSON.stringify(obj));
    return true;
  }
}
