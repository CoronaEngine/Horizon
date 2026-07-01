<#
    dev.ps1
    -------
    Unified developer entry point for common Horizon workflows.

    Usage:
        .\tools\dev.ps1 status
        .\tools\dev.ps1 install
        .\tools\dev.ps1 package ShaderCompileScripts
        .\tools\dev.ps1 configure
        .\tools\dev.ps1 build Horizon
        .\tools\dev.ps1 build-fast Horizon
        .\tools\dev.ps1 rebuild Horizon
        .\tools\dev.ps1 build Horizon -Configuration Release
        .\tools\dev.ps1 update
        .\tools\dev.ps1 clean
        .\tools\dev.ps1 format-check
        .\tools\dev.ps1 format
        .\tools\dev.ps1 format src/hardware_wrapper_vulkan
#>
[CmdletBinding()]
Param(
    [Parameter(Position = 0)]
    [ValidateSet("status", "install", "package", "configure", "build", "build-fast", "rebuild", "update", "clean", "format-check", "format")]
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
. (Join-Path $PSScriptRoot "horizon-conan-recipes.ps1")

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

function Remove-RepoPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RelativePath
    )

    $target = Join-Path $RepoRoot $RelativePath
    if (-not (Test-Path -LiteralPath $target)) {
        Write-Host "[INFO] Not found: $RelativePath"
        return
    }

    $resolvedTarget = (Resolve-Path -LiteralPath $target).Path
    $resolvedRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
    $rootPrefix = $resolvedRoot.TrimEnd("\") + "\"
    if (($resolvedTarget -eq $resolvedRoot) -or (-not $resolvedTarget.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase))) {
        throw "Refusing to remove path outside repository root: $resolvedTarget"
    }

    Write-Host "[INFO] Removing $resolvedTarget"
    Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
}

function Invoke-CleanBuildTree {
    Remove-RepoPath -RelativePath "build"
    Remove-RepoPath -RelativePath "install"
}

function Invoke-CleanProject {
    Write-Host "[INFO] Removing ignored local build/cache files"
    Invoke-NativeCommand -FilePath "git" -Arguments @("clean", "-fdX")
}

function Get-ConanBuildDir {
    return (Join-Path $RepoRoot "build\conan")
}

function Convert-ToComparablePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return $Path.Replace("\", "/").TrimEnd("/").ToLowerInvariant()
}

function Get-CMakeCacheValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CacheFile,

        [Parameter(Mandatory = $true)]
        [string]$Name
    )

    foreach ($line in (Get-Content -LiteralPath $CacheFile)) {
        if ($line -match "^$([regex]::Escape($Name)):[^=]*=(.*)$") {
            return $Matches[1]
        }
    }

    return $null
}

