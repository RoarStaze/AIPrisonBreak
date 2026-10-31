$ErrorActionPreference = 'Stop'
$model = 'qwen3.5:4b-q4_K_M'
if (-not (Get-Command ollama -ErrorAction SilentlyContinue)) {
    Write-Host 'Ollama is required. Install it first from https://ollama.com/download/windows' -ForegroundColor Yellow
    exit 1
}
Write-Host "Installing the game AI model: $model" -ForegroundColor Cyan
& ollama pull $model
if ($LASTEXITCODE -ne 0) { throw "Model installation failed (exit code $LASTEXITCODE)." }
Write-Host 'Officer Voss model installed. Launch AIPrisonBreak.exe.' -ForegroundColor Green
