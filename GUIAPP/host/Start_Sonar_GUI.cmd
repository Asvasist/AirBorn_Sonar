@echo off
cd /d "%~dp0"
if exist "..\.venv\Scripts\python.exe" (
    "..\.venv\Scripts\python.exe" sonar_gui.py
) else (
    py -3 sonar_gui.py
)
if errorlevel 1 pause
