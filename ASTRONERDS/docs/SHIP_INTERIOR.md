# Rescue ship interior gameplay

The deck uses dark blue steel with continuous shading, muted teal light pools and sparse pixel wear. Floor cell outlines and repeated corridor stripes are removed; the underlying gameplay grid and collision are unchanged.

The deck has no floor crack patterns. Bulkheads retain varied fractures, chipped top plates and split seams. These surface details follow the existing cosmetic damage toggle and do not open holes or alter collision.

Crash debris is scattered deterministically across the full passage network, with sparse spillover into rooms: wrenches, loose wires and circuit chips. Each piece has a varied offset and angle and shares the scene's lighting and depth ordering. Door thresholds, the airlock and key interaction points stay clear. The Crew Room round table holds three damaged TVs with cracked blank screens and broken stands. Five toppled seats lie on the floor around it at varied angles; their decoration is depth sorted and preserves walking routes. These additions are decorative and do not change walls, furniture, collision or mission routes; they stay in place across redraws and pauses.

Map2 is a 32 x 57 playable ship interior, half the previous 64-cell width. The crew, storage, cargo, monitor and engine rooms retain their connected floor plan. Rooms, corridors and fixtures are compacted horizontally while keeping clear routes between every mission station.

The forward room above the Crew Room is inaccessible after hull damage. Collapsed pressure-hatch panels, bent structural beams and exposed cables fill its six-cell entrance at (13,8) through (18,9). Amber warning lamps and a nearby `HULL DAMAGE / PASSAGE BLOCKED` notice explain the obstruction. Its collision prevents walking, jumping or landing on the debris, and mining and E cannot clear it. This physical wreckage remains visible when cosmetic ship aging is toggled off. The Crew, Storage, Cargo, Monitor and Engine mission routes remain open.

The sealed forward room has lost power: its floor and hull receive only faint cool ambient light, its wall lamps are off, and its console screen is blank. The amber wreckage lamps remain visible at the blocked entrance. The outage persists when cosmetic ship aging is toggled off.

The interior now uses Map1's angled bird's-eye projection: a 22/11-pixel ground basis, 20-pixel height scale, smooth camera following and the same full-size Unit-01 meshes. WASD is screen-relative in both maps. Space jumps with the same gravity, battery cost and landing behavior. Both robot models retain their steering, tread motion, suspension, independent head turn, blinking, blue eye rays and shutdown presentation. B toggles eye lighting. G keeps the exterior planet-camera preference; the interior remains a flat projected deck.

`ShipInterior.hpp` owns the shared floor and furniture plan. `WorldGrid` stores visible support heights for beds, tables and machinery. Hull cells use a separate 100-unit collision height, so they remain visually realistic while being impossible to clear or land on. `IsometricScene` depth-sorts the interior furniture, walls and robot, uses fixed low foreground bulkheads for visibility and continuous full-height rear walls and applies electrical lighting. `ShipInteriorRenderer.cpp` shares the main HUD, inventory, observation controls and pause menus. The minimap shows the entire deck with station markers.

The injured astronaut is in the first crew bunk at (10.5,17.5), wearing a white pressure suit with a navy visor, dark gloves and boots, and orange chest controls. The floating diagnostic display is between the first and second bunks at y=19.5. Both remain visible with cosmetic aging disabled. The Storage Room observation table is at (3.5,17.5), its craft table is at (3.5,24), and the charger is in the Monitor Room at (22.5,37.5). Boarding starts at (16,53); the exit is at (16,55.5). Mission markers use these coordinates indoors and the surface airlock outdoors. E interacts; use the rear ramp to return to Map1.

Completing the carrier repair switches its visuals to an intact eight-wheel flatbed with restored rear panels and the articulated claw. Loose debris, hanging wires, smoke and electrical sparks disappear. Its compact scale and collision footprint stay the same.

The Cargo Hangar entrance at (24,20) starts closed and opens automatically when the robot comes within two cells of either side of the opening. Its 0.3-second sliding animation must clear before the passage becomes walkable. The door stays open while the robot occupies the threshold or remains within 2.6 cells, then closes after the robot moves away. The wider release range prevents repeated opening and closing near the sensor boundary. The surrounding west bulkhead prevents bypassing or jumping through the closed doorway; the rover is parked farther east to leave a clear lane. Storage also opens automatically from either side.

The cargo craft follows the damaged claw-arm reference, at half the previous linear size relative to the player. Its charcoal flatbed has orange trim, white headlights, four surviving front wheels, a broken rear axle and dangling wires. A bent articulated claw replaces the tall lift mast; electrical arcs, falling sparks and light smoke animate on simulation time. Small metal scraps lie beneath the damaged end. The body occupies (28,20) through (29,21), with the arm extending into (27,20). The reduced collision footprint clears the former parking area at x=26. The deck supports jumping at 0.575 units; the arm's separate collision region reaches 1.55 units.

The Monitor Room has the same automatic entrance at (19,35), with its own sealed west bulkhead and independent sliding door. The old desk and floor console bank have been removed. A PC desk against the TV wall at (25,32) has a monitor, keyboard, mouse and tower, with a chair at (26,34). Three TVs hang above it, displaying damaged signal, radar and navigation feeds with cracked glass and flickering static. The charger sits to the left of the PC, with clear access from the door. Storage's observation and crafting tables remain usable for the mission.

Mission 1 requires three first-aid kits from Map1 and two food/water boxes from Storage at (1.5,19.5) and Engine at (24.5,47.5). All five count toward delivery progress. Collected food boxes disappear and free their floor space. The active supply marker points to remaining supplies on the current map, or to the airlock when the player needs to change maps.

Wall cutaways depend only on the deck geometry, so bulkheads keep their height as the robot moves. Flush metal cap plates, base rails and spaced panel seams form continuous walls. The robot casts separate soft tread, chassis and head shadows; the footprint turns with steering, fades and spreads during a jump, and lands on furniture tops as well as the floor.

Monitor traces, reactor bands, charging rails and the astronaut's breathing follow simulation time and pause with the game. Rendering does not advance the simulation. The exterior and interior cameras maintain separate anchors so transfers and restarting do not sweep across the other map.

Generate gameplay stills and a four-second walking/jumping GIF:

```sh
make -j2
python3 scripts/render_preview.py --ship-only --output build/ship-preview
```

This writes `ship-interior.png`, `ship-interior.gif`, and separate `ship-airlock`, `ship-engine`, `ship-monitor`, `ship-monitor-tvs`, `ship-pc-setup`, `ship-cargo`, `ship-storage` and `ship-crew` PNGs at 1280 x 720 with nearest-neighbor scaling. All frames come from actual walking and map transfers. Pillow is needed only for packaging. Raw frames are available with `./build/horizon --ship-preview build/ship-frames`.

`ship-blocked-forward-room.png` shows the collapsed passage from the Crew Room approach.

`make test` includes actual routes through all rooms, astronaut and observation access, recharge, the narrowed width, Map1's projection scale and controls, camera following, jumping onto a bunk, paused animation and the clickable airlock round trip. It also runs the complete supply/delivery/observation mission.

`cargo-door.gif` and `monitor-door.gif`, with matching `*-door-closed.png` / `*-door-entered.png` stills, show the automatic approach and entry sequences. Ship tests cover automatic opening from both sides and near end panels, sealed boundaries, jump prevention, pause-safe animation, threshold safety, sensor hysteresis, closing after moving away, independent doors, PC furniture collision, relocated charging and animated TVs.
