# Blackwater simulation architecture

## Design

Native first-person C++ game; fullscreen OS window using raylib, **not** HTML, Chromium, Electron, or a local web app.

The simulation is authoritative. Neither the language model nor player-input strings may inject arbitrary code into the game.

### Guard loop
1. **Perceive:** field-of-view and raycast occlusion; audible events, environmental objects, CCTV reports, state of locks and lighting.
2. **Remember:** a rolling stream of game events and the current observation. Future versions can add persistent long-term memory.
3. **Decide:** Ollama local LLM is queried on a worker thread with a strict, finite JSON action vocabulary.
4. **Act:** commands are validated by the world simulation; movement uses grid pathfinding and physical interaction requires proximity. Security control commands emulate an officer's central-console privileges.
5. **React:** deterministic reflex behavior handles pursuit and proximity between model responses, so inference latency never freezes the game.

### Safety and authority boundary
The in-fiction officer can command authorized *game-world* systems: alarms, locks, surveillance and lights. The AI cannot execute arbitrary system commands, access player files, or exercise OS/world control outside the simulated environment. Rejected model actions are logged and ignored.

### Prototype limitations
The native game loop and gameplay rules are initial systems, not proof of AAA character realism, advanced animation or production-grade graphical fidelity. Those require commissioned/licensed detailed meshes, PBR materials and substantial content work.
