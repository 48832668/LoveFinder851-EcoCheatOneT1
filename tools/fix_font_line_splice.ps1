$ErrorActionPreference = 'Stop'

<#
  修复 PickSoul 生成的字库文件中的「行拼接」数据丢失问题。

  问题：生成器为反斜杠字符输出的注释是 `// \`。
        C/C++ 里行尾的反斜杠是【行拼接】(translation phase 2)，
        而它在【注释移除】(phase 3) 之前执行 ——
        于是下一行整个被拼进注释里，字模数据被静默吃掉，
        导致该字符之后的全部字形错位一格。

  修复：把行尾的 `// \` 改成 `// backslash`，让行末不是反斜杠。

  用法：每次用 PickSoul 重新生成字库后跑一次本脚本。
        用法:  pwsh -File tools\fix_font_line_splice.ps1
#>

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$fontDir = Join-Path $root 'LoveFinderLibForPY32_LL\FontLib'

if (-not (Test-Path $fontDir)) {
    Write-Host "  [!!] 找不到字库目录: $fontDir" -ForegroundColor Red
    exit 1
}

$totalFixed = 0
foreach ($name in @('fonts.cpp', 'font_data.cpp')) {
    $p = Join-Path $fontDir $name
    if (-not (Test-Path $p)) { continue }

    $lines = [System.IO.File]::ReadAllLines($p)
    $fixed = 0
    for ($i = 0; $i -lt $lines.Count; $i++) {
        # 行尾是反斜杠（允许其后有空白）—— 危险
        if ($lines[$i] -match '^(.*?)\\\s*$') {
            $lines[$i] = $Matches[1] + 'backslash'
            $fixed++
        }
    }

    if ($fixed -gt 0) {
        [System.IO.File]::WriteAllLines($p, $lines)
        Write-Host "  [OK] $name : 修复 $fixed 行" -ForegroundColor Green
        $totalFixed += $fixed
    } else {
        Write-Host "  [--] $name : 无需修复" -ForegroundColor DarkGray
    }
}

Write-Host ""
if ($totalFixed -gt 0) {
    Write-Host "  共修复 $totalFixed 行。请重新编译工程。" -ForegroundColor Cyan
} else {
    Write-Host "  没有发现行拼接问题。" -ForegroundColor Cyan
}
