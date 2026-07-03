import json
import threading
from typing import Optional, Callable
import serial
import serial.tools.list_ports


class TelemetryData:
    def __init__(self):
        self.batt_percent = 0
        self.batt_mv = 0
        self.charging = False
        self.usb_powered = False
        self.steps = 0
        self.wifi = False
        self.acc_x = self.acc_y = self.acc_z = 0.0
        self.gyr_x = self.gyr_y = self.gyr_z = 0.0
        self.intensity = 0
        self.mets = 0.0
        self.calories = 0.0

    def update(self, d: dict):
        self.batt_percent = d.get("b", self.batt_percent)
        self.batt_mv = d.get("mv", self.batt_mv)
        self.charging = bool(d.get("chg", 0))
        self.usb_powered = bool(d.get("usb", 0))
        self.steps = d.get("st", self.steps)
        self.wifi = bool(d.get("wifi", 0))
        acc = d.get("acc", None)
        if acc and len(acc) == 3:
            self.acc_x, self.acc_y, self.acc_z = acc
        gyr = d.get("gyr", None)
        if gyr and len(gyr) == 3:
            self.gyr_x, self.gyr_y, self.gyr_z = gyr
        self.intensity = d.get("int", self.intensity)
        self.mets = d.get("met", self.mets)
        self.calories = d.get("cal", self.calories)


class SerialIO(threading.Thread):
    def __init__(self, port: str, baud: int = 115200):
        super().__init__(daemon=True)
        self.port = port
        self.baud = baud
        self.ser: Optional[serial.Serial] = None
        self.running = False
        self.on_telemetry: Optional[Callable[[TelemetryData], None]] = None
        self.on_event: Optional[Callable[[str, str], None]] = None
        self.on_log: Optional[Callable[[str], None]] = None
        self.on_audio_start: Optional[Callable[[int], None]] = None      # total ADPCM bytes
        self.on_audio_chunk: Optional[Callable[[int, str], None]] = None  # seq, base64
        self.on_audio_end: Optional[Callable[[int], None]] = None         # last seq
        self._lock = threading.Lock()

    def start(self):
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=1)
            self.running = True
            super().start()
            return True
        except serial.SerialException as e:
            if self.on_log:
                self.on_log(f"[ERR] Serial open failed: {e}")
            return False

    def stop(self):
        self.running = False
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass

    def send(self, text: str):
        if self.ser and self.ser.is_open:
            with self._lock:
                self.ser.write((text + "\n").encode("utf-8"))

    def send_command(self, cmd: str, **kwargs):
        d = {"c": cmd, **kwargs}
        self.send(json.dumps(d, ensure_ascii=False))

    def list_ports() -> list:
        return [p.device for p in serial.tools.list_ports.comports()]

    def run(self):
        buf = ""
        while self.running and self.ser and self.ser.is_open:
            try:
                data = self.ser.read(512)
                if not data:
                    continue
                text = data.decode("utf-8", errors="replace")
                buf += text
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    if line.startswith("{"):
                        self._parse_json(line)
                    else:
                        if self.on_log:
                            self.on_log(line)
            except serial.SerialException:
                break
            except Exception as e:
                if self.on_log:
                    self.on_log(f"[ERR] {e}")

    def _parse_json(self, line: str):
        try:
            d = json.loads(line)
            evt = d.get("e", "")
            if evt == "t":
                t = TelemetryData()
                t.update(d)
                if self.on_telemetry:
                    self.on_telemetry(t)
            elif evt == "va":
                sub = d.get("s", "")
                if sub == "start":
                    total = d.get("len", 0)
                    if self.on_audio_start:
                        self.on_audio_start(total)
                elif sub == "data":
                    seq = d.get("seq", 0)
                    data = d.get("d", "")
                    if self.on_audio_chunk:
                        self.on_audio_chunk(seq, data)
                elif sub == "end":
                    last_seq = d.get("seq", 0)
                    if self.on_audio_end:
                        self.on_audio_end(last_seq)
            elif evt:
                msg = d.get("msg", "")
                if self.on_event:
                    self.on_event(evt, msg)
        except json.JSONDecodeError:
            if self.on_log:
                self.on_log(f"[JSON err] {line}")
