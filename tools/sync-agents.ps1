<#
    sync-agents.ps1
    ----------------
    Helper for checking and prompting synchronization between
    human-maintained Chinese agent files and English AI-facing files.

    Usage:
        .\tools\sync-agents.ps1
        .\tools\sync-agents.ps1 -Prompt
            Print the prompt to give an AI agent after editing Chinese sources.
            Running the script without switches prints the same prompt.

        .\tools\sync-agents.ps1 -Check
            Check whether English files contain sync markers matching the
            current Chinese source SHA256 hashes.

    This script does not modify files.
#>
Param(
    [switch]$Check,
    [switch]$Prompt
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path "$PSScriptRoot\..").Path

$syncPairs = @(
    @{
        Source = "AGENTS.zh-CN.md"
        Target = "AGENTS.md"
        Marker = "AGENTS_ZH_CN_SHA256"
        Label = "root AGENTS"
    },
    @{
        Source = "docs/agents/zh-CN/index.md"
        Target = "docs/agents/index.md"
        Marker = "AGENT_DOCS_INDEX_ZH_CN_SHA256"
        Label = "agent docs index"
    },
    @{
        Source = "docs/agents/zh-CN/build.md"
        Target = "docs/agents/build.md"
        Marker = "AGENT_DOCS_BUILD_ZH_CN_SHA256"
        Label = "build context"
    },
    @{
        Source = "docs/agents/zh-CN/codegraph.md"
        Target = "docs/agents/codegraph.md"
        Marker = "AGENT_DOCS_CODEGRAPH_ZH_CN_SHA256"
        Label = "codegraph context"
    },
    @{
        Source = "docs/agents/zh-CN/git.md"
        Target = "docs/agents/git.md"
        Marker = "AGENT_DOCS_GIT_ZH_CN_SHA256"
        Label = "git context"
    },
    @{
        Source = "docs/agents/zh-CN/formatting.md"
        Target = "docs/agents/formatting.md"
        Marker = "AGENT_DOCS_FORMATTING_ZH_CN_SHA256"
        Label = "formatting context"
    },
    @{
        Source = "docs/agents/zh-CN/vulkan.md"
        Target = "docs/agents/vulkan.md"
        Marker = "AGENT_DOCS_VULKAN_ZH_CN_SHA256"
        Label = "vulkan context"
    },
    @{
        Source = "docs/agents/zh-CN/helicon.md"
        Target = "docs/agents/helicon.md"
        Marker = "AGENT_DOCS_HELICON_ZH_CN_SHA256"
        Label = "helicon context"
    },
    @{
        Source = "docs/agents/zh-CN/push-constants.md"
        Target = "docs/agents/push-constants.md"
        Marker = "AGENT_DOCS_PUSH_CONSTANTS_ZH_CN_SHA256"
        Label = "push constant context"
    },
    @{
        Source = "docs/tasks/zh-CN/formatting.md"
        Target = "docs/tasks/formatting.md"
        Marker = "TASK_DOCS_FORMATTING_ZH_CN_SHA256"
        Label = "formatting task notes"
    },
    @{
        Source = "docs/tasks/zh-CN/hardware-buffer-validation.md"
        Target = "docs/tasks/hardware-buffer-validation.md"
        Marker = "TASK_DOCS_HARDWARE_BUFFER_VALIDATION_ZH_CN_SHA256"
        Label = "hardware buffer validation task notes"
    },
    @{
        Source = "docs/tasks/zh-CN/optional-build-targets.md"
        Target = "docs/tasks/optional-build-targets.md"
        Marker = "TASK_DOCS_OPTIONAL_BUILD_TARGETS_ZH_CN_SHA256"
        Label = "optional build targets task notes"
    },
    @{
        Source = "docs/tasks/zh-CN/shader-reflection.md"
        Target = "docs/tasks/shader-reflection.md"
        Marker = "TASK_DOCS_SHADER_REFLECTION_ZH_CN_SHA256"
        Label = "shader reflection task notes"
    },
    @{
        Source = "docs/tasks/zh-CN/vulkan-backend.md"
        Target = "docs/tasks/vulkan-backend.md"
        Marker = "TASK_DOCS_VULKAN_BACKEND_ZH_CN_SHA256"
        Label = "vulkan backend task notes"
    },
    @{
        Source = "docs/tasks/zh-CN/examples-new-api-visible-window.md"
        Target = "docs/tasks/examples-new-api-visible-window.md"
        Marker = "TASK_DOCS_EXAMPLES_NEW_API_VISIBLE_WINDOW_ZH_CN_SHA256"
        Label = "examples new API visible window task notes"
    },
    @{
        Source = "docs/tasks/zh-CN/runtime-diagnostics-reporting.md"
        Target = "docs/tasks/runtime-diagnostics-reporting.md"
        Marker = "TASK_DOCS_RUNTIME_DIAGNOSTICS_REPORTING_ZH_CN_SHA256"
        Label = "runtime diagnostics reporting task notes"
    },
    @{
        Source = "docs/tasks/zh-CN/deadlock-freedom.md"
        Target = "docs/tasks/deadlock-freedom.md"
        Marker = "TASK_DOCS_DEADLOCK_FREEDOM_ZH_CN_SHA256"
        Label = "deadlock freedom task notes"
    },
    @{
        Source = ".agents/skills/horizon-workflow/SKILL.zh-CN.md"
        Target = ".agents/skills/horizon-workflow/SKILL.md"
        Marker = "HORIZON_WORKFLOW_SKILL_ZH_CN_SHA256"
        Label = "horizon workflow skill"
    },
    @{
        Source = ".agents/skills/agent-project-system/SKILL.zh-CN.md"
        Target = ".agents/skills/agent-project-system/SKILL.md"
        Marker = "AGENT_PROJECT_SYSTEM_SKILL_ZH_CN_SHA256"
        Label = "agent project system skill"
    }
)

