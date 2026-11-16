# NASA ISRU integration and simulation assumptions

## Documented process, configurable fictional feedstock

NASA Johnson Space Center describes regolith handling/heating, water evolution and capture, purification, chemical conversion and water electrolysis in its [ISRU development overview](https://www.nasa.gov/reference/jsc-in-situ-resource-utilization/). `NASA_ISRU_Refinery` models these as distinct resource transformations, bundling capture and purification into the thermal-extraction recipe.

The map, resources and Unit-01 mission are fictional; the day/night clock adopts published Phobos timing. The hydrated C-type feedstock is a gameplay assumption, not a claim that Phobos has this composition. NASA's [mineral absorption study](https://ntrs.nasa.gov/citations/20130009708) reports ambiguous candidate phases and uncertain composition; the game does not treat those hypotheses as measured mineable deposits.

## Phobos day and night

Phobos has illuminated and dark sides, with measured temperature differences, and no atmosphere; see [NASA's Phobos overview](https://science.nasa.gov/mars/moons/phobos/). Day/night is therefore included, without an Earth-like atmospheric sky or weather model.

The [NASA NSSDCA Mars Fact Sheet](https://nssdc.gsfc.nasa.gov/planetary/factsheet/marsfact.html) lists Phobos' sidereal rotation/orbit as **0.31891 Earth days** and Mars' sidereal year as **686.980 Earth days**. For this game's uniform prograde mean-solar model, we derive:

```text
mean solar day = 86400 / (1 / 0.31891 - 1 / 686.980)
               ≈ 27566.62 Earth seconds ≈ 7 h 39 min 27 s
```

The game compresses this model by 60×: one complete cycle takes about 459.44 gameplay seconds (7 minutes 39 seconds), split into equal reference day/night halves of about 3 minutes 50 seconds. This is a gameplay time scale, not a claim that the physical Phobos day lasts minutes. The internal local clock still divides the cycle into 24 normalized hours; the HUD instead shows gameplay minutes and seconds until dawn or dusk. Sunrise/sunset are idealized at local 06:00/18:00 for an equatorial reference site, with a cosine sunlight curve. Mission start uses an arbitrary late-afternoon phase. It is not a live UTC ephemeris, surveyed landing-site clock, eclipse prediction or model of the irregular body's self-shadowing, seasonal illumination and libration. DLR's [MMX rover planning paper, sections 2.5–2.6](https://elib.dlr.de/215570/1/2025spaceops-MMX-Rover-MPS-paper_final.pdf) discusses local Phobos time and activity-based rover power planning.

## Battery and fictional danger periods

Battery charge is separate from structural integrity. Task-dependent battery accounting is qualitatively consistent with rover power planning; the game's percentages and charging rates are balance choices, not measurements of a real rover. Idle costs 0.003% per second, actual movement costs 0.04% per world unit, a successful jump costs 0.35%, valid mining costs 0.10% per second and placement costs 0.12%. Heavy refining operations cost 4–6% per transaction, with lower costs for lighter work. Failed operations do not consume task energy or ingredients.

The broken recharge box restores 4% per second while the robot is inside the wing shelter, grounded and stationary within it. The cabinet damage, transfer arcs and buffered power supply are fictional visual/game systems that work during either lighting phase. It does not repair impact- or radiation-damaged structural integrity or revive an empty, failed robot.

**Creatures, hunts, attack damage and randomized danger windows are entirely fictional.** They are not claims of life on Phobos or astronomical time zones. Calm intervals last 180–300 gameplay seconds, followed by a 30-second audible return-home countdown and a 45–65-second hunt. Creatures can drain charge through melee contact; habitat shelter blocks them. This schedule is independent of the accelerated day/night clock. Hard falls above a tuned 7.5-unit/s safe landing speed and fast collisions also damage structural integrity. Ordinary jumps remain safe; these are fictional game thresholds. Below 40% battery, a presentation-only damaged gait adds visible hesitation and head droop without changing the control speed. At zero charge or integrity, the robot collapses and shuts down and the mission fails; restarting begins a new mission.

`ISRUConfig` makes the chosen feedstock water mass fraction, recovery efficiency and extraction temperature explicit. The default is 10 wt% bound water, 90% capture, and 873.15 K (600°C). These are **design assumptions**, not NASA-validated values for this body. A universal phyllosilicate dehydration temperature does not exist: mineral identity, pressure, residence time and particle size matter. The configured temperature describes the fictional plant and is informational; this prototype does not simulate heat transfer or temperature-dependent kinetics.

One 20,000 g mineral batch therefore yields 1,800 g purified water and 18,200 g residual/uncaptured material. `Slag` combines tailings and all remaining mass for bookkeeping; a real plant would separate vapor losses and solid residue. Ice melting is an idealized lossless alternative.

## Electrolysis and cryogenic storage

NASA's [2023 ISRU overview](https://ntrs.nasa.gov/api/citations/20230012260/downloads/ISRU%20Overview%202023%20-%20%20Videos%20Removed.pdf) describes water electrolysis:

```text
2 H2O -> 2 H2 + O2
```

The gas ratio is **2:1 by moles**, approximately **1:8 by mass**. Using rounded whole-number atomic masses, the game's integer recipe turns 1,800 g water into 200 g hydrogen gas and 1,600 g oxygen gas. More precise atomic weights yield approximately 11.19% hydrogen by mass instead of the game's 11.11%. The approximation is intentional and mass is conserved exactly in integer grams.

Electrolysis does **not** directly yield liquid hydrogen or liquid oxygen. NASA's [Technology Assessment for Producing Propellant From Lunar Water](https://ntrs.nasa.gov/citations/20230005189) discusses separation, purification, gas drying and subsequent liquefaction. Accordingly, a separate `liquefy_propellant()` stage uses the fictional base's powered cryocooler before fuel packaging is possible. Cooling time, plant power, gas drying, heat rejection, storage pressure, boil-off and losses are abstracted.

Each refined-fuel inventory unit is a kit containing **two separate cryogenic compartments**, representing 200 g LH2 and 1,600 g LOX. It is not a premixed storage tank. That 8:1 ratio follows this game's simplified water mass balance; operational engines may use different mixture ratios. The rocket's two-kit requirement is a progression token, not a physically sized Earth-return propulsion budget.

## Metals and upgrades

NASA's [ISRU overview](https://www.nasa.gov/reference/jsc-in-situ-resource-utilization/) covers chemical conversion of regolith, including hydrogen and carbothermal reduction. The game's smelting recipe bundles sorting, reduction, flux and forming: 1 kg iron/nickel ore plus 1 kg regolith produces two 400 g structural plates and 1.2 kg slag. This is an assumed metal recovery, not a measured ore assay. Heating arbitrary regolith alone would not produce pure iron/nickel alloy.

The concentrated solar thermal drill and electrostatic dust treads are fictional upgrades inspired by space resource and dust engineering. Their threefold mining rate and removal of the dust speed penalty are game balance choices. The manufactured scrap and polyethylene are recovered from a lander cache; neither is fabricated from clay or bare regolith by an undocumented recipe.

## Radiation and the ending

NASA Langley's [Radiation Analysis and Shielding Design](https://ddtrb.larc.nasa.gov/radiation/) discusses solar proton events and hydrogen-rich shielding materials including water and polyethylene. This supports the qualitative choice of polyethylene as a radiation-protection upgrade; it does not validate a universal shielding multiplier.

`RadiationStorm` uses a fictional 3,500–12,000 mSv/h dosimeter field. The front traverses the scene in four game seconds, thermal peaks are exaggerated for the dramatic sequence, and actual solid overhead tiles attenuate exposure using a game-selected exponential coefficient. An open shaft provides no overhead protection. Directionality, particle spectrum, secondary radiation, material density and shielding geometry are not transported physically.

Unit-01's narrative dosimeter integrates `mSv += mSv_per_hour * seconds / 3600`. The shield reduces that field by a chosen factor of 0.45. **Sieverts describe biological dose equivalent, not a sufficient metric for electronic failure.** Real hardware assessment would require absorbed dose in materials, particle spectra and single-event effects. Mapping mSv and temperature to robot integrity is explicitly fictional, as is the forced hardware meltdown on manual launch.

The current overhead game has independent vertical motion with constant gameplay gravity of 9.81 world-height units/s² and a 5.2-unit/s jump impulse. This produces a ballistic arc, with normal horizontal movement and physical landings on supporting terrain or low blocks. Those values are intentionally tuned for play and do not represent the asteroid's surface gravity. Northern map cells do not act as overhead radiation cover. The archived side-view physics uses 18 tile/s² gravity and a tuned jump impulse. Tile lengths are not calibrated to meters; neither mode models Phobos surface gravity. Endgame heat spikes and the “Earth Saved” data-core narrative are story elements, not NASA mission predictions.
