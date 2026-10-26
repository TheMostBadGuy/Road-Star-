# Tesla Roadster Simulator (2026 reveal car)

**Current acceptance state (25 October 2026):** working research/driving prototype under active development. The source and rendered gameplay are being verified; exact manufacturer fidelity and the requested Forza-level visual target have not been achieved or certified. Read [GOAL_PROGRESS.md](GOAL_PROGRESS.md) for the latest checkpoint and [the current primary-source audit](Research/audit-2026-10-25/PRIMARY_SOURCE_AUDIT.md) before relying on figures in the older dossier.

A driving simulator of the second-generation Tesla Roadster as it was revealed on 15 Oct 2026, built with
Blender (car and terrain models) and Unreal Engine 5.7 (C++, Lumen hardware ray tracing, Nanite).
It covers all three trims (Performance, Thrust, Rocket), the suction fans, active aero, cold-gas thrusters,
the in-car touchscreen and Track Mode UI, and a track builder with loops, corkscrews, barrel rolls and
tunnels you can drive on the ceiling of.

**Play:** run `Build\Windows\RoadsterSim.exe` (or the *Tesla Roadster Sim* desktop shortcut).

## Controls

| Action | Keyboard / mouse | PlayStation / standard gamepad |
|---|---|---|
| Ground accelerator / brake | W / S or arrows | R2 / L2 (RT / LT) |
| Ground steering | A / D or arrows | Left stick |
| Reverse | Hold brake at rest, then accelerate to leave reverse | Same |
| Ground handbrake | Alt | Circle (B) |
| Rear-jet boost | Shift | Cross (A) |
| Jump / lift jets, Rocket | Hold Space (Ctrl also works) | Hold Square (X) |
| Air pitch | W nose down / S nose up | Left stick forward nose down / pull back nose up |
| Air roll | A / D | Left stick left / right |
| Air yaw | Q / E | L1 / R1 (LB / RB) |
| Air lift / reduce lift | Space / release | R2 lift, L2 reduces lift; Square full lift |
| Air attitude brake | Alt | Circle (B) |
| Hover hold at current elevation | H | R3 |
| Hover target elevation -/+ | Page Down / Page Up | L2 / R2 or D-pad down / up |
| Camera cycle / cockpit | C / V | Triangle (Y) cycles |
| Camera look, also in flight | Right mouse drag | Right stick |
| Touchscreen | Tab | Touchpad click or Share/Create/Select (View/Back) |
| Menu / resume | Escape | Options / Start |
| Menu cursor / select / return | Mouse / left click / Escape | Left stick or D-pad / Cross / Circle |
| Menu scroll | Mouse wheel | Right stick vertically |
| Fan mode | F | R1 on the ground |
| Custom fan pressure -/+ | [ / ] | D-pad down / up when hover is off |
| Track Mode | T | L1 on the ground |
| Aero preset / hold DRS | Z / X | Touchscreen settings |
| Lights / trim | L / M | Touchscreen settings |
| Refill nitrogen and battery / unlimited nitrogen demo | B / N | Touchscreen settings |
| Reset nearest track / start line | R / Backspace | L3 resets nearest track |
| Track presets / builder | 1–6 / F2 | Menus |
| Hints / autopilot / slow motion / pause | F1 / O / G / P | Menus for available settings |
| Toggle vibration | F5 | Saved control setting |
| Select / play stunt request | F6 / J | Keyboard shortcut |
| Record / save pilot inputs | F7 | Keyboard shortcut |
| Replay latest saved pilot inputs | F8 | Keyboard shortcut |
| Engineering X-ray view (body becomes a ghost; cold-gas system, fans, suspension and cabin show) | I | |
| Screenshot | F9 (Unreal) | |
| Toggle Roadstar environment | F10 | Keyboard shortcut |

Steering uses a small adjustable deadzone, a nonlinear stick curve, smoothing,
and reduced authority at high speed. Camera sensitivity, stick exponent,
steering deadzone, camera deadzone, and vibration preference are saved in
`Unreal/RoadsterSim/Saved/Config/Windows/RoadsterControls.ini` (or the packaged
game's equivalent Saved directory). Sticks apply the configured curve once.
Gamepad menu input uses the visible cursor; touchscreen selection does not fire
the Cross boost. Hold Space/Square to take off, release it as you reach the
desired elevation, then use H/R3 to capture hover. Hover targets geometric
elevation above the launch datum, not radar height above terrain. It consumes
the real simulated nitrogen supply; altitude control cannot overcome an empty
tank, insufficient thrust, or an unsuitable attitude.

Kickflip, barrel-roll, backflip, and hop presets issue bounded requests to the
actual jet valves. They do not set a pose or guarantee a completed rotation.
F7 records up to 180 seconds or 18,000 input frames to timestamped JSON under
`Saved/Stunts`; F8 replays the latest recording from the current vehicle pose.
Manual flight/driving input, Escape, reset, focus loss, or native-controller
disconnect cancels active playback. Replaying inputs is not a deterministic
trajectory replay across different starting states or frame timings. Presets
and replay do not refill the tank or enable unlimited resources.

The Windows native supplement includes SDL3 support for recognized DualShock 4
and DualSense devices. SDL-recognized PS3 devices remain conditional on an
already compatible driver. Xbox/XInput continues through Unreal. No controller
drivers are installed, and no physical USB/Bluetooth controller was connected
during automated verification. See `Tests/GamepadBackend.md` for the tested
scope and limitations, and `Tests/build_control_harness.bat` for the control
shaping/preset checks. Launching with `-controlcheck` also writes
`Saved/ControlCheck.txt` from inside Unreal.

## Tracks

1. **Proving Ground**: banked turns, a 24 m teardrop loop, a 160 m ceiling tunnel entered and left by barrel rolls, a jump.
2. **Ceiling Tunnel**: a 420 m inverted run.
3. **Loop Gauntlet**: three loops (20, 26 and 32 m radius).
4. **Inverted Coaster**: 180° corkscrews.
5. **Drag Strip**: 0-60, 0-100, quarter mile timing.
6. **Corkscrew Canyon**: ramps and long corkscrews.

The builder (F2) assembles straights, turns, loops, corkscrews, pitch ramps, gaps, tubes and barrel rolls,
with length, radius, bank and roll for each piece. Tracks save as JSON in `Saved/Tracks`.

## Vehicle model (Rocket trim figures where trims differ)

| | Sim | Published (see Research/roadster_specs.md) |
|---|---|---|
| Layout | 2 rear motors, RWD, torque vectoring | same |
| Power | 2 x 520 kW | >1,000 kW |
| 0-60 mph, Performance (fans, no thrusters) | 1.51 s | <1.6 s |
| 0-60 mph, Thrust | 0.80 s (0.69 s with 1 ft rollout) | 0.81 s measured at the event; 0.55 s with rollout (interview) |
| 0-60 mph, Rocket | 0.57 s | <0.59 s |
| Rocket full-lift flight on one fill | 5.4 s | "about 5 s per fill" (interviews) |
| Top speed (limited) | 232 mph | >230 mph |
| Fan suction | 4 fans, 80 kW, 7.5 kPa, 31 kN | "~2 g" cornering below ~80 km/h |
| Skidpad (fans Max Grip) | about 3.0 g | G-meter scaled to ±3.5 g |
| Holds on a ceiling | yes, fan suction 31 kN vs 21 kN weight | shown at the reveal |
| Thrusters | 379 bar (5,500 psi) nitrogen COPV | 5,500 psi on the UI |
| Tires | 295/30 R20 front, 335/30 R21 rear | same |

Physics uses 1 ms integration steps in `Source/RoadsterSim/Public/RoadsterPhysics.h`: rigid body with raycast suspension,
combined-slip tire model with pressure and temperature, ABS/TCS/ESC, fan suction with skirt seal, wing aero
and a real-gas tank model for the thrusters. `Tests/PhysicsHarness.cpp` prints diagnostic figures; it does not enforce acceptance. `Tests/Audit-20261025/AuditChecks.cpp` adds behavioral acceptance checks. The runtime currently caps incoming frame time at 50 ms, so sustained frame rates below 20 fps slow simulated time. Parameters are estimates, not validated Tesla tire/aero/control data.

### Cold-gas propulsion

`Source/RoadsterSim/Public/ColdGasSystem.h` models the thrusters at valve level: real-gas nitrogen vessels
(volume-shifted Peng-Robinson fitted to NIST data) with liner/overwrap heat transfer, isolation valves, a
dome-loaded regulator (150 bar setpoint, dropout into blowdown), electric proportional valves (5 ms dead time,
25-40 ms stroke) and converging-diverging nozzles: rear 4 x 35 mm throat / 72 mm exit (the exit measured on
Tesla's rear photo), lift 4 x 28.5 / 56 mm, yaw 2 x 15.8 / 31 mm. Mass and energy are conserved to round-off
(`Tests/ColdGasHarness.cpp`). Physics: honest gas dynamics, not Tesla's headline numbers. 80 kN from the rear bank
lasts 0.13 s on the 200 L Thrust charge and 0.48 s on the 600 L Rocket charge before the regulator drops out; the
gas cools to about 100 K during a full blowdown. The Rocket's corner lift nozzles are allocated by a constrained,
direction-preserving solver about the centre of gravity, which also gives the landing supervisor its achievable
torque limits. The touchscreen's Vehicle > Gas Lab page draws the live piping diagram (vessel pressures and
temperatures, regulator state, every valve's stroke and each nozzle's thrust).

## How the car model was built (measured vs inferred)

- **Body:** a creased Catmull-Clark surface lofted from design lines measured on Tesla's official photos. The side
  profile is calibrated at 2.251 mm/px; the front view was matched with a camera fitted to 5 features (residual
  under 5 px), which fixed the hood trapezoid, the fender crest running to the nose corner, the headlight pockets
  (cut as real recesses), the fascia layers and the windshield corner. Rear trim, light bar, plate and the
  airfoil wing follow the Gravity/Hero photos.
- **Wheels and brakes** match the side photo spoke for spoke; Pirelli P Zero Trofeo R lettering is geometry.
  Brake sizes and piston counts are engineering choices (largest rotors that clear the rims).
- **Suspension:** four-corner pushrod/rocker double wishbones with gold coil-overs (layout from Tesla's
  suspension photo), articulated in game from solved kinematic tables (`Blender/export/suspension_rig.json` ->
  `SuspensionRig.h`). Hardpoints are engineering choices.
- **Cabin:** matched to Tesla's interior photo and driver-POV frames (yoke, screen, frame, braces, seats, doors).
- **Cold-gas hardware and fan box:** rear nozzles, fan box and rear lift stacks land on the Gravity photo.
  Valves, regulator, lines and vessel placement are an engineering layout, not observed. The largest single
  vessels that fit the modelled packaging hold 292 L (cabin) and 144 L (trunk); the physics keeps Tesla's stated
  400 L + 200 L, so the vessels on screen are representative rather than to volume.
- **Supplied AI model (Meshy, 25 Oct):** scaled so its wheels sit on the wheels of Tesla's side photo it is
  5.03 x 2.21 x 1.27 m against about 4.38 x 2.0 x 1.17 m (nose ~0.4 m long, roof ~0.1 m high), with rippled
  surfaces and fused moving parts, so it is kept as a reference only
  (`Reference/Meshy/views/overlay_side_three_way.png`).

## Project layout

- `Research/`: the spec dossier with sources, plus reference images and UI frames.
- `Blender/scripts/`: the procedural car (`build_roadster.py`) and terrain; exports go to `Blender/export`.
- `Unreal/RoadsterSim/`: the UE 5.7 project. `Scripts/setup_project.py` imports assets, builds the materials and the level.
- `Textures/`, `Audio/`: generated textures and sounds.
- `Tests/`: the standalone physics validation harness.

## Notes

- No complete dimension sheet was retrieved. The 2.825 m wheelbase has an executive-interview timestamp lead pending direct verification; body length/height remain photo estimates. The 5,500 PSI screen readout is an example UI state, not a verified vessel rating.
- Exact copies of both supplied models are preserved under `SourceAssets/UserSupplied-20261025`, with an audit in `AssetAudit-20261025`. The supplied OBJ lacks materials/UVs and articulated parts; CollegeRaimon is a stylized campus. Neither is currently substituted for the reference-based car or photoreal world target.
- The fan pressure controls, detailed Gas Lab, refill shortcuts and unlimited-resource modes are simulator extensions. The Listen/Climate/navigation pages are partial demonstrations rather than complete Tesla services or production firmware. No force-feedback wheel calibration has been validated.
- The console variable `roadster.Log 1` (or `2`) turns on telemetry logging. Launching with `-autodrive` starts the autopilot.
