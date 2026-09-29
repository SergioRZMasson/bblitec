# Unfinished work

Internal work, qualification, performance and refusal defects. Capability gaps are the Limits in
[features](docs/features.md) and [UI](docs/ui.md); [audit](audit.md) tracks audit findings;
[status](docs/status.md) owns measurements.

## Qualification

- [ ] Qualify `playroom` startup parity on both backends without changing its source, references or gates.
- [ ] Move mixed mesh/billboard ordering into a generated draw plan after retaining source deferred-build completion order and prior stable binding order; keep equal-order/depth and camera-less refusals until those inputs and ordering-neutrality coverage exist.
- [ ] Give remaining sliced PAL code standalone concern headers that harness fixtures include.
- [ ] Android: resolve same-device emulator visual gaps and qualify the full registry on a physical device.
- [ ] iOS: qualify device bundles on hardware.
- [ ] Linux/Vulkan: every registered scene within its thresholds against same-host browser references.
- [ ] macOS/Metal: every registered scene within its thresholds against same-host browser references, fonts included, on an Apple Silicon host.

## Performance

- [ ] `playroom`: bound the loading-timer gaps of the synchronous world-build task (Bullet world construction) and of the first rendered frame (first Bullet step; SDL_GPU first-use uniform-buffer and pipeline costs); sustain interactive frame pacing after impact. Its texture decodes run one at a time behind `image_decoder_mutex`, because SDL_image's codec initializers update unsynchronized state on every load; an atomic initialization in the `sdl3-image` overlay port would let the native workers decode them in parallel.
- [ ] `scene290`: at least 100 FPS uncapped through impact and settling (`BBLITE_BENCHMARK_FRAMES=0`, `BBLITE_FPS_PROFILE`, the `scene290-live` twin); settling holds it on both backends, but one-second windows during impact still fall below it in some runs, with Bullet stepping the bottleneck (`pal_physics_bullet.cpp`).
- [ ] `minecraft`: worst frame of a chunk-crossing sprint replay (`-,+ShiftLeft,+KeyW,+Space`) at most 16.7 ms (`BBLITE_FPS_PROFILE` maximum interval); the scene's meshing and voxel-light floods dominate it (`BBLITE_SOURCE_PROFILE=meshChunk,WorldLight.compute,WorldLight.warmFor,WaterSim.settleChunk,generateChunk,ChunkRenderer.buildChunk,ChunkRenderer.processQueue`, [debugging](docs/debugging.md)).

## Dependencies

- [ ] Drop `sdl-multisample-read.patch` at the first SDL release after 3.4.16 (libsdl-org/SDL#15838 merged), and `png-grey-ramp-last-index.patch` once an SDL_image release builds the ramp over the last index; `d3d12-multisample-lines.patch` needs a new upstream proposal (libsdl-org/SDL#16183 closed unmerged). `native/patches/manifest.json` records each patch's upstream state.
