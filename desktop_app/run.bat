@echo off
chcp 65001 > nul
setlocal enabledelayedexpansion

cd /d "%~dp0"

if not exist ".venv\" (
    echo [*] Creating Python virtual environment...
    python -m venv .venv
    if errorlevel 1 (
        echo [!] Failed to create venv. Make sure Python 3 is installed.
        pause
        exit /b 1
    )
)

echo [*] Activating venv...
call .venv\Scripts\activate.bat

echo [*] Installing dependencies...
pip install -q -r requirements.txt
if errorlevel 1 (
    echo [!] pip install failed.
    pause
    exit /b 1
)

echo [*] Starting SmartBracelet Monitor...
python main.py

if errorlevel 1 (
    echo [!] App exited with error.
    pause
)
