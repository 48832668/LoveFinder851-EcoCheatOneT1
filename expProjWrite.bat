@echo off
chcp 65001 >nul
title 例程烧录助手
setlocal enabledelayedexpansion

rem ============================================================
rem  expProjWrite.bat - 例程烧录助手
rem  罗列当前所有 Keil 例程，选择序号后自动编译 + 烧录 + 重启
rem
rem  依赖:
rem    cfg\keil.cfg     → KEIL_HOME = Keil MDK 安装目录
rem    cfg\openocd.cfg  → OPENOCD_HOME = OpenOCD 解压根目录
rem ============================================================

set "SCRIPT_DIR=%~dp0"

rem ===================== 读取配置文件 =====================

rem 读取 keil.cfg
if not exist "%SCRIPT_DIR%cfg\keil.cfg" (
  echo [错误] 找不到 cfg\keil.cfg，请先按 cfg\ 目录中的说明文件填写路径。
  pause & exit /b 1
)
for /f "usebackq tokens=1,* delims==" %%A in ("%SCRIPT_DIR%cfg\keil.cfg") do (
  if /i "%%A"=="KEIL_HOME" set "KEIL_HOME=%%B"
)
if not defined KEIL_HOME (
  echo [错误] cfg\keil.cfg 中未定义 KEIL_HOME。请编辑后重试。
  pause & exit /b 1
)
if not exist "%KEIL_HOME%\UV4\UV4.exe" (
  echo [错误] 找不到 UV4.exe: %KEIL_HOME%\UV4\UV4.exe
  echo        请检查 cfg\keil.cfg 中的 KEIL_HOME 路径是否正确。
  pause & exit /b 1
)
set "UV4_EXE=%KEIL_HOME%\UV4\UV4.exe"

rem 读取 openocd.cfg
if not exist "%SCRIPT_DIR%cfg\openocd.cfg" (
  echo [错误] 找不到 cfg\openocd.cfg，请先按 cfg\ 目录中的说明文件填写路径。
  pause & exit /b 1
)
for /f "usebackq tokens=1,* delims==" %%A in ("%SCRIPT_DIR%cfg\openocd.cfg") do (
  if /i "%%A"=="OPENOCD_HOME" set "OPENOCD_HOME=%%B"
)
if not defined OPENOCD_HOME (
  echo [错误] cfg\openocd.cfg 中未定义 OPENOCD_HOME。请编辑后重试。
  pause & exit /b 1
)
if not exist "%OPENOCD_HOME%\bin\openocd.exe" (
  echo [错误] 找不到 openocd.exe: %OPENOCD_HOME%\bin\openocd.exe
  echo        请检查 cfg\openocd.cfg 中的 OPENOCD_HOME 路径是否正确。
  pause & exit /b 1
)
set "OPENOCD_EXE=%OPENOCD_HOME%\bin\openocd.exe"
set "OPENOCD_SCRIPTS=%OPENOCD_HOME%\share\openocd\scripts"

rem ===================== 扫描例程 =====================

echo.
echo 正在扫描例程...
echo.

set "idx=0"
for /d %%D in ("%SCRIPT_DIR%examples\*") do (
  for %%F in ("%%D\MDK-ARM\*.uvprojx") do (
    set /a "idx+=1"
    set "PROJ_DIR[!idx!]=%%D"
    set "PROJ_UVPROJX[!idx!]=%%F"
    set "PROJ_NAME[!idx!]=%%~nF"
    set "PROJ_DISP[!idx!]=%%~nxD (%%~nF)"
  )
)

set "TOTAL=%idx%"

if %TOTAL%==0 (
  echo [提示] 没有找到任何例程（examples\*\MDK-ARM\*.uvprojx）。
  pause & exit /b 0
)

:menu
cls
echo ========================================
echo         例程烧录助手
echo ========================================
echo.

