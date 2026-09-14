@echo off
chcp 65001 >nul
rem ============================================================
rem  expProjWrite.bat - "Li Cheng Shao Lu Zhu Shou" launcher
rem  All UI logic lives in tools\expProjWrite.ps1 (Unicode-safe).
rem  This wrapper stays pure ASCII so cmd.exe never mis-parses it.
rem ============================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\expProjWrite.ps1"
if errorlevel 1 pause