# Wiring ColdGasSystem.h into RoadsterPhysics.h

`Unreal/RoadsterSim/Source/RoadsterSim/Public/ColdGasSystem.h` replaces the old blowdown block in `Vehicle::Step` (lines 686-757). In the old block, thrust was proportional to tank pressure, came on and off instantly, and mass flow was F/700. The new model works at valve level: real-gas nitrogen vessels, an isolation valve, a dome-loaded regulator, a manifold, electric valves and CD nozzles. The flight-assist attitude controller stays exactly as it is. Only the actuator changes: the controller now requests thrust per nozzle, the cold-gas model decides what the hardware actually delivers, and the vehicle applies that delivered thrust.

Validation and all numbers: `Tests/ColdGasHarness.cpp`. Build and run it with:

```
cmd /c "call \"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat\" >nul 2>&1 && cd /d E:\TeslaRoadsterSim\Tests && cl /nologo /O2 /std:c++17 /EHsc ColdGasHarness.cpp /Fe:ColdGasHarness.exe"
E:\TeslaRoadsterSim\Tests\ColdGasHarness.exe
```

## 1. Include and member

Top of `RoadsterPhysics.h`, with the other includes:

```cpp
#include "ColdGasSystem.h"
```

In `class Vehicle`, next to `double TankKg = 0;`:

```cpp
ColdGas::System Gas;    // valve-level cold-gas system (copyable; the isentrope table is a shared static)
```

`ColdGas::System` is self-contained and copyable, so `Vehicle v = MakeCar(...)` in the harnesses keeps working. On first construction in a process, it builds a real-gas nozzle table once, which takes about 75 ms. To keep that off the first frame, call `(void)Roadster::ColdGas::IsentropeTable::Get();` at module start-up.

## 2. Init, refill, pressure, mass

In `Vehicle::Init()`, replace `TankKg = S.FullTankKg();` with:

```cpp
const bool fitted = S.TankVolumeL > 0;
Gas = ColdGas::System(fitted ? S.TankVolumeL : 200.0);   // 200 L -> Thrust, 600 L -> Rocket (200 + 400 on a common header)
Gas.SetArmed(fitted && Set.bThrustersArmed);
Gas.Fill(S.TankFullBar, S.GasTempK);                     // armed fill: lines charged to lock-up, "Boost Ready" at 5,497 psi
TankKg = fitted ? Gas.GasMassKg() + Gas.ManifoldMassKg() : 0.0;
```

Replace `RefillTank()` and `TankBar()` with:

```cpp
void RefillTank()
{
    Gas.SetArmed(S.TankVolumeL > 0 && Set.bThrustersArmed);
    Gas.Fill(S.TankFullBar, S.GasTempK);
    TankKg = S.TankVolumeL > 0 ? Gas.GasMassKg() + Gas.ManifoldMassKg() : 0.0;
}
double TankBar() const { return S.TankVolumeL > 0 ? Gas.Tel().DisplayBar : 0.0; }
```

`TotalMass()` needs no change. It already uses `TankKg`, which the thruster block now refreshes every step from the gas model (vessels plus manifold).

`Spec::Z()`, `GasKgAt()`, `FullTankKg()` and `ExhaustVelocity` are no longer used. Delete them or leave them. Note that the old `Z = 1 + 0.0008 p` gives 66.9 kg in 200 L at 379 bar, while NIST gives 71.1 kg, so it is 6 % low. `RearThrustFullN`, `LiftThrustFullN` and `YawThrustFullN` remain only as normalisers for the plume telemetry (0..1).

## 3. Replace the thruster block (RoadsterPhysics.h, "cold-gas thrusters", lines 686-757)

Everything from `// ---- cold-gas thrusters (blowdown: ...)` to the closing brace before `// ---- powertrain torque request` becomes:

