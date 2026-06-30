from typing import Optional
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton,
    QComboBox, QTextEdit, QLineEdit, QTabWidget, QGroupBox,
    QSplitter, QMainWindow, QStatusBar, QCheckBox, QFrame,
)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QObject
from PyQt6.QtGui import QFont, QTextCursor, QColor, QTextCharFormat
import pyqtgraph as pg
import json
import time as time_module

from serial_io import SerialIO, TelemetryData
from mimo_client import MimoClient

MAX_HISTORY = 300

pg.setConfigOptions(background="#f0f4f8", foreground="#7f8c8d")


class HistoryBuffer:
    def __init__(self, maxlen=MAX_HISTORY):
        self.data = []
        self.maxlen = maxlen
    def append(self, ts, val):
        self.data.append((ts, val))
        if len(self.data) > self.maxlen:
            self.data.pop(0)
    def values(self):
        if not self.data:
            return [], []
        return zip(*self.data) if len(self.data[0]) == 2 else ([], [])


class TelemetrySignals(QObject):
    telemetry = pyqtSignal(TelemetryData)
    event = pyqtSignal(str, str)
    log = pyqtSignal(str)
    connected = pyqtSignal(bool)
    mimo_chunk = pyqtSignal(str)
    mimo_done = pyqtSignal(str)
    mimo_error = pyqtSignal(str)
    mimo_log = pyqtSignal(str)


class GlassValue(QLabel):
    def __init__(self, text="---", color="#2c3e50"):
        super().__init__(text)
        self._color = color
        self.setFont(QFont("Segoe UI", 13, QFont.Weight.Bold))
        self.setStyleSheet(f"color: {color}; background: transparent;")

    def set(self, text):
        self.setText(text)


