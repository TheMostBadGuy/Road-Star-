# Gas experiments: validation record, 2026-10-25

This change adds pure nitrogen, helium and argon as coherent thermodynamic choices to the existing cold-gas system. It does not establish Tesla's proprietary vessel, valve, nozzle, thrust, safety or durability specification. All original nitrogen hardware assumptions are retained.

## Configuration API

`GasSpecies` is an enum with `Nitrogen`, `Helium`, `Argon`. `Config::Propellant` selects a gas at construction and defaults to nitrogen. Each system owns its choice; immutable nozzle tables are cached separately for each species.

`bool System::ConfigureExperiment(GasSpecies species, double pressureBar, double temperatureK = kAmbientK)` validates and resets/refills the installed vessels, walls and manifold, preserving armed state. It resets time, commands and bookkeeping. Treat it as a pit experiment, not an inflight gas swap. It rejects nonfinite inputs, unknown species, fills below 1.01325 bar, fills above the lower of 690 bar and the installed vessel MEOP, and temperatures outside 200–350 K. Rejection does not change the current system.

`bool System::FillByMassKg(double tankMassKg, double temperatureK = kAmbientK)` is an alternate way to specify tank inventory. It computes density from total vessel volume, then derives common pressure from the selected EOS and calls `ConfigureExperiment`. It excludes the feed manifold and rejects nonpositive inventory or any resulting fill outside the pressure/rating envelope. Mass, pressure, temperature and fixed volume cannot all be changed independently. For example, selecting half the original tank mass at the same temperature derives a lower pressure. It does not pretend that an empty tank can retain 379 bar.

`Propellant()`, `PropellantProperties()`, `FillPressureBar()` and `MaxFillPressureBar()` expose the selected gas and actual experiment inputs. The properties contain name, molar mass, gas constant, critical/triple data, acentric factor and ideal cp/gamma. The existing `Fill(bar,K)` remains available for backward compatibility. New controls should use the validated API.

No dry-air pseudo-fluid is exposed: proper mixture composition, saturation and possible fractionation deserve explicit mixture thermodynamics.

## Physics and primary sources

