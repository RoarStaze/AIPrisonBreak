# AI Prison Break: Blackwater

A native fullscreen Windows first-person prison escape game, built in C++ using raylib. No browser or web UI.

This repository is under active development. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the agent model and world interaction design.

## Build (Windows)

Requires CMake, a C++17 compiler and Git. Dependencies are fetched with CMake.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\AIPrisonBreak.exe
```

For natural-language guard decisions, run Ollama locally and pull `qwen3:4b-instruct` (`ollama pull qwen3:4b-instruct`). The game must continue to function if Ollama is offline.

Downloadable builds are produced via GitHub Actions after a successful build. This is an early native prototype, not a finished photorealistic game.
