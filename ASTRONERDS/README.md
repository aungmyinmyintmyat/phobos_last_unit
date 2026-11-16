# Unit-01: Project Horizon

Project Horizon is a C++17, software-rendered exploration game starring Unit-01, a small utility robot aboard a damaged rescue ship near Phobos. The SDL game opens on an animated space menu. The current campaign follows Unit-01 across the surface and ship interior, from emergency recovery to restoring the rescue ship.

## Current campaign

- **Mission 1 — Act 1:** Recover three first-aid kits and two food-and-water boxes from the surface, deliver them to the injured astronaut, then inspect silicate, water ice and rock-fragment samples.
- **Mission 2:** Find the damaged carrier robot in the cargo hangar, collect its wheel, arm and control chip, then stay nearby while Unit-01 repairs it.
- **Mission 3:** Collect the **USB drive** and S-01 coordinates from the Monitor Room PC. Carry the USB to **S-01 at (119,60)** and press **E** to scan it for four seconds. The scan is saved onto the USB and completes Mission 3.
- **Mission 4:** On the return trip to base, recover the separate **Mars Exploration data chip** at **(112,48)**. Bring it and the USB to the Monitor Room PC, upload the scan record, read four science passages and answer nine multiple-choice questions. Wrong answers can be retried; every answer must be correct to complete the mission. Press **N** or choose Continue after all nine correct answers to resume Mission 6 in the same ship.
- **Mission 6:** Gather hull plates, wiring and sealant from Storage. This replaces the removed Mission 5 placeholder and has its own menu entry.
- **Mission 7:** Communicate with C-02, fit its seat, collect the Astronerd and escort C-02 to S-01.
- **Mission 8:** Repair S-01, board the Astronerd through its door, then transfer all C-02 and U-01 power to launch the ship. Both robots remain powered off on the planet.

Mission 1's surface has three material enclosures with distinct silicate, ice and broken-rock formations. Breaking the first barrier of each type gives one sample. Surface debris and procedural material shapes are seeded, so their appearance stays consistent when revisiting areas or changing camera views.

Map1 also contains sparse, seeded, mineable ore, hydrated-mineral and ice deposits.
Hub roads, supply enclosures, the cliff opening and mission destinations remain clear.
Mission 1's inventory shows first-aid kits and food/water in separate boxes with
independent counts; the three observation samples occupy their own boxes. The
inventory panel also shows amounts of the scattered minerals collected.

Map 2 is a compact, walkable 32 × 57 ship deck containing the crew room, storage room, cargo hangar, monitor room and engine room. Sliding doors open as Unit-01 approaches. Storage contains the Observed Table and a separate Craft Table; the Monitor Room contains the PC and three animated displays. The ship and surface share the same bird's-eye camera, movement, jumping and full-size robot model.

Choose between two Unit-01 models with **K**. Mark 1 keeps the original gold design; Mark 2 has a mint chassis with warm trim. Both have animated blue optics and eye lighting that follows the robot's head and movement. The game uses seeded, procedural surface materials and software-rendered light and shadows.

The **Missions** menu includes Missions 1–5. **Options** contains sound settings. Mission progress is local to the running game; there is no account or network service.

See the [Mission 1 specification](docs/MISSION_1.md) and [ship interior notes and preview commands](docs/SHIP_INTERIOR.md).

## Build and run

The default build works without SDL2 and includes the terminal renderer, demos and image exports. To build and launch the graphical game, install SDL2 development files and `pkg-config` first.

```sh
make -j2
make run
```

On Debian or Ubuntu, install the optional SDL2 dependency with:

```sh
sudo apt install libsdl2-dev pkg-config
```

Useful commands:

```sh
./build/horizon --window       # graphical game, when built with SDL2
./build/horizon --terminal     # text interface
./build/horizon --demo         # legacy isometric expedition demo
./build/horizon --side-window  # archived side-scrolling prototype
./build/horizon --side-demo    # side-scrolling demo
./build/horizon --help
```

The Makefile detects SDL2 with `pkg-config`. CMake 3.16+ is also supported:

```sh
cmake -S . -B build/cmake -DHORIZON_SDL=ON
cmake --build build/cmake -j2
ctest --test-dir build/cmake --output-on-failure
```

Set `-DHORIZON_SDL=OFF` to build without the SDL window. `make test` runs the project test suite.

## Controls

| Input | Action |
| --- | --- |
| WASD / arrow keys | Move; W / Up moves toward the top of the screen |
| Space | Jump; movement can continue while airborne |
| J / left mouse button | Mine the targeted material barrier |
| Right mouse button | Place regolith in the legacy expedition mode |
| E / Enter | Collect a nearby supply, deliver to the astronaut, use a station, or interact with a mission object |
| C | Open the Craft Table when nearby |
| Tab | Open inventory |
| T | Show or hide mission tasks |
| H | Open the field guide |
| K | Switch between Mark 1 and Mark 2 robot models |
| G | Switch camera projection |
| B | Toggle eye lighting |
| U | Toggle sound |
| F11 | Toggle fullscreen and windowed mode |
| Esc / P | Open or close menus; pause or resume |
| N | Restart the current mission |

Use the mouse to select menu entries, mission items and observation samples. The Observed Table accepts one sample at a time; each inspection takes six seconds, and the sample returns to inventory when the result is ready. Observed samples can be reviewed at the table.

## Previews and project structure

Export the current game frame or ship walkthrough clips with:

```sh
./build/horizon --snapshot build/gameplay.ppm
./build/horizon --ship-preview build/ship-frames
python3 scripts/render_preview.py --ship-only --output build/ship-preview
```

The preview script needs Pillow only to package the exported frames. More details are in [SHIP_INTERIOR.md](docs/SHIP_INTERIOR.md).

Core source is under `src/` and public interfaces are under `include/horizon/`:

- `Game` coordinates missions, maps, interactions and simulation updates.
- `WorldGrid` defines the surface and ship maps, terrain, barriers and supplies.
- `ShipInterior` describes the deck layout, doors and fixtures.
- `Renderer`, `IsometricScene` and `SceneLighting` draw the software-rendered game.
- `RobotEyes` and `RobotAppearance` define the shared eye geometry, lighting and robot model presentation.
- `Mission1` contains the opening mission's reusable task and observation data.
- `Mission4` holds the Mars reading passages and quiz answer key.

The earlier isometric expedition and side-scrolling prototype remain available for demos and regression coverage; the default graphical game starts with the ship-and-surface campaign described above.

The S-01 escort, repair, boarding and final charging sequence is described in [the rescue missions](docs/MISSIONS_6_7.md).
