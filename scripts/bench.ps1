# NLOJ problem-read bench + Redis fault injection.
# Run from repo root: powershell -File scripts/bench.ps1
# Requires nloj_api listening on 8080 and at least one problem row.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$bench = Join-Path $root "build\bin\Debug\nloj_api_bench.exe"
$report = Join-Path $root "docs\bench-report.generated.md"
$base = "http://127.0.0.1:8080"

if (-not (Test-Path $bench)) {
    Write-Error "missing nloj_api_bench.exe, build target nloj_api_bench first"
}

function Invoke-Health {
    return Invoke-RestMethod -Uri ($base + "/api/v1/health") -TimeoutSec 5
}

function Invoke-Bench {
    param(
        [string]$Path,
        [int]$Threads,
        [int]$Seconds,
        [switch]$SkipCache,
        [int]$RotateFrom = 0,
        [int]$RotateSpan = 0
    )
    $benchArgs = @(
        "--host", "127.0.0.1",
        "--port", "8080",
        "--path", $Path,
        "--threads", "$Threads",
        "--seconds", "$Seconds"
    )
    if ($SkipCache) {
        $benchArgs += "--skip-cache"
    }
    if ($RotateSpan -gt 0) {
        $benchArgs += @("--rotate-from", "$RotateFrom", "--rotate-span", "$RotateSpan")
    }
    $line = & $bench @benchArgs
    Write-Host $line
    if ($line -notmatch "ok=(\d+) err=(\d+) qps=([\d.]+) p50_ms=([\d.]+) p95_ms=([\d.]+) p99_ms=([\d.]+)") {
        throw "bad bench output: $line"
    }
    return [pscustomobject]@{
        raw    = "$line"
        ok     = [int64]$Matches[1]
        err    = [int64]$Matches[2]
        qps    = [double]$Matches[3]
        p50_ms = [double]$Matches[4]
        p95_ms = [double]$Matches[5]
        p99_ms = [double]$Matches[6]
    }
}

function Format-Row {
    param($Name, $R, $Note)
    $q = $R.qps.ToString("0.0")
    $a = $R.p50_ms.ToString("0.000")
    $b = $R.p95_ms.ToString("0.000")
    $c = $R.p99_ms.ToString("0.000")
    return "| $Name | $q | $a | $b | $c | $($R.ok) | $($R.err) | $Note |"
}

Write-Host "health..."
$health0 = Invoke-Health
if ($health0.data.status -ne "UP") {
    throw "nloj_api or MySQL is down"
}

$list = Invoke-RestMethod -Uri ($base + "/api/v1/problems?pageNum=1&pageSize=1") -TimeoutSec 5
$problemId = 0
if ($list.data.records -and $list.data.records.Count -gt 0) {
    $problemId = [int64]$list.data.records[0].id
}
if ($problemId -le 0) {
    throw "no problem rows; create one first"
}

$detailPath = "/api/v1/problems/" + $problemId
Write-Host ("warmup " + $detailPath)
for ($i = 0; $i -lt 20; $i++) {
    Invoke-RestMethod -Uri ($base + $detailPath) -TimeoutSec 5 | Out-Null
}

$cacheBeforeHot = (Invoke-Health).data.cache
Write-Host "scene 1: cached detail"
$hot = Invoke-Bench -Path $detailPath -Threads 16 -Seconds 8
$cacheAfterHot = (Invoke-Health).data.cache

Write-Host "scene 2: skip cache"
$nocache = Invoke-Bench -Path $detailPath -Threads 8 -Seconds 5 -SkipCache

Write-Host "scene 3: problem list"
$listR = Invoke-Bench -Path "/api/v1/problems?pageNum=1&pageSize=20" -Threads 8 -Seconds 5

$missFrom = 800000001
Write-Host "scene 4a: missing ids first wave"
$miss1 = Invoke-Bench -Path "/api/v1/problems" -Threads 8 -Seconds 4 -RotateFrom $missFrom -RotateSpan 32
Write-Host "scene 4b: missing ids again"
$miss2 = Invoke-Bench -Path "/api/v1/problems" -Threads 8 -Seconds 4 -RotateFrom $missFrom -RotateSpan 32

$cacheAfterMiss = (Invoke-Health).data.cache

