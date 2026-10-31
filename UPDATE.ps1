param(
    [switch] $Launch
)
$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$root = $PSScriptRoot
$repository = "RoarStaze/AIPrisonBreak"
$release = Join-Path $root "build/Release"
$incoming = Join-Path $root "build/.update-incoming"

function Check-Exit([string] $step) {
    if ($LASTEXITCODE -ne 0) { throw "$step failed (exit $LASTEXITCODE)." }
}

if (-not (Test-Path -LiteralPath (Join-Path $root ".git"))) {
    throw "This is not a Git clone. Clone $repository into this folder before updating."
}
foreach ($tool in @("git","gh")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool is required but is not installed or is not in PATH."
    }
}
if (Get-Process -Name "AIPrisonBreak" -ErrorAction SilentlyContinue) {
    throw "Close AIPrisonBreak.exe before updating, then retry."
}

Write-Host "[1/3] Pulling main from GitHub..." -ForegroundColor Cyan
& git -C $root pull --ff-only origin main
Check-Exit "git pull"
$revision = (& git -C $root rev-parse HEAD).Trim()
Check-Exit "git rev-parse"

Write-Host "[2/3] Finding the successful Windows build for commit $($revision.Substring(0,7))..." -ForegroundColor Cyan
$runsJson = & gh run list --repo $repository --workflow windows.yml --branch main --commit $revision --status success --limit 10 --json databaseId,headSha
Check-Exit "gh run list"
$runs = @($runsJson | ConvertFrom-Json)
$run = $runs | Where-Object { $_.headSha -eq $revision } | Select-Object -First 1
if ($null -eq $run) {
    throw "No successful Windows build yet for commit $($revision.Substring(0,7)). Check https://github.com/$repository/actions and retry after the build succeeds."
}

# GitHub CLI extracts the artifact into staging; no ZIP is kept locally.
if (Test-Path -LiteralPath $incoming) {
    Remove-Item -LiteralPath $incoming -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $incoming | Out-Null
try {
    Write-Host "[3/3] Updating game in place (no extra ZIPs or game folders)..." -ForegroundColor Cyan
    & gh run download ([string] $run.databaseId) --repo $repository --name AIPrisonBreak --dir $incoming
    Check-Exit "gh run download"

    $freshExe = Join-Path $incoming "AIPrisonBreak.exe"
    if (-not (Test-Path -LiteralPath $freshExe)) {
        throw "The GitHub artifact does not contain AIPrisonBreak.exe."
    }

    New-Item -ItemType Directory -Force -Path $release | Out-Null
    foreach ($file in (Get-ChildItem -LiteralPath $incoming -Recurse -File)) {
        $relative = $file.FullName.Substring($incoming.Length).TrimStart('\','/')
        $destination = Join-Path $release $relative
        $folder = Split-Path -Parent $destination
        New-Item -ItemType Directory -Force -Path $folder | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }
} finally {
    if (Test-Path -LiteralPath $incoming) {
        Remove-Item -LiteralPath $incoming -Recurse -Force
    }
}
$exe = Join-Path $release "AIPrisonBreak.exe"
Write-Host "Updated in place: $exe" -ForegroundColor Green
if ($Launch) {
    Start-Process -FilePath $exe -WorkingDirectory $release
}
