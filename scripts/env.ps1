# 载入本工作区的 Zephyr 构建环境（在任意 PowerShell 会话中 dot-source 本脚本）
# 注意：dot-source 时 $PSScriptRoot 在 PowerShell 5.1 下可能为空，
# 因此优先用 $MyInvocation.MyCommand.Path 反推脚本自身路径。
$envScript = $MyInvocation.MyCommand.Path
if (-not $envScript) { $envScript = $PSScriptRoot + "\env.ps1" }
$WorkspaceRoot = Split-Path -Parent (Split-Path -Parent $envScript)

$env:ZEPHYR_BASE = Join-Path $WorkspaceRoot "zephyr"
# SDK 放在工作区内（.sdk），避免系统盘目录被清理或回滚
$env:ZEPHYR_SDK_INSTALL_DIR = Join-Path $WorkspaceRoot ".sdk\zephyr-sdk-1.0.0"

# Zephyr 的构建脚本依赖这套 Python（已装 pyelftools / pykwalify / anytree 等）
$ZephyrPython = "D:\ExPrograms\DevelopTool\Python314\python.exe"
$env:WEST_PYTHON = $ZephyrPython
$env:PYTHONIOENCODING = "utf-8"

# 工具链二进制加入当前进程 PATH（不影响系统全局设置）
$sdkBin = Join-Path $env:ZEPHYR_SDK_INSTALL_DIR "arm-zephyr-eabi\bin"
if ((Test-Path -LiteralPath $sdkBin) -and ($env:PATH -notlike "*$sdkBin*")) {
    $env:PATH = "$sdkBin;$env:PATH"
}

Write-Host "[zephyr] workspace  = $WorkspaceRoot"
Write-Host "[zephyr] ZEPHYR_BASE = $env:ZEPHYR_BASE"
Write-Host "[zephyr] SDK         = $env:ZEPHYR_SDK_INSTALL_DIR"
