<#
    code-format.ps1
    ----------------
    Horizon clang-format workflow.

    Default scope:
        Changed C/C++ files under first-party source roots.

    Usage:
        .\tools\code-format.ps1 -Check
        .\tools\code-format.ps1
        .\tools\code-format.ps1 -Check src/hardware_wrapper_vulkan/hardware_context.h
        .\tools\code-format.ps1 src/hardware_wrapper_vulkan
        .\tools\code-format.ps1 -All
#>
[CmdletBinding()]
Param(
    [switch]$Check,
    [switch]$All,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Targets = @()
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$CppExtensions = @(".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx")
$DefaultRoots = @(
    "include",
    "src/hardware_wrapper_vulkan",
    "src/Helicon",
    "examples",
    "tests",
    "tools"
)

function Get-RelativeRepoPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $rootPath = [System.IO.Path]::GetFullPath($RepoRoot)
    if (-not $rootPath.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $rootPath += [System.IO.Path]::DirectorySeparatorChar
    }

    if (-not $fullPath.StartsWith($rootPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Formatting path is outside the repository: $Path"
    }

    return $fullPath.Substring($rootPath.Length).Replace("\", "/")
}

function Test-UnderRelativeRoot {
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string[]]$Roots
    )

    $normalizedPath = $RelativePath.Replace("\", "/").TrimStart("/")
    foreach ($root in $Roots) {
        $normalizedRoot = $root.Replace("\", "/").Trim("/")
        if ($normalizedPath.Equals($normalizedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
        if ($normalizedPath.StartsWith($normalizedRoot + "/", [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }

    return $false
}

function Test-CppPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $extension = [System.IO.Path]::GetExtension($Path).ToLowerInvariant()
    return $CppExtensions -contains $extension
}

function Get-DefaultRootItems {
    $items = @()
    foreach ($root in $DefaultRoots) {
        $fullPath = Join-Path $RepoRoot $root
        if (Test-Path -LiteralPath $fullPath) {
            $items += Get-Item -LiteralPath $fullPath
        }
    }

    return @($items)
}

function Get-ChangedFormatFiles {
    $paths = @()
    $paths += (& git -C $RepoRoot diff --name-only --diff-filter=ACMRTUXB)
    $paths += (& git -C $RepoRoot diff --cached --name-only --diff-filter=ACMRTUXB)
    $paths += (& git -C $RepoRoot ls-files --others --exclude-standard)

    $files = @()
    foreach ($relativePath in $paths | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique) {
        $fullPath = Join-Path $RepoRoot $relativePath
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) { continue }
        if (-not (Test-CppPath -Path $fullPath)) { continue }
        if (-not (Test-UnderRelativeRoot -RelativePath $relativePath -Roots $DefaultRoots)) { continue }
        $files += Get-Item -LiteralPath $fullPath
    }

    return @($files | Sort-Object -Property FullName -Unique)
}

function Get-FilesFromItems {
    param([Parameter(Mandatory = $true)][object[]]$Items)

    $files = @()
    foreach ($item in $Items) {
        if ($item.PSIsContainer) {
            $files += Get-ChildItem -LiteralPath $item.FullName -Recurse -File | Where-Object {
                Test-CppPath -Path $_.FullName
            }
        }
        elseif (Test-CppPath -Path $item.FullName) {
            $files += $item
        }
    }

    return @($files | Sort-Object -Property FullName -Unique)
}

function Get-ExplicitItems {
    param([Parameter(Mandatory = $true)][string[]]$Inputs)

    $items = @()
    foreach ($inputPath in $Inputs) {
        $fullPath = if ([System.IO.Path]::IsPathRooted($inputPath)) {
            $inputPath
        }
        else {
            Join-Path $RepoRoot $inputPath
        }

        if (-not (Test-Path -LiteralPath $fullPath)) {
            throw "Formatting path does not exist: $inputPath"
        }

        $item = Get-Item -LiteralPath $fullPath
        $null = Get-RelativeRepoPath -Path $item.FullName
        $items += $item
    }

    return @($items)
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git not found in PATH. git is required to discover changed files."
}

if (-not (Get-Command clang-format -ErrorAction SilentlyContinue)) {
    throw "clang-format not found in PATH. Install LLVM/clang-format and try again."
}

$targetValues = @($Targets)
$files = @(
    if ($targetValues.Count -gt 0) {
        Get-FilesFromItems -Items @(Get-ExplicitItems -Inputs $targetValues)
    }
    elseif ($All) {
        Get-FilesFromItems -Items @(Get-DefaultRootItems)
    }
    else {
        Get-ChangedFormatFiles
    }
)

if (-not $files -or $files.Count -eq 0) {
    Write-Host "No C/C++ files matched the formatting scope."
    exit 0
}

$mode = if ($Check) { "Checking" } else { "Formatting" }
Write-Host "$mode $($files.Count) C/C++ file(s)."

$arguments = @("--style=file")
if ($Check) {
    $arguments += @("--dry-run", "--Werror")
}
else {
    $arguments += "-i"
}
$arguments += @($files | ForEach-Object { $_.FullName })

& clang-format @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
