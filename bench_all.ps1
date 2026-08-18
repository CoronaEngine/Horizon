param(
    [string]$Suffix = "",
    [int]$Frames = 500,
    [int]$Warmup = 100,
    [string]$OutFile = ""
)

$examples = @(
    "baseline", "edsl", "glsl", "ibl", "edsl_ibl", "drawstress", "raymarch",
    "bump", "deferred", "sponza", "shadowmaps", "edsl_shadowmaps",
    "shadowvolumes", "edsl_shadowvolumes", "assao", "sky", "ssr", "edsl_ssr",
    "edsl_sky", "gpudrivenrendering", "disney_pbr", "edsl_disney_pbr",
    "rsm", "edsl_rsm", "edsl_sponza"
)

$exe = "e:\Horizon\build\conan\examples\release\examples\Release\HorizonExamples.exe"
$env:HORIZON_BENCH_FRAMES = "$Frames"
$env:HORIZON_BENCH_WARMUP = "$Warmup"
$env:HORIZON_PRESENT_MODE = "immediate"

$results = @()
foreach ($ex in $examples) {
    $env:HORIZON_BENCH_LABEL = "$ex$Suffix"
    $out = & $exe $ex 2>&1 | Select-String "\[bench\]" | Select-Object -First 1
    if ($out) {
        Write-Output $out.Line
        $results += $out.Line
    } else {
        Write-Output "[bench] $ex$Suffix FAILED-OR-NO-OUTPUT"
        $results += "[bench] $ex$Suffix FAILED-OR-NO-OUTPUT"
    }
}

if ($OutFile -ne "") {
    $results | Set-Content -Encoding utf8 $OutFile
}
