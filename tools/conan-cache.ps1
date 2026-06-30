<#
    conan-cache.ps1
    ---------------
    Local Conan cache maintenance for Horizon dependencies.

    Usage:
        .\tools\conan-cache.ps1 list
        .\tools\conan-cache.ps1 list slang
        .\tools\conan-cache.ps1 list slang -Version 2026.10
        .\tools\conan-cache.ps1 list -Reference slang/2026.10
        .\tools\conan-cache.ps1 update
        .\tools\conan-cache.ps1 update slang
        .\tools\conan-cache.ps1 update slang -Version 2026.10
        .\tools\conan-cache.ps1 list slang -Version 2026.10 -PackageId "*"
        .\tools\conan-cache.ps1 remove slang/2026.10
        .\tools\conan-cache.ps1 remove slang/2026.10 -DryRun
        .\tools\conan-cache.ps1 remove slang -Version 2026.10 -Force
        .\tools\conan-cache.ps1 clear
        .\tools\conan-cache.ps1 clear -DryRun
#>
[CmdletBinding()]
Param(
    [Parameter(Position = 0)]
    [ValidateSet("list", "update", "remove", "clear")]
    [string]$Command = "list",

    [Parameter(Position = 1)]
    [Alias("Name")]
    [string]$Package,

    [Parameter()]
    [string]$Version,

    [Parameter()]
    [string]$User,

    [Parameter()]
    [string]$Channel,

    [Parameter()]
    [string]$Reference,

    [Parameter()]
    [string]$PackageId,

    [Parameter()]
    [string]$PackageQuery,

    [Parameter()]
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",

    [Parameter()]
    [switch]$Force,

    [Parameter()]
    [switch]$DryRun
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

function Get-ConanProfile {
    switch ($Configuration) {
        "Debug" { return (Join-Path $RepoRoot "conan\profiles\windows-msvc-debug") }
        "Release" { return (Join-Path $RepoRoot "conan\profiles\windows-msvc-release") }
        "RelWithDebInfo" { return (Join-Path $RepoRoot "conan\profiles\windows-msvc-relwithdebinfo") }
        "MinSizeRel" { return (Join-Path $RepoRoot "conan\profiles\windows-msvc-minsizerel") }
    }
}

function Invoke-LocalRecipeExports {
    Invoke-HorizonConanLocalRecipeExports -RepoRoot $RepoRoot
}

function Test-ReferenceLikeValue {
    param([Parameter(Mandatory = $true)][string]$Value)

    return ($Value.Contains("/") -or $Value.Contains("@") -or $Value.Contains("*") -or
            $Value.Contains("#") -or $Value.Contains(":"))
}

function Assert-ReferenceInputs {
    if ($Reference -and $Package) {
        throw "Use either -Reference or Package, not both."
    }

    if ($Reference -and ($Version -or $User -or $Channel)) {
        throw "Do not combine -Reference with -Version, -User, or -Channel."
    }

    if (($User -and (-not $Channel)) -or ($Channel -and (-not $User))) {
        throw "-User and -Channel must be provided together."
    }

    if ((-not $Package) -and (-not $Reference) -and ($Version -or $User -or $Channel)) {
        throw "Package is required when using -Version, -User, or -Channel."
    }

    if ($Package -and (Test-ReferenceLikeValue -Value $Package) -and ($Version -or $User -or $Channel)) {
        throw "Do not combine a reference-like Package value with -Version, -User, or -Channel."
    }

    if ($PackageId -and (-not $Package) -and (-not $Reference)) {
        throw "-PackageId requires Package or -Reference."
    }
}

function Get-ConanCachePattern {
    Assert-ReferenceInputs

    if ($Reference) {
        return $Reference
    }

    if (-not $Package) {
        return "*"
    }

    if (Test-ReferenceLikeValue -Value $Package) {
        return $Package
    }

    $versionPart = "*"
    if ($Version) {
        $versionPart = $Version
    }

    $pattern = "$Package/$versionPart"
    if ($User -and $Channel) {
        $pattern = "$pattern@$User/$Channel"
    }

    if ($PackageId) {
        if ($pattern.Contains(":")) {
            throw "Do not pass -PackageId when the reference already contains a package section."
        }
        $pattern = "$pattern`:$PackageId"
    }

    return $pattern
}

function Get-ConanReferenceName {
    param([Parameter(Mandatory = $true)][string]$Value)

    $name = $Value
    $slashIndex = $name.IndexOf("/")
    if ($slashIndex -ge 0) {
        $name = $name.Substring(0, $slashIndex)
    }

    $atIndex = $name.IndexOf("@")
    if ($atIndex -ge 0) {
        $name = $name.Substring(0, $atIndex)
    }

    if ([string]::IsNullOrWhiteSpace($name)) {
        throw "Could not derive package name from reference: $Value"
    }

    return $name
}

function Get-ConanUpdateName {
    Assert-ReferenceInputs

    if ($Reference) {
        return (Get-ConanReferenceName -Value $Reference)
    }

    if ($Package) {
        return (Get-ConanReferenceName -Value $Package)
    }

    return $null
}

function Get-ConanConcreteReference {
    $pattern = Get-ConanCachePattern

    if ($pattern.Contains("*") -or $pattern.Contains("#") -or $pattern.Contains(":")) {
        return $null
    }

    if ($pattern -notmatch "^[^/]+/[^@/]+(@[^/]+/[^/]+)?$") {
        return $null
    }

    return $pattern
}

function Invoke-ConanCacheList {
    $pattern = Get-ConanCachePattern
    Write-Host "[INFO] Listing local Conan cache entries matching: $pattern"
    $arguments = @("list", $pattern, "--cache")
    if ($PackageQuery) {
        $arguments += @("--package-query", $PackageQuery)
    }
    Invoke-NativeCommand -FilePath "conan" -Arguments $arguments
}

function Invoke-ConanInstallUpdate {
    if ($PackageId -or $PackageQuery) {
        throw "-PackageId and -PackageQuery are only supported by list/remove."
    }

    if (($User -or $Channel) -and (-not $Version) -and (-not $Reference)) {
        throw "Update with -User/-Channel requires -Version or a full -Reference."
    }

    $profile = Get-ConanProfile
    $updateName = Get-ConanUpdateName
    $concreteReference = Get-ConanConcreteReference

    $installArguments = @("install")
    if ($concreteReference) {
        Write-Host "[INFO] Updating/installing Conan reference: $concreteReference"
        $installArguments += @("--requires", $concreteReference)
    }
    else {
        Write-Host "[INFO] Updating/installing Horizon dependency graph"
        $installArguments += "."
    }

    $installArguments += @(
        "-pr:a", $profile,
        "-pr:b", $profile,
        "--build=missing"
    )

    if ($updateName) {
        $installArguments += @("--update", $updateName)
    }
    else {
        $installArguments += "--update"
    }

    Invoke-LocalRecipeExports
    Invoke-NativeCommand -FilePath "conan" -Arguments $installArguments
}

function Invoke-ConanCacheRemove {
    if ((-not $Package) -and (-not $Reference)) {
        throw "Refusing to remove without a package name or -Reference."
    }

    $pattern = Get-ConanCachePattern
    Write-Host "[INFO] Matching local Conan cache entries:"
    $listArguments = @("list", $pattern, "--cache")
    if ($PackageQuery) {
        $listArguments += @("--package-query", $PackageQuery)
    }
    Invoke-NativeCommand -FilePath "conan" -Arguments $listArguments

    if ((-not $Force) -and (-not $DryRun)) {
        Write-Host "[WARN] This removes matching recipes/packages from the global Conan cache."
        $answer = Read-Host "Type YES to remove entries matching '$pattern'"
        if ($answer -ne "YES") {
            Write-Host "[SKIP] Remove cancelled."
            return
        }
    }

    $removeArguments = @("remove", $pattern, "--confirm")
    if ($PackageQuery) {
        $removeArguments += @("--package-query", $PackageQuery)
    }
    if ($DryRun) {
        Write-Host "[INFO] Dry run: removing local Conan cache entries matching: $pattern"
        $removeArguments += "--dry-run"
    }
    else {
        Write-Host "[INFO] Removing local Conan cache entries matching: $pattern"
    }
    Invoke-NativeCommand -FilePath "conan" -Arguments $removeArguments
}

function Invoke-ConanCacheClear {
    if ($Package -or $Version -or $User -or $Channel -or $Reference -or $PackageId -or $PackageQuery) {
        throw "clear does not accept package/reference filters. Use remove for filtered cleanup."
    }

    $pattern = "*"
    $removeArguments = @("remove", $pattern, "--confirm")
    if ($DryRun) {
        Write-Host "[INFO] Dry run: removing all local Conan cache entries"
        $removeArguments += "--dry-run"
    }
    else {
        Write-Host "[WARN] Removing all recipes/packages from the global Conan cache."
    }
    Invoke-NativeCommand -FilePath "conan" -Arguments $removeArguments
}

Push-Location -LiteralPath $RepoRoot
try {
    switch ($Command) {
        "list" {
            Invoke-ConanCacheList
        }
        "update" {
            Invoke-ConanInstallUpdate
        }
        "remove" {
            Invoke-ConanCacheRemove
        }
        "clear" {
            Invoke-ConanCacheClear
        }
    }
}
finally {
    Pop-Location
}
