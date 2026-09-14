@echo off
chcp 65001 >nul
rem ============================================================
rem  reset_py32.bat - PY32F003 reset helper
rem  Usage:
rem     reset_py32.bat          reset and run
rem     reset_py32.bat halt     reset and halt
rem  All logic lives in tools\reset_py32.ps1 (Unicode-safe).
rem  This wrapper stays pure ASCII so cmd.exe never mis-parses it.
rem ============================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\reset_py32.ps1" %*
if errorlevel 1 pause