# Roadstar Earth environment: bounded handoff

2026-10-25. New engine-independent C++17 module:
`E:\TeslaRoadsterSim\Unreal\RoadsterSim\Source\RoadsterSim\Public\PlanetEnvironment.h`.
This subtask adds the module and this test folder; it does not edit the vehicle physics, pawn, controller or UI. The earlier 80-mesh Blender source handoff remains unchanged.

## API and frame

```cpp
namespace Earth {
    inline constexpr double MeanRadiusM = 6371000.0;
    inline constexpr double MuM3PerS2 = 3.986004418e14;
    inline constexpr double AtmosphereReferenceCeilingM = 1000000.0;
    struct Vec3 { double X, Y, Z; /* arithmetic operators */ };
    double Dot(Vec3 const&, Vec3 const&);
    Vec3 Cross(Vec3 const&, Vec3 const&);
    double Length(Vec3 const&);
    Vec3 EarthCenteredPosition(Vec3 const& position);
    double Altitude(Vec3 const& position);
    struct AtmosphereSample {
        double AltitudeM, TemperatureK, PressurePa, DensityKgM3;
    };
    AtmosphereSample Sample(Vec3 const& position);
    AtmosphereSample SampleAltitude(double geometricAltitudeM);
    Vec3 Gravity(Vec3 const& position);
    struct OrbitSample {
        bool Valid, Bound, Impacting;
        double RadiusM, AltitudeM;
        double SpecificEnergyJPerKg, SpecificAngularMomentumM2PerS;
        double Eccentricity, PeriapsisAltitudeM, ApoapsisAltitudeM;
        double CircularSpeedMps, EscapeSpeedMps;
    };
    OrbitSample OrbitalElements(Vec3 const& position, Vec3 const& velocity);
}
```

Positions use a **nonrotating Earth-centred Cartesian frame translated to the launch point**. The launch surface is `(0,0,0)` and the centre is `(0,0,-6371000)` metres. Initial +Z is up. Position and velocity remain in these fixed axes as the vehicle travels around the planet; the axes do not follow the local tangent. Position vectors relative to the centre are `(X,Y,Z+R)`. Altitude is their magnitude minus R, including when far from the initial vertical axis.

No Earth rotation, Coriolis/centrifugal acceleration, rotating-atmosphere velocity, weather, terrain elevation, oblateness, other-body gravity, or ephemerides are included. Atmospheric wind is initially zero in this nonrotating frame. The caller supplies terrain/contact and uses density with velocity relative to its chosen air-velocity model. A vehicle drag implementation using a fixed drag coefficient remains an aerodynamic approximation, especially in rarefied gas.

Gravity is the central acceleration `-mu*r/|r|^3`. Specific energy is `|v|^2/2-mu/|r|`; specific angular momentum is `|r cross v|`. The eccentricity vector is `(v cross h)/mu-r/|r|`. Periapsis and apoapsis are derived from this osculating conic. `Bound` means negative specific energy. `Impacting` means the osculating conic's periapsis radius intersects the model Earth; it does not independently establish time to a future collision. An unbound conic has infinite apoapsis altitude. At the point-mass singularity gravity returns zero and orbital elements are invalid; the point-mass interior is not a solid-Earth gravity model.

## Atmosphere source and approximation

