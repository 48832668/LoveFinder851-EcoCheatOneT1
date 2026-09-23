# ============================================================
#  autoPush.ps1 - 一键提交并推送(自动 add / commit / push)
#  用法: 在仓库根目录执行  .\autoPush.ps1
#  只需输入 commit 消息,其余全自动完成。
#
#  首次运行如被系统拦截,先执行一次:
#     Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
# ============================================================

$ErrorActionPreference = 'Stop'

# 定位到脚本所在目录(即仓库根)
Set-Location -LiteralPath $PSScriptRoot

# 统一 UTF-8:保证与 git 交互的中文(输入/输出/管道)不乱码
$utf8 = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = $utf8
[Console]::OutputEncoding = $utf8
[Console]::InputEncoding = $utf8

function Show-Step([string]$text) { Write-Host $text -ForegroundColor Cyan }
function Show-Error([string]$text) { Write-Host $text -ForegroundColor Red }
function Show-Ok([string]$text)   { Write-Host $text -ForegroundColor Green }

try {
    # --- 检查 git ---
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Show-Error "[错误] 未找到 git,请先安装 Git for Windows 并加入 PATH。"
        Read-Host "按回车退出"
        exit 1
    }

    # --- 检查是否为 git 仓库 ---
    git rev-parse --is-inside-work-tree 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Show-Error "[错误] 当前目录不是 git 仓库,请把脚本放到仓库根目录运行。"
        Read-Host "按回车退出"
        exit 1
    }

    # --- 输入 commit 消息(回车使用默认值,不允许双引号) ---
    $msg = ''
    do {
        $msg = Read-Host "请输入 commit 消息(直接回车使用默认值 'auto update')"
        if ([string]::IsNullOrWhiteSpace($msg)) { $msg = 'auto update' }
        if ($msg.Contains('"')) {
            Show-Error "[提示] commit 消息中不能包含双引号,请重新输入。"
        }
    } while ($msg.Contains('"'))

    # --- 1/4: 暂存所有改动 ---
    Show-Step ""
    Show-Step "[1/4] git add -A (暂存所有改动) ..."
    git add -A
    if ($LASTEXITCODE -ne 0) { throw "git add 失败" }

    # --- 检查是否有实际改动 ---
    $staged = @(git diff --cached --name-only)
    if ($staged.Count -eq 0) {
        Show-Error "[提示] 没有检测到任何改动,跳过 commit 和 push。"
        Read-Host "按回车退出"
        exit 0
    }

    # --- 2/4: 提交(消息经 stdin 以 UTF-8 传入,避免 ANSI 参数乱码) ---
    Show-Step "[2/4] git commit -m `"$msg`" ..."
    $msg | git commit -F -
    if ($LASTEXITCODE -ne 0) { throw "git commit 失败(请先配置 git config user.name / user.email)" }

    # --- 3/4: 推送(首次自动设置 upstream) ---
    Show-Step "[3/4] git push ..."
    git rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) {
        git push -u origin HEAD
    } else {
        git push
    }
    if ($LASTEXITCODE -ne 0) { throw "git push 失败(请检查网络和远程仓库权限)" }

    # --- 4/4: 完成 ---
    Show-Ok ""
    Show-Ok "[4/4] 完成!已成功提交并推送到远程仓库。"
    Show-Ok "       commit 消息: $msg"
    Read-Host "按回车退出"
    exit 0
}
catch {
    Show-Error ""
    Show-Error "[失败] $($_.Exception.Message)"
    Read-Host "按回车退出"
    exit 1
}
