from typing import Optional
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QComboBox, QTextEdit, QLineEdit, QTabWidget, QGroupBox,
    QGridLayout, QSplitter, QMainWindow, QStatusBar, QScrollArea,
    QFrame, QCheckBox, QSpacerItem, QSizePolicy,
)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QObject
from PyQt6.QtGui import QFont, QTextCursor
import pyqtgraph as pg
import json
import time as time_module

from serial_io import SerialIO, TelemetryData
from xiaozhi_client import XiaozhiClient

MAX_HISTORY = 300


class HistoryBuffer:
    def __init__(self, maxlen=MAX_HISTORY):
        self.data = []
        self.maxlen = maxlen
    def append(self, val):
        self.data.append(val)
        if len(self.data) > self.maxlen:
            self.data.pop(0)
    def values(self):
        return list(self.data)


class TelemetrySignals(QObject):
    telemetry = pyqtSignal(TelemetryData)
    event = pyqtSignal(str, str)
    log = pyqtSignal(str)
    connected = pyqtSignal(bool)
    xz_connected = pyqtSignal(bool)
    xz_stt = pyqtSignal(str)
    xz_llm = pyqtSignal(str)
    xz_tts_text = pyqtSignal(str)
    xz_tts_state = pyqtSignal(str)
    xz_error = pyqtSignal(str)
    xz_log = pyqtSignal(str)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("SmartBracelet Monitor")
        self.setMinimumSize(1000, 700)

        self.signals = TelemetrySignals()
        self.serial: Optional[SerialIO] = None
        self.connected = False

        self.xz = XiaozhiClient()
        self.xz_connected = False

        self.hist_batt = HistoryBuffer()
        self.hist_steps = HistoryBuffer()
        self.hist_acc_x = HistoryBuffer()
        self.hist_acc_y = HistoryBuffer()
        self.hist_acc_z = HistoryBuffer()
        self.hist_intensity = HistoryBuffer()
        self.hist_calories = HistoryBuffer()

        self.chat_history: list[dict] = []

        self._build_ui()
        self._connect_signals()

        self._port_timer = QTimer()
        self._port_timer.timeout.connect(self._scan_ports)
        self._port_timer.start(2000)
        self._scan_ports()

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        vbox = QVBoxLayout(central)
        vbox.setContentsMargins(4, 4, 4, 4)
        vbox.setSpacing(4)

        toolbar = QHBoxLayout()
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(120)
        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self._toggle_connect)
        toolbar.addWidget(QLabel("Port:"))
        toolbar.addWidget(self.port_combo)
        toolbar.addWidget(self.connect_btn)
        toolbar.addSpacing(16)
        self.status_serial = QLabel("● Disconnected")
        self.status_serial.setStyleSheet("color: #666; font-weight: bold;")
        toolbar.addWidget(self.status_serial)
        toolbar.addStretch()
        vbox.addLayout(toolbar)

        self.tabs = QTabWidget()
        vbox.addWidget(self.tabs, 1)

        self._build_overview_tab()
        self._build_sensor_tab()
        self._build_voice_tab()
        self._build_console_tab()

        self.status = QStatusBar()
        self.setStatusBar(self.status)

    # ── helpers ──
    def _kv_row(self, parent, label: str, value: str = "---"):
        row = QHBoxLayout()
        lbl = QLabel(label)
        lbl.setFont(QFont("Segoe UI", 10))
        lbl.setStyleSheet("color: #888;")
        val = QLabel(value)
        val.setFont(QFont("Consolas", 12, QFont.Weight.Bold))
        row.addWidget(lbl)
        row.addWidget(val, 1)
        parent.addLayout(row)
        return val

    def _group(self, title: str) -> QGroupBox:
        g = QGroupBox(title)
        g.setFont(QFont("Segoe UI", 10, QFont.Weight.Bold))
        g.setStyleSheet("""
            QGroupBox { border: 1px solid #444; border-radius: 6px; margin-top: 6px; padding: 14px 8px 8px 8px; }
            QGroupBox::title { subcontrol-origin: margin; left: 10px; }
        """)
        return g

    # ── Overview tab ──
    def _build_overview_tab(self):
        tab = QWidget()
        lo = QVBoxLayout(tab)

        top = QHBoxLayout()
        bg = self._group("Battery")
        bgl = QVBoxLayout(bg)
        self.lbl_batt = self._kv_row(bgl, "Level:", "---")
        self.lbl_batt_mv = self._kv_row(bgl, "Voltage:", "--- mV")
        self.lbl_chg = self._kv_row(bgl, "Charging:", "---")
        self.lbl_usb = self._kv_row(bgl, "USB:", "---")
        top.addWidget(bg)

        sg = self._group("Steps")
        sgl = QVBoxLayout(sg)
        self.lbl_steps = self._kv_row(sgl, "Today:", "0")
        self.lbl_wifi = self._kv_row(sgl, "WiFi:", "---")
        top.addWidget(sg)

        cg = self._group("Calories")
        cgl = QVBoxLayout(cg)
        self.lbl_cal = self._kv_row(cgl, "Today:", "0 kcal")
        self.lbl_intensity = self._kv_row(cgl, "Intensity:", "---")
        self.lbl_mets = self._kv_row(cgl, "METs:", "---")
        top.addWidget(cg)
        lo.addLayout(top)

        self.batt_plot = pg.PlotWidget(title="Battery %")
        self.batt_plot.setLabel("left", "%")
        self.batt_plot.setLabel("bottom", "samples")
        self.batt_curve = self.batt_plot.plot(pen="#00d4ff")
        lo.addWidget(self.batt_plot, 1)

        self.steps_plot = pg.PlotWidget(title="Steps")
        self.steps_plot.setLabel("left", "steps")
        self.steps_plot.setLabel("bottom", "samples")
        self.steps_curve = self.steps_plot.plot(pen="#00d488")
        lo.addWidget(self.steps_plot, 1)

        self.tabs.addTab(tab, "Overview")

    # ── Sensors tab ──
    def _build_sensor_tab(self):
        tab = QWidget()
        lo = QVBoxLayout(tab)

        ig = self._group("IMU (Live)")
        igl = QVBoxLayout(ig)
        row = QHBoxLayout()
        self.lbl_acc = self._kv_row(row, "ACC:", "---")
        self.lbl_gyr = self._kv_row(row, "GYR:", "---")
        igl.addLayout(row)
        lo.addWidget(ig)

        self.acc_plot = pg.PlotWidget(title="Accelerometer (g)")
        self.acc_plot.setLabel("left", "g")
        self.acc_plot.setLabel("bottom", "samples")
        self.acc_plot.addLegend()
        self.acc_x_curve = self.acc_plot.plot(pen="#ff4444", name="X")
        self.acc_y_curve = self.acc_plot.plot(pen="#44ff44", name="Y")
        self.acc_z_curve = self.acc_plot.plot(pen="#4444ff", name="Z")
        lo.addWidget(self.acc_plot, 1)

        self.int_plot = pg.PlotWidget(title="Motion Intensity")
        self.int_plot.setLabel("left", "%")
        self.int_plot.setLabel("bottom", "samples")
        self.int_curve = self.int_plot.plot(pen="#ffaa00")
        lo.addWidget(self.int_plot, 1)

        self.tabs.addTab(tab, "Sensors")

    # ── Voice Chat tab (XiaoZhi AI) ──
    def _build_voice_tab(self):
        tab = QWidget()
        lo = QHBoxLayout(tab)

        # Left panel: connection + input
        left = QVBoxLayout()

        conn = self._group("XiaoZhi Connection")
        cl = QVBoxLayout(conn)

        row_url = QHBoxLayout()
        row_url.addWidget(QLabel("WS URL:"))
        self.xz_url = QLineEdit()
        self.xz_url.setPlaceholderText("wss://api.xiaozhi.me/ws")
        row_url.addWidget(self.xz_url, 1)
        cl.addLayout(row_url)

        row_tok = QHBoxLayout()
        row_tok.addWidget(QLabel("Token:"))
        self.xz_token = QLineEdit()
        self.xz_token.setPlaceholderText("(optional)")
        row_tok.addWidget(self.xz_token, 1)
        cl.addLayout(row_tok)

        row_btn = QHBoxLayout()
        self.xz_connect_btn = QPushButton("Connect")
        self.xz_connect_btn.clicked.connect(self._toggle_xz)
        row_btn.addWidget(self.xz_connect_btn)
        self.xz_status = QLabel("Disconnected")
        self.xz_status.setStyleSheet("color: #666; font-weight: bold;")
        row_btn.addWidget(self.xz_status, 1)
        cl.addLayout(row_btn)

        left.addWidget(conn)

        # Chat display
        self.chat_display = QTextEdit()
        self.chat_display.setReadOnly(True)
        self.chat_display.setFont(QFont("Segoe UI", 11))
        self.chat_display.setStyleSheet("background: #0d0d1a; color: #ccc; border: 1px solid #333; border-radius: 4px;")
        left.addWidget(self.chat_display, 1)

        # Input row
        inp = QHBoxLayout()
        self.chat_input = QLineEdit()
        self.chat_input.setPlaceholderText("Type a message to XiaoZhi AI...")
        self.chat_input.returnPressed.connect(self._send_chat)
        inp.addWidget(self.chat_input, 1)

        self.send_chat_btn = QPushButton("Send")
        self.send_chat_btn.clicked.connect(self._send_chat)
        inp.addWidget(self.send_chat_btn)

        self.send_to_watch_cb = QCheckBox("→ Watch")
        self.send_to_watch_cb.setChecked(True)
        self.send_to_watch_cb.setToolTip("Forward AI response to watch via serial")
        inp.addWidget(self.send_to_watch_cb)

        left.addLayout(inp)

        lo.addLayout(left, 3)

        # Right panel: conversation log + quick actions
        right = QVBoxLayout()

        actions = self._group("Quick Actions")
        al = QVBoxLayout(actions)

        row1 = QHBoxLayout()
        btn_hello = QPushButton("Say Hello")
        btn_hello.clicked.connect(lambda: self._quick_chat("你好"))
        row1.addWidget(btn_hello)
        btn_time = QPushButton("Ask Time")
        btn_time.clicked.connect(lambda: self._quick_chat("现在几点了？"))
        row1.addWidget(btn_time)
        btn_weather = QPushButton("Ask Weather")
        btn_weather.clicked.connect(lambda: self._quick_chat("今天天气怎么样？"))
        row1.addWidget(btn_weather)
        al.addLayout(row1)

        row2 = QHBoxLayout()
        self.btn_abort = QPushButton("Abort")
        self.btn_abort.clicked.connect(self._abort_xz)
        self.btn_abort.setEnabled(False)
        row2.addWidget(self.btn_abort)
        btn_clear = QPushButton("Clear Chat")
        btn_clear.clicked.connect(self._clear_chat)
        row2.addWidget(btn_clear)
        btn_send_dnd = QPushButton("DND Toggle")
        btn_send_dnd.clicked.connect(lambda: self._send_raw('{"c":"dnd","on":1}' if self.connected else ""))
        row2.addWidget(btn_send_dnd)
        al.addLayout(row2)

        right.addWidget(actions)

        # Voice log
        log_g = self._group("Voice Log")
        ll = QVBoxLayout(log_g)
        self.xz_log_view = QTextEdit()
        self.xz_log_view.setReadOnly(True)
        self.xz_log_view.setFont(QFont("Consolas", 9))
        self.xz_log_view.setStyleSheet("background: #0d0d1a; color: #666; border: none;")
        self.xz_log_view.setMaximumHeight(150)
        ll.addWidget(self.xz_log_view)
        right.addWidget(log_g)

        lo.addLayout(right, 2)

        self.tabs.addTab(tab, "Voice AI")

    # ── Console tab ──
    def _build_console_tab(self):
        tab = QWidget()
        lo = QVBoxLayout(tab)

        # Notify send
        nf = QHBoxLayout()
        self.notif_app = QLineEdit()
        self.notif_app.setPlaceholderText("app (e.g. wx)")
        nf.addWidget(self.notif_app)
        self.notif_title = QLineEdit()
        self.notif_title.setPlaceholderText("title")
        nf.addWidget(self.notif_title, 1)
        self.notif_body = QLineEdit()
        self.notif_body.setPlaceholderText("body")
        nf.addWidget(self.notif_body, 1)
        btn_notif = QPushButton("Send Notify")
        btn_notif.clicked.connect(self._send_notify)
        nf.addWidget(btn_notif)
        lo.addLayout(nf)

        # Command input
        cmd_row = QHBoxLayout()
        self.cmd_input = QLineEdit()
        self.cmd_input.setPlaceholderText("JSON command or raw text...")
        self.cmd_input.returnPressed.connect(self._send_command)
        self.send_btn = QPushButton("Send")
        self.send_btn.clicked.connect(self._send_command)
        cmd_row.addWidget(self.cmd_input, 1)
        cmd_row.addWidget(self.send_btn)
        lo.addLayout(cmd_row)

        # Quick command buttons
        qr = QHBoxLayout()
        for label, cmd_data in [
            ("DND ON", '{"c":"dnd","on":1}'),
            ("DND OFF", '{"c":"dnd","on":0}'),
            ("Sync Time", '{"c":"time","epoch":' + str(int(time_module.time())) + '}'),
            ("OTA", '{"c":"ota","url":"http://example.com/firmware.bin"}'),
        ]:
            btn = QPushButton(label)
            btn.clicked.connect(lambda checked, d=cmd_data: self._send_raw(d))
            qr.addWidget(btn)
        qr.addStretch()
        lo.addLayout(qr)

        self.log_view = QTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setFont(QFont("Consolas", 10))
        self.log_view.setStyleSheet("background: #0d0d1a; color: #ccc; border: 1px solid #333;")
        lo.addWidget(self.log_view, 1)

        self.tabs.addTab(tab, "Console")

    # ── Signal wiring ──
    def _connect_signals(self):
        self.signals.telemetry.connect(self._on_telemetry)
        self.signals.event.connect(self._on_event)
        self.signals.log.connect(self._on_log)
        self.signals.connected.connect(self._on_connected)
        self.signals.xz_connected.connect(self._on_xz_connected)
        self.signals.xz_stt.connect(self._on_xz_stt)
        self.signals.xz_llm.connect(self._on_xz_llm)
        self.signals.xz_tts_text.connect(self._on_xz_llm)
        self.signals.xz_tts_state.connect(self._on_xz_tts_state)
        self.signals.xz_error.connect(self._on_xz_error)
        self.signals.xz_log.connect(self._on_xz_log)

    # ── Serial port ──
    def _scan_ports(self):
        import serial.tools.list_ports
        current = self.port_combo.currentText()
        self.port_combo.blockSignals(True)
        self.port_combo.clear()
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo.addItems(ports)
        if current in ports:
            self.port_combo.setCurrentText(current)
        self.port_combo.blockSignals(False)

    def _toggle_connect(self):
        if self.connected:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        port = self.port_combo.currentText()
        if not port:
            return
        self.serial = SerialIO(port)
        self.serial.on_telemetry = lambda t: self.signals.telemetry.emit(t)
        self.serial.on_event = lambda evt, msg: self.signals.event.emit(evt, msg)
        self.serial.on_log = lambda msg: self.signals.log.emit(msg)
        if self.serial.start():
            self.signals.connected.emit(True)

    def _disconnect(self):
        if self.serial:
            self.serial.stop()
            self.serial = None
        self.signals.connected.emit(False)

    def _on_connected(self, ok: bool):
        self.connected = ok
        self.connect_btn.setText("Disconnect" if ok else "Connect")
        self.status_serial.setText("● Connected" if ok else "● Disconnected")
        self.status_serial.setStyleSheet("color: #0d0; font-weight: bold;" if ok else "color: #666; font-weight: bold;")
        if ok:
            self.status.showMessage(f"Serial connected", 3000)

    # ── XiaoZhi connection ──
    def _toggle_xz(self):
        if self.xz_connected:
            self.xz.disconnect()
            self.signals.xz_connected.emit(False)
        else:
            url = self.xz_url.text().strip()
            if not url:
                self._on_xz_log("Enter WebSocket URL first")
                return
            token = self.xz_token.text().strip()
            self.xz_connect_btn.setEnabled(False)
            self.xz_status.setText("Connecting...")
            self.xz_status.setStyleSheet("color: #ffaa00; font-weight: bold;")

            self.xz.on_connected = lambda sid: self.signals.xz_connected.emit(True)
            self.xz.on_disconnected = lambda: self.signals.xz_connected.emit(False)
            self.xz.on_stt = lambda text: self.signals.xz_stt.emit(text)
            self.xz.on_llm = lambda text: self.signals.xz_llm.emit(text)
            self.xz.on_tts_text = lambda text: self.signals.xz_tts_text.emit(text)
            self.xz.on_tts_state = lambda state: self.signals.xz_tts_state.emit(state)
            self.xz.on_error = lambda msg: self.signals.xz_error.emit(msg)
            self.xz.on_log = lambda msg: self.signals.xz_log.emit(msg)

            ok = self.xz.connect(url, token)
            if not ok:
                self.signals.xz_error.emit("Connection failed (timeout)")
                self.xz_connect_btn.setEnabled(True)

    def _on_xz_connected(self, ok: bool):
        self.xz_connected = ok
        self.xz_connect_btn.setEnabled(True)
        self.xz_connect_btn.setText("Disconnect" if ok else "Connect")
        self.xz_status.setText("Connected" if ok else "Disconnected")
        self.xz_status.setStyleSheet("color: #0d0;" if ok else "color: #666;")
        self.btn_abort.setEnabled(ok)
        if ok:
            self._append_chat("System", "Connected to XiaoZhi AI ✓", "#00d488")
        else:
            self._append_chat("System", "Disconnected", "#888")

    def _on_xz_stt(self, text: str):
        self._append_chat("You (voice)", text, "#00d4ff")

    def _on_xz_llm(self, text: str):
        self._append_chat("XiaoZhi AI", text, "#ffffff")

    def _on_xz_tts_state(self, state: str):
        if state == "start":
            self.xz_status.setText("AI speaking...")
            self.xz_status.setStyleSheet("color: #ffaa00; font-weight: bold;")
        elif state == "stop":
            self.xz_status.setText("Idle")
            self.xz_status.setStyleSheet("color: #0d0;")

    def _on_xz_error(self, msg: str):
        self._on_xz_log(f"[ERROR] {msg}")

    def _on_xz_log(self, msg: str):
        self.xz_log_view.append(msg)
        sb = self.xz_log_view.verticalScrollBar()
        sb.setValue(sb.maximum())

    def _send_chat(self):
        text = self.chat_input.text().strip()
        if not text:
            return
        self.chat_input.clear()
        self._append_chat("You", text, "#00d4ff")

        if not self.xz_connected:
            self._append_chat("System", "Not connected to XiaoZhi AI", "#ff4444")
            return

        self.xz.send_text(text)
        self.xz_status.setText("Waiting for AI...")
        self.xz_status.setStyleSheet("color: #ffaa00; font-weight: bold;")

    def _quick_chat(self, text: str):
        self.chat_input.setText(text)
        self._send_chat()

    def _abort_xz(self):
        self.xz.abort()

    def _clear_chat(self):
        self.chat_history.clear()
        self.chat_display.clear()

    def _append_chat(self, role: str, text: str, color: str = "#ccc"):
        from PyQt6.QtGui import QTextCharFormat, QColor
        cur = self.chat_display.textCursor()
        cur.movePosition(QTextCursor.MoveOperation.End)

        fmt = QTextCharFormat()
        fmt.setForeground(QColor(color))
        fmt.setFontWeight(QFont.Weight.Bold)
        cur.insertText(f"{role}: ", fmt)

        fmt2 = QTextCharFormat()
        fmt2.setForeground(QColor("#ccc"))
        cur.insertText(f"{text}\n\n", fmt2)

        self.chat_display.setTextCursor(cur)
        self.chat_display.ensureCursorVisible()

        self.chat_history.append({"role": role, "text": text, "color": color})

        # If "Send to Watch" is checked and this is an AI response, forward it
        if role == "XiaoZhi AI" and self.send_to_watch_cb.isChecked() and self.connected:
            last_user = ""
            for entry in reversed(self.chat_history):
                if entry["role"] == "You":
                    last_user = entry["text"]
                    break
            self._forward_to_watch(last_user, text)

    def _forward_to_watch(self, transcription: str, response: str):
        arg = f"{transcription}|{response}"
        cmd = json.dumps({"c": "voice", "vc": "result", "arg": arg}, ensure_ascii=False)
        self._send_raw(cmd)

    # ── Console actions ──
    def _send_command(self):
        text = self.cmd_input.text().strip()
        if not text:
            return
        self._send_raw(text)
        self.cmd_input.clear()

    def _send_notify(self):
        app = self.notif_app.text().strip()
        title = self.notif_title.text().strip()
        body = self.notif_body.text().strip()
        if not app or not title:
            return
        cmd = json.dumps({"c": "notify", "app": app, "title": title, "body": body}, ensure_ascii=False)
        self._send_raw(cmd)

    def _send_raw(self, text: str):
        if self.serial and self.connected:
            self.serial.send(text)
            self._on_log(f">>> {text}")

    # ── Event handlers ──
    def _on_telemetry(self, t: TelemetryData):
        self.lbl_batt.setText(f"{t.batt_percent}%")
        self.lbl_batt_mv.setText(f"{t.batt_mv} mV")
        self.lbl_chg.setText("Yes" if t.charging else "No")
        self.lbl_usb.setText("Yes" if t.usb_powered else "No")
        self.lbl_steps.setText(str(t.steps))
        self.lbl_wifi.setText("Connected" if t.wifi else "Off")
        self.lbl_cal.setText(f"{t.calories:.1f} kcal")
        self.lbl_intensity.setText(f"{t.intensity}%")
        self.lbl_mets.setText(f"{t.mets:.2f}")

        self.lbl_acc.setText(f"X={t.acc_x:.3f}  Y={t.acc_y:.3f}  Z={t.acc_z:.3f}")
        self.lbl_gyr.setText(f"X={t.gyr_x:.1f}  Y={t.gyr_y:.1f}  Z={t.gyr_z:.1f}")

        self.hist_batt.append(t.batt_percent)
        self.hist_steps.append(t.steps)
        self.hist_acc_x.append(t.acc_x)
        self.hist_acc_y.append(t.acc_y)
        self.hist_acc_z.append(t.acc_z)
        self.hist_intensity.append(t.intensity)
        self.hist_calories.append(t.calories)

        self.batt_curve.setData(self.hist_batt.values())
        self.steps_curve.setData(self.hist_steps.values())
        self.acc_x_curve.setData(self.hist_acc_x.values())
        self.acc_y_curve.setData(self.hist_acc_y.values())
        self.acc_z_curve.setData(self.hist_acc_z.values())
        self.int_curve.setData(self.hist_intensity.values())

    def _on_event(self, evt: str, msg: str):
        self._on_log(f"[EVENT] {evt}: {msg}")

    def _on_log(self, msg: str):
        self.log_view.append(msg)
        sb = self.log_view.verticalScrollBar()
        sb.setValue(sb.maximum())

    def closeEvent(self, event):
        self.xz.disconnect()
        self._disconnect()
        event.accept()
