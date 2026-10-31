# Environmental texture source and license

The optional photographic environment textures used by the Windows build are taken from [Poly Haven](https://polyhaven.com/), where they are offered under **CC0**:

- Concrete Wall 001 — https://polyhaven.com/a/concrete_wall_001 — Dimitrios Savva, Rico Cilliers
- Concrete Floor Worn 001 — https://polyhaven.com/a/concrete_floor_worn_001 — Dimitrios Savva, Rico Cilliers
- Rusty Metal 02 — https://polyhaven.com/a/rusty_metal_02 — Rob Tuytel

The GitHub Actions Windows build downloads 1K diffuse maps from the Poly Haven CDN into the release artifact. Files are not required for compilation or game logic. If they are unavailable, the executable generates procedural textures automatically. No texture is downloaded at runtime by the game.

These textures are not a substitute for proper PBR shaders, high-fidelity models, baked lighting and animation. The current native prototype is still visually under development.

## Additional CC0 3D geometry

Five prison props are downloaded during GitHub Actions builds from the [Prison Complex Builder](https://3dassets.dev/packs/prison-complex-builder) pack at 3DAssets.dev (CC0 1.0 Universal). Selected models: cell bunk (asset 25595), toilet/sink combination (25597), CCTV camera (25643), control room desk (25646), and ceiling light bank (25615). Files use original metre units and are optional: failure to load a model uses procedural fallback geometry. The artwork is AI-generated, not hand-reviewed for final production realism.