- Nitrogen uses the exact existing translated Peng–Robinson code, ideal vibrational term, saturation ancillaries, transport fits and numerical constants. The legacy `Eos::` namespace and constants retain their nitrogen meaning. New code dispatches through a per-system `Thermodynamics` object. The side-by-side regression compares every step to `ColdGasSystem.before.h`.
- Helium uses the 23 residual terms, analytic derivatives, ideal monatomic cp = 2.5 R and gamma = 5/3 from [Ortiz Vega et al., NIST IR 8474 (2023)](https://doi.org/10.6028/NIST.IR.8474). The implementation follows the report's prescribed fitted molar R = 8.314472 J/(mol K), M = 0.004002602 kg/mol, Tc = 5.1953 K, pc = 0.22832 MPa and reducing density = 17383.7 mol/m³. The report's printed Table 3 check numbers differ roughly 1.2 ppm from the explicitly prescribed R convention; assertions state a 2 ppm tolerance. This small convention difference is also visible against WebBook, not hidden by fitted offsets.
- Argon uses the 41 residual terms of [Tegeler, Span and Wagner, JPCRD 28 (1999), 779–850](https://doi.org/10.1063/1.556037), with fitted molar R = 8.31451 J/(mol K), M = 0.039948 kg/mol, Tc = 150.687 K, pc = 4.863 MPa and reducing density = 535.6 kg/m³. Ideal cp = 2.5 R and gamma = 5/3. A [NIST-hosted copy](https://www.nist.gov/system/files/documents/srd/jpcrd561.pdf) is saved alongside this record.
- Coefficient transcription was cross-checked against the official [CoolProp helium](https://github.com/CoolProp/CoolProp/blob/master/dev/fluids/Helium.json) and [argon](https://github.com/CoolProp/CoolProp/blob/master/dev/fluids/Argon.json) data. The downloaded JSON snapshots and MIT license are retained. Argon uses those EOS-derived saturation ancillary fits, with their stated maximum discrepancies of about 0.008% pressure, 0.235% liquid density and 0.213% vapor density.
- Pressure, energy, enthalpy, entropy, heat capacities, sound and Joule–Thomson coefficient derive from one Helmholtz potential for each added gas. Tank and manifold conservation laws are unchanged. The real-gas nozzle table is generated with the selected EOS and R. Monatomic subsonic nozzle formulas and exit Mach use gamma = 5/3. IEC restriction sizing uses the ideal specific-heat factor F_gamma = gamma/1.4 for the added gases, following the manufacturer's [Control Valve Handbook](https://www.emerson.com/en/final-control/catalog/products-and-software/valves/control-valves/control-valve-handbook).
- Nitrogen's literal nozzle arithmetic is preserved to avoid even small numerical changes to old runs. Helium's table spans stagnation temperatures 20–500 K so its cold blowdown and negative-JT warming are not clamped onto the nitrogen grid. Argon retains a vapor-side stagnation floor and an equilibrium tank condensation model.

## Practical limits and confidence

The added gases use published reference EOS, rather than a speed or thrust multiplier. Validation of their numerical implementation is separate from validation of the simulator's assumed hardware. Saved WebBook tables at 200, 300 and 350 K include pressures 1, 101, 201, 301, 401, 501, 601 and 701 bar (phase-label duplicate rows are retained). Density, cv/cp and sound comparisons are within about 2 ppm across these checks; enthalpy *differences* are compared because absolute reference offsets are arbitrary. This is agreement between formulations/data outputs, not a claim of ppm physical accuracy.

The NIST helium report explicitly calls its formulation preliminary and does not make definitive overall uncertainty claims. Argon's paper range extends to 700 K and 1000 MPa, and helium's report range to 1500 K and 2000 MPa, but **the simulator's experiment inputs are deliberately much narrower**. A few grid checks do not validate all of either reference EOS's range or the complete coupled blowdown system.

Helium and argon heat-transfer coefficients currently use a dilute transport approximation anchored to 300 K NIST values and T^0.70 scaling: He viscosity 19.9 microPa s, conductivity 0.1559 W/(m K); Ar 22.8 microPa s and 0.0178 W/(m K). Dense transport corrections are omitted. Sources: [NIST helium table](https://www.nist.gov/pml/sensor-science/fluid-metrology/database-thermophysical-properties-gases-used-semiconductor-9), [NIST argon table](https://www.nist.gov/pml/sensor-science/fluid-metrology/database-thermophysical-properties-gases-used-semiconductor-2). This limits warm-up and wall-coupled long-duration predictions even though the EOS and bookkeeping are consistent.

The existing frozen/metastable nozzle treatment, ideal continuation at a spinodal, linearized valve JT temperature change, table interpolation, line/valve fits and Summerfield separation approximation remain. There is no solid deposition, ice particle, helium liquid/superfluid, mixture, permeation/leak, structural requalification or combustion model. Argon supersaturation below its triple point is only a qualitative indicator. No chemical energy is created. Helium exhaust velocity is higher, but equal-pressure helium mass and total available impulse are lower than nitrogen in these fixed vessels.

For a vacuum/flight scene, ambient pressure can be zero. Tank-wall `ambientK` describes the equipment compartment, not the temperature of deep space; the existing outer convection model is not a spacecraft radiative thermal model. Configuration changes should occur as a new experiment/refill, rather than magically changing gas identity inside a sealed live tank.

## Reproduction and evidence

Run `run_checks.bat` for the staged candidate, or `run_live_checks.bat` for the installed header plus the original nitrogen diagnostic. The scripts use the existing MSVC 18 Community compiler and place all temporary/build output on E:. No installation is needed. `GasChecks.cpp` compares legacy and current nitrogen in the same process, checks EOS identities and inverse state solves, tests input rejection without mutation and mass/pressure roundtrips at both rating boundaries, exercises vacuum expansion at 200/350 K, and runs six 5-second finite-supply blowdowns across both installed vessel configurations.

`build_candidate.py` generates only the candidate header from the saved before-copy and source snapshots. It never overwrites the live header. `baseline_output.log` is the untouched legacy diagnostic. `checks_output.log` and `live_output.log` contain assertion results and representative fill/Isp/impulse data. `source_hashes.txt` records SHA-256 hashes of retained source material and installed/candidate headers. Passing these numerical checks is not hands-on game control, visual, real-time-performance or hardware verification.

Installed-header result: **227/227 assertions pass**, with clean MSVC `/W4` compilation. Default nitrogen's side-by-side thrust, mass and impulse drift is exactly zero in the 2.5-second schedule. The original diagnostic also passes. Full-output comparison (excluding the first table-build timing line and normalizing only measured microsecond execution-cost values) is recorded separately; all reported physics values are identical. Across the six standard bursts, mass-balance residuals are below 1e-9 kg, and first-law residuals are below 0.01 J.

Representative 200 L / 379 bar / 293.15 K results, after 0.5 seconds of manifold charging and then five seconds of full rear command:

| Gas | Initial tank mass | Initial sea-level Isp estimate | Integrated impulse | Residual tank mass |
| --- | ---: | ---: | ---: | ---: |
| Nitrogen | 71.053 kg | 62.537 s | 29.369 kN s | 7.022 kg |
| Helium | 10.574 kg | 162.031 s | 11.928 kN s | 0.860 kg |
| Argon | 118.915 kg | 46.386 s | 33.361 kN s | 17.165 kg |

These specific values depend on the assumed regulator, nozzle, thermal and cutoff models, as well as the gas. Initial Isp is not the average Isp over the cooling blowdown.

`run_timing.bat` measures standalone wall-clock runtime, not game frame rate. First lazy table builds took approximately 0.08 s for nitrogen, 3.2 s for helium and 3.7 s for argon on this machine; later reconfiguration is below 0.2 ms. A loading flow can warm the two additional tables with `IsentropeTable::Get(GasSpecies::Helium)` and `IsentropeTable::Get(GasSpecies::Argon)` to avoid a first-selection pause. One simulated second of boost took approximately 0.019, 0.050 and 0.117 wall seconds respectively in the optimized single-process harness. These are local measurements, not a promised rendering budget.