```cpp
// ---- cold-gas thrusters: valve-level model (ColdGasSystem.h)
{
    const bool fitted = S.TankVolumeL > 0;
    Gas.SetArmed(fitted && Set.bThrustersArmed);
    const double auth = Clamp(Set.BoostLevel, 0.25, 1.0);   // "Valve authority": caps every thrust request

    // rear bank: BOOST asks for a fraction of rated thrust; the model gives what the manifold can deliver
    for (int k = 0; k < 4; ++k)
        Gas.CommandThrust(k, S.bHasRearThrusters ? Clamp(in.Boost, 0, 1) * auth * Gas.Cfg().Nozzle[k].RatedThrustN : 0.0);

    // corner lift + front yaw pair (Rocket): the attitude controller is unchanged; allocation is capped per nozzle
    // by what that nozzle can deliver right now (falls as the vessels blow down)
    double corner[4] = { 0, 0, 0, 0 }, yawL = 0, yawR = 0;
    if (S.bHasLiftThrusters && Gas.Tel().bArmed)
    {
        const double base = Clamp(in.Lift, 0, 1);
        V3 tauB;
        const bool assist = Set.bFlightAssist && (airborne || base > 0.05);
        const V3 upB = Q.Unrotate(V3(0, 0, 1));
        if (assist)
        {
            const double pitchCmd = Clamp(in.Pitch, -1, 1) * 0.5, rollCmd = Clamp(in.Roll, -1, 1) * 0.6;
            V3 want = (upB + V3(-std::sin(pitchCmd), std::sin(rollCmd), 0) * 1.0).Norm();
            const V3 err = V3(0, 0, 1).Cross(want);
            tauB = V3(err.x * 70000.0 - wBody.x * 9000.0, err.y * 70000.0 - wBody.y * 22000.0, 0);
        }
        else if (airborne)
            tauB = V3(in.Roll * 18000.0, -in.Pitch * 30000.0, 0);
        const double X0 = S.Wheelbase * 0.5, Y0 = 0.62;
        const double a = tauB.x / (4 * Y0 * Y0), b = -tauB.y / (4 * X0 * X0);
        double cap[4], capMean = 0;
        for (int k = 0; k < 4; ++k) { cap[k] = Gas.MaxThrustN(ColdGas::LiftFL + k) * auth; capMean += 0.25 * cap[k]; }
        for (int k = 0; k < 4; ++k)
        {
            const double xk = (k < 2) ? X0 : -X0, yk = (k % 2) ? Y0 : -Y0;
            corner[k] = Clamp(base * capMean + a * yk + b * xk, 0, cap[k]);
        }
        if (base < 0.02) for (double& c : corner) if (c < 0.05 * capMean) c = 0;   // deadband on pure trim, saves gas
        const double yawCmd = Clamp(in.Yaw, -1, 1) + (airborne && assist ? Clamp(-wBody.z * 0.8, -1, 1) : 0.0);
        if (yawCmd > 0.02) yawR = yawCmd * Gas.MaxThrustN(ColdGas::YawLeftFiring) * auth;
        if (yawCmd < -0.02) yawL = -yawCmd * Gas.MaxThrustN(ColdGas::YawRightFiring) * auth;
    }
    for (int k = 0; k < 4; ++k) Gas.CommandThrust(ColdGas::LiftFL + k, corner[k]);
    Gas.CommandThrust(ColdGas::YawLeftFiring, yawR);    // nozzle 8: jet exits to the left -> nose pushed right
    Gas.CommandThrust(ColdGas::YawRightFiring, yawL);   // nozzle 9: jet exits to the right -> nose pushed left

    if (Set.bInfiniteGas && Gas.Tel().GasFraction < 0.995) Gas.TopUp();   // demo mode (books stay balanced)
    Gas.Step(h, ColdGas::kPatm, S.AmbientC + 273.15);                     // sub-steps internally; deterministic

    // apply the thrust actually delivered (averaged over the internal sub-steps) at the nozzle exits
    double rear = 0, lift = 0;
    for (int k = 0; k < 4; ++k)
    {
        const double F = Gas.Nozzle(k).ThrustN;
        if (F > 0) addForceAt(fwd * F, BodyToWorld({ -S.Wheelbase * 0.5 - S.RearOverhang, -0.33 + 0.22 * k, 0.36 }));
        rear += F;
    }
    for (int k = 0; k < 4; ++k)
    {
        const double F = Gas.Nozzle(ColdGas::LiftFL + k).ThrustN;
        T.ThrustCorner[k] = F / S.LiftThrustFullN;
        if (F <= 0) continue;
        const double xk = (k < 2) ? S.Wheelbase * 0.5 + 0.15 : -S.Wheelbase * 0.5 + 0.25, yk = (k % 2) ? 0.78 : -0.78;
        addForceAt(up * F, BodyToWorld({ xk, yk, 0.12 }));
        lift += F;
    }
    const V3 nose = BodyToWorld({ S.Wheelbase * 0.5 + S.FrontOverhang - 0.1, 0, 0.35 });
    const double FyR = Gas.Nozzle(ColdGas::YawLeftFiring).ThrustN, FyL = Gas.Nozzle(ColdGas::YawRightFiring).ThrustN;
    if (FyR > 0) addForceAt(right * FyR, nose);
    if (FyL > 0) addForceAt(-right * FyL, nose);

    TankKg = fitted ? Gas.GasMassKg() + Gas.ManifoldMassKg() : 0.0;
    T.ThrustRear = rear / S.RearThrustFullN;
    T.ThrustYawR = FyR / S.YawThrustFullN; T.ThrustYawL = FyL / S.YawThrustFullN;
    T.ThrustN = rear + lift + FyR + FyL;
    T.TankBar = fitted ? Gas.Tel().DisplayBar : 0.0;   // volume-weighted vessel pressure -> "Thruster  xxxx PSI"
    T.TankKg = TankKg;
    T.TankPercent = fitted ? Gas.Tel().GasFraction : 0.0;   // usable gas (above the 10 bar residual) vs a full fill
}
```