for /l %%I in (1,1,%TOTAL%) do (
  echo   [%%I] !PROJ_DISP[%%I]!
)
echo.
echo   [R] 刷新列表
echo   [Q] 退出
echo.

set "choice="
set /p "choice=请选择例程序号: "

if /i "!choice!"=="Q" exit /b 0
if /i "!choice!"=="R" goto :menu

rem 验证输入是否为有效数字
set "is_num=1"
for /f "delims=0123456789" %%C in ("!choice!") do set "is_num=0"
if "!is_num!"=="0" (
  echo 输入无效，请重新输入。
  timeout /t 2 >nul
  goto :menu
)

if !choice! LSS 1 (
  echo 序号超出范围，请重新输入。
  timeout /t 2 >nul
  goto :menu
)
if !choice! GTR %TOTAL% (
  echo 序号超出范围，请重新输入。
  timeout /t 2 >nul
  goto :menu
)

set "SEL_PROJ_DIR=!PROJ_DIR[%choice%]!"
set "SEL_UVPROJX=!PROJ_UVPROJX[%choice%]!"
set "SEL_NAME=!PROJ_NAME[%choice%]!"
set "SEL_DISP=!PROJ_DISP[%choice%]!"
set "SEL_HEX=!SEL_PROJ_DIR!\MDK-ARM\Objects\!SEL_NAME!.hex"
set "BUILD_LOG=%TEMP%\!SEL_NAME!_build.log"

echo.
echo ========================================
echo  已选: !SEL_DISP!
echo ========================================

rem ===================== 编译 =====================
echo.
echo [1/3] 正在编译 ...
"%UV4_EXE%" -b "!SEL_UVPROJX!" -o "!BUILD_LOG!" -j0
set "BUILD_EXIT=%ERRORLEVEL%"
type "!BUILD_LOG!" | findstr /i /c:"error" /c:"warning" /c:"0 Error" /c:"0 Warning"

if %BUILD_EXIT%==0 (
  echo [OK] 编译成功（无错误）。
) else if %BUILD_EXIT%==1 (
  echo [OK] 编译成功（有警告，可忽略）。
) else (
  echo [失败] 编译出错（UV4 退出码 %BUILD_EXIT%）。
  echo        查看编译日志: !BUILD_LOG!
  pause & exit /b %BUILD_EXIT%
)

rem ===================== 烧录 =====================
echo.
echo [2/3] 正在连接 DAPLink 并烧录 ...
if not exist "!SEL_HEX!" (
  echo [错误] 找不到 HEX 文件: !SEL_HEX!
  pause & exit /b 1
)

"%OPENOCD_EXE%" -s "%OPENOCD_SCRIPTS%" ^
  -f interface/cmsis-dap.cfg -f target/stm32f0x.cfg ^
  -c "transport select swd" ^
  -c "program {!SEL_HEX!} 0x08000000 verify" ^
  -c "reset run" ^
  -c "shutdown"
set "FLASH_EXIT=%ERRORLEVEL%"

if %FLASH_EXIT%==0 (
  echo [OK] 烧录完成。
) else (
  echo [失败] 烧录失败（OpenOCD 退出码 %FLASH_EXIT%）。
  echo        请检查 USB 连接/供电/SWD 接线。
  pause & exit /b %FLASH_EXIT%
)

rem ===================== 复位 =====================
echo.
echo [3/3] 正在复位 ...
"%OPENOCD_EXE%" -s "%OPENOCD_SCRIPTS%" ^
  -f interface/cmsis-dap.cfg -f target/stm32f0x.cfg ^
  -c "transport select swd" -c "init" -c "reset run" -c "shutdown"
if %ERRORLEVEL%==0 (
  echo [OK] 复位完成，程序已运行。
) else (
  echo [失败] 复位失败（可尝试烧录后手动按复位键）。
)
echo.
echo ========================================
echo          操作完毕!
echo ========================================
echo.
pause
goto :menu