@echo off
chcp 65001 >nul
setlocal

rem ============================================================
rem  PY32F003 一键重启（SWD + DAPLink + OpenOCD）
rem  用法:
rem     reset_py32.bat         复位并运行（普通重启，等价按复位键）
rem     reset_py32.bat halt    复位后停在复位向量（调试用）
rem  若 OpenOCD 装在别处，可先设置环境变量:
rem     set OPENOCD_HOME=C:\path\to\openocd-v0.12.0-i686-w64-mingw32
rem ============================================================

if not defined OPENOCD_HOME set OPENOCD_HOME=%USERPROFILE%\Downloads\openocd-v0.12.0-i686-w64-mingw32.tar\openocd-v0.12.0-i686-w64-mingw32

set OPENOCD_EXE=%OPENOCD_HOME%\bin\openocd.exe
set OPENOCD_SCRIPTS=%OPENOCD_HOME%\share\openocd\scripts

if not exist "%OPENOCD_EXE%" (
  echo [错误] 找不到 OpenOCD: %OPENOCD_EXE%
  echo 请设置 OPENOCD_HOME 指向 openocd 解压目录后重试。
  exit /b 1
)

if /i "%~1"=="halt" (
  set RESET_CMD=reset halt
) else (
  set RESET_CMD=reset run
)

echo 正在通过 DAPLink 连接 PY32F003 ...
"%OPENOCD_EXE%" -s "%OPENOCD_SCRIPTS%" ^
  -f interface/cmsis-dap.cfg -f target/stm32f0x.cfg ^
  -c "transport select swd" -c "init" -c "%RESET_CMD%" -c "shutdown"
set EXITCODE=%ERRORLEVEL%

if %EXITCODE%==0 (
  echo.
  echo [OK] 复位完成。
) else (
  echo.
  echo [失败] OpenOCD 退出码 %EXITCODE%，请检查 USB 连接/供电/SWD 接线。
)
endlocal & exit /b %EXITCODE%
