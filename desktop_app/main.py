#!/usr/bin/env python3
"""SmartBracelet Desktop Monitor — USB Serial Telemetry Viewer

Usage:
    pip install -r requirements.txt
    python main.py
"""

import sys
from PyQt6.QtWidgets import QApplication
from telemetry_view import MainWindow


def main():
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    app.setStyleSheet("""
        QMainWindow, QWidget {
            background: #0d0d1a;
            color: #ccc;
        }
        QTabWidget::pane {
            border: 1px solid #333;
            background: #0d0d1a;
        }
        QTabBar::tab {
            background: #1a1a2e;
            color: #888;
            padding: 8px 16px;
            border: 1px solid #333;
            border-bottom: none;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }
        QTabBar::tab:selected {
            background: #0d0d1a;
            color: #00d4ff;
            border-bottom: 1px solid #0d0d1a;
        }
        QComboBox {
            background: #1a1a2e;
            color: #ccc;
            border: 1px solid #555;
            padding: 4px 8px;
            border-radius: 4px;
        }
        QComboBox::drop-down {
            border: none;
        }
        QPushButton {
            background: #1a1a2e;
            color: #00d4ff;
            border: 1px solid #00d4ff;
            padding: 6px 16px;
            border-radius: 4px;
            font-weight: bold;
        }
        QPushButton:hover {
            background: #00d4ff;
            color: #000;
        }
        QLineEdit {
            background: #1a1a2e;
            color: #ccc;
            border: 1px solid #555;
            padding: 6px 8px;
            border-radius: 4px;
            font-family: Consolas;
        }
        QGroupBox {
            border: 1px solid #444;
            border-radius: 6px;
            margin-top: 8px;
            padding-top: 12px;
            color: #888;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
        }
        QLabel {
            background: transparent;
        }
        QStatusBar {
            background: #1a1a2e;
            color: #888;
        }
    """)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
