<#
    dev.ps1
    -------
    Unified developer entry point for common Horizon workflows.

    Usage:
        .\tools\dev.ps1 status
        .\tools\dev.ps1 configure
        .\tools\dev.ps1 build Horizon
        .\tools\dev.ps1 format-check
        .\tools\dev.ps1 format
        .\tools\dev.ps1 format src/hardware_wrapper_vulkan
#>
[CmdletBinding()]
Param(
    [Parameter(Position = 0)]
    [ValidateSet("status", "configure", "build", "format-check", "format")]
    [string]$Command = "status",

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [ValidateNotNullOrEmpty()]
    [string[]]$Target = @("Horizon")
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [string[]]$Arguments = @()
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

function Get-FormatArguments {
    $targetValues = @($Target)
    if ($targetValues.Count -eq 1 -and $targetValues[0] -eq "Horizon") {
        return @()
    }

    return @($targetValues)
}

function Invoke-FormatScript {
    param([Parameter(Mandatory = $true)][bool]$CheckOnly)

    $script = Join-Path $RepoRoot "tools\code-format.ps1"
    $arguments = @(Get-FormatArguments)
    if ($CheckOnly) {
        & $script -Check @arguments
    }
    else {
        & $script @arguments
    }
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Push-Location -LiteralPath $RepoRoot
try {
    switch ($Command) {
        "status" {
            Invoke-NativeCommand -FilePath "git" -Arguments @("status", "--short", "--branch")
            Invoke-NativeCommand -FilePath "cmake" -Arguments @("--list-presets")
        }
        "configure" {
            Invoke-NativeCommand -FilePath "cmake" -Arguments @("--preset", "ninja-msvc")
        }
        "build" {
            Invoke-NativeCommand -FilePath "cmake" -Arguments @("--build", "--preset", "msvc-debug", "--target", $Target[0])
        }
        "format-check" {
            Invoke-FormatScript -CheckOnly $true
        }
        "format" {
            Invoke-FormatScript -CheckOnly $false
        }
    }
}
finally {
    Pop-Location
}
