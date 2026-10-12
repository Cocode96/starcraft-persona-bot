@echo off
cd /d "%~dp0"
python scripts\watch_match.py
if errorlevel 1 pause