Notes on the block:

- **Allocation.** The allocation math is the old one. The only change is that `cap` now comes per corner from `Gas.MaxThrustN()`, not from `LiftThrustFullN * pFrac`. This keeps the controller's differential thrust achievable as the vessels blow down. It is the full-open thrust at the *present* manifold pressure, so it slightly overstates what a nozzle gets once the regulator droops under load. The flight assist closes that loop on its own the next frame.
- **Commands.** `CommandThrust(i, N)` inverts the valve, line and throat model with a few model evaluations. Thrust against valve stroke is strongly nonlinear: 50 % stroke gives 92 % thrust on the rear nozzle. So always command thrust, not stroke. `Command(i, stroke)` is still available.
- **Response.** There is one step of command-to-force latency plus the valve dynamics: 5 ms dead time, about 25 ms to reach 90 % on the rear valves, and 3 ms / ~10 ms on the yaw valves. That is the real behaviour of electric valves. Instant thrust is gone.
- **BOOST.** BOOST is now limited by the gas, not by a pressure ratio. The rear bank holds about 78-80 kN only until the regulator drops out: 0.13 s on Thrust, 0.48 s on Rocket at 379 bar. It then blows down (table in section 7).

## 4. Settings mapping

| Setting | Wire to |
|---|---|
| `bThrustersArmed` | `Gas.SetArmed()` every step (done in the block). Arming opens the motorised isolation valve(s) over 0.25 s and soft-starts the regulator dome at 300 bar/s, so the manifold reaches lock-up (153 bar) in about 0.6 s. Disarming vents the dome, which shuts the regulator. |
| `BoostLevel` ("Valve authority" 25-100 %) | Scales every thrust request (done in the block). |
| `bInfiniteGas` | `Gas.TopUp()` when the gas fraction falls (done in the block). Restores the vessels and walls without venting the lines; the conservation counters book it as an inflow. |
| Pit refill button / B key | `Vehicle::RefillTank()` (section 2). If armed, the car is "Boost Ready" at 5,497 psi immediately. |
| Optional: regulator setpoint | `Gas.SetSetpointBar(bar)`, default 150 bar. A lower setpoint means less thrust but a longer regulated phase. |
| Optional: valve mode | `Gas.SetValveMode(ColdGas::ValveMode::PWM)`. Proportional is the default; see section 8. |

## 5. UI telemetry ("Thruster · 5,500 PSI · 100 %")

The existing UI code works unchanged through `T.TankBar` and `T.TankPercent`:

- `RoadsterInterface.cpp:509` (track-mode bar): `T.TankBar * kPsiPerBar` reads 5,497 psi after a fill, as in Tesla's UI.
- `RoadsterInterface.cpp:510`: `T.TankPercent` is now the usable-gas fraction. "Boost Ready" above 98 %.
- `RoadsterHUD.cpp:126-128`: same fields.

Expect one realistic change: after a burst, the PSI reading climbs back up. Gas blown down to ~8 bar at ~100 K warms through the liner and reaches 22 bar at 1 min, 28.6 bar at 10 min and 29.4 bar at 1 h (harness section (e)). The percentage barely moves, because it tracks mass, not pressure.

Replace the three lines on the Thrusters page that still use the old model (`RoadsterInterface.cpp:462-466`):

```cpp
const auto& G = Pawn->Sim.Gas;
UI.Text(FString::Printf(TEXT("Impulse left %.0f kN·s"), G.RemainingImpulseNs() / 1000.0), LX + LW - 24, Y + 440, 24, Red, EAlign::Right);
UI.Text(FString::Printf(TEXT("Rear thrust available  %.0f kN"), 4 * G.MaxThrustN(0) * Set.BoostLevel / 1000.0), LX + 24, Y + 520, 24, Red);
if (S.bHasLiftThrusters)
{
    double Lift = 0; for (int32 k = 4; k < 8; ++k) Lift += G.MaxThrustN(k);
    UI.Text(FString::Printf(TEXT("Lift (4 corners)  %.0f kN vs weight %.0f kN"), Lift * Set.BoostLevel / 1000.0, Pawn->Sim.TotalMass() * kG / 1000.0), LX + 24, Y + 570, 24, Red);
}
```