function Assert-CMakeCacheMatchesRepo {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CacheFile
    )

    $sourceDir = Get-CMakeCacheValue -CacheFile $CacheFile -Name "CMAKE_HOME_DIRECTORY"
    if ($sourceDir) {
        $expectedSource = Convert-ToComparablePath -Path $RepoRoot
        $actualSource = Convert-ToComparablePath -Path $sourceDir
        if ($actualSource -ne $expectedSource) {
            throw "CMake cache belongs to '$sourceDir', not '$RepoRoot'. Run '.\tools\dev.ps1 rebuild $($Target[0])'."
        }
    }

    $cacheDir = Get-CMakeCacheValue -CacheFile $CacheFile -Name "CMAKE_CACHEFILE_DIR"
    if ($cacheDir) {
        $expectedCacheDir = Convert-ToComparablePath -Path (Get-ConanBuildDir)
        $actualCacheDir = Convert-ToComparablePath -Path $cacheDir
        if ($actualCacheDir -ne $expectedCacheDir) {
            throw "CMake cache directory is '$cacheDir', not '$(Get-ConanBuildDir)'. Run '.\tools\dev.ps1 rebuild $($Target[0])'."
        }
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
    $requiresOcarina = $false
    $requiresOcarinaTests = $false
    $requiresOcarinaVulkan = $false
    $requiresVisionHotfix = $false

    if ($targetValues -contains "ShaderCompileScripts") {
        $options += "&:with_tools=True"
    }

    if ($targetValues -contains "HorizonExamples") {
        $options += "&:with_examples=True"
    }

    if ($targetValues -contains "HorizonTests") {
        $options += "&:with_tests=True"
    }

    if ($targetValues -contains "HorizonSmokeBenchmarks") {
        $options += "&:with_benchmarks=True"
    }

    foreach ($targetValue in $targetValues) {
        if ($targetValue -like "ocarina*") {
            $requiresOcarina = $true
        }
        if ($targetValue -like "ocarina-test-*") {
            $requiresOcarinaTests = $true
        }
        if ($targetValue -eq "ocarina-backend-vulkan") {
            $requiresOcarinaVulkan = $true
        }
        if ($targetValue -like "vision-hotfix*") {
            $requiresOcarina = $true
            $requiresVisionHotfix = $true
        }
    }

    if ($requiresOcarina) {
        $options += "&:with_ocarina=True"
        $options += "&:with_cuda=True"
    }

    if ($requiresOcarinaTests) {
        $options += "&:with_ocarina_tests=True"
    }

    if ($requiresOcarinaVulkan) {
        $options += "&:with_ocarina_vulkan=True"
    }

    if ($requiresVisionHotfix) {
        $options += "&:with_vision_hotfix=True"
    }

    return $options
}

function Invoke-ConanInstall {
    param([bool]$Update = $false)

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

    if ($Update) {
        $installArguments += "--update"
    }

    Invoke-HorizonConanLocalRecipeExports -RepoRoot $RepoRoot
    Invoke-NativeCommand -FilePath "conan" -Arguments $installArguments
}

function Invoke-ConanCreate {
    param([bool]$Update = $false)

    $profile = Get-ConanProfile
    $installOptions = Get-ConanInstallOptions
    $createArguments = @(
        "create",
        ".",
        "-pr:a", $profile,
        "-pr:b", $profile
    )
    foreach ($option in $installOptions) {
        $createArguments += @("-o", $option)
    }
    $createArguments += "--build=missing"

    if ($Update) {
        $createArguments += "--update"
    }

    Invoke-HorizonConanLocalRecipeExports -RepoRoot $RepoRoot
    Invoke-NativeCommand -FilePath "conan" -Arguments $createArguments
}

function Invoke-CMakeConfigure {
    Import-ConanBuildEnvironment
    Invoke-NativeCommand -FilePath "cmake" -Arguments @("--preset", "conan-default")
}

function Invoke-CMakeBuild {
    $buildDir = Get-ConanBuildDir
    $cacheFile = Join-Path $buildDir "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cacheFile)) {
        throw "CMake cache was not found. Run '.\tools\dev.ps1 configure' or '.\tools\dev.ps1 build' first."
    }
    Assert-CMakeCacheMatchesRepo -CacheFile $cacheFile

    Invoke-NativeCommand -FilePath "cmake" -Arguments @("--build", $buildDir, "--config", $Configuration, "--target", $Target[0])
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
        "package" {
            Invoke-ConanCreate
        }
        "configure" {
            Invoke-ConanInstall
            Invoke-CMakeConfigure
        }
        "build" {
            Invoke-ConanInstall
            Invoke-CMakeConfigure
            Invoke-CMakeBuild
        }
        "build-fast" {
            Import-ConanBuildEnvironment
            Invoke-CMakeBuild
        }
        "rebuild" {
            Invoke-CleanBuildTree
            Invoke-ConanInstall
            Invoke-CMakeConfigure
            Invoke-CMakeBuild
        }
        "update" {
            Invoke-ConanInstall -Update $true
            Invoke-CMakeConfigure
        }
        "clean" {
            Invoke-CleanProject
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
