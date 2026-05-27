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
#>
[CmdletBinding()]
Param(
    [Parameter(Position = 0)]
    [ValidateSet("status", "configure", "build", "format-check", "format")]
    [string]$Command = "status",

    [Parameter(Position = 1)]
    [ValidateNotNullOrEmpty()]
    [string]$Target = "Horizon"
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
            Invoke-NativeCommand -FilePath "cmake" -Arguments @("--build", "--preset", "msvc-debug", "--target", $Target)
        }
        "format-check" {
            & (Join-Path $RepoRoot "tools\code-format.ps1") -Check
        }
        "format" {
            & (Join-Path $RepoRoot "tools\code-format.ps1")
        }
    }
}
finally {
    Pop-Location
}