Optional extra readouts. All of these are in `Gas.Tel()` (`ColdGas::Telemetry`) and `Gas.Nozzle(i)` (`ColdGas::NozzleOutput`):

```cpp
const ColdGas::Telemetry& GT = Pawn->Sim.Gas.Tel();
// regulator / manifold
GT.ManifoldBar, GT.RegOutletK, GT.RegOpening, GT.bRegDropout    // "REG DROPOUT - BLOWDOWN"
// per vessel (GT.NumTanks = 1 or 2)
GT.Tank[t].PressureBar, GT.Tank[t].TempK, GT.Tank[t].MassKg, GT.Tank[t].LinerMinK,
GT.Tank[t].bLinerBelowRating                                   // "TANK LINER COLD" (< -40 C rating)
GT.Tank[t].bTwoPhase                                           // "N2 CONDENSING" (vessel reached the dew line)
GT.Tank[t].bCutoff                                             // "ISOLATED - RESIDUAL" (10 bar Type-IV floor)
// budget
GT.TotalImpulseNs, GT.BankImpulseNs[ColdGas::BankRear], Pawn->Sim.Gas.DeltaVBudget(Pawn->Sim.TotalMass())
```

## 6. Plume visuals (RoadsterPawn.cpp:541-544)

Per-nozzle outputs are better than the bank averages:

```cpp
const auto& G = Sim.Gas;
for (int32 k = 0; k < 4; ++k) Plume(PlumesRear[k], G.Nozzle(k).ThrustFrac, 2.6f, 0.30f);
for (int32 k = 0; k < 4; ++k) Plume(PlumesLift[k], G.Nozzle(4 + k).ThrustFrac, 2.2f, 0.28f);
Plume(PlumesYaw[0], G.Nozzle(8).ThrustFrac, 1.2f, 0.18f);   // left-firing jet
Plume(PlumesYaw[1], G.Nozzle(9).ThrustFrac, 1.2f, 0.18f);
// fog / frost: exit static temperature is 40-100 K and the jet is supersaturated -> white condensation plume
const ColdGas::NozzleOutput& N = G.Nozzle(0);
const float Fog = N.MassFlowKgS > 0 ? FMath::Clamp(float(FMath::Loge(FMath::Max(1.0, N.Supersaturation)) / 4.0), 0.f, 1.f) : 0.f;
if (PlumeMID) { PlumeMID->SetScalarParameterValue(TEXT("Fog"), Fog); PlumeMID->SetScalarParameterValue(TEXT("Speed"), float(N.GasExitVelocity)); }
```

Useful per-nozzle fields: `PlumeTempK` (exit static temperature), `GasExitVelocity` (about 550-620 m/s when fresh), `ExitMach` (about 2.9), `ExitPressurePa`, `bSeparated` (overexpanded with flow separation at the end of a blowdown, which gives a ragged, short plume), `Valve` (stroke, for animating a pintle).

Nozzle geometry for the 3D artist:

| Nozzle | Throat | Exit (inner) | Area ratio | Feed line |
|---|---|---|---|---|
| Rear (4) | 35.0 mm | 72 mm (as modelled) | 4.23 | 60 mm |
| Lift (4) | 28.5 mm | 56 mm | 3.86 | 50 mm |
| Yaw (2) | 15.8 mm | 31 mm | 3.85 | 30 mm |

All are 15° conical divergent sections. The 72 mm rear exit matches Tesla's photo; the model uses exactly that.

## 7. What changes in Tests/PhysicsHarness.cpp

Expect these numbers to move. They are the honest result, not a regression.

| Burst (rear bank, 100 %, sea level) | Plateau | Regulated | Thrust < 8 kN | Impulse | Mean Isp |
|---|---|---|---|---|---|
| Thrust 200 L @ 379 bar | 77.9 kN | 0.13 s | 0.81 s | 29.4 kN·s | 46.8 s |
| Rocket 600 L @ 379 bar | 79.5 kN | 0.48 s | 2.27 s | 89.0 kN·s | 47.7 s |
| Thrust @ 690 bar (rated vessel) | 79.5 kN | 0.24 s | 0.95 s | 39.0 kN·s | 43.6 s |
| Rocket @ 690 bar | 80.2 kN | 0.80 s | 2.77 s | 117.5 kN·s | 44.0 s |