$redisHot = $null
$redisMiss = $null
$redisNote = "skipped"
try {
    $svc = Get-Service -Name "Redis" -ErrorAction Stop
    if ($svc.Status -eq "Running") {
        Write-Host "scene 5: stop Redis"
        Stop-Service -Name "Redis" -Force
        Start-Sleep -Seconds 1
        $redisHot = Invoke-Bench -Path $detailPath -Threads 8 -Seconds 3
        $redisMiss = Invoke-Bench -Path "/api/v1/problems" -Threads 4 -Seconds 3 -RotateFrom 810000001 -RotateSpan 8
        Start-Service -Name "Redis"
        Start-Sleep -Seconds 1
        $redisNote = "Stop-Service Redis then Start-Service. health.redis may stay UP (probe once at process start)."
    }
} catch {
    $redisNote = "skipped: " + $_
}

$when = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
$hitHot = 0.0
if ($cacheAfterHot.total -gt $cacheBeforeHot.total) {
    $dTotal = [double]($cacheAfterHot.total - $cacheBeforeHot.total)
    $dHit = [double](($cacheAfterHot.l1Hit + $cacheAfterHot.redisHit) - ($cacheBeforeHot.l1Hit + $cacheBeforeHot.redisHit))
    $hitHot = $dHit / $dTotal
}

$nl = [Environment]::NewLine
$rows = @(
    (Format-Row "detail cached" $hot "16 threads x 8s after warmup"),
    (Format-Row "detail skip-cache" $nocache "8 threads x 5s header X-NLOJ-Skip-Cache: 1"),
    (Format-Row "problem list" $listR "8 threads x 5s no Redis"),
    (Format-Row "miss first wave" $miss1 "8 threads x 4s, 32 missing ids"),
    (Format-Row "miss second wave" $miss2 "same ids, expect __nil__ cache")
)
if ($null -ne $redisHot) {
    $rows += (Format-Row "Redis down hot id" $redisHot "L1 may still hit")
    $rows += (Format-Row "Redis down new miss" $redisMiss "MySQL fallback, cache write fails")
}

$dCalls = ([int64]$cacheAfterHot.total) - ([int64]$cacheBeforeHot.total)
$dHits = ([int64]($cacheAfterHot.l1Hit + $cacheAfterHot.redisHit)) - ([int64]($cacheBeforeHot.l1Hit + $cacheBeforeHot.redisHit))
$hitPct = $hitHot.ToString("0.000")

$md = @(
    "# NLOJ load test and fault injection",
    "",
    "Measured on this machine at $when. Client: ``nloj_api_bench`` (``scripts/bench.ps1``).",
    "Target: ``nloj_api`` $base. ``GET /api/v1/problems/{id}`` uses L1 + Redis; list is uncached.",
    "",
    "| item | value |",
    "| --- | --- |",
    "| OS | Windows |",
    "| problem id | $problemId |",
    "| health.mysql | $($health0.data.mysql) |",
    "| health.redis before | $($health0.data.redis) |",
    "| health.rabbitmq | $($health0.data.rabbitmq) |",
    "| cache after warmup | total=$($cacheBeforeHot.total) l1Hit=$($cacheBeforeHot.l1Hit) redisHit=$($cacheBeforeHot.redisHit) mysqlLoad=$($cacheBeforeHot.mysqlLoad) hitRate=$($cacheBeforeHot.hitRate) |",
    "",
    "## comparison",
    "",
    "| scene | QPS | P50 ms | P95 ms | P99 ms | ok | err | note |",
    "| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |"
) + $rows + @(
    "",
    "## cache hit rate (hot id scene)",
    "",
    "Delta of ``GET /health`` ``data.cache`` around the cached-detail run:",
    "",
    "- calls: $dCalls",
    "- hits (L1+Redis): $dHits",
    "- **hit rate: $hitPct**",
    "- after miss scenes: total=$($cacheAfterMiss.total) l1Hit=$($cacheAfterMiss.l1Hit) redisHit=$($cacheAfterMiss.redisHit) mysqlLoad=$($cacheAfterMiss.mysqlLoad) hitRate=$($cacheAfterMiss.hitRate)",
    "",
    "## takeaways",
    "",
    "- Same problem id: cached QPS should beat skip-cache; P99 should be lower. List has no cache, closer to skip-cache detail.",
    "- Missing ids: first wave hits MySQL; second wave should use short-TTL ``__nil__`` (anti-penetration), QPS up, ``mysqlLoad`` not growing 1:1.",
    "- Redis fault: $redisNote",
    "- Architecture target (cached problem read P99 < 100ms) uses the ``detail cached`` row.",
    "",
    "## reproduce",
    "",
    "``````powershell",
    "cmake --build --preset debug --target nloj_api_bench",
    "powershell -File scripts/bench.ps1",
    "``````",
    ""
)

[System.IO.File]::WriteAllLines($report, $md)
Write-Host ("wrote " + $report)
Write-Host ("hot hitRate=" + $hitPct + " QPS=" + $hot.qps)
