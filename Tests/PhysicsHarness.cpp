// Standalone validation of the Roadster dynamics core against the figures Tesla and its engineers gave.
// Build + run: Tests/build_harness.bat
#include "../Unreal/RoadsterSim/Source/RoadsterSim/Public/RoadsterPhysics.h"
#include <cstdio>
#include <vector>

using namespace Roadster;

struct Plane { V3 P, N; };

static TraceFn MakeTrace(std::vector<Plane> planes)
{
	return [planes](const V3& s, const V3& d, double len, double r) -> Hit
	{
		Hit best; double bestT = 1e30;
		for (const Plane& pl : planes)
		{
			const double ds = (s - pl.P).Dot(pl.N);
			const double dn = d.Dot(pl.N);
			if (ds < 0) continue;
			double t;
			if (ds <= r) t = 0;
			else if (dn < -1e-9) t = (ds - r) / -dn;
			else continue;
			if (t > len || t >= bestT) continue;
			bestT = t;
			best.bHit = true; best.CenterDist = t;
			best.Point = s + d * t - pl.N * r;
			best.Dist = (best.Point - s).Dot(d);
			best.Normal = pl.N;
		}
		return best;
	};
}

static const TraceFn Flat = MakeTrace({ { V3(0,0,0), V3(0,0,1) } });

static Vehicle MakeCar(Trim trim)
{
	Vehicle v; v.Init(trim);
	v.Place(V3(0, 0, 0), Quat());
	Inputs in; in.Brake = 0.2;
	for (int i = 0; i < 2000; ++i) v.Step(0.001, in, Flat);
	return v;
}

static void DragRun(const char* name, Trim trim, AeroPreset aero, bool boost)
{
	Vehicle v = MakeCar(trim);
	v.Set.Aero = aero;
	for (auto& w : v.Wheels) { w.SurfaceTempC = 75; w.CarcassTempC = 55; }   // warm tires, as for a timed run
	Inputs in; in.Brake = 1.0; in.Throttle = 1.0;   // launch control staging, fans spooling
	for (int i = 0; i < 1500; ++i) v.Step(0.001, in, Flat);
	in.Brake = 0; if (boost) in.Boost = 1.0;
	double t = 0;
	while (t < 30.0 && v.T.TQuarter < 0) { v.Step(0.001, in, Flat); t += 0.001; }
	std::printf("%-30s 0-60 %5.2fs (1ft rollout %5.2fs) | 0-100mph %5.2fs | 1/4mi %5.2fs @ %5.1f mph | peak %.2f g | tank %3.0f%%\n",
		name, v.T.T60, v.T.T60Rollout, v.T.T100mph, v.T.TQuarter, v.T.QuarterTrapMph, v.T.PeakG, v.T.TankPercent * 100);
}

