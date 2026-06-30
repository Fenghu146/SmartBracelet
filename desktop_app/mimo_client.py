import json
import threading
import time
from typing import Optional, Callable
from urllib.request import Request, urlopen
from urllib.error import URLError


class MimoClient:
    def __init__(self):
        self._thread: Optional[threading.Thread] = None
        self._stop = False
        self._busy = False

        self.on_chunk: Optional[Callable[[str], None]] = None
        self.on_done: Optional[Callable[[str], None]] = None
        self.on_error: Optional[Callable[[str], None]] = None
        self.on_log: Optional[Callable[[str], None]] = None

    def _log(self, msg: str):
        if self.on_log:
            self.on_log(msg)

    def send(self, text: str, api_key: str = "", model: str = "mimo-v2.5",
             base_url: str = "https://platform.xiaomimimo.com/v1",
             system_prompt: str = "You are a helpful voice assistant."):
        if self._busy:
            self._log("Already busy, abort first")
            return
        self._busy = True
        self._stop = False
        self._thread = threading.Thread(
            target=self._do_request,
            args=(text, api_key, model, base_url, system_prompt),
            daemon=True,
        )
        self._thread.start()

    def abort(self):
        self._stop = True

    def _do_request(self, text: str, api_key: str, model: str,
                    base_url: str, system_prompt: str):
        url = f"{base_url.rstrip('/')}/chat/completions"
        body = json.dumps({
            "model": model,
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": text},
            ],
            "stream": True,
        }).encode()

        req = Request(url, data=body, method="POST")
        req.add_header("Content-Type", "application/json")
        if api_key:
            req.add_header("Authorization", f"Bearer {api_key}")

        full = ""
        try:
            resp = urlopen(req, timeout=30)
        except URLError as e:
            msg = f"HTTP error: {e.reason}"
            self._log(msg)
            if self.on_error:
                self.on_error(msg)
            self._busy = False
            return

        try:
            buf = b""
            while not self._stop:
                chunk = resp.read(1024)
                if not chunk:
                    break
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    line = line.strip()
                    if not line or line.startswith(b":"):
                        continue
                    if line == b"data: [DONE]":
                        break
                    if line.startswith(b"data: "):
                        try:
                            data = json.loads(line[6:])
                        except json.JSONDecodeError:
                            continue
                        delta = (
                            data.get("choices", [{}])[0]
                            .get("delta", {})
                            .get("content", "")
                        )
                        if delta:
                            full += delta
                            if self.on_chunk:
                                self.on_chunk(delta)
        except Exception as e:
            msg = f"Stream error: {e}"
            self._log(msg)
            if self.on_error:
                self.on_error(msg)
        finally:
            resp.close()

        if full and self.on_done:
            self.on_done(full)
        self._busy = False
