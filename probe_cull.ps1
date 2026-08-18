param(
    [string[]]$Examples = @("ibl", "edsl_ibl", "sponza", "edsl_sponza", "assao"),
    [int]$Frames = 400,
    [int]$Warmup = 120
)

$exe = "e:\Horizon\build\conan\examples\release\examples\Release\HorizonExamples.exe"
$env:HORIZON_PRESENT_MODE = "immediate"
New-Item -ItemType Directory -Force e:\Horizon\dumps | Out-Null
Add-Type -Path e:\Horizon\DumpCompare.cs

foreach ($ex in $Examples) {
    $env:HORIZON_BENCH_LABEL = $ex
    Write-Output "=== $ex"

    # 1) 每种剔除模式抓一帧原始像素（带 vkDeviceWaitIdle，只用于正确性）
    $env:HORIZON_BENCH_FRAMES = "120"
    $env:HORIZON_BENCH_WARMUP = "10"
    $env:HORIZON_FRAME_HASH = "100"
    $env:HORIZON_FIXED_DT = "0.006944"
    foreach ($m in @("none", "front", "back")) {
        $env:HORIZON_EX_CULL = $m
        $env:HORIZON_FRAME_HASH_DUMP = "e:\Horizon\dumps\${ex}_$m.bin"
        $h = (& $exe $ex 2>&1 | Select-String "framehash" | Select-Object -Last 1).Line
        if ($h -match "hash=([0-9a-f]+)") { Write-Output "  hash $m = $($Matches[1])" }
    }
    Remove-Item Env:\HORIZON_FRAME_HASH_DUMP -ErrorAction SilentlyContinue
    Remove-Item Env:\HORIZON_FRAME_HASH -ErrorAction SilentlyContinue
    Remove-Item Env:\HORIZON_FIXED_DT -ErrorAction SilentlyContinue

    # 2) 逐像素比对
    foreach ($m in @("front", "back")) {
        $a = "e:\Horizon\dumps\${ex}_none.bin"
        $b = "e:\Horizon\dumps\${ex}_$m.bin"
        if ((Test-Path $a) -and (Test-Path $b)) {
            Write-Output "  diff none-vs-$m : $([DumpCompare]::Run($a, $b))"
        }
    }

    # 3) 交替测性能（无 hash 干扰），取 gate 中位数
    $env:HORIZON_BENCH_FRAMES = "$Frames"
    $env:HORIZON_BENCH_WARMUP = "$Warmup"
    $samples = @{ "none" = @(); "front" = @(); "back" = @() }
    1..3 | ForEach-Object {
        foreach ($m in @("none", "front", "back")) {
            $env:HORIZON_EX_CULL = $m
            $line = (& $exe $ex 2>&1 | Select-String "\[bench\]" | Select-Object -First 1).Line
            if ($line -match "gate=([\d.]+)ms") { $samples[$m] += [double]$Matches[1] }
            Start-Sleep -Milliseconds 1200
        }
    }
    foreach ($m in @("none", "front", "back")) {
        $v = $samples[$m] | Sort-Object
        if ($v.Count -gt 0) {
            $med = $v[[int]([math]::Floor($v.Count / 2))]
            Write-Output ("  gate $m : min={0:F3} med={1:F3} fps_med={2:F1}  [{3}]" -f $v[0], $med, (1000.0 / $med), ($samples[$m] -join ', '))
        }
    }
    Remove-Item Env:\HORIZON_EX_CULL -ErrorAction SilentlyContinue
}