class GlassLabel(QLabel):
    def __init__(self, text):
        super().__init__(text)
        self.setFont(QFont("Segoe UI", 10))
        self.setStyleSheet("color: #7f8c8d; background: transparent; font-weight: 500;")


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("SmartBracelet Monitor")
        self.setMinimumSize(1060, 720)

        self.signals = TelemetrySignals()
        self.serial: Optional[SerialIO] = None
        self.connected = False

        self.mimo = MimoClient()
        self.mimo_busy = False
        self._last_user_text = ""

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

    def _kv_row(self, parent, label: str):
        row = QHBoxLayout()
        row.setContentsMargins(0, 0, 0, 0)
        row.addWidget(GlassLabel(label))
        v = GlassValue()
        row.addWidget(v, 1, Qt.AlignmentFlag.AlignRight)
        parent.addLayout(row)
        return v

    def _glass_card(self, title: str) -> QGroupBox:
        g = QGroupBox(title)
        g.setStyleSheet("""
            QGroupBox {
                background: rgba(255,255,255,0.6);
                border: 1px solid rgba(255,255,255,0.4);
                border-radius: 14px;
                margin-top: 10px;
                padding: 18px 14px 14px 14px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 14px;
                color: #7f8c8d;
                font-size: 11px;
                font-weight: 600;
                letter-spacing: 0.5px;
                text-transform: uppercase;
            }
        """)
        return g

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        vbox = QVBoxLayout(central)
        vbox.setContentsMargins(12, 8, 12, 8)
        vbox.setSpacing(6)

        # Toolbar
        tb = QHBoxLayout()
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(140)
        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self._toggle_connect)
        tb.addWidget(QLabel("Port", styleSheet="color:#7f8c8d;font-weight:600;font-size:11px;"))
        tb.addWidget(self.port_combo)
        tb.addWidget(self.connect_btn)
        tb.addSpacing(20)

        self.status_serial = QLabel("● Disconnected")
        self.status_serial.setStyleSheet("color: #bbb; font-weight: 600; font-size: 12px;")
        tb.addWidget(self.status_serial)
        tb.addStretch()

        help_lbl = QLabel("v1.0")
        help_lbl.setStyleSheet("color: #ccc; font-size: 11px;")
        tb.addWidget(help_lbl)

        vbox.addLayout(tb)

        self.tabs = QTabWidget()
        vbox.addWidget(self.tabs, 1)

        self._build_overview_tab()
        self._build_sensor_tab()
        self._build_voice_tab()
        self._build_console_tab()

        self.status = QStatusBar()
        self.status.showMessage("Ready")
        self.setStatusBar(self.status)

    # ── Overview tab ──
    def _build_overview_tab(self):
        tab = QWidget()
        lo = QVBoxLayout(tab)
        lo.setSpacing(8)

        cards = QHBoxLayout()
        cards.setSpacing(10)

        bg = self._glass_card("Battery")
        bl = QVBoxLayout(bg)
        bl.setSpacing(2)
        self.lbl_batt = self._kv_row(bl, "Level")
        self.lbl_batt_mv = self._kv_row(bl, "Voltage")
        self.lbl_chg = self._kv_row(bl, "Charging")
        self.lbl_usb = self._kv_row(bl, "USB")
        cards.addWidget(bg)

        sg = self._glass_card("Steps")
        sl = QVBoxLayout(sg)
        sl.setSpacing(2)
        self.lbl_steps = self._kv_row(sl, "Today")
        self.lbl_wifi = self._kv_row(sl, "WiFi")
        cards.addWidget(sg)

        cg = self._glass_card("Activity")
        cl = QVBoxLayout(cg)
        cl.setSpacing(2)
        self.lbl_cal = self._kv_row(cl, "Calories")
        self.lbl_intensity = self._kv_row(cl, "Intensity")
        self.lbl_mets = self._kv_row(cl, "METs")
        cards.addWidget(cg)

        lo.addLayout(cards)

        # Charts
        chart_row = QHBoxLayout()
        chart_row.setSpacing(10)

        self.batt_plot = pg.PlotWidget(title="Battery")
        self.batt_plot.setLabel("left", "%")
        self.batt_plot.setLabel("bottom", "")
        self.batt_plot.showGrid(x=True, y=True, alpha=0.3)
        self.batt_curve = self.batt_plot.plot(pen=pg.mkPen("#4a90d9", width=2))
        chart_row.addWidget(self.batt_plot, 1)

        self.steps_plot = pg.PlotWidget(title="Steps")
        self.steps_plot.setLabel("left", "steps")
        self.steps_plot.setLabel("bottom", "")
        self.steps_plot.showGrid(x=True, y=True, alpha=0.3)
        self.steps_curve = self.steps_plot.plot(pen=pg.mkPen("#27ae60", width=2))
        chart_row.addWidget(self.steps_plot, 1)

        lo.addLayout(chart_row, 1)
        self.tabs.addTab(tab, "Overview")

    # ── Sensors tab ──
    def _build_sensor_tab(self):
        tab = QWidget()
        lo = QVBoxLayout(tab)
        lo.setSpacing(8)

        ig = self._glass_card("IMU Live")
        il = QVBoxLayout(ig)
        il.setSpacing(2)

        def _imu_row(label, color="#4a90d9"):
            r = QHBoxLayout()
            r.addWidget(GlassLabel(label))
            v = GlassValue(color=color)
            r.addWidget(v, 1, Qt.AlignmentFlag.AlignRight)
            il.addLayout(r)
            return v

        self.lbl_acc = _imu_row("ACC", "#e74c3c")
        self.lbl_gyr = _imu_row("GYR", "#8e44ad")
        lo.addWidget(ig)

        plots = QHBoxLayout()
        plots.setSpacing(10)

        self.acc_plot = pg.PlotWidget(title="Accelerometer")
        self.acc_plot.setLabel("left", "g")
        self.acc_plot.setLabel("bottom", "")
        self.acc_plot.addLegend()
        self.acc_plot.showGrid(x=True, y=True, alpha=0.3)
        self.acc_x_curve = self.acc_plot.plot(pen=pg.mkPen("#e74c3c", width=1.5), name="X")
        self.acc_y_curve = self.acc_plot.plot(pen=pg.mkPen("#27ae60", width=1.5), name="Y")
        self.acc_z_curve = self.acc_plot.plot(pen=pg.mkPen("#4a90d9", width=1.5), name="Z")
        plots.addWidget(self.acc_plot, 1)

        ig2 = self._glass_card("Motion")
        i2l = QVBoxLayout(ig2)
        i2l.setSpacing(2)
        self.lbl_int2 = self._kv_row(i2l, "Intensity")
        self.lbl_met2 = self._kv_row(i2l, "METs")
        self.lbl_cal2 = self._kv_row(i2l, "Calories")

        self.int_plot = pg.PlotWidget(title="Intensity")
        self.int_plot.setLabel("left", "%")
        self.int_plot.setLabel("bottom", "")
        self.int_plot.showGrid(x=True, y=True, alpha=0.3)
        self.int_curve = self.int_plot.plot(pen=pg.mkPen("#f39c12", width=2))
        i2l.addWidget(self.int_plot, 1)

        plots.addWidget(ig2, 1)
        lo.addLayout(plots, 1)

        self.tabs.addTab(tab, "Sensors")

    # ── Voice Chat tab ──
    def _build_voice_tab(self):
        tab = QWidget()
        lo = QHBoxLayout(tab)
        lo.setSpacing(10)

        # Left
        left = QVBoxLayout()
        left.setSpacing(6)

        cfg = self._glass_card("MiMo AI")
        cl = QVBoxLayout(cfg)
        cl.setSpacing(6)

        urow = QHBoxLayout()
        urow.addWidget(GlassLabel("API Key"))
        self.mimo_api_key = QLineEdit()
        self.mimo_api_key.setPlaceholderText("sk-... (optional if using local proxy)")
        self.mimo_api_key.setEchoMode(QLineEdit.EchoMode.Password)
        urow.addWidget(self.mimo_api_key, 1)
        cl.addLayout(urow)

        mrow = QHBoxLayout()
        mrow.addWidget(GlassLabel("Model"))
        self.mimo_model = QLineEdit("mimo-v2.5")
        mrow.addWidget(self.mimo_model, 1)
        mrow.addWidget(GlassLabel("Base URL"))
        self.mimo_base_url = QLineEdit("https://platform.xiaomimimo.com/v1")
        mrow.addWidget(self.mimo_base_url, 2)
        cl.addLayout(mrow)

        self.mimo_status = QLabel("Idle")
        self.mimo_status.setStyleSheet("color: #7f8c8d; font-weight: 600; font-size: 12px;")
        cl.addWidget(self.mimo_status)

        left.addWidget(cfg)

        # Chat
        self.chat_display = QTextEdit()
        self.chat_display.setReadOnly(True)
        self.chat_display.setFont(QFont("Segoe UI", 11))
        self.chat_display.setStyleSheet("""
            QTextEdit {
                background: rgba(255,255,255,0.55);
                border: 1px solid rgba(255,255,255,0.3);
                border-radius: 12px;
                padding: 12px;
                color: #2c3e50;
            }
        """)
        left.addWidget(self.chat_display, 1)

        inp = QHBoxLayout()
        self.chat_input = QLineEdit()
        self.chat_input.setPlaceholderText("Type a message to MiMo...")
        self.chat_input.returnPressed.connect(self._send_chat)
        inp.addWidget(self.chat_input, 1)

        self.send_chat_btn = QPushButton("Send")
        self.send_chat_btn.clicked.connect(self._send_chat)
        inp.addWidget(self.send_chat_btn)

        self.send_to_watch_cb = QCheckBox("Forward to Watch")
        self.send_to_watch_cb.setChecked(True)
        inp.addWidget(self.send_to_watch_cb)

        left.addLayout(inp)

        lo.addLayout(left, 3)

        # Right
        right = QVBoxLayout()
        right.setSpacing(6)

        acts = self._glass_card("Quick Prompts")
        al = QVBoxLayout(acts)
        al.setSpacing(4)

        r1 = QHBoxLayout()
        for label, prompt in [("Hello", "你好"), ("Time", "现在几点了？"), ("Weather", "今天天气怎么样？")]:
            btn = QPushButton(label)
            btn.setToolTip(prompt)
            btn.clicked.connect(lambda checked, p=prompt: self._quick_chat(p))
            r1.addWidget(btn)
        al.addLayout(r1)

        r2 = QHBoxLayout()
        self.btn_abort = QPushButton("Abort")
        self.btn_abort.clicked.connect(self._abort_mimo)
        self.btn_abort.setEnabled(False)
        r2.addWidget(self.btn_abort)

        btn_clear = QPushButton("Clear")
        btn_clear.clicked.connect(self._clear_chat)
        r2.addWidget(btn_clear)

        btn_dnd = QPushButton("DND")
        btn_dnd.clicked.connect(lambda: self._send_raw('{"c":"dnd","on":1}') if self.connected else None)
        r2.addWidget(btn_dnd)

        al.addLayout(r2)
        right.addWidget(acts)

        log_g = self._glass_card("Protocol Log")
        ll = QVBoxLayout(log_g)
        self.mimo_log_view = QTextEdit()
        self.mimo_log_view.setReadOnly(True)
        self.mimo_log_view.setFont(QFont("Consolas", 9))
        self.mimo_log_view.setMaximumHeight(130)
        self.mimo_log_view.setStyleSheet("""
            QTextEdit {
                background: rgba(255,255,255,0.4);
                border: none;
                border-radius: 8px;
                padding: 6px;
                color: #7f8c8d;
                font-size: 10px;
            }
        """)
        ll.addWidget(self.mimo_log_view)
        right.addWidget(log_g)

        lo.addLayout(right, 2)

        self.tabs.addTab(tab, "Voice AI")

    # ── Console tab ──
    def _build_console_tab(self):
        tab = QWidget()
        lo = QVBoxLayout(tab)
        lo.setSpacing(6)

        nf = QHBoxLayout()
        nf.setSpacing(6)
        self.notif_app = QLineEdit()
        self.notif_app.setPlaceholderText("app")
        self.notif_app.setMaximumWidth(80)
        nf.addWidget(self.notif_app)
        self.notif_title = QLineEdit()
        self.notif_title.setPlaceholderText("title")
        nf.addWidget(self.notif_title, 1)
        self.notif_body = QLineEdit()
        self.notif_body.setPlaceholderText("body")
        nf.addWidget(self.notif_body, 2)
        btn_n = QPushButton("Send Notify")
        btn_n.clicked.connect(self._send_notify)
        nf.addWidget(btn_n)
        lo.addLayout(nf)

        cmd_row = QHBoxLayout()
        self.cmd_input = QLineEdit()
        self.cmd_input.setPlaceholderText("JSON command or raw text...")
        self.cmd_input.returnPressed.connect(self._send_command)
        self.send_btn = QPushButton("Send")
        self.send_btn.clicked.connect(self._send_command)
        cmd_row.addWidget(self.cmd_input, 1)
        cmd_row.addWidget(self.send_btn)
        lo.addLayout(cmd_row)

        qr = QHBoxLayout()
        qr.setSpacing(6)
        for label, cmd_data in [
            ("DND ON", '{"c":"dnd","on":1}'),
            ("DND OFF", '{"c":"dnd","on":0}'),
            ("Sync Time", '{"c":"time","epoch":' + str(int(time_module.time())) + '}'),
        ]:
            btn = QPushButton(label)
            btn.clicked.connect(lambda checked, d=cmd_data: self._send_raw(d))
            qr.addWidget(btn)
        qr.addStretch()
        lo.addLayout(qr)

        self.log_view = QTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setFont(QFont("Consolas", 10))
        self.log_view.setStyleSheet("""
            QTextEdit {
                background: rgba(255,255,255,0.5);
                border: 1px solid rgba(255,255,255,0.3);
                border-radius: 10px;
                padding: 8px;
                color: #2c3e50;
            }
        """)
        lo.addWidget(self.log_view, 1)

        self.tabs.addTab(tab, "Console")

    # ── Signal wiring ──
    def _connect_signals(self):
        self.signals.telemetry.connect(self._on_telemetry)
        self.signals.event.connect(self._on_event)
        self.signals.log.connect(self._on_log)
        self.signals.connected.connect(self._on_connected)
        self.signals.mimo_chunk.connect(self._on_mimo_chunk)
        self.signals.mimo_done.connect(self._on_mimo_done)
        self.signals.mimo_error.connect(self._on_mimo_error)
        self.signals.mimo_log.connect(self._on_mimo_log)

    # ── Serial ──
    def _scan_ports(self):
        import serial.tools.list_ports
        cur = self.port_combo.currentText()
        self.port_combo.blockSignals(True)
        self.port_combo.clear()
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo.addItems(ports)
        if cur in ports:
            self.port_combo.setCurrentText(cur)
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
        if ok:
            self.status_serial.setText("● Connected")
            self.status_serial.setStyleSheet("color: #27ae60; font-weight: 600; font-size: 12px;")
            self.status.showMessage("Serial connected", 3000)
        else:
            self.status_serial.setText("● Disconnected")
            self.status_serial.setStyleSheet("color: #bbb; font-weight: 600; font-size: 12px;")

    # ── MiMo ──
    def _send_chat(self):
        text = self.chat_input.text().strip()
        if not text:
            return
        self.chat_input.clear()
        self._append_chat("You", text, "#4a90d9")
        self.chat_history.append({"role": "user", "text": text})
        self._last_user_text = text
        self.mimo_busy = True
        self.send_chat_btn.setEnabled(False)
        self.btn_abort.setEnabled(True)
        self.mimo_status.setText("Thinking...")
        self.mimo_status.setStyleSheet("color: #f39c12; font-weight: 600;")

        self._ai_text_buf = ""
        self.mimo.on_chunk = lambda chunk: self.signals.mimo_chunk.emit(chunk)
        self.mimo.on_done = lambda full: self.signals.mimo_done.emit(full)
        self.mimo.on_error = lambda msg: self.signals.mimo_error.emit(msg)
        self.mimo.on_log = lambda msg: self.signals.mimo_log.emit(msg)

        api_key = self.mimo_api_key.text().strip()
        model = self.mimo_model.text().strip() or "mimo-v2.5"
        base_url = self.mimo_base_url.text().strip() or "https://platform.xiaomimimo.com/v1"
        self.mimo.send(text, api_key=api_key, model=model, base_url=base_url)
        self.signals.mimo_log.emit(f">>> send to {model}")

    def _on_mimo_chunk(self, chunk: str):
        self._ai_text_buf += chunk
        cur = self.chat_display.textCursor()
        cur.movePosition(QTextCursor.MoveOperation.End)
        fmt_text = QTextCharFormat()
        fmt_text.setForeground(QColor("#2c3e50"))
        cur.insertText(chunk, fmt_text)
        self.chat_display.setTextCursor(cur)
        self.chat_display.ensureCursorVisible()

    def _on_mimo_done(self, full: str):
        self.mimo_busy = False
        self.send_chat_btn.setEnabled(True)
        self.btn_abort.setEnabled(False)
        self.mimo_status.setText("Idle")
        self.mimo_status.setStyleSheet("color: #27ae60; font-weight: 600;")
        # Add trailing newline after response
        cur = self.chat_display.textCursor()
        cur.movePosition(QTextCursor.MoveOperation.End)
        cur.insertText("\n", QTextCharFormat())
        self.chat_history.append({"role": "AI", "text": full})
        if self.send_to_watch_cb.isChecked() and self.connected:
            self._forward_to_watch(self._last_user_text, full)

    def _on_mimo_error(self, msg: str):
        self.mimo_busy = False
        self.send_chat_btn.setEnabled(True)
        self.btn_abort.setEnabled(False)
        self.mimo_status.setText("Error")
        self.mimo_status.setStyleSheet("color: #e74c3c; font-weight: 600;")
        self._on_mimo_log(f"[ERR] {msg}")

    def _on_mimo_log(self, msg: str):
        self.mimo_log_view.append(msg)
        sb = self.mimo_log_view.verticalScrollBar()
        sb.setValue(sb.maximum())

    def _quick_chat(self, text: str):
        self.chat_input.setText(text)
        self._send_chat()

    def _abort_mimo(self):
        self.mimo.abort()
        self.mimo_busy = False
        self.send_chat_btn.setEnabled(True)
        self.btn_abort.setEnabled(False)
        self.mimo_status.setText("Aborted")
        self.mimo_status.setStyleSheet("color: #e74c3c; font-weight: 600;")

    def _clear_chat(self):
        self.chat_history.clear()
        self.chat_display.clear()

    def _append_chat(self, role: str, text: str, color: str = "#2c3e50"):
        cur = self.chat_display.textCursor()
        cur.movePosition(QTextCursor.MoveOperation.End)
        fmt_name = QTextCharFormat()
        fmt_name.setForeground(QColor(color))
        fmt_name.setFontWeight(QFont.Weight.Bold)
        cur.insertText(f"{role}: ", fmt_name)
        fmt_text = QTextCharFormat()
        fmt_text.setForeground(QColor("#2c3e50"))
        cur.insertText(f"{text}\n\n", fmt_text)
        self.chat_display.setTextCursor(cur)
        self.chat_display.ensureCursorVisible()
        if role == "You":
            self._last_user_text = text

    def _forward_to_watch(self, transcription: str, response: str):
        arg = f"{transcription}|{response}"
        cmd = json.dumps({"c": "voice", "vc": "result", "arg": arg}, ensure_ascii=False)
        self._send_raw(cmd)

    # ── Console ──
    def _send_command(self):
        text = self.cmd_input.text().strip()
        if text:
            self._send_raw(text)
            self.cmd_input.clear()

    def _send_notify(self):
        app = self.notif_app.text().strip()
        title = self.notif_title.text().strip()
        body = self.notif_body.text().strip()
        if app and title:
            cmd = json.dumps({"c": "notify", "app": app, "title": title, "body": body}, ensure_ascii=False)
            self._send_raw(cmd)

    def _send_raw(self, text: str):
        if self.serial and self.connected:
            self.serial.send(text)
            self._on_log(f">>> {text}")

    # ── Handlers ──
    def _on_telemetry(self, t: TelemetryData):
        self.lbl_batt.set(f"{t.batt_percent}%")
        self.lbl_batt_mv.set(f"{t.batt_mv} mV")
        self.lbl_chg.set("Yes" if t.charging else "No")
        self.lbl_usb.set("Yes" if t.usb_powered else "No")
        self.lbl_steps.set(str(t.steps))
        self.lbl_wifi.set("Connected" if t.wifi else "Off")
        self.lbl_cal.set(f"{t.calories:.1f} kcal")
        self.lbl_intensity.set(f"{t.intensity}%")
        self.lbl_mets.set(f"{t.mets:.2f}")

        self.lbl_acc.set(f"X={t.acc_x:.3f}  Y={t.acc_y:.3f}  Z={t.acc_z:.3f}")
        self.lbl_gyr.set(f"X={t.gyr_x:.1f}  Y={t.gyr_y:.1f}  Z={t.gyr_z:.1f}")

        now = time_module.time()
        self.hist_batt.append(now, t.batt_percent)
        self.hist_steps.append(now, t.steps)
        self.hist_acc_x.append(now, t.acc_x)
        self.hist_acc_y.append(now, t.acc_y)
        self.hist_acc_z.append(now, t.acc_z)
        self.hist_intensity.append(now, t.intensity)
        self.hist_calories.append(now, t.calories)

        def _update_plot(curve, buf):
            xs, ys = buf.values()
            curve.setData(x=list(xs), y=list(ys))

        _update_plot(self.batt_curve, self.hist_batt)
        _update_plot(self.steps_curve, self.hist_steps)
        _update_plot(self.acc_x_curve, self.hist_acc_x)
        _update_plot(self.acc_y_curve, self.hist_acc_y)
        _update_plot(self.acc_z_curve, self.hist_acc_z)
        _update_plot(self.int_curve, self.hist_intensity)

        # Auto-scroll X axis: show last 60 seconds
        window = 60.0
        for p in [self.batt_plot, self.steps_plot, self.acc_plot, self.int_plot]:
            p.setXRange(now - window, now, padding=0)

    def _on_event(self, evt: str, msg: str):
        self._on_log(f"[EVENT] {evt}: {msg}")

    def _on_log(self, msg: str):
        self.log_view.append(msg)
        sb = self.log_view.verticalScrollBar()
        sb.setValue(sb.maximum())

    def closeEvent(self, event):
        self.mimo.abort()
        self._disconnect()
        event.accept()
