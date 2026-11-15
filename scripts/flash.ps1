<#
.SYNOPSIS
    烧录已构建的固件。
.EXAMPLE
    .\scripts\flash.ps1                 # 默认 pyocd + ST-Link
    .\scripts\flash.ps1 -Runner openocd
#>
param(
    [string]$Board = "stm32f429i_disc1",
    [string]$Runner = "pyocd"
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "env.ps1")

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build\$Board"

Push-Location $root
try {
    & west flash -d $buildDir -r $Runner
    $code = $LASTEXITCODE
}
finally {
    Pop-Location
}

if ($code -ne 0) { throw "烧录失败（exit=$code）" }
Write-Host "[flash] done"
