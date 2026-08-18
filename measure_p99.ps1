param(
    [string[]]$Examples = @("sponza", "edsl_sponza", "rsm", "edsl_rsm", "drawstress"),
    [int]$Frames = 600,  # 10秒 @ 60fps，实际会更多
    [int]$Warmup = 120
)

$exe = "e:\Horizon\build\conan\examples\release\examples\Release\HorizonExamples.exe"
$env:HORIZON_PRESENT_MODE = "immediate"
$env:HORIZON_BENCH_FRAMES = "$Frames"
$env:HORIZON_BENCH_WARMUP = "$Warmup"

foreach ($ex in $Examples) {
    $env:HORIZON_BENCH_LABEL = $ex
    Write-Output "=== $ex"

    # 收集完整的帧时间分布
    $output = & $exe $ex 2>&1 | Out-String

    # 提取所有 gate 时间（从详细日志或修改 bench 输出）
    # 先看看 bench 输出了什么
    $benchLine = $output | Select-String "\[bench\]" | Select-Object -First 1
    if ($benchLine) {
        Write-Output "  $($benchLine.Line)"
    }

    # 如果有 gpu_total，也输出
    $gpuLine = $output | Select-String "gpu_total" | Select-Object -First 1
    if ($gpuLine) {
        Write-Output "  $($gpuLine.Line)"
    }

    Start-Sleep -Milliseconds 500
}

Remove-Item Env:\HORIZON_BENCH_LABEL, Env:\HORIZON_BENCH_FRAMES, Env:\HORIZON_BENCH_WARMUP -ErrorAction SilentlyContinue