The old model gave 66.9 kg × 700 m/s = 46.8 kN·s for the Thrust trim, and its impulse fell only by exponential decay.

Effects on the existing tests:

- **Drag runs ("Thrust + BOOST", "Rocket + BOOST").** These get the full 80 kN only for the first 0.13 s (Thrust) or 0.48 s (Rocket), then less. Expect 0-60 mph times to lengthen somewhat. The 25 ms valve opening also delays thrust onset.
- **"Rocket full lift" test.** It loops `while (v.TankKg > 0.01)`. Vessels now stop at the 10 bar residual, so the loop runs to its 20 s cap. Change the condition to `v.T.TankPercent > 0.001`.
- **Hover.** Thrust-matched hover (`ColdGasHarness` section (c)) lasts 3.2 s at 379 bar and 4.3 s at 690 bar. It ends when the vessels fall to about 57 bar and the four fully open lift valves can no longer carry the car's weight. About 40 % of the gas is then still on board.

## 8. Valve choice: why proportional is the default

Tesla's wording is "all-electric actuators … micro adjustments within a fraction of a second".

- **Size.** Each rear valve passes about 33 kg/s of 150 bar nitrogen through a ~60 mm port. Fast solenoid valves do not exist at that size. Pilot-operated ones are on/off and take 30-100 ms.
- **"All-electric".** This contrasts with the pneumatic or hydraulic actuation normally used for valves this big. The natural reading is a motor-driven (EMA) poppet or pintle under position control: Tesla's own competence (motors and drive-by-wire), proportional by nature.
- **"Micro adjustments within a fraction of a second".** That describes continuous throttling with a ~20-40 ms stroke. It does not describe bang-bang pulsing.

The harness backs this up:

- **20 ms command pulse.** A rear proportional valve delivers 99 % of the ideal square-pulse impulse (400 N·s).
- **Minimum impulse bit.** The controller stretches commands to 10 ms, giving 131 N·s rear and 26 N·s yaw. The yaw figure is a 1.3 °/s yaw-rate quantum on the car.
- **50 % PWM.** At 10 Hz it gives 61 % of full thrust (55 % at 20 Hz), with 0-20 kN ripple and a duty-to-thrust map that depends on frequency. That is usable, but cruder.

PWM remains available. With PWM, the nozzle runs at its design pressure during each pulse instead of throttled, which saves about 10 % Isp at low thrust. The cost is ripple.

## 9. Cost, determinism, threading

Measured with the harness (`/O2`, MSVC, one core), per 1 ms vehicle step:

| Situation | Cost |
|---|---|
| Full rear burst | 19-34 µs |
| Flight assist trimming 4 lift nozzles | 23-27 µs |
| Armed and parked | 0.6-5 µs |

That is 0.1-3 % of a 1 ms frame. Internal sub-steps are 0.25 ms while gas moves and 20 ms when everything is shut.

The model is fully deterministic: a repeated run is bit-identical, and nothing depends on wall-clock time or randomness.

Mass and energy are conserved to round-off: ~1e-13 relative over every scenario, checked by `MassBalanceErrorKg()` and `EnergyBalanceErrorJ()`.

`IsentropeTable::Get()` is a C++11 thread-safe function-local static. A `System` itself is not shared between threads.

## 10. Findings to resolve in Spec (not changed here)

- **TrimExtraMassKg** (Thrust 75 kg, Rocket 210 kg) cannot hold a 690 bar-rated Type-IV vessel set. The modelled walls alone are 96 kg (200 L) and 187 kg (400 L), consistent with Toyota-Mirai-class tank mass efficiency. Add valves, regulator, lines and nozzles (~30-60 kg) on top. Rated for 379 bar only, the vessels would be roughly half as heavy.
- **ExhaustVelocity 700 m/s (Isp 71 s)** is the loss-free vacuum value. At sea level, from these nozzles, fresh gas gives 61-62 s. Over a full blowdown the mean is 44-48 s, because the gas cools to about 100 K, the nitrogen dew line, by the end.
- **Liner temperature.** The liner minimum falls below the -40 °C Type-IV rating after a 690 bar Rocket burst (-51 °C), and the gas itself reaches the dew line in every full blowdown. The model handles both (two-phase vessel state, telemetry flags). Tesla would need a warmer-rated liner, a larger residual pressure, or a duty-cycle limit.
