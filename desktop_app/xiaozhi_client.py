import json
import threading
import time
from typing import Optional, Callable
import websocket


class XiaozhiClient:
    def __init__(self):
        self.ws: Optional[websocket.WebSocketApp] = None
        self.session_id = ""
        self.connected = False
        self._thread: Optional[threading.Thread] = None
        self._stop = False

        self.on_connected: Optional[Callable[[str], None]] = None
        self.on_disconnected: Optional[Callable[[], None]] = None
        self.on_stt: Optional[Callable[[str], None]] = None
        self.on_tts_text: Optional[Callable[[str], None]] = None
        self.on_tts_state: Optional[Callable[[str], None]] = None
        self.on_llm: Optional[Callable[[str], None]] = None
        self.on_error: Optional[Callable[[str], None]] = None
        self.on_log: Optional[Callable[[str], None]] = None

    def _log(self, msg: str):
        if self.on_log:
            self.on_log(msg)

    def connect(self, url: str, token: str = "", timeout: float = 10.0) -> bool:
        if self.ws:
            self.disconnect()

        self._stop = False
        result_holder = [None]
        event = threading.Event()

        def on_open(ws):
            self._log(f"WebSocket opened: {url}")
            hello = {
                "type": "hello",
                "version": 2,
                "transport": "websocket",
                "audio_params": {
                    "format": "opus",
                    "sample_rate": 16000,
                    "channels": 1,
                    "frame_duration": 60,
                },
            }
            if token:
                hello["token"] = token
            ws.send(json.dumps(hello))
            self._log("Sent client hello")

        def on_message(ws, data):
            if isinstance(data, str):
                self._handle_json(data, result_holder, event)
            else:
                self._log(f"Binary msg: {len(data)} bytes (audio — not played on desktop)")

        def on_error(ws, err):
            msg = str(err)
            self._log(f"WS error: {msg}")
            if not result_holder[0]:
                result_holder[0] = False
                event.set()
            if self.on_error:
                self.on_error(msg)

        def on_close(ws, close_status_code, close_msg):
            self.connected = False
            self._log(f"Disconnected (code={close_status_code})")
            if self.on_disconnected:
                self.on_disconnected()

        self.ws = websocket.WebSocketApp(
            url,
            on_open=on_open,
            on_message=on_message,
            on_error=on_error,
            on_close=on_close,
        )

        self._thread = threading.Thread(target=self.ws.run_forever, daemon=True)
        self._thread.start()

        if not event.wait(timeout=timeout):
            self.disconnect()
            return False
        return result_holder[0] is True

    def disconnect(self):
        self._stop = True
        if self.ws:
            try:
                self.ws.close()
            except Exception:
                pass
            self.ws = None
        self.connected = False
        self.session_id = ""

    def send_text(self, text: str) -> bool:
        if not self.connected:
            return False
        self._send_json({
            "session_id": self.session_id,
            "type": "listen",
            "state": "start",
            "mode": "auto",
        })
        self._send_json({
            "session_id": self.session_id,
            "type": "listen",
            "state": "detect",
            "text": text,
        })
        self._send_json({
            "session_id": self.session_id,
            "type": "listen",
            "state": "stop",
        })
        self._log(f"Sent text: {text[:60]}...")
        return True

    def abort(self) -> bool:
        if not self.connected:
            return False
        self._send_json({
            "session_id": self.session_id,
            "type": "abort",
        })
        return True

    def _send_json(self, obj: dict):
        if self.ws and self.connected:
            self.ws.send(json.dumps(obj))

    def _handle_json(self, data: str, result_holder: list, event: threading.Event):
        try:
            msg = json.loads(data)
        except json.JSONDecodeError:
            return
        t = msg.get("type", "")
        if t == "hello":
            self.session_id = msg.get("session_id", "")
            self.connected = True
            self._log(f"Server hello — session: {self.session_id}")
            result_holder[0] = True
            event.set()
            if self.on_connected:
                self.on_connected(self.session_id)
        elif t == "stt":
            text = msg.get("text", "")
            self._log(f"STT: {text}")
            if self.on_stt:
                self.on_stt(text)
        elif t == "tts":
            state = msg.get("state", "")
            self._log(f"TTS state: {state}")
            if state:
                if self.on_tts_state:
                    self.on_tts_state(state)
            text = msg.get("text", "")
            if text and self.on_tts_text:
                self.on_tts_text(text)
        elif t == "llm":
            text = msg.get("text", "") or msg.get("content", "")
            if text and self.on_llm:
                self.on_llm(text)
            elif text and self.on_tts_text:
                self.on_tts_text(text)
        elif t == "error":
            err = msg.get("message", "") or msg.get("error", "Server error")
            self._log(f"Server error: {err}")
            if self.on_error:
                self.on_error(err)
