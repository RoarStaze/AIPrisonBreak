# AI Prison Break: Blackwater

A **native fullscreen Windows first-person game**, written in C++ with [raylib](https://www.raylib.com/). There is no browser, Electron, embedded page, or web dashboard.

The game is being developed entirely in this GitHub repository. GitHub Actions builds the executable and runs tests.

## Play a build

1. Open the **Actions** tab in this repository.
2. Select **Windows game build** and open the newest **successful** run.
3. Download **AIPrisonBreak-Windows-x64** from the run's Artifacts section.
4. Extract the ZIP and double-click **AIPrisonBreak.exe**.

This application starts fullscreen at the native display resolution. **F11** toggles fullscreen. No web browser is involved.

### Optional local AI (recommended)

Install [Ollama](https://ollama.com/download/windows), then in PowerShell:

```powershell
ollama pull qwen3:4b-instruct
```

Ollama runs on your machine at `127.0.0.1:11434`. The game continues functioning with deterministic guard movement if Ollama is unavailable; advanced language-model decisions and conversation require the installed model. The repository never asks you to put API keys in source code.

## Controls

| Control | Action |
| --- | --- |
| WASD | Walk |
| Mouse | Look |
| Left Shift | Sprint (uses stamina) |
| C or Ctrl | Crouch |
| E | Search, collect, hack, open or unlock nearby interactables |
| T | Talk to Officer Voss |
| Enter | Submit conversation |
| F | Toggle flashlight |
| Esc | Pause / return |
| R | Restart while paused or after game over |
| F11 | Toggle fullscreen |

## What the AI actually controls

**World perception:** Vision cone with raycast occlusion, auditory events, CCTV reports, visible objects and door state.

**Embodied actions:** Move to any navigable waypoint, look, search, investigate and physically interact with nearby devices and items.

**Elevated prison privileges:** Remotely set alarm levels, operate doors, CCTV and lighting. This is a game-world authority system, **not** permission to execute operating-system commands.

**Timing:** The simulation runs continuously while Ollama plans asynchronously. Deterministic pursuit and patrol maintain responsive movement during model inference.

**Trust boundary:** Every LLM command is validated by `World::command`. Invalid commands are rejected and recorded as events.

## Developer build

On Windows, install Git, CMake and Visual Studio Build Tools (MSVC C++), then:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\AIPrisonBreak.exe
```

CMake fetches raylib 5.5 and nlohmann/json automatically.

## Scope and realism

This is an **early native prototype**. Its modeled props, character, and procedural textures are placeholders; the scene is not yet photorealistic or a commercial-quality production game. Realistic appearance will require licensed high-quality models, material assets, environment art, lighting/shadow pipelines, and animation. Native fullscreen delivery and a real AI gameplay foundation come first.

## Photo materials

Windows build artifacts include CC0 photographic wall, floor and metal textures when the Poly Haven CDN is reachable during CI. The game has procedural offline fallbacks. See [texture credits](docs/ASSET_LICENSES.md).