function Get-AbsoluteRepoPath {
    param([string]$RelativePath)
    return Join-Path $repoRoot $RelativePath
}

function Get-SyncState {
    param([hashtable]$Pair)

    $sourcePath = Get-AbsoluteRepoPath $Pair.Source
    $targetPath = Get-AbsoluteRepoPath $Pair.Target

    if (-not (Test-Path -LiteralPath $sourcePath)) {
        throw "Missing source file: $($Pair.Source)"
    }

    if (-not (Test-Path -LiteralPath $targetPath)) {
        throw "Missing target file: $($Pair.Target)"
    }

    $sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $targetText = Get-Content -Raw -Encoding UTF8 -LiteralPath $targetPath
    $markerPattern = "<!-- $($Pair.Marker): ([a-fA-F0-9]+) -->"
    $hasMarker = $targetText -match $markerPattern
    $storedHash = if ($hasMarker) { $Matches[1].ToLowerInvariant() } else { "" }

    return [pscustomobject]@{
        Label = $Pair.Label
        Source = $Pair.Source
        Target = $Pair.Target
        Marker = $Pair.Marker
        SourceHash = $sourceHash
        StoredHash = $storedHash
        HasMarker = $hasMarker
        InSync = ($hasMarker -and $storedHash -eq $sourceHash)
    }
}

$states = foreach ($pair in $syncPairs) {
    Get-SyncState -Pair $pair
}

if ($Check) {
    $failed = @($states | Where-Object { -not $_.InSync })

    if ($failed.Count -gt 0) {
        foreach ($state in $failed) {
            if (-not $state.HasMarker) {
                Write-Host "$($state.Target) has no sync marker for $($state.Source)."
            }
            else {
                Write-Host "$($state.Target) is stale for $($state.Source)."
                Write-Host "Stored hash:  $($state.StoredHash)"
                Write-Host "Current hash: $($state.SourceHash)"
            }
        }
        exit 1
    }

    Write-Host "All agent English files are in sync with Chinese sources."
    exit 0
}

$markerLines = $states | ForEach-Object {
    "- $($_.Target): <!-- $($_.Marker): $($_.SourceHash) -->"
}

$pairLines = $states | ForEach-Object {
    "- $($_.Source) -> $($_.Target)"
}

$promptLines = @(
    'Please synchronize the English AI-facing files from their Chinese source files.',
    '',
    'This is the `=sa` project command: sync all agent context files.',
    '',
    'Rules:',
    '- Treat Chinese files as the source of truth.',
    '- Keep English files concise, direct, and optimized for AI context.',
    '- Preserve command names, paths, warnings, validation rules, and forbidden actions.',
    '- Preserve each file''s purpose and section structure where practical.',
    '- Add or update these markers in the matching English files:',
    ($markerLines -join [Environment]::NewLine),
    '- Do not modify Chinese source files unless explicitly asked.',
    '- Do not modify unrelated files.',
    '- Do not create `.agents/skills/*/zh-CN/SKILL.md`; use `SKILL.zh-CN.md` for Chinese skill sources to avoid duplicate skill discovery.',
    '',
    'Pairs:',
    ($pairLines -join [Environment]::NewLine),
    '',
    'After editing, run:',
    '.\tools\sync-agents.ps1 -Check'
)

Write-Output ($promptLines -join [Environment]::NewLine)
