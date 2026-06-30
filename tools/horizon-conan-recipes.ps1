$HorizonLocalConanRecipes = @(
    "conan\recipes\ktm",
    "conan\recipes\pfr",
    "conan\recipes\slang",
    "conan\recipes\vulkan-memory-allocator"
)

function Test-HorizonConanLocalRecipeExportEnabled {
    $value = $env:HORIZON_CONAN_EXPORT_LOCAL_RECIPES
    if ([string]::IsNullOrWhiteSpace($value)) {
        return $true
    }

    return $value -notin @("0", "false", "False", "FALSE", "off", "Off", "OFF", "no", "No", "NO")
}

function Invoke-HorizonConanLocalRecipeExports {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot
    )

    if (-not (Test-HorizonConanLocalRecipeExportEnabled)) {
        Write-Host "[INFO] Skipping local Conan recipe exports because HORIZON_CONAN_EXPORT_LOCAL_RECIPES is disabled."
        return
    }

    foreach ($recipePath in $HorizonLocalConanRecipes) {
        $absoluteRecipePath = Join-Path $RepoRoot $recipePath
        if (-not (Test-Path -LiteralPath $absoluteRecipePath)) {
            throw "Local Conan recipe was not found: $absoluteRecipePath"
        }

        & conan export $absoluteRecipePath
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
}