Primary source: NOAA/NASA/USAF, [U.S. Standard Atmosphere, 1976, NASA-TM-X-74335 / NOAA-S/T-76-1562](https://ntrs.nasa.gov/citations/19770009539), [official PDF](https://ntrs.nasa.gov/api/citations/19770009539/downloads/19770009539.pdf).

The official 243-page PDF is saved here as `US_Standard_Atmosphere_1976_NASA.pdf`.
SHA256: `23EB6B68346D4ABC6071FC598872D51C40CF8C613D1A5D1006C6261BCD09DCEA`.
The scan's extracted OCR is unreliable; all implemented upper table rows and formula pages were visually reviewed from the locally rendered page images.

- Through 86 km geometric altitude, hydrostatic pressure follows equations18,23,33, using geopotential height `6356766*z/(6356766+z)`, sea-level pressure101325 Pa, temperature288.15 K, g0=9.80665 m/s^2, molar mass28.9644 kg/kmol and universal gas constant8314.32 J/(kmol K). The seven temperature lapse layers start at geopotential0,11,20,32,47,51,71 km. Density uses molecular-scale temperature and the standard molar mass.
- Between80 and86 km the returned kinetic temperature includes the molecular-weight correction in Table8. Source formula/correction images: `formula-024.png` through `formula-028.png` (PDF24-28, printed pp.8-12). The atmosphere's effective radius/g0 conventions are distinct from the requested spherical orbital R/mu assumptions.
- Above86 km, kinetic temperature follows the published isothermal, elliptical, linear and asymptotic equations25,27,29,31. It is not linearly interpolated across the sharp thermospheric rise.
- Pressure and density above86 km use35 sparse knots from TableI printed pp.68-73, PDF83-88, in `upper-083.png` through `upper-088.png`. Pressure is converted from millibars to Pa by multiplying by100. Pressure and density are interpolated in log space. The rounded86 km pressure/density anchor is bridged to the exact lower-layer limit to prevent a small artificial jump.
- This sparse upper model is an approximation between knots. Independent held-out rows at240 and380 km show maximum pressure/density errors of2.8991% and0.8071%. These are spot checks, not a universal interpolation-error bound. Thermospheric density also varies with actual solar and geomagnetic conditions; the standard profile is a reference state.
- Above1000 km, pressure/density continue the950-1000 km logarithmic slopes and temperature approaches1000 K. This continuation is explicitly unvalidated extrapolation, not an extension of the reference table. No abrupt vacuum cutoff is imposed. Below ground, thermodynamic values clamp to sea level while `AltitudeM` retains the negative radial altitude.

Independent lower checks use geometric-altitude TableI rows: 11 km from PDF74/printed59,20 km from PDF76/printed61,50 km from PDF80/printed65, and86 km from PDF83/printed68. The saved `lower-*` and `upper-*` page images identify their column headings and units.

## Test results

Run `build_and_run.bat` from PowerShell or a Windows command prompt. It initializes the installed Visual Studio18 MSVC environment, compiles with C++17, optimization and `/W4`, then runs the standalone test executable. No Unreal types or libraries are linked. Test stdout is preserved in `test_run.log`; machine-readable results are in `test_results.json`.

**61/61 checks pass, with no compiler warnings.** The suite verifies sea-level/11/20/50/86 km reference values, upper100/120/200/400/1000 km reference values, held-out interpolation samples, positive finite monotonic pressure/density, all hydrostatic joins, radial altitude and inverse-square direction/magnitude, circular/elliptical/suborbital/radial/escape trajectories, and one-orbit energy/angular-momentum conservation.

The coast scenario is explicitly initialized at400 km geometric altitude with inertial tangential velocity7672.59864838501 m/s. Its two-body period is5544.85509598079 s. It uses independent fourth-order Runge-Kutta integration of the module's gravity over2048 equal steps, with no thrust, drag, orbit constraint or altitude reset:

| Metric | Observed | Acceptance threshold |
| --- | ---: | ---: |
| Maximum relative specific-energy drift | 5.61773e-14 | 2e-10 |
| Maximum relative angular-momentum drift | 2.80886e-14 | 1e-10 |
| Position closure after one orbit | 0.0000881747 m | 0.01 m |
| Velocity closure after one orbit | 9.99145e-8 m/s | 1e-5 m/s |
| Position-error reduction128 to256 steps | 17.9633 times | greater than10 times |

Trajectory samples are in `coast_orbit.csv`; standard-atmosphere/gravity samples through1500 km are in `environment_samples.csv`. A coasting orbit is an initialized demonstration scenario. It does not imply that the stock vehicle can attain orbital speed with its finite compressed-gas supply. Runtime vehicle integration, force application, views, actual controls and performance still require the parent engine verification.

Header SHA256 at handoff: `01A89CAF1498F5BB68CDF284E2FF63701FE856530E6C3E9FB36D9BAB47F1E20F`.
