<#
  sync_fontlib.ps1 — 同步/校验两个后端库的 FontLib

  背景
  ----
  FontLib 是【驱动无关】的（只处理 uint16_t 位图数组，不碰 SPI/GPIO），
  但它同时存在于两个后端库里，让「LL 例程只依赖 LL 库、HAL 例程只依赖
  HAL 库」这个不变量成立：

      LoveFinderLibForPY32_LL/FontLib/     ← 唯一真相源
      LoveFinderLibForPY32_HAL/FontLib/    ← 由本脚本从上面同步而来

  本脚本让这份复制【由工具产生】，而不是靠手工维护，因此不会分叉。

  用法
  ----
      pwsh -File tools\sync_fontlib.ps1            # 同步（LL -> HAL）
      pwsh -File tools\sync_fontlib.ps1 -Verify    # 只校验，不改动
                                                     不一致时以退出码 1 结束

  真相源
  ------
  font_manifest.json 是字库的唯一真相源，PickSoul 只写 LL 那份。
  改了字库之后跑一次本脚本即可。
#>
param(
    [switch]$Verify
)

$ErrorActionPreference = 'Stop'

$root   = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$srcDir = Join-Path $root 'LoveFinderLibForPY32_LL\FontLib'
$dstDir = Join-Path $root 'LoveFinderLibForPY32_HAL\FontLib'

if (-not (Test-Path $srcDir)) { Write-Host "  [!!] 真相源不存在: $srcDir" -ForegroundColor Red; exit 2 }
if (-not (Test-Path $dstDir)) { New-Item -ItemType Directory -Path $dstDir -Force | Out-Null }

function Get-FileHashSafe($p) {
    if (Test-Path $p) { return (Get-FileHash $p -Algorithm SHA256).Hash }
    return $null
}

$srcFiles = @(Get-ChildItem $srcDir -File | Sort-Object Name)
$dstFiles = @(Get-ChildItem $dstDir -File | Sort-Object Name)

Write-Host ""
Write-Host "  真相源 (LL): $($srcFiles.Count) 个文件"
Write-Host "  目标   (HAL): $($dstFiles.Count) 个文件"
Write-Host ""

$diff = @()
foreach ($sf in $srcFiles) {
    $df = Join-Path $dstDir $sf.Name
    $sh = Get-FileHashSafe $sf.FullName
    $dh = Get-FileHashSafe $df
    if ($sh -ne $dh) { $diff += $sf }
}

# 目标里多出来的文件也算不一致
$srcNames = $srcFiles | Select-Object -ExpandProperty Name
$extra = $dstFiles | Where-Object { $srcNames -notcontains $_.Name }

if ($Verify) {
    if ($diff.Count -eq 0 -and $extra.Count -eq 0) {
        Write-Host "  [OK] 两份 FontLib 完全一致 ✅" -ForegroundColor Green
        exit 0
    }
    Write-Host "  [!!] 不一致：" -ForegroundColor Red
    foreach ($d in $diff)  { Write-Host ("       差异   " + $d.Name) -ForegroundColor Red }
    foreach ($e in $extra) { Write-Host ("       多余   " + $e.Name) -ForegroundColor Red }
    Write-Host ""
    Write-Host "  运行不带 -Verify 的本脚本可自动同步。" -ForegroundColor Yellow
    exit 1
}

if ($diff.Count -eq 0 -and $extra.Count -eq 0) {
    Write-Host "  [--] 已经一致，无需同步" -ForegroundColor DarkGray
    exit 0
}

foreach ($d in $diff) {
    Copy-Item $d.FullName (Join-Path $dstDir $d.Name) -Force
    Write-Host ("  [OK] 同步 " + $d.Name) -ForegroundColor Green
}
foreach ($e in $extra) {
    Remove-Item $e.FullName -Force
    Write-Host ("  [OK] 删除多余的 " + $e.Name) -ForegroundColor Green
}

Write-Host ""
Write-Host "  完成。两个后端库的 FontLib 现已一致。" -ForegroundColor Cyan
Write-Host "  提示：HAL 侧例程需重新编译才会用到新字库。" -ForegroundColor Cyan
