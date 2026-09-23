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
    3) OpenOCD 烧录（tools\py32_flash.tcl，SWD 固定 200 kHz，范围 0x08000000-0x0800FFFF）
    4) OpenOCD 复位运行（同样保持 200 kHz）
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

$FlashTclPath = Join-Path $ScriptDir 'tools\py32_flash.tcl'
if (-not (Test-Path -LiteralPath $FlashTclPath)) { Fail "找不到烧录驱动脚本：$FlashTclPath" }

# ===================== 扫描例程 =====================
# 目录结构：examples_LL\<例程名>\MDK-ARM\*.uvprojx
function Get-Examples {
    $list = @()
    $exRoot = Join-Path $ScriptDir 'examples_LL'
    if (-not (Test-Path $exRoot)) { return $list }

    foreach ($projDir in Get-ChildItem -LiteralPath $exRoot -Directory | Sort-Object Name) {
        $mdk = Join-Path $projDir.FullName 'MDK-ARM'
        if (-not (Test-Path $mdk)) { continue }
        foreach ($proj in Get-ChildItem -LiteralPath $mdk -Filter '*.uvprojx' -File) {
            $label = $projDir.Name
            $list += [pscustomobject]@{
                Name    = $label
                Uvprojx = $proj.FullName
                MdkDir  = $mdk
                Hex     = Join-Path $mdk ("Objects\" + $proj.BaseName + ".hex")
            }
        }
    }
    return $list
}

# ===================== 解析 Intel HEX 为 128 字节页 =====================
# 返回 hashtable: 页基址 -> byte[128]（未覆盖字节填 0xFF）。
# 地址超出 0x08000000-0x0800FFFF（64KB）时抛异常。
function Get-HexPages {
    param([string]$HexPath)

    $data = @{}
    $ext  = [int64]0
    foreach ($line in [System.IO.File]::ReadLines($HexPath)) {
        $l = $line.Trim()
        if (-not $l.StartsWith(':') -or $l.Length -lt 11) { continue }
        $len  = [Convert]::ToInt32($l.Substring(1, 2), 16)
        $addr = [Convert]::ToInt32($l.Substring(3, 4), 16)
        $type = [Convert]::ToInt32($l.Substring(7, 2), 16)
        if ($type -eq 0) {
            for ($i = 0; $i -lt $len; $i++) {
                $a = $ext + $addr + $i
                if ($a -lt 0x08000000 -or $a -gt 0x0800FFFF) {
                    throw ("HEX 数据超出 Flash 范围 0x08000000-0x0800FFFF（地址 0x{0:X8}）" -f $a)
                }
                $data[$a] = [Convert]::ToInt32($l.Substring(9 + 2 * $i, 2), 16)
            }
        }
        elseif ($type -eq 2) { $ext = ([int64][Convert]::ToInt32($l.Substring(9, 4), 16)) * 16 }
        elseif ($type -eq 4) { $ext = ([int64][Convert]::ToInt32($l.Substring(9, 4), 16)) -shl 16 }
        # 01=EOF、03/05=起始地址记录：忽略
    }
    if ($data.Count -eq 0) { throw "HEX 文件中没有有效数据记录" }

    $pages = @{}
    foreach ($a in $data.Keys) {
        $pb = ($a -shr 7) -shl 7
        if (-not $pages.ContainsKey($pb)) {
            $buf = New-Object byte[] 128
            for ($j = 0; $j -lt 128; $j++) { $buf[$j] = 0xFF }
            $pages[$pb] = $buf
        }
        $pages[$pb][$a - $pb] = [byte]$data[$a]
    }
    return $pages
}

# ===================== 复位 =====================
function Invoke-Reset {
    Write-Host ""
    Write-Host "[3/3] 正在通过 OpenOCD 复位运行 ..." -ForegroundColor Cyan
    # py32_flash.tcl 里覆盖了 stm32f0x 的 reset 钩子：
    #   reset_start 保持 200 kHz；reset_init 不执行（其对 STM32F0 RCC/FLASH 的
    #   写入在 PY32F003 上会破坏寄存器状态）。
    $driverTcl = $FlashTclPath.Replace('\', '/')
    $ocdArgs = @(
        '-s', $OcdScripts,
        '-f', 'interface/cmsis-dap.cfg',
        '-f', 'target/stm32f0x.cfg',
        '-c', 'transport select swd',
        '-c', 'adapter speed 200',
        '-c', 'init',
        '-c', "source {$driverTcl}",
        '-c', 'reset run',
        '-c', 'shutdown'
    )
    # PS 5.1 注意：openocd 日志走 stderr，2>&1 会把它包装成 ErrorRecord，
    # 在 $ErrorActionPreference='Stop' 下会抛 NativeCommandError 中断脚本，
    # 所以只在这条命令执行期间把偏好临时降为 Continue。
    $eap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $OcdExe @ocdArgs 2>&1 |
            ForEach-Object { Write-Host "      $_" -ForegroundColor DarkGray }
        $rc = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $eap
    }
    if ($rc -eq 0) {
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

    # ---------- 2/3 烧录（OpenOCD + tools\py32_flash.tcl，200 kHz） ----------
    Write-Host ""
    Write-Host "[2/3] 正在烧录（OpenOCD，SWD 200 kHz，0x08000000-0x0800FFFF）..." -ForegroundColor Cyan

    if (-not (Test-Path -LiteralPath $sel.Hex)) {
        Write-Host "      [失败] 找不到 HEX 文件，编译可能未生成：" -ForegroundColor Red
        Write-Host ("                " + $sel.Hex) -ForegroundColor DarkGray
        Write-Host "      请检查 Keil 工程 Options for Target -> Output -> Create HEX File。" -ForegroundColor Yellow
        Read-Host "      按回车返回菜单" | Out-Null
        continue
    }

    # --- 解析 Intel HEX -> 128 字节页（超出 64KB 范围直接报错） ---
    try {
        $pages = Get-HexPages $sel.Hex
    } catch {
        Write-Host ("      [失败] " + $_.Exception.Message) -ForegroundColor Red
        Read-Host "      按回车返回菜单" | Out-Null
        continue
    }

    # --- 生成 OpenOCD Tcl 数据脚本（纯 ASCII，写入 %TEMP%） ---
    $dataTcl = Join-Path $env:TEMP ($sel.Name + "_flash_data.tcl")
    $tcl = New-Object System.Collections.Generic.List[string]
    $tcl.Add("# auto-generated by expProjWrite.ps1 - do not edit")
    $tcl.Add("py32_flash_begin")
    foreach ($pb in ($pages.Keys | Sort-Object)) {
        $buf = $pages[$pb]
        $words = New-Object System.Collections.Generic.List[string]
        for ($w = 0; $w -lt 32; $w++) {
            $o = $w * 4
            $val = [int64]$buf[$o] + ([int64]$buf[$o + 1] -shl 8) +
                   ([int64]$buf[$o + 2] -shl 16) + ([int64]$buf[$o + 3] -shl 24)
            $words.Add(("0x{0:x8}" -f $val))
        }
        $tcl.Add(("py32_erase_page 0x{0:x8}" -f $pb))
        $tcl.Add(("py32_program_page 0x{0:x8} {{{1}}}" -f $pb, ($words -join ' ')))
    }
    $tcl.Add("py32_flash_end")
    [System.IO.File]::WriteAllText(
        $dataTcl, (($tcl -join "`r`n") + "`r`n"), (New-Object System.Text.UTF8Encoding($false)))

    # --- 残留的 openocd 会抢占 CMSIS-DAP 设备，先结束 ---
    $busy = Get-Process -Name openocd -ErrorAction SilentlyContinue
    if ($busy) {
        Write-Host "      [提示] 结束残留的 openocd 进程 ..." -ForegroundColor Yellow
        $busy | Stop-Process -Force
        Start-Sleep -Milliseconds 500
    }

    # --- 执行烧录 ---
    $driverTcl = $FlashTclPath.Replace('\', '/')
    $dataPath  = $dataTcl.Replace('\', '/')
    $ocdArgs = @(
        '-s', $OcdScripts,
        '-f', 'interface/cmsis-dap.cfg',
        '-f', 'target/stm32f0x.cfg',
        '-c', 'transport select swd',
        '-c', 'adapter speed 200',
        '-c', 'init',
        '-c', 'halt',
        '-c', "source {$driverTcl}",
        '-c', "source {$dataPath}",
        '-c', 'shutdown'
    )
    # PS 5.1 注意：同 Invoke-Reset，openocd 的 stderr 日志经 2>&1 会在
    # ErrorActionPreference='Stop' 下抛 NativeCommandError，临时降为 Continue。
    $eap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $ocdOut  = & $OcdExe @ocdArgs 2>&1
        $ocdExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $eap
    }
    $ocdOut | ForEach-Object { Write-Host "      $_" -ForegroundColor DarkGray }
    $ocdText = $ocdOut | Out-String

    # 驱动成功时输出 PY32_FLASH_OK；出错时 OpenOCD 以非 0 退出并打印 py32:/Error: 行
    if (($ocdExit -eq 0) -and ($ocdText -match 'PY32_FLASH_OK')) {
        Write-Host ("      [OK] 烧录完成（{0} 页 x 128 字节，逐页读回校验通过，SWD 200 kHz）。" -f $pages.Count) -ForegroundColor Green
    } else {
        Write-Host ("      [失败] OpenOCD 烧录失败（退出码 {0}）。" -f $ocdExit) -ForegroundColor Red
        Write-Host "      常见原因：接线/供电异常、调试器被其他软件占用、或上方灰色日志中的 py32: / Error: 行。" -ForegroundColor Yellow
        Read-Host "      按回车返回菜单" | Out-Null
        continue
    }

    # ---------- 3/3 复位 ----------
    Invoke-Reset

    Write-Host ""
    Write-Host "========================================" -ForegroundColor DarkCyan
    Write-Host "          操作完毕" -ForegroundColor White
    Write-Host "========================================" -ForegroundColor DarkCyan
    Write-Host ""
    Read-Host "按回车返回菜单" | Out-Null
}
