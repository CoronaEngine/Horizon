[CmdletBinding()]
param(
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..\..\..')
)
$executableDirectory = Join-Path $repositoryRoot 'cmake-build-debug\examples'
$runtimeDirectory = Join-Path $repositoryRoot 'cmake-build-debug\bin'
$executablePath = Join-Path $executableDirectory 'HorizonExamples.exe'
$shaderDirectory = Join-Path $executableDirectory 'shaders'

if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    throw "HorizonExamples.exe is missing. Build the CLion Debug target first."
}

if (-not (Test-Path -LiteralPath $shaderDirectory -PathType Container)) {
    throw "The baseline shader directory is missing. Build the CLion Debug target first."
}

if ($ValidateOnly) {
    Write-Output "Horizon baseline launch inputs are valid."
    exit 0
}

$originalPath = $env:PATH
$childPathEntries = @($executableDirectory)
if (Test-Path -LiteralPath $runtimeDirectory -PathType Container) {
    $childPathEntries += $runtimeDirectory
}
$childPathEntries += $originalPath

try {
    $env:PATH = $childPathEntries -join [System.IO.Path]::PathSeparator
    $process = Start-Process `
        -FilePath $executablePath `
        -ArgumentList 'baseline' `
        -WorkingDirectory $executableDirectory `
        -PassThru
}
finally {
    $env:PATH = $originalPath
}

Write-Output "Started HorizonExamples in baseline mode (PID $($process.Id))."
