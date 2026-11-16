> Historical notes for `--side-window` and `--side-demo`. The default game is now isometric; see the root README.

# Archived side-scrolling prototype

A modular **C++17** sandbox prototype: Unit-01 mines a destructible asteroid, refines resources, upgrades its tools, repairs a rescue rocket and makes a final sacrificial climb during a solar particle event.

Includes a scrolling **640×360 pixel-art renderer**, an optional real-time SDL2 window, a dependency-free terminal frontend, a deterministic mission demo and automated system tests. Sprites and text are drawn in C++; no downloaded assets are required.

**Local play only:** world tiles, inventory, upgrades, quest history and rocket state live in C++ memory. There is no database, game server, account system, cloud storage, analytics or network integration. Restarting begins a fresh mission; progress is not persisted.

## Animation and mission preview

Unit-01 now has animated treads, directional eyes/arms, blinking, movement dust, landing squash and a smoothly following camera. Mining includes a pulsing laser, sparks, progressive block cracks, a cutting progress bar, break debris and resource pickup labels. Successful crafting/repairs show machine lights, orbiting particles, an output-transfer animation and a confirmation label. Recipe transactions still complete immediately; the short transfer animation is visual feedback and does not grant extra items.

The gameplay HUD uses a narrow health/objective bar and a material icon hotbar, leaving 79% of the screen for the world. A compact current-task card stays visible; the full checklist and recipe text open on demand. Material icons share their colors and shapes across inventory, recipes, pickups and objective markers. Hover over a material to see its name; kilograms and individual item counts are shown directly beneath the icons.

The background now uses a software-rendered, shaded planet with rotating surface detail, six slowly tumbling perspective-projected asteroid meshes, three parallax layers of faceted mountains, twinkling stars, and a faceted cave back wall. These decorative 3D objects animate behind the 2D mining grid; they never change terrain collisions or resource generation. No GPU-specific API or external assets are required.

Aim the cursor and use **J** for mining/placement: hold J over a solid mineable tile to cut it, or press J over an empty cell to place 1 kg regolith. The mode is chosen on the initial press, so holding J after a block breaks cannot place it back. Release and press again to place deliberately. Nearby empty cells show a placement ghost, green when valid and red when unavailable. Left/right mouse remain alternate mining/placement inputs.

Press **C** beside the base to open clickable recipe cards: ingredient icons and amounts → output icons and amounts. Available recipes have a green border; missing quantities are red. Press **Tab** to inspect all materials, **T** for the six-step checklist, or **H** for a paused visual controls/processing guide. Escape closes an open menu before exiting the game. The help and materials badges are also clickable. Inventory and crafting block movement input while the simulation continues; the guide pauses the simulation.

Gathering and crafting completion is remembered after materials are consumed. Gold icon beacons mark the active objective, with an arrow and distance when it is off-screen. The story follows **Act I / Discovery → Act II / Repair → Act III / Sacrifice → Game End**. UI panels consume mouse input so clicks on the checklist or recipe cards do not mine tiles behind them. Routine status paragraphs are absent from the gameplay view; failed actions produce a brief notice.

Generate reviewable previews from actual simulated mining, crafting and mission frames:

```sh
python3 scripts/render_preview.py
```

The optional preview script requires Pillow and writes `build/previews/mission-preview.gif`, individual mining/crafting/climb/launch GIFs, PNG stills, and `contact-sheet.png`. Separate `craft-menu.png`, `materials.png`, `guide.png` and `task-panel.png` show the on-demand screens. The game itself does not require Python. `./build/horizon --preview build/frames` exports the underlying PPM frames and a manifest without Python or a display. These previews use the documented hand-authored mission fixture, not the default procedural map.

The current local architecture and story flow are documented in [docs/ARCHITECTURE.md](ARCHITECTURE.md).

## Build and run

With GCC or Clang and Make, no third-party library is required:

```sh
make -j2
./build/horizon --terminal
./build/horizon --demo
make test
```

For the pixel-art window on Debian/Ubuntu, install SDL2 development headers and pkg-config, then rebuild:

```sh
sudo apt install libsdl2-dev pkg-config
make clean
make -j2
./build/horizon --window
```

The Makefile detects SDL2 at build time. `--window` reports clearly when graphics were not compiled in. The terminal, demo and PPM image output always work.

Alternatively, use CMake 3.16 or later:

```sh
cmake -S . -B build/cmake -DHORIZON_SDL=ON
cmake --build build/cmake -j2
ctest --test-dir build/cmake --output-on-failure
./build/cmake/horizon --window
```

`-DHORIZON_SDL=OFF` explicitly selects a graphics-library-free build. `-DHORIZON_SANITIZERS=ON` enables AddressSanitizer and UndefinedBehaviorSanitizer with GCC/Clang. The Make equivalent is `make sanitize`.

Render a standalone image without a display:

```sh
./build/horizon --snapshot build/horizon.ppm
```

`--demo` uses a hand-authored resource lane instead of the procedural map, then runs actual movement, mining, crafting, upgrades, repairs, scaffold climbing and launch through the same simulation. It fails with a nonzero exit code if the mission cannot finish. Its manufactured starting supplies match the normal game.

## Window controls

| Input | Action |
| --- | --- |
| A/D or left/right | Accelerate and move |
| Space | Jump while grounded |
| W or up | Climb an adjacent wall or overlapping scaffold |
| J at the cursor | Hold to mine solid material; press on empty space to place regolith |
| Hold left mouse | Mine the pointed tile within 3.1 tiles and line of sight |
| Right mouse | Place 1 kg regolith; unsupported dust falls |
| C | Open/close the visual recipe menu beside the base; click an available card to craft |
| Tab | Open/close the full materials inventory; hover icons for names |
| E / I | Extract hydrated minerals / melt ice at base |
| O / L / F | Electrolyze / liquefy gases / package propellant at base |
| M | Smelt structural plates at base |
| 1 / 2 / 3 | Craft solar drill / dust treads / polyethylene shielding at base |
| R | Complete the next rocket milestone at its ground socket |
| V | Activate the manual valve at the scaffold summit during the SPE |
| H | Open/close the visual field guide; simulation pauses while it is open |
| T | Expand/collapse the full checklist; current task stays visible |
| N / Escape | Start a fresh mission / exit |

