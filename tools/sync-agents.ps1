<#
    sync-agents.ps1
    ----------------
    Helper for keeping AGENTS.md synchronized with AGENTS.zh-CN.md.

    Usage:
        .\tools\sync-agents.ps1
            Print the prompt to give Codex after editing AGENTS.zh-CN.md.
            This is the script form of the `=sa` project command.

        .\tools\sync-agents.ps1 -Prompt
            Same as the default behavior.

        .\tools\sync-agents.ps1 -Check
            Check whether AGENTS.md contains a sync marker matching the current
            AGENTS.zh-CN.md SHA256 hash.

    This script does not modify AGENTS.md or AGENTS.zh-CN.md.
#>
Param(
    [switch]$Check,
    [switch]$Prompt
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = Resolve-Path "$PSScriptRoot\.."
$chinesePath = Join-Path $repoRoot "AGENTS.zh-CN.md"
$englishPath = Join-Path $repoRoot "AGENTS.md"

if (-not (Test-Path $chinesePath)) {
    throw "Missing AGENTS.zh-CN.md"
}

if (-not (Test-Path $englishPath)) {
    throw "Missing AGENTS.md"
}

$chineseHash = (Get-FileHash $chinesePath -Algorithm SHA256).Hash.ToLowerInvariant()
$englishText = Get-Content -Raw -Path $englishPath
$markerPattern = "<!-- AGENTS_ZH_CN_SHA256: ([a-fA-F0-9]+) -->"
$hasMarker = $englishText -match $markerPattern
$storedHash = if ($hasMarker) { $Matches[1].ToLowerInvariant() } else { "" }

if ($Check) {
    if (-not $hasMarker) {
        Write-Host "AGENTS.md has no sync marker."
        Write-Host "Current AGENTS.zh-CN.md SHA256: $chineseHash"
        exit 1
    }

    if ($storedHash -ne $chineseHash) {
        Write-Host "AGENTS.md is stale."
        Write-Host "Stored hash:  $storedHash"
        Write-Host "Current hash: $chineseHash"
        exit 1
    }

    Write-Host "AGENTS.md is in sync with AGENTS.zh-CN.md."
    exit 0
}

Write-Host @"
Please update AGENTS.md from AGENTS.zh-CN.md.

This is the `=sa` project command: sync agents.

Rules:
- Treat AGENTS.zh-CN.md as the source of truth.
- Keep AGENTS.md in English.
- Preserve the same section structure.
- Make the English concise, direct, and optimized as AI project context.
- Preserve all commands, paths, warnings, and forbidden actions.
- Add or update this marker near the top of AGENTS.md after syncing:
  <!-- AGENTS_ZH_CN_SHA256: $chineseHash -->
- Do not modify AGENTS.zh-CN.md unless explicitly asked.
- Do not modify unrelated files.
"@
