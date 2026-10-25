# Roadster reference and layout overview

Prepared 25 October 2026. Open [the interactive local guide](ROADSTER_LAYOUT.html) for official image references, component diagrams, cockpit frames, evidence filters and validation links. The guide loads no online fonts, scripts, analytics or remote media. External source links are optional.

The October 2026 reveal vehicle is the relevant reference. Keep the 2017 red concept's specifications separate. [Tesla's Q3 filing](https://ir.tesla.com/_flysystem/s3/sec/000162828026066738/tsla-20261021-gen.pdf) confirms the 15 October reveal and lists Roadster under design development. Final production engineering is not established by the preview.

## Evidence categories

| Category | How to read it |
|---|---|
| Manufacturer statement | A stated capability or visible source UI state; not independent performance validation |
| Executive / preview claim | Interview or firsthand preview report; conditions and configuration matter |
| Photo estimate | Scale/proportion inference with perspective uncertainty |
| Authored simulation measurement | A fact about this project's geometry or model, not the real car |
| Unknown | Manufacturer value stays `null`; simulation assumptions remain separate |

The [machine-readable registry](audit-2026-10-25/specification_evidence.json) records sources, access limits and unknown values. Read [the detailed primary-source audit](audit-2026-10-25/PRIMARY_SOURCE_AUDIT.md) before using older entries in `roadster_specs.md`.

## Layout and dimensions

| Item | Current evidence |
|---|---|
| Wheelbase | Executive-reported 2,825 mm; direct Top Gear video timestamp verification pending, approximately 16:30–16:36 |
| Body width | Executive-reported approximately 2,000 mm; timestamp verification pending, approximately 34:50–34:54 |
| Overall length | Photo estimate approximately 4,380 ±80 mm; manufacturer value `null` |
| Overall height | Photo estimate approximately 1,170 ±40 mm; manufacturer value `null` |
| Blender body bounds | 4.3347 × 1.9860 × 1.0351 m; the last value is a mesh extent, not ground-to-roof height |
| Blender screen surface | 0.332 × 0.207 m; actual Tesla display size and resolution unknown |

Authored dimensions come from [the Blender geometry record](../Verification-20261025/authored_car_geometry.json). Separate wheels, wing and other components can extend beyond the body bounds. Diagram shapes in the guide are explanatory schematics, not Tesla CAD.

The [firsthand Top Gear preview](https://www.topgear.com/car-news/electric/tesla-roadster-has-finally-landed-thrusters-0-60mph-059s-and-flight) reports twin rear motors, rear-wheel drive, a roughly 1 MW Performance model and short-duration cold-gas boost. Its packaging description gives a 200 L rear vessel and an additional 400 L passenger-position vessel for Rocket. These claims do not determine actual tank wall thickness, mounts, plumbing or final Rocket mass.

## Cockpit and pressure

Preserved official cabin photography and frames attributed to [Tesla's 23 October UI video](https://x.com/Tesla/status/2113730362232680854) are linked in the guide. The frames show Track Dashboard and Setup, including fan modes Off, Braking, Low Drag and Max Grip. A visible **5,500 PSI** thruster readout is an example UI state, not a storage-pressure rating. Likewise **28 PSI** tire readings do not establish recommended cold pressure or optimum hot pressure.

The simulator's engineering pressure controls, Gas Lab, refill shortcuts and unlimited resources are project extensions. Screen hardware dimensions, full interaction behavior and production firmware remain unknown.

## Current simulator functionality

| Function | Implementation and evidence boundary |
|---|---|
| Driving and cameras | Implemented keyboard/gamepad controls; inspect [runtime log](../Verification-20261025/EditorFinal/Exterior/runtime.log) and [control register](../README.md) |
| Fan and active aero | Modes, suction, wing presets and DRS implemented; seal/force maps are approximations |
| Pressure and thrusters | Tire controls and finite cold-gas system implemented; [gas diagnostics](../Tests/Audit-20261025/coldgas_output.log) assess model bookkeeping |
| Trim switching | Implemented; [acceptance checks](../Tests/Audit-20261025/acceptance_output.log) include trim-specific gas restrictions |
| Track builder | Pieces and JSON save/load implemented; follow [current checkpoint](../GOAL_PROGRESS.md) for actual runtime/save validation |
| Inverted/tunnel tracks | Implemented; inspect [tunnel capture](../Verification-20261025/TunnelDrive/shot_03.png) and [runtime log](../Verification-20261025/track-verification-runtime-2.log) |
| Listen / Climate / Navigate | Partial demonstration screens, not complete Tesla services |
| Force-feedback wheel | No validated wheel configuration or factory steering-feedback calibration |

The [numerical audit](../Tests/Audit-20261025/AUDIT.md) records **19 passing behavioral checks**, including ceiling contact versus fan-off falling, finite battery behavior, gas consumption/conservation and timestep comparison. These checks use analytic surfaces and do not establish every Unreal track transition or real Tesla accuracy. Runtime frame time is capped at 50 ms; sustained rendering below 20 fps can slow simulated time.

Review [the saved exterior](../Verification-20261025/EditorFinal/Exterior/shot_00.png) and [cockpit](../Verification-20261025/EditorFinal/Cockpit/shot_00.png) captures against the source photographs. Packaging completion, exact manufacturer fidelity and the user's visual benchmark are not claimed by this guide. Factory CAD, tire force curves, aerodynamic maps and final production control/software data remain absent.
