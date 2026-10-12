@echo off
cd /d "%~dp0"
python scripts\watch_replay.py
if errorlevel 1 pause
