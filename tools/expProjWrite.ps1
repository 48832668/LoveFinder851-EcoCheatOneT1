<#
  例程烧录助手 (expProjWrite)
  ------------------------------------------------------------------
  为什么用 PowerShell 而不是纯 .bat：
    cmd.exe 解析 UTF-8 批处理文件时，多字节中文会让解析器错位，
    出现 "'查' is not recognized" 之类的随机报错。
    PowerShell 对 Unicode 完全安全，中文菜单稳定显示。
    expProjWrite.bat 只作为双击入口（纯 ASCII），调用本脚本。

  流程：
    1) 扫描 examples\*\MDK-ARM\*.uvprojx
    2) Keil UV4 -b 编译
    3) 提示用 Keil 下载（自动烧录受限于工具链，见下方说明）
    4) OpenOCD 复位运行
#>

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$ScriptDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$CfgDir    = Join-Path $ScriptDir 'cfg'

# ===================== 读取配置 =====================
function Read-CfgValue {
    param([string]$File, [string]$Key)
    if (-not (Test-Path $File)) { return $null }
    foreach ($line in Get-Content -LiteralPath $File -Encoding UTF8) {
        $t = $line.Trim()
        if ($t -eq '' -or $t.StartsWith('#')) { continue }
        $i = $t.IndexOf('=')
        if ($i -lt 1) { continue }
        if ($t.Substring(0, $i).Trim() -ieq $Key) {
            return $t.Substring($i + 1).Trim()
        }
    }
    return $null
}

function Fail([string]$msg) {
    Write-Host ""
    Write-Host "  [错误] $msg" -ForegroundColor Red
    Write-Host ""
    Read-Host "按回车退出" | Out-Null
    exit 1
}

$KeilHome = Read-CfgValue (Join-Path $CfgDir 'keil.cfg') 'KEIL_HOME'
if (-not $KeilHome) { Fail "cfg\keil.cfg 中未定义 KEIL_HOME，请先按 cfg 目录说明填写。" }

$Uv4 = Join-Path $KeilHome 'UV4\UV4.exe'
if (-not (Test-Path $Uv4)) { Fail "找不到 UV4.exe：$Uv4  （请检查 cfg\keil.cfg）" }

$OcdHome = Read-CfgValue (Join-Path $CfgDir 'openocd.cfg') 'OPENOCD_HOME'
if (-not $OcdHome) { Fail "cfg\openocd.cfg 中未定义 OPENOCD_HOME，请先按 cfg 目录说明填写。" }

$OcdExe     = Join-Path $OcdHome 'bin\openocd.exe'
$OcdScripts = Join-Path $OcdHome 'share\openocd\scripts'
if (-not (Test-Path $OcdExe)) { Fail "找不到 openocd.exe：$OcdExe  （请检查 cfg\openocd.cfg）" }