int main()
{
	// ---------- static
	for (Trim tr : { Trim::Performance, Trim::Thrust, Trim::Rocket })
	{
		Vehicle v = MakeCar(tr);
		v.Set.Fan = FanMode::Off; v.Set.Aero = AeroPreset::Custom;
		Inputs in; for (int i = 0; i < 3000; ++i) v.Step(0.001, in, Flat);
		const double sum = v.Wheels[0].Load + v.Wheels[1].Load + v.Wheels[2].Load + v.Wheels[3].Load;
		std::printf("%-12s mass %6.0f kg | wheel loads F %5.0f R %5.0f N (sum %6.0f vs weight %6.0f) | front %.1f%% | body z %+.4f | tank %5.1f kg @ %.0f bar (%.0f psi)\n",
			TrimName(tr), v.TotalMass(), v.Wheels[0].Load, v.Wheels[2].Load, sum, v.TotalMass() * kG, 100 * (v.Wheels[0].Load + v.Wheels[1].Load) / sum,
			v.BodyOriginWorld().z, v.TankKg, v.TankBar(), v.TankBar() * kPsiPerBar);
	}

	// ---------- acceleration (targets: Performance <1.6 s, Thrust ~0.81 s / <1 s, Rocket <0.59 s)
	DragRun("Performance (fans auto)", Trim::Performance, AeroPreset::Auto, false);
	DragRun("Performance (fans off)", Trim::Performance, AeroPreset::LowDrag, false);
	DragRun("Thrust + BOOST", Trim::Thrust, AeroPreset::Auto, true);
	DragRun("Rocket + BOOST", Trim::Rocket, AeroPreset::Auto, true);

	// ---------- top speed (target >230 mph limiter; drag-limited beyond)
	for (int unr = 0; unr < 2; ++unr)
	{
		Vehicle v = MakeCar(Trim::Performance); v.Set.Aero = AeroPreset::Auto; v.Set.bUnrestrictedTopSpeed = unr;
		Inputs in; in.Throttle = 1;
		for (int i = 0; i < 120000; ++i) v.Step(0.001, in, Flat);
		std::printf("Top speed %s: %.1f mph (%.0f km/h), motor %.0f rpm, elec %.0f kW, drag %.0f N, wing %.2f\n",
			unr ? "(unrestricted)" : "(limiter)   ", v.T.Speed / kMph, v.T.Speed * 3.6, v.T.MotorRPM, v.T.PowerKW, v.T.DragN, v.T.WingAngle);
	}

	// ---------- braking 60-0
	for (int fanOn = 0; fanOn < 2; ++fanOn)
	{
		Vehicle v = MakeCar(Trim::Performance); v.Set.Aero = AeroPreset::Custom; v.Set.Fan = fanOn ? FanMode::Braking : FanMode::Off;
		v.Place(v.BodyOriginWorld(), v.Q, 60 * kMph);
		Inputs in; in.Brake = 1;
		const V3 start = v.X; double t = 0;
		do { v.Step(0.001, in, Flat); t += 0.001; } while (v.V.Len() > 0.05 && t < 10);
		std::printf("Braking 60-0 (fan %-7s): %5.1f ft in %.2f s\n", FanModeName(v.Set.Fan), (v.X - start).Len() / 0.3048, t);
	}

	// ---------- skidpad (target: >2 g at low speed with fans, Moravy)
	for (FanMode fm : { FanMode::Off, FanMode::MaxGrip })
	{
		for (double target : { 15.0, 25.0, 45.0 })
		{
			Vehicle v = MakeCar(Trim::Performance); v.Set.Aero = AeroPreset::Custom; v.Set.Fan = fm; v.Set.bESC = false; v.Set.bTCS = true;
			v.Place(v.BodyOriginWorld(), v.Q, target);
			double maxLat = 0; Inputs in;
			for (int i = 0; i < 14000; ++i)
			{
				in.Throttle = Clamp((target - v.T.ForwardSpeed) * 0.4 + 0.2, 0, 1);
				in.Steer = std::min(1.0, i / 12000.0);
				v.Step(0.001, in, Flat);
				if (i > 500 && v.T.WheelsInContact == 4) maxLat = std::max(maxLat, std::fabs(v.T.AccelG.y));
			}
			std::printf("Skidpad @ %2.0f m/s (fan %-8s): peak lateral %.2f g\n", target, FanModeName(fm), maxLat);
		}
	}

	// ---------- ceiling: car parked upside down on a ceiling at z = 6
	{
		const TraceFn cave = MakeTrace({ { V3(0,0,6), V3(0,0,-1) } });
		for (Trim tr : { Trim::Performance, Trim::Rocket })
		{
			for (FanMode fm : { FanMode::Off, FanMode::MaxGrip })
			{
				Vehicle v; v.Init(tr); v.Set.Aero = AeroPreset::Custom; v.Set.Fan = fm;
				v.Place(V3(0, 0, 6), Quat::AxisAngle(V3(1, 0, 0), kPi));
				v.FanRPM = fm == FanMode::MaxGrip ? v.S.FanMaxRPM : 0; v.SkirtDeploy = 1;   // arrived via the tunnel wall
				Inputs in; double minZ = 1e9;
				for (int i = 0; i < 5000; ++i) { v.Step(0.001, in, cave); minZ = std::min(minZ, v.BodyOriginWorld().z); }
				in.Throttle = 0.5;
				for (int i = 0; i < 5000; ++i) { v.Step(0.001, in, cave); minZ = std::min(minZ, v.BodyOriginWorld().z); }
				std::printf("Ceiling %-11s fan %-8s: z after 10 s %.3f (min %.3f), speed %4.1f m/s, suction %.2f kPa (%.0f N) vs weight %.0f N, seal %.2f, contacts %d\n",
					TrimName(tr), FanModeName(fm), v.BodyOriginWorld().z, minZ, v.T.Speed, v.T.FanPressureKPa, v.T.FanDownforceN, v.TotalMass() * kG, v.T.FanSeal, v.T.WheelsInContact);
			}
		}
	}

	// ---------- Rocket hover
	{
		Vehicle v = MakeCar(Trim::Rocket); v.Set.Aero = AeroPreset::Custom; v.Set.Fan = FanMode::Off;
		Inputs in; in.Lift = 1.0; double t = 0, maxZ = 0, airT = 0;
		while (t < 20 && v.T.TankPercent > 0.001) { v.Step(0.001, in, Flat); t += 0.001; maxZ = std::max(maxZ, v.BodyOriginWorld().z); if (v.T.WheelsInContact == 0) airT += 0.001; }
		std::printf("Rocket full lift: max height %.1f m, airborne %.1f s, gas used in %.1f s, roll/pitch error at end %.1f deg\n",
			maxZ, airT, t, std::acos(Clamp(v.Q.Up().z, -1, 1)) * 180 / kPi);
	}

	// ---------- Rocket flight assist: dropped upside down from 6 m, must land on its wheels
	{
		Vehicle v; v.Init(Trim::Rocket);
		v.Place(V3(0, 0, 8), Quat::AxisAngle(V3(1, 0, 0), kPi * 0.95));
		Inputs in;
		for (int i = 0; i < 4000; ++i) v.Step(0.001, in, Flat);
		std::printf("Flight assist: dropped inverted from 8 m -> up.z %.2f, contacts %d, tank %.0f%%\n", v.Q.Up().z, v.T.WheelsInContact, v.T.TankPercent * 100);
	}
	return 0;
}