Window mode runs in real time. The terminal advances only when a timed command runs; `help` lists commands, and `scan` prints landmarks. In terminal mode, `mine 18 36 2` operates the laser for two seconds; `right 1` moves for one second. Movement coasts briefly because acceleration and friction are simulated. `jump` advances one second; shorter durations such as `jump 0.15` let you combine a jump with a subsequent movement command.

Coordinates use tiles, with **x rightward and y downward**. Unit-01's body is 0.7×0.85 tiles. The default world is 160×72, surface row 36; Unit-01 starts beside the base around (12,35). `H` mineral blocks, `O` ore, `I` ice and `*` data cores identify resources in the terminal.

## Complete the mission

1. Mine ore and regolith. Guaranteed ore/hydrated starter seams lie at x18–35, y36. Each mined ore/dust block gives 1 kg; a hydrated block represents a larger 20 kg feedstock batch.
2. Return to the base at (10.5,35.5). Smelt **1 kg ore + 1 kg dust → 2 plates**. Six batches supply all three upgrades plus hull and fuel lines; three batches suffice for repairs alone.
3. Craft upgrades if desired. Each costs 2 plates plus 1 kg scrap, or 1 kg polyethylene for shielding. Your crashed lander's cache starts with 2 kg scrap and 1 kg polyethylene. These manufactured materials are finite salvage.
4. For each of two propellant kits, process one hydrated block: **extract → electrolyze → liquefy → fuel**. Ice provides an alternative water source via `melt`.
5. Dig down to the NASA data core at **(80,46)**. Mine it and climb back out along a shaft wall. Regolith can fall into excavations, so choose a supported shaft or work below the dust layer.
6. At the rocket ground socket **(140.5,35.5)**, weld the hull (4 plates), hook up fuel lines (2 plates + 2 fuel kits), and socket the core. Each step consumes its ingredients once.
7. Calibrate launch control only when ready. The SPE automatically begins on the next simulation tick. Its front crosses the world in four game seconds.
8. Move onto the scaffold at x138 and hold climb to the summit valve **(138.5,19)**. Activate it while Unit-01 is still operational. Radiation and heat permanently reduce integrity; shielding delays radiation damage.
9. The rocket departs toward Earth, Unit-01 permanently shuts down, and the screen fades to black: **“Mission Complete. Earth Saved.”** Failure to reach the valve before shutdown gives a separate mission-failed state.

## Source layout

Each subsystem has a public header under `include/horizon/` and a matching implementation under `src/`.

| Subsystem | Responsibility |
| --- | --- |
| `Types` | Coordinates, tile catalog, item/upgrade types and action results |
| `Inventory` | Integer gram/count stacks and atomic multi-resource transactions |
| `WorldGrid` | Seeded layers, tile queries, mining work, drops, placement, grid ray tracing and dust gravity |
| `Unit7Entity` | Acceleration, jump, wall/scaffold climbing, per-axis collisions, mining, inventory and irreversible integrity loss |
| `NASA_ISRU_Refinery` | Thermal extraction, ice melting, electrolysis, liquefaction, fuel packaging, smelting and upgrade recipes |
| `RocketRepairSystem` | Ordered, proximity-gated structural and fuel/data milestones |
| `RadiationStorm` / `ClimaxEngine` | Sweeping exposure, actual overhead cover, endgame states, launch and fade |
| `Game` | Exclusive subsystem ownership via `std::unique_ptr`, input, actions and fixed 60 Hz coordination |
| `Presentation` | Bounded particles, mining/crafting effects, locomotion pose and animation timing |
| `SpaceBackground` | Lit sphere, rotating perspective meshes, parallax terrain and cave back walls |
| `TargetTool` | Context-sensitive J control, latched mining mode and single-press placement |
| `Preview` | Headless capture of real mission frames for preview GIFs and stills |
| `Renderer` | Virtual renderer interface, terminal viewport and standalone software pixel art |
| `SDLRenderer` | Optional window, streaming texture, event loop and SDL resource lifetime via RAII |

The simulation never includes SDL headers. Dust updates eight times per second using bottom-to-top traversal, protecting the robot's occupied cells. Physics uses substeps no longer than 1/120 s, bounded fall speed and per-axis contact resolution. Mining uses per-tile persistent work and grid DDA to prevent cutting through intervening walls.

Crafting validates every input and output on a staged inventory copy before committing, including overflow and repeated input checks. The rocket's launch mutation is private to `ClimaxEngine`; ordinary repair calls cannot bypass the storm or summit checks. After launch, the intentional robot shutdown does not change success into failure.

## Science and scope

NASA's [ISRU research overview](https://www.nasa.gov/reference/jsc-in-situ-resource-utilization/) informs the extraction and processing chain. The implementation distinguishes gases produced by electrolysis from the later cryogenic liquids, consistent with NASA's [propellant production assessment](https://ntrs.nasa.gov/citations/20230005189). Numerical assumptions, sources and scientific limits are documented in [docs/SCIENCE.md](SCIENCE.md).

This is a playable architecture prototype. Production-scale plant power, processing queues, cryogenic boil-off, orbital mechanics, save files, audio and network play are not implemented. The asteroid's composition is fictional; recipe quantities, radiation damage and platforming gravity are designed for gameplay.
