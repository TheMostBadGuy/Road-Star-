# Roadster source audit — 25 October 2026

This is an additive audit of the pre-existing `roadster_specs.md` and `reference_images.md`. Those files and their downloaded media have been preserved. This document establishes evidence strength; it does not certify that a simulation matches an actual Tesla prototype or a final production car.

## Verified current source state

[Tesla's current product page](https://www.tesla.com/roadster?trk=public_post_comment-text) is indexed with the redesigned October 2026 vehicle. Its body text was readable in the search index. Direct web extraction on 25 October returned only the footnotes, and a normal HTTP fetch returned 403. Record this limitation rather than claiming a complete live HTML archive.

Tesla describes Performance, Thrust, and Rocket. Performance has two seats and fan-assisted suction; Thrust adds rear cold-gas acceleration; Rocket has one seat and perimeter thrusters for translation and rotation. The page describes steer-by-wire, a fixed seat, a central touchscreen and two forward displays. It describes stationary inverted adhesion through fans and ground sealing, electrically actuated cold-gas control, an Ultrawhite/microsuede cabin, and magnesium, titanium and carbon materials. Its footnotes restrict certain capabilities to Rocket and state the page generally reflects optional Thrust equipment. Treat these as manufacturer descriptions, with trim-specific caveats.

[Tesla's Q3 2026 filing](https://ir.tesla.com/_flysystem/s3/sec/000162828026066738/tsla-20261021-gen.pdf), accessed 25 October, independently establishes that Tesla unveiled the next-generation car on 15 October and lists Roadster under design development. It does not supply a detailed vehicle engineering specification. Printed update pages 11–12 contain official Roadster photographs (PDF pages 13–14, zero-based).

## Primary interview and numeric specifications

[Top Gear's first-hand visit](https://www.topgear.com/car-news/electric/tesla-roadster-has-finally-landed-thrusters-0-60mph-059s-and-flight), published 16 October, was retrieved in full. It reports two rear motors, rear-wheel drive, a 310–320 mile target, sub-1,700 kg lightest model, and approximately 500 kW charging. Its trim ladder gives roughly 800 kW base, 1 MW Performance, a 200 litre rear vessel for Thrust, and an additional 400 litres plus six more nozzles for Rocket. It reports short-duration boost, not sustained flight. These are executive/preview claims, not independent measurements or final homologated specifications. The reported Rocket acceleration target is 0.59 s to 60 mph; rollout, configuration, and test conditions must stay attached to any comparison.

The associated [49-minute interview video](https://www.youtube.com/watch?v=JdkIOhFmbKI) is the right primary reference for unresolved details. The video link was resolved from Top Gear's own embed. The direct video page exposed no transcript through the available web reader. Secondary transcript summaries point to wheelbase at 16:30–16:36 and width at 34:50–34:54. Therefore **2,825 mm wheelbase and approximately 2 m width are executive-reported values awaiting direct timestamp verification**, rather than factory dimension drawings. This improves the provenance trail in the older dossier, which attributed them only to Wikipedia. No official length, height, axle tracks, center of gravity, or CAD drawings were located in this audit.

Do not combine the 2017 red 2+2 concept's 200 kWh/620-mile/tri-motor AWD targets with the October 2026 car. A commercially labeled “Roadster 2025” specification sheet is not evidence of the redesigned Rocket variant.

## Cockpit display audit

Primary source link: [Tesla UI video, 23 October](https://x.com/Tesla/status/2113730362232680854). Direct retrieval returned 403. The pre-existing local extracted frames were visually inspected, not redownloaded. Their provenance is the prior `reference_images.md` and is not a cryptographically verified capture of that post. Preserve the distinction.

The inspected `ui_t18_trackmode_setup_custom.jpg` shows Auto, Low Drag, High Downforce and Custom presets; Front Wing; Rear Wing; Aero Load; Fan; Hood Vent; Barge Boards; Aero Balance; Brake Balance; ESC; and TCS. Fan choices read Off, Braking, Low Drag and Max Grip. The bottom bar reads **Thruster · 5,500 PSI** and **100% · Boost Ready**. The dashboard frame shows brake/acceleration/regen, component temperatures, tires, G-meter, track map, sector/lap information, dynamics/aero state and fan state.

**5,500 PSI is a displayed example.** It does not establish maximum vessel pressure, regulator pressure, refill target, safe operating limit, or a pressure-control affordance. Similarly the visible 28 PSI tire readings do not establish an optimum, recommended cold pressure, tire model curve, or allowed limits. An extra pressure slider or engineering panel in this simulator is a simulator extension unless Tesla's interface is shown supporting it. Keep the main replica screen faithful and disclose extensions in an engineering page.

Screen hardware dimensions, native pixel resolution, production firmware, exact interaction timing, fonts and all screen states remain unknown. Do not describe the UI as pixel-exact. A screenshot used as a passive texture is insufficient for a working telemetry screen.

## Audit of uncertain physical parameters

The unitless official `Roadster-05-Graph-Desktop.jpg` was inspected. It illustrates fans producing low-speed downforce and a tire-load limit; it supplies no axis numbers. It cannot calibrate absolute downforce or crossover speed.

The existing code's fan pressure, effective sealed area, leakage law, seal reach, duty mapping, tire pressure optimum and thermal/friction curves are simulation parameters. The manufacturer's stationary inversion claim constrains the minimum force; it does not uniquely determine these values. A quoted 4,700 kg-force reveal readout or 20 kW/fan claim should be labeled a reported prototype figure until the primary event/interview timestamp is directly checked. Kilograms in a downforce graphic are force-equivalent mass: convert to newtons before integrating motion.

The previously saved dossier's ideal-gas back-of-envelope at 690 bar must not be treated as an exact nitrogen fill mass. Real-gas compressibility, temperature, reserve pressure, nozzle expansion, transient cooling and regulators materially affect usable impulse. An approximately 10,000 PSI vessel claim and a 5,500 PSI UI readout can refer to different conditions; neither proves the other false.

The general [NASA thrust equation](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/rocket-thrust-equation/) and [specific impulse definition](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/specific-impulse/) were retrieved. Use `F = mass_flow * exhaust_velocity + exit_area * (exit_pressure - ambient_pressure)` and integrate force over time. These equations do not provide Tesla's mass flow, nozzle dimensions, efficiency, tank thermal model or control logic. A five-second duration claim must not silently become five seconds at peak thrust.

## Implementation and validation implications

1. Expose separate manufacturer claims, executive reports, geometric estimates, and simulator calibration values in a machine-readable profile. Exact unknowns are `null`, not invented facts.
2. Keep global gravity active during ceiling driving. Apply suction toward the contacted surface only while skirts can seal. Require positive tire normal load and friction limits. Turning fans off on a stationary ceiling must let the car fall.
3. Conserve gas mass and energy, keep rear/lift/yaw gas budgets explicit, and disable thrust when pressure or gas is exhausted. Recharge should be a pit action with any accelerated refill clearly marked as a convenience.
4. Model fan electrical power and resulting state of charge. Free-air fan operation must not create adhesion to a distant ceiling. Respect curved-surface seal loss.
5. Compare orthographic front/side/rear renders against preserved official photos, with wheelbase as a stated scale reference. A uniformly scaled generic coupe cannot establish correct geometry, cockpit, wheel placement, body panel shapes or Rocket tank layout.
6. Test flat acceleration, braking, stationary inversion, fan-off detachment, wall transition, loop/tunnel traversal, gas depletion, camera orientation and cockpit input interaction. Retain telemetry and screen captures for each. Physics unit tests alone do not show actual driving or visual quality.
7. No source supplies private tire data, aero maps, structural CAD, steering feedback calibration or final production software. Call the result a source-grounded simulation with estimates until those gaps are closed. Visual superiority to Forza requires user/reviewer comparison of representative gameplay; engine settings or a single beauty render cannot establish it.

## Saved reference map

- Exterior primary imagery: existing `Research/images/tesla_official_*.jpg`; URLs are in `Research/reference_images.md`.
- Interior geometry: `tesla_official_Roadster-03-Carousel-Slide-4-Desktop.jpg`.
- Side proportion reference: `tesla_official_Roadster-02-Pilot-Desktop.jpg`.
- Active rear hardware: `tesla_official_Roadster-06-Gravity-Desktop.jpg` and `07-Augments`.
- Cockpit telemetry: `Research/images/ui_frames/ui_t20_trackmode_dashboard.jpg`.
- Cockpit setup: `Research/images/ui_frames/ui_t18_trackmode_setup_custom.jpg`.
- No new remote media was downloaded in this audit.

All accessed online sources above were checked on 2026-10-25, America/New_York client date. Local pre-existing references are evidence of available files and visual content, with prior provenance stated separately. The companion JSON is a provenance registry, not an authoritative production vehicle configuration.
