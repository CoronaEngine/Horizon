<#
    dev.ps1
    -------
    Unified developer entry point for common Horizon workflows.

    Usage:
        .\tools\dev.ps1 status
        .\tools\dev.ps1 install
        .\tools\dev.ps1 configure
        .\tools\dev.ps1 build Horizon
        .\tools\dev.ps1 build Horizon -Configuration Release
        .\tools\dev.ps1 format-check
        .\tools\dev.ps1 format
        .\tools\dev.ps1 format src/hardware_wrapper_vulkan
#>
[CmdletBinding()]
Param(
    [Parameter(Position = 0)]
    [ValidateSet("status", "install", "configure", "build", "format-check", "format")]
    [string]$Command = "status",

    [Parameter()]
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",

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

function Get-MsvcBuildPreset {
    switch ($Configuration) {
        "Debug" { return "conan-debug" }
        "Release" { return "conan-release" }
        "RelWithDebInfo" { return "conan-relwithdebinfo" }
        "MinSizeRel" { return "conan-minsizerel" }
    }
}

function Import-BatchEnvironment {
    param([Parameter(Mandatory = $true)][string]$BatchFile)

    if (-not (Test-Path -LiteralPath $BatchFile)) {
        throw "Environment batch file was not found: $BatchFile"
    }

    $escapedBatchFile = $BatchFile.Replace('"', '\"')
    $environment = & cmd.exe /d /s /c "`"call `"$escapedBatchFile`" >nul && set`""
    foreach ($line in $environment) {
        $separator = $line.IndexOf("=")
        if ($separator -gt 0) {
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            Set-Item -Path "Env:$name" -Value $value
        }
    }
}

function Import-ConanBuildEnvironment {
    $buildEnv = Join-Path $RepoRoot "build\conan\generators\conanbuild.bat"
    Import-BatchEnvironment -BatchFile $buildEnv
}

function Get-ConanProfile {
    switch ($Configuration) {
        "Debug" { return (Join-Path $RepoRoot "conan\profiles\windows-msvc-debug") }
        "Release" { return (Join-Path $RepoRoot "conan\profiles\windows-msvc-release") }
        "RelWithDebInfo" { return (Join-Path $RepoRoot "conan\profiles\windows-msvc-relwithdebinfo") }
        "MinSizeRel" { return (Join-Path $RepoRoot "conan\profiles\windows-msvc-minsizerel") }
    }
}

function Get-ConanInstallOptions {
    $targetValues = @($Target)
    $options = @()

    if ($targetValues -contains "ShaderCompileScripts") {
        $options += "&:with_tools=True"
    }

    if ($targetValues -contains "HorizonExamples") {
        $options += "&:with_examples=True"
    }

    if ($targetValues -contains "HorizonTests") {
        $options += "&:with_tests=True"
    }

    return $options
}

function Invoke-ConanInstall {
    $profile = Get-ConanProfile
    $installOptions = Get-ConanInstallOptions
    $installArguments = @(
        "install",
        ".",
        "-pr:a", $profile,
        "-pr:b", $profile
    )
    foreach ($option in $installOptions) {
        $installArguments += @("-o", $option)
    }
    $installArguments += "--build=missing"

    Invoke-NativeCommand -FilePath "conan" -Arguments @("export", "conan\recipes\ktm")
    Invoke-NativeCommand -FilePath "conan" -Arguments @("export", "conan\recipes\pfr")
    Invoke-NativeCommand -FilePath "conan" -Arguments @("export", "conan\recipes\slang")
    Invoke-NativeCommand -FilePath "conan" -Arguments @("export", "conan\recipes\vulkan-memory-allocator")
    Invoke-NativeCommand -FilePath "conan" -Arguments $installArguments
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
            Invoke-NativeCommand -FilePath "conan" -Arguments @("--version")
            Invoke-NativeCommand -FilePath "cmake" -Arguments @("--list-presets")
        }
        "install" {
            Invoke-ConanInstall
        }
        "configure" {
            Invoke-ConanInstall
            Import-ConanBuildEnvironment
            Invoke-NativeCommand -FilePath "cmake" -Arguments @("--preset", "conan-default")
        }
        "build" {
            Invoke-ConanInstall
            Import-ConanBuildEnvironment
            Invoke-NativeCommand -FilePath "cmake" -Arguments @("--preset", "conan-default")
            Invoke-NativeCommand -FilePath "cmake" -Arguments @("--build", "--preset", (Get-MsvcBuildPreset), "--target", $Target[0])
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
