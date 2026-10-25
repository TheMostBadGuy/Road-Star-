# Numerical physics audit — 25 October 2026

This audit validates local model behavior. It does not validate Tesla's proprietary engineering, production capabilities, or the Unreal game's collision/rendering performance.

## Executed

- MSVC 18 Community x64, `/O2 /std:c++17 /EHsc /W3`.
- `run_existing.bat`: compiles and runs the existing PhysicsHarness and ColdGasHarness, exit 0. The original harnesses print diagnostics and return 0 without assertions; their exit codes alone are not acceptance evidence.
- `run_acceptance.bat`: compiles and runs 19 behavioral acceptance checks, **19 PASS / 0 FAIL**, exit 0 (after the footprint improvement below).
- All new binaries, object files, temporary files, and logs stay in this audit directory on E:.
- No unrelated existing dirty files changed.

## Defects repaired

`Unreal/RoadsterSim/Source/RoadsterSim/Public/RoadsterPhysics.h` is the only simulator source edited by this audit. The original header is retained as `RoadsterPhysics.before.h` (SHA256 `23F9A3475673DA15988F5065E0197B18858D5F764FFD658F6D13989D627759EA`).

1. An empty finite battery previously maintained full fan suction indefinitely: 31,273.846 N after two seconds with zero battery energy. The fan target now drops to zero and the rotor spins down through its existing response. The acceptance case now reads 0.001746 N after two seconds. Unlimited battery demo mode still produces 31,273.846 N.
2. At standstill, the motor power limiter skipped torque scaling when mechanical power was near zero. With no battery power it could inject repeated low-speed torque. The limiter now zeros rear drive torque when available electrical power is zero. The acceptance test samples every step from standstill: maximum requested rear torque is zero. Unlimited battery mode still produces drive torque.

These are behavior corrections within the existing approximate model. Rotor coast-down is a first-order response, not a separate rotational-energy storage model.

## Footprint seal improvement

The underfloor seal now samples the center and four corners of an explicitly estimated 2.70 x 1.56 m footprint. Its 4.212 m2 rectangle is consistent with the existing estimated 4.2 m2 suction area; it is not a reconstruction of Tesla's actual floor cavities.

Each probe checks finite distance, the 0.60 m maximum trace reach, and a surface normal within 60 degrees of the local floor normal. It cannot seal against a reversed face or an unrelated perpendicular wall. A missing probe contributes a 0.12 m equivalent open-edge leak gap. Valid gaps retain the existing skirt deployment and residual gap calculation. A shared-plenum estimate averages gap-squared leakage before calculating common pressure; pressure therefore falls across the whole plenum when part of the floor loses coverage.

New acceptance results at identical initial fan/vehicle states:

- Full plane: 31,273.846 N suction.
- Narrow patch that intersects only the center probe: 4.329% of full-plane suction.
- Partial floor with the two front corners unsupported: 8.299% of full-plane suction.
- All returned normals reversed: zero suction.
- Returned hits beyond the finite fan reach: zero suction.
- All previous ceiling, battery, propulsion, conservation and timestep checks still pass. Existing full-plane diagnostic values remain unchanged at the printed precision.

The pre-footprint header is retained as `RoadsterPhysics.before_footprint.h`. Final header SHA256 after this pass: `F34EDD71659C76BE9891B37F42E837B671FB67C0A730CCB09B5722754D6A874F`.

## Observed model results after repairs

| Case | Result |
|---|---:|
| Performance 0–60 mph, fans Auto | 1.51 s; 1-foot rollout 1.33 s |
| Performance 0–60 mph, fans off | 2.62 s |
| Thrust 0–60 mph + boost | 0.80 s; rollout 0.69 s |
| Rocket 0–60 mph + boost | 0.57 s; rollout 0.45 s |
| Limited top speed, Performance | 232.1 mph |
| Unlimited top speed, Performance | 255.3 mph |
| 60–0 braking, fan off / braking | 96.4 ft / 57.4 ft |
| Peak skidpad lateral acceleration, fans Max Grip | 2.87–3.01 g |
| Rocket ceiling contact with fans | Four contacts, z=6.000 m for ten seconds |
| Ceiling contact without fans | Lost; falls under gravity |
| Rocket full lift case | 28.9 m peak height, 5.4 s airborne |
| Inverted drop from 8 m with assist | Upright, four contacts at four seconds |

These are numerical outputs using estimated inputs and tuned controls, not independent measured performance data. Source claims and matching model output are separate evidence categories.

## Cold-gas checks

- Repeat Thrust burst is bit-identical in this build.
- Maximum printed frame-integrated mass discrepancy across full-burst cases: approximately 1.6e-12 kg.
- Printed first-law residuals are approximately 3e-14 to 1e-13 of the model's recorded energy throughput.
- At 379 bar fill: Thrust rear bank falls below 90% of 80 kN at 0.136 s; Rocket at 0.503 s. Impulses are 29.38 and 89.03 kN s.
- Thrust-matched hover, local assumed hardware: 3.19 s at 379 bar and 4.30 s at 690 bar.
- Nitrogen EOS comparison against the existing stored NIST table: worst density discrepancy 1.95%, enthalpy discrepancy 2.36 kJ/kg over the printed 200–350 K cases. NIST was not independently refetched in this bounded audit.
- Conservation residuals assess internal bookkeeping; they do not establish the correctness of nozzle, tank, valve, thermal-transfer, or vehicle parameters.

## Acceptance coverage

The new executable checks ceiling support versus fan-off falling, loss of suction with no surface, partial footprint coverage, surface normal and reach validity, trim-specific thrusters, disarming, gas consumption and conservation, refill pressure, timestep convergence, and finite versus unlimited battery fan/motor behavior. The half-timestep acceleration comparison differs by 0.000318 m/s after three seconds.

## Material limits and outstanding checks

- The supplied chassis dimensions, mass, inertia, suspension rates, tire friction, battery capacity, drag/downforce coefficients, suction pressure and effective area, pressure-vessel volumes, valve dynamics, nozzle sizing and hardware masses remain a mixture of photo-derived values and engineering assumptions. No factory CAD, tire force curves, wind-tunnel data or validated Tesla control maps are present.
- The five-point fan seal improves track-edge and seam behavior but remains an estimated reduced model. It does not solve underfloor airflow, independent pressure chambers, flexible skirt deformation or continuously integrate pressure over the footprint; holes between the five sample points can remain undetected. The flat ceiling test does not establish correct behavior on curved/segmented tunnels, which the parent agent is testing in Unreal.
- The Unreal pawn uses 1 ms substeps but adds `min(DeltaSeconds, 0.05)` to its accumulator. Sustained rendering below 20 fps therefore advances less than real elapsed time. Stalls are intentionally discarded; this is not a guaranteed real-time 1 kHz physics rate.
- Tests use analytic infinite planes. Actual Unreal procedural-mesh normals, triangle seams, self intersections, high-speed tunneling, loop entry/exit, corkscrew transitions and tunnel ceilings still need runtime inspection.
- No vehicle structural deformation, occupant biomechanics or factory-calibrated force-feedback steering model was validated.
- The new header changes require an Unreal rebuild/package before they reach a standalone executable.

## Files

- `AuditChecks.cpp`, `run_acceptance.bat`, `acceptance_build.log`, `acceptance_output.log`.
- `run_existing.bat`, `physics_build.log`, `physics_output.log`, `coldgas_build.log`, `coldgas_output.log`.
- `RoadsterPhysics.before.h`: recoverable pre-edit header.
