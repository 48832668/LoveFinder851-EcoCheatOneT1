<#
  reset_py32.ps1 - PY32F003 Yi Jian Chong Qi (DAPLink + OpenOCD)
  Usage:
    .\reset_py32.ps1          reset and run
    .\reset_py32.ps1 halt     reset and halt
  OpenOCD path: cfg\openocd.cfg (OPENOCD_HOME) > env > default
#>

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$ScriptDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$CfgFile   = Join-Path $ScriptDir 'cfg\openocd.cfg'

$OcdHome = $null
if (Test-Path $CfgFile) {
    foreach ($line in Get-Content -LiteralPath $CfgFile -Encoding UTF8) {
        $t = $line.Trim()
        if ($t -eq '' -or $t.StartsWith('#')) { continue }
        $i = $t.IndexOf('=')
        if ($i -gt 0 -and $t.Substring(0, $i).Trim() -ieq 'OPENOCD_HOME') {
            $OcdHome = $t.Substring($i + 1).Trim()
        }
    }
}
if (-not $OcdHome) { $OcdHome = $env:OPENOCD_HOME }
if (-not $OcdHome) { $OcdHome = 'C:\Env\openocd-v0.12.0-i686-w64-mingw32' }

$OcdExe     = Join-Path $OcdHome 'bin\openocd.exe'
$OcdScripts = Join-Path $OcdHome 'share\openocd\scripts'

if (-not (Test-Path $OcdExe)) {
    Write-Host "[ERROR] Cannot find OpenOCD: $OcdExe" -ForegroundColor Red
    Write-Host "        Please edit cfg\openocd.cfg or set OPENOCD_HOME."
    exit 1
}

$resetCmd = if ($args -contains 'halt') { 'reset halt' } else { 'reset run' }

Write-Host "Connecting PY32F003 via DAPLink ..." -ForegroundColor Cyan
& $OcdExe -s $OcdScripts `
    -f interface/cmsis-dap.cfg -f target/stm32f0x.cfg `
    -c 'transport select swd' -c 'init' -c $resetCmd -c 'shutdown' 2>&1 |
    ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "[OK] Reset done." -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "[FAIL] OpenOCD exit code $LASTEXITCODE. Check USB/SWD/power." -ForegroundColor Red
}
exit $LASTEXITCODE