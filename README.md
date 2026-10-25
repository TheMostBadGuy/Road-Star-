# Tesla Roadster Simulator (2026 reveal car)

A driving simulator of the second-generation Tesla Roadster as it was revealed on 15 Oct 2026, built with
Blender (car and terrain models) and Unreal Engine 5.7 (C++, Lumen hardware ray tracing, Nanite).
It covers all three trims (Performance, Thrust, Rocket), the suction fans, active aero, cold-gas thrusters,
the in-car touchscreen and Track Mode UI, and a track builder with loops, corkscrews, barrel rolls and
tunnels you can drive on the ceiling of.

**Play:** run `Build\Windows\RoadsterSim.exe` (or the *Tesla Roadster Sim* desktop shortcut).

## Controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Accelerate / brake | W / S (or arrows) | RT / LT |
| Steer | A / D | Left stick |
| Reverse (Tesla style) | Hold brake at a standstill, then accelerate | same |
| Handbrake | Space | B |
| BOOST (rear thrusters) | Shift | A |
| Lift (corner thrusters, Rocket) | Ctrl | X |
| Yaw nozzles | Q / E | D-pad left / right |
| In the air: pitch / roll | W, S / A, D | sticks |
| Fan mode (Off, Braking, Low Drag, Max Grip, Custom) | F | RB |
| Underbody pressure -/+ (custom fan) | [ / ] | D-pad down / up |
| Aero preset (Auto, Low Drag, High Downforce, Custom) | Z | |
| DRS (wing flat) | X (hold) | |
| Track Mode (ESC and TCS off, lap timer) | T | LB |
| Trim (Performance, Thrust, Rocket) | M | |
| Refill nitrogen and battery | B | |
| Unlimited nitrogen (demo) | N | |
| Camera: cycle / cockpit | C / V | Y |
| Look around | Right mouse drag | Right stick |
| Touchscreen (mouse driven) | Tab | View / Back |
| Hide hints | F1 | |
| Reset to track / to start line | R / Backspace | L3 |
| Track presets | 1 to 6 | |
| Track builder | F2 | |
| Autopilot demo lap | O | |
| Slow motion / pause | G / P | |
| Menu | Esc | Start |

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
| 0-60 mph, Thrust (1 ft rollout) | 0.66 s (0.56 s) | 0.81 s measured, 0.55 s with rollout |
| 0-60 mph, Rocket | 0.59 s | <0.59 s |
| Top speed (limited) | 232 mph | >230 mph |
| Fan suction | 4 fans, 80 kW, 7.5 kPa, 31 kN | "~2 g" cornering below ~80 km/h |
| Skidpad (fans Max Grip) | about 3.0 g | G-meter scaled to ±3.5 g |
| Holds on a ceiling | yes, fan suction 31 kN vs 21 kN weight | shown at the reveal |
| Thrusters | 379 bar (5,500 psi) nitrogen COPV | 5,500 psi on the UI |
| Tires | 295/30 R20 front, 335/30 R21 rear | same |

Physics runs at 1 kHz in `Source/RoadsterSim/Public/RoadsterPhysics.h`: rigid body with raycast suspension,
combined-slip tire model with pressure and temperature, ABS/TCS/ESC, fan suction with skirt seal, wing aero
and a real-gas tank model for the thrusters. `Tests/PhysicsHarness.cpp` checks the figures in the table above.

## Project layout

- `Research/`: the spec dossier with sources, plus reference images and UI frames.
- `Blender/scripts/`: the procedural car (`build_roadster.py`) and terrain; exports go to `Blender/export`.
- `Unreal/RoadsterSim/`: the UE 5.7 project. `Scripts/setup_project.py` imports assets, builds the materials and the level.
- `Textures/`, `Audio/`: generated textures and sounds.
- `Tests/`: the standalone physics validation harness.

## Notes

- Tesla published no dimension sheet. The body was measured from Tesla's official photos (wheelbase 2.825 m).
  Figures that executives gave inconsistently are flagged in the dossier.
- The console variable `roadster.Log 1` (or `2`) turns on telemetry logging. Launching with `-autodrive` starts the autopilot.