# ===================== 扫描例程 =====================
function Get-Examples {
    $list = @()
    $exRoot = Join-Path $ScriptDir 'examples'
    if (-not (Test-Path $exRoot)) { return $list }
    foreach ($dir in Get-ChildItem -LiteralPath $exRoot -Directory | Sort-Object Name) {
        $mdk = Join-Path $dir.FullName 'MDK-ARM'
        if (-not (Test-Path $mdk)) { continue }
        foreach ($proj in Get-ChildItem -LiteralPath $mdk -Filter '*.uvprojx' -File) {
            $list += [pscustomobject]@{
                Name    = $dir.Name
                Uvprojx = $proj.FullName
                MdkDir  = $mdk
                Hex     = Join-Path $mdk ("Objects\" + $proj.BaseName + ".hex")
            }
        }
    }
    return $list
}

# ===================== 复位 =====================
function Invoke-Reset {
    Write-Host ""
    Write-Host "[3/3] 正在通过 OpenOCD 复位运行 ..." -ForegroundColor Cyan
    & $OcdExe -s $OcdScripts `
        -f interface/cmsis-dap.cfg -f target/stm32f0x.cfg `
        -c "transport select swd" -c "init" -c "reset run" -c "shutdown" 2>&1 |
        ForEach-Object { Write-Host "      $_" -ForegroundColor DarkGray }
    if ($LASTEXITCODE -eq 0) {
        Write-Host "      [OK] 复位完成，程序已运行。" -ForegroundColor Green
    } else {
        Write-Host "      [注意] 复位失败（可手动按复位键）。" -ForegroundColor Yellow
    }
}

# ===================== 主循环 =====================
while ($true) {
    $examples = Get-Examples

    Clear-Host
    Write-Host "========================================" -ForegroundColor DarkCyan
    Write-Host "          例程烧录助手" -ForegroundColor White
    Write-Host "========================================" -ForegroundColor DarkCyan
    Write-Host ""

    if ($examples.Count -eq 0) {
        Write-Host "  [提示] 没有找到任何例程 (examples\*\MDK-ARM\*.uvprojx)" -ForegroundColor Yellow
        Read-Host "按回车退出" | Out-Null
        exit 0
    }

    for ($i = 0; $i -lt $examples.Count; $i++) {
        Write-Host ("  [{0}] {1}" -f ($i + 1), $examples[$i].Name)
    }
    Write-Host ""
    Write-Host "  [R] 刷新列表"
    Write-Host "  [Q] 退出"
    Write-Host ""

    $choice = Read-Host "请选择例程序号"

    if ($choice -ieq 'Q') { exit 0 }
    if ($choice -ieq 'R') { continue }

    $idx = 0
    if (-not [int]::TryParse($choice, [ref]$idx)) {
        Write-Host "  输入无效，请重新输入。" -ForegroundColor Yellow
        Start-Sleep -Seconds 2
        continue
    }
    if ($idx -lt 1 -or $idx -gt $examples.Count) {
        Write-Host "  序号超出范围，请重新输入。" -ForegroundColor Yellow
        Start-Sleep -Seconds 2
        continue
    }

    $sel = $examples[$idx - 1]
    $buildLog = Join-Path $env:TEMP ($sel.Name + "_build.log")

    Write-Host ""
    Write-Host "========================================" -ForegroundColor DarkCyan
    Write-Host ("  已选: " + $sel.Name) -ForegroundColor White
    Write-Host "========================================" -ForegroundColor DarkCyan

    # ---------- 1/3 编译 ----------
    Write-Host ""
    Write-Host "[1/3] 正在编译 ..." -ForegroundColor Cyan
    & $Uv4 -b $sel.Uvprojx -o $buildLog -j0 | Out-Null
    $buildExit = $LASTEXITCODE
    if (Test-Path $buildLog) {
        Get-Content -LiteralPath $buildLog | ForEach-Object {
            Write-Host "      $_" -ForegroundColor DarkGray
        }
    }

    if ($buildExit -eq 0 -or $buildExit -eq 1) {
        Write-Host "      [OK] 编译成功。" -ForegroundColor Green
    } else {
        Write-Host ("      [失败] 编译出错（UV4 退出码 {0}）。" -f $buildExit) -ForegroundColor Red
        Write-Host ("      日志: " + $buildLog) -ForegroundColor DarkGray
        Read-Host "按回车返回菜单" | Out-Null
        continue
    }

    # ---------- 2/3 烧录 ----------
    Write-Host ""
    Write-Host "[2/3] 烧录" -ForegroundColor Cyan
    Write-Host "      说明：本芯片 PY32F003 无法用命令行自动烧录 ——" -ForegroundColor Yellow
    Write-Host "        · OpenOCD 0.12 无 PY32 flash 驱动（会报 Cannot identify target as a stm32x）" -ForegroundColor DarkGray
    Write-Host "        · Keil UV4 -f 批处理模式其 CMSIS-AGDI 无法连接 DAPLink" -ForegroundColor DarkGray
    Write-Host "      请在 Keil 中点击 Download（下载）按钮完成烧录：" -ForegroundColor Yellow
    Write-Host ("        工程: " + $sel.Uvprojx) -ForegroundColor White
    if (Test-Path $sel.Hex) {
        Write-Host ("        HEX : " + $sel.Hex) -ForegroundColor DarkGray
    }
    Write-Host ""
    $ans = Read-Host "      烧录完成后按回车继续（输入 S 跳过复位直接返回）"
    if ($ans -ieq 'S') { continue }

    # ---------- 3/3 复位 ----------
    Invoke-Reset

    Write-Host ""
    Write-Host "========================================" -ForegroundColor DarkCyan
    Write-Host "          操作完毕" -ForegroundColor White
    Write-Host "========================================" -ForegroundColor DarkCyan
    Write-Host ""
    Read-Host "按回车返回菜单" | Out-Null
}
