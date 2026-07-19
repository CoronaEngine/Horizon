[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repositoryRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..\..\..')
)
$buildDirectoryName = 'cmake-build-debug'
$buildDirectory = Join-Path $repositoryRoot $buildDirectoryName
$cachePath = Join-Path $buildDirectory 'CMakeCache.txt'

if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) {
    throw "The CLion Debug cache is missing. Load the CLion Debug profile before building."
}

function Get-CMakeCacheValue {
    param(
        [Parameter(Mandatory)]
        [string]$Key
    )

    $escapedKey = [Regex]::Escape($Key)
    $entry = Select-String -LiteralPath $cachePath -Pattern "^${escapedKey}:[^=]+=(.*)$" |
        Select-Object -First 1

    if ($null -eq $entry) {
        throw "The CLion Debug cache does not contain $Key. Reload the profile in CLion."
    }

    return $entry.Matches[0].Groups[1].Value
}

$cmakeCommand = Get-CMakeCacheValue -Key 'CMAKE_COMMAND'
$cxxCompiler = Get-CMakeCacheValue -Key 'CMAKE_CXX_COMPILER'
$cachedBuildDirectory = Get-CMakeCacheValue -Key 'CMAKE_CACHEFILE_DIR'
$buildType = Get-CMakeCacheValue -Key 'CMAKE_BUILD_TYPE'
$generator = Get-CMakeCacheValue -Key 'CMAKE_GENERATOR'

if (-not [System.IO.Path]::GetFullPath($cachedBuildDirectory).Equals(
        $buildDirectory,
        [StringComparison]::OrdinalIgnoreCase
    )) {
    throw "The cache belongs to a different build directory. Reload the CLion Debug profile."
}

if ($buildType -ne 'Debug' -or $generator -ne 'Ninja') {
    throw "The build tree is not the CLion Debug Ninja configuration."
}

if (-not (Test-Path -LiteralPath $cmakeCommand -PathType Leaf)) {
    throw "The CMake executable recorded by CLion is unavailable."
}

$visualStudioMatch = [Regex]::Match(
    $cxxCompiler,
    '^(.*?)[\\/]VC[\\/]Tools[\\/]MSVC[\\/]',
    [Text.RegularExpressions.RegexOptions]::IgnoreCase
)

if ($visualStudioMatch.Success) {
    $visualStudioRoot = $visualStudioMatch.Groups[1].Value
    $devShellModule = Join-Path $visualStudioRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'

    if (-not (Test-Path -LiteralPath $devShellModule -PathType Leaf)) {
        throw "The Visual Studio developer shell selected by CLion is unavailable."
    }

    Import-Module $devShellModule
    Enter-VsDevShell `
        -VsInstallPath $visualStudioRoot `
        -SkipAutomaticLocation `
        -DevCmdArguments '-arch=x64 -host_arch=x64'
}

Push-Location $repositoryRoot
try {
    & $cmakeCommand --build $buildDirectoryName --target HorizonExamples -j 30
    $buildExitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}

exit $buildExitCode
