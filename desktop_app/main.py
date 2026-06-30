#!/usr/bin/env python3
"""SmartBracelet Desktop Monitor — USB Serial Telemetry Viewer

Usage:
    pip install -r requirements.txt
    python main.py
"""

import sys
from PyQt6.QtWidgets import QApplication
from PyQt6.QtGui import QPalette, QColor
from telemetry_view import MainWindow

GLASS_BG = "#f0f4f8"

STYLE = f"""
QMainWindow, QWidget {{
    background: {GLASS_BG};
    color: #2c3e50;
    font-family: "Segoe UI", "Microsoft YaHei", sans-serif;
}}
/* ── Tabs ── */
QTabWidget::pane {{
    border: none;
    background: transparent;
}}
QTabBar::tab {{
    background: rgba(255,255,255,0.5);
    color: #7f8c8d;
    padding: 10px 22px;
    margin-right: 2px;
    border: 1px solid rgba(255,255,255,0.3);
    border-bottom: none;
    border-top-left-radius: 10px;
    border-top-right-radius: 10px;
    font-weight: 600;
    font-size: 12px;
}}
QTabBar::tab:selected {{
    background: rgba(255,255,255,0.85);
    color: #4a90d9;
    border-bottom: 2px solid #4a90d9;
}}
QTabBar::tab:hover:!selected {{
    background: rgba(255,255,255,0.7);
    color: #2c3e50;
}}
/* ── Combo / Input ── */
QComboBox, QLineEdit {{
    background: rgba(255,255,255,0.75);
    color: #2c3e50;
    border: 1px solid rgba(0,0,0,0.08);
    padding: 6px 10px;
    border-radius: 8px;
    font-size: 12px;
}}
QComboBox:focus, QLineEdit:focus {{
    border: 1px solid #4a90d9;
    background: rgba(255,255,255,0.9);
}}
QComboBox::drop-down {{
    border: none;
    width: 24px;
}}
/* ── Buttons ── */
QPushButton {{
    background: rgba(74,144,217,0.15);
    color: #4a90d9;
    border: 1px solid rgba(74,144,217,0.25);
    padding: 7px 18px;
    border-radius: 8px;
    font-weight: 600;
    font-size: 12px;
}}
QPushButton:hover {{
    background: rgba(74,144,217,0.25);
    border: 1px solid rgba(74,144,217,0.4);
}}
QPushButton:pressed {{
    background: rgba(74,144,217,0.35);
}}
QPushButton:disabled {{
    background: rgba(0,0,0,0.05);
    color: #bbb;
    border: 1px solid rgba(0,0,0,0.06);
}}
/* ── Group boxes ── */
QGroupBox {{
    background: rgba(255,255,255,0.6);
    border: 1px solid rgba(255,255,255,0.4);
    border-radius: 14px;
    margin-top: 10px;
    padding: 18px 12px 12px 12px;
}}
QGroupBox::title {{
    subcontrol-origin: margin;
    left: 14px;
    color: #7f8c8d;
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.5px;
    text-transform: uppercase;
}}
/* ── Labels ── */
QLabel {{
    background: transparent;
}}
/* ── Status bar ── */
QStatusBar {{
    background: rgba(255,255,255,0.7);
    border-top: 1px solid rgba(0,0,0,0.04);
    color: #7f8c8d;
    font-size: 11px;
}}
/* ── Scroll bars ── */
QScrollBar:vertical {{
    background: transparent;
    width: 6px;
    margin: 0;
}}
QScrollBar::handle:vertical {{
    background: rgba(0,0,0,0.12);
    border-radius: 3px;
    min-height: 30px;
}}
QScrollBar::handle:vertical:hover {{
    background: rgba(0,0,0,0.2);
}}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{
    height: 0;
}}
/* ── Text edits ── */
QTextEdit {{
    background: rgba(255,255,255,0.7);
    color: #2c3e50;
    border: 1px solid rgba(0,0,0,0.06);
    border-radius: 8px;
    padding: 8px;
    font-size: 12px;
}}
/* ── Checkbox ── */
QCheckBox {{
    color: #7f8c8d;
    font-size: 12px;
}}
QCheckBox::indicator {{
    width: 16px;
    height: 16px;
    border-radius: 4px;
    border: 1px solid rgba(0,0,0,0.15);
    background: rgba(255,255,255,0.6);
}}
QCheckBox::indicator:checked {{
    background: #4a90d9;
    border: 1px solid #4a90d9;
}}
"""


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")

    palette = QPalette()
    palette.setColor(QPalette.ColorRole.Window, QColor(0xf0, 0xf4, 0xf8))
    palette.setColor(QPalette.ColorRole.WindowText, QColor(0x2c, 0x3e, 0x50))
    palette.setColor(QPalette.ColorRole.Base, QColor(0xff, 0xff, 0xff))
    palette.setColor(QPalette.ColorRole.Text, QColor(0x2c, 0x3e, 0x50))
    palette.setColor(QPalette.ColorRole.Button, QColor(0xff, 0xff, 0xff, 0xb0))
    palette.setColor(QPalette.ColorRole.ButtonText, QColor(0x2c, 0x3e, 0x50))
    palette.setColor(QPalette.ColorRole.Highlight, QColor(0x4a, 0x90, 0xd9))
    palette.setColor(QPalette.ColorRole.HighlightedText, QColor(0xff, 0xff, 0xff))
    app.setPalette(palette)

    app.setStyleSheet(STYLE)

    w = MainWindow()
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
