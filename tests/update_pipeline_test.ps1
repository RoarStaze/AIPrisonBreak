$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$workflow = Get-Content -LiteralPath (Join-Path $repo ".github/workflows/windows.yml") -Raw
$updaterPath = Join-Path $repo "UPDATE.ps1"
$ignorePath = Join-Path $repo ".gitignore"

function Assert([bool] $condition, [string] $message) {
    if (-not $condition) { throw "Update pipeline test failure: $message" }
}
Assert ($workflow -match '(?m)^\s+name:\s+AIPrisonBreak\s*$') "artifact name must be exactly AIPrisonBreak (AIPrisonBreak.zip)"
Assert ($workflow -notmatch 'name:\s+AIPrisonBreak-Windows-x64') "old artifact name remains"
Assert (Test-Path -LiteralPath $updaterPath) "missing updater script"
Assert (Test-Path -LiteralPath $ignorePath) "missing gitignore"
$script = Get-Content -LiteralPath $updaterPath -Raw
$tokens = $null
$parseErrors = $null
$null = [System.Management.Automation.Language.Parser]::ParseFile(
    $updaterPath, [ref] $tokens, [ref] $parseErrors)
Assert ($parseErrors.Count -eq 0) ("PowerShell parse failed: " + ($parseErrors -join "; "))
Assert ($script -match 'pull --ff-only') "updater must pull without resetting local edits"
Assert ($script -match 'gh run list') "updater must identify the matching successful build"
Assert ($script -match '(?s)--commit\s+\$[A-Za-z]+') "updater must match the local git revision"
Assert ($script -match 'gh run download') "updater must fetch the compiled release"
Assert ($script -match '--name AIPrisonBreak') "updater must use the new artifact name"
Assert ($script -match 'Remove-Item') "temporary files must be cleaned up"
Assert ($script -match 'build[\\/]Release') "binary must be installed in the stable build/Release directory"
Write-Host "Update pipeline checks passed."
