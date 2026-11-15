<#
.SYNOPSIS
    构建 app/ 应用到指定 STM32 板（默认正点原子阿波罗 V2 / apollo_f429）。
.EXAMPLE
    .\scripts\build.ps1
    .\scripts\build.ps1 -Pristine
    .\scripts\build.ps1 -Board stm32f429i_disc1
    .\scripts\build.ps1 -ExtraConf boards\stm32f429i_disc1.conf
#>
param(
    [string]$Board = "apollo_f429",
    [switch]$Pristine,
    [string]$ExtraConf = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "env.ps1")

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build\$Board"

$westArgs = @("build", "-b", $Board, "-d", $buildDir)
if ($Pristine) { $westArgs += @("-p", "always") }

$extra = @()

# 自定义板 apollo_f429 的 BOARD_ROOT 与 ZEPHYR_EXTRA_MODULES 已内置在 app/CMakeLists.txt，
# 脚本不再重复传参：IDE 直接打开或裸 cmake 配置时也能定位板定义与驱动模块。

if ($ExtraConf -ne "") { $extra += "-DEXTRA_CONF_FILE=$ExtraConf" }

if ($extra.Count -gt 0) {
    $westArgs += "app"
    $westArgs += "--"
    $westArgs += $extra
}
else {
    $westArgs += "app"
}

Push-Location $root
try {
    & west @westArgs
    $code = $LASTEXITCODE
}
finally {
    Pop-Location
}

if ($code -ne 0) {
    Write-Host ""
    Write-Host "构建失败。若出现 multiple definition 之类的链接错误，" -ForegroundColor Yellow
    Write-Host "通常是同一 build 目录的 CMake 增量状态残留，请执行：.\scripts\build.ps1 -Pristine" -ForegroundColor Yellow
    throw "west build 失败（exit=$code）"
}

$elf = Join-Path $buildDir "zephyr\zephyr.elf"
$bin = Join-Path $buildDir "zephyr\zephyr.bin"
Write-Host ""
Write-Host "[build] ok -> $elf"
Write-Host ("[build] size: elf {0:N0} B / bin {1:N0} B" -f (Get-Item -LiteralPath $elf).Length, (Get-Item -LiteralPath $bin).Length)
