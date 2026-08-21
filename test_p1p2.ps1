# 测试 P1+P2 优化效果

param(
    [string]$Example = "all"
)

$env:HORIZON_PRESENT_MODE = "immediate"
$env:HORIZON_BENCH_FRAMES = "1200"
$env:HORIZON_WARMUP_FRAMES = "120"

$examples = @(
    "example_ibl",
    "example_edsl_ibl",
    "example_rsm",
    "example_edsl_rsm",
    "example_sponza",
    "example_edsl_sponza",
    "example_assao"
)

$buildDir = "build\conan\examples\relwithdebinfo\examples"

function Test-Example {
    param([string]$name)

    $exePath = Join-Path $buildDir "$name.exe"
    if (-not (Test-Path $exePath)) {
        Write-Host "❌ $name not found" -ForegroundColor Red
        return
    }

    Write-Host "`n=== Testing $name ===" -ForegroundColor Cyan

    $output = & $exePath 2>&1 | Out-String

    # 提取 p99
    if ($output -match "p99:\s*(\d+\.\d+)\s*ms") {
        $p99 = [double]$matches[1]
        $target = 6.944
        $status = if ($p99 -lt $target) { "✅" } else { "❌" }
        $color = if ($p99 -lt $target) { "Green" } else { "Yellow" }

        Write-Host "$status $name : p99 = $p99 ms (target < $target ms)" -ForegroundColor $color

        return [PSCustomObject]@{
            Example = $name
            P99_ms = $p99
            Target_ms = $target
            Pass = ($p99 -lt $target)
        }
    } else {
        Write-Host "⚠️  $name : 无法提取 p99" -ForegroundColor Yellow
        return $null
    }
}

Write-Host "开始测试 P1+P2 优化效果..." -ForegroundColor Green
Write-Host "配置: immediate present, 1200 frames, 120 warmup`n"

$results = @()

if ($Example -eq "all") {
    foreach ($ex in $examples) {
        $result = Test-Example $ex
        if ($result) {
            $results += $result
        }
        Start-Sleep -Seconds 1
    }
} else {
    $result = Test-Example $Example
    if ($result) {
        $results += $result
    }
}

Write-Host "`n=== 汇总 ===" -ForegroundColor Cyan
$results | Format-Table -AutoSize

$passed = ($results | Where-Object { $_.Pass }).Count
$total = $results.Count
Write-Host "`n达标: $passed / $total" -ForegroundColor $(if ($passed -eq $total) { "Green" } else { "Yellow" })
