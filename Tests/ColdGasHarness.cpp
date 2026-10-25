// Validation harness for ColdGasSystem.h (cold-gas nitrogen propulsion, valve level).
// Build: cl /nologo /O2 /std:c++17 /EHsc ColdGasHarness.cpp /Fe:ColdGasHarness.exe
#include "../Unreal/RoadsterSim/Source/RoadsterSim/Public/ColdGasSystem.h"
#include <chrono>
#include <cstdio>
#include <vector>

using namespace Roadster::ColdGas;

static void Title(const char* s) { std::printf("\n=============== %s ===============\n", s); }
static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

constexpr double kDt = 0.001;               // vehicle step (1 kHz)
constexpr double kRocketDryKg = 1700 + 210; // Spec.DryMassKg + Rocket TrimExtraMassKg (RoadsterPhysics.h)

// ------------------------------------------------------------------------------------------------ 0. EOS check
struct NistRow { double T, Pbar, rho, h, s, w, jt; };
// NIST Chemistry WebBook SRD 69 (Span et al. 2000 reference EOS), retrieved 2026-10-25. h kJ/kg, s kJ/kg K, JT K/bar.
static const NistRow kNist[] = {
	{ 300, 1, 1.1233, 311.20, 6.8457, 353.16, 0.21197 }, { 300, 101, 112.81, 291.77, 5.4130, 379.90, 0.13632 },
	{ 300, 201, 213.45, 279.02, 5.1611, 426.50, 0.071074 }, { 300, 301, 295.51, 272.78, 5.0090, 483.78, 0.026209 },
	{ 300, 401, 360.36, 271.30, 4.9025, 543.45, -0.0022939 }, { 300, 501, 412.19, 272.95, 4.8218, 601.37, -0.020559 },
	{ 300, 701, 490.23, 281.73, 4.7037, 707.20, -0.040929 }, { 293.15, 379, 355.60, 261.89, 4.8915, 530.37, 0.0033841 },
	{ 293.15, 690, 494.86, 271.72, 4.6775, 704.90, -0.040100 }, { 250, 51, 70.966, 243.51, 5.4368, 329.30, 0.25449 },
	{ 250, 151, 209.36, 218.45, 5.0265, 372.72, 0.13715 }, { 250, 301, 363.55, 201.85, 4.7500, 481.42, 0.030006 },
	{ 250, 501, 482.82, 202.04, 4.5629, 623.29, -0.021971 }, { 250, 701, 556.83, 211.62, 4.4478, 740.77, -0.041419 },
	{ 200, 51, 95.409, 181.87, 5.1611, 287.34, 0.38449 }, { 200, 151, 298.90, 140.29, 4.6761, 353.45, 0.15354 },
	{ 200, 301, 470.80, 122.31, 4.3942, 516.62, 0.015181 }, { 200, 701, 639.72, 137.58, 4.1170, 806.91, -0.043840 },
	{ 150, 26, 69.696, 131.69, 5.0462, 233.52, 0.74087 }, { 150, 201, 562.55, 33.416, 3.9932, 529.56, 0.013077 },
	{ 125, 11, 33.730, 115.93, 5.1501, 214.06, 1.0923 }, { 125, 21, 76.573, 99.323, 4.8598, 198.65, 1.0963 },
	{ 350, 201, 178.81, 341.90, 5.3552, 450.10, 0.048549 }, { 350, 701, 437.18, 348.55, 4.9098, 692.69, -0.042338 },
};

static void EosCheck()
{
	Title("0. Nitrogen EOS (translated Peng-Robinson) vs NIST WebBook (Span et al. 2000)");
	std::printf("   T[K]  p[bar] | rho model/NIST [kg/m3]  err%%  | h err [kJ/kg] | s err [J/kgK] | w err%% | muJT model/NIST [K/bar]\n");
	double worstRho = 0, worstH = 0;
	for (const NistRow& r : kNist)
	{
		const Eos::Props P = Eos::EvaluatePT(r.Pbar * 1e5, r.T);
		const double er = 100 * (P.rho / r.rho - 1), eh = P.h / 1000 - r.h, es = P.s - r.s * 1000, ew = 100 * (P.w / r.w - 1);
		if (r.T >= 200) { worstRho = std::max(worstRho, std::fabs(er)); worstH = std::max(worstH, std::fabs(eh)); }
		std::printf("  %6.1f %6.0f  | %7.2f / %7.2f  %+5.2f |   %+6.2f      |   %+6.1f      | %+5.1f | %+.3f / %+.3f\n",
			r.T, r.Pbar, P.rho, r.rho, er, eh, es, ew, P.muJT * 1e5, r.jt);
	}
	std::printf("  worst over 200-350 K, 1-700 bar: density %.2f %%, enthalpy %.2f kJ/kg (plain PR: 6-12 %% density error)\n", worstRho, worstH);

	// isenthalpic regulator outlet: 379 bar / 293.15 K throttled to 150 bar
	const Eos::Props in = Eos::EvaluatePT(379e5, 293.15);
	double lo = 200, hi = 300;
	for (int i = 0; i < 80; ++i) { const double m = 0.5 * (lo + hi); (Eos::EvaluatePT(150e5, m).h < in.h ? lo : hi) = m; }
	std::printf("  JT across regulator 379 -> 150 bar from 293.15 K: model %.1f K, NIST (h-matched) 282.2 K\n", 0.5 * (lo + hi));
	std::printf("  Saturation at 77.355 K: p %.4f bar (1.0133), rho' %.1f (806.1), rho'' %.3f (4.613) kg/m3; Tsat(20 bar) %.2f K (115.6)\n",
		Eos::PSat(77.355) / 1e5, Eos::RhoLiqSat(77.355), Eos::RhoVapSat(77.355), Eos::TSat(20e5));
	std::printf("  Fill density: 379 bar/293 K %.1f kg/m3 (NIST 355.6) -> 200 L holds %.1f kg; 690 bar %.1f (NIST 494.9) -> %.1f kg\n",
		Eos::EvaluatePT(379e5, 293.15).rho, 0.2 * Eos::EvaluatePT(379e5, 293.15).rho, Eos::EvaluatePT(690e5, 293.15).rho, 0.2 * Eos::EvaluatePT(690e5, 293.15).rho);
	std::printf("  (old RoadsterPhysics Z = 1 + 0.0008 p model: 200 L at 379 bar = %.1f kg, i.e. %+.1f %%)\n",
		379e5 * 0.2 / ((1 + 0.0008 * 379) * 296.8 * 293), 100 * (379e5 * 0.2 / ((1 + 0.0008 * 379) * 296.8 * 293) / (0.2 * Eos::EvaluatePT(379e5, 293.15).rho) - 1));
}

// ------------------------------------------------------------------------------------------------ 1. design
static double NozzleThrustAt(Config cfg, int i, double dt, double pm, double Tm, double* mdot = nullptr, double* isp = nullptr, NozzleOutput* out = nullptr)
{
	cfg.Nozzle[i].ThroatDiaM = dt;
	System s(cfg);
	const NozzleOutput o = s.Evaluate(i, 1.0, pm, Tm);
	if (mdot) *mdot = o.MassFlowKgS;
	if (isp) *isp = o.ExitVelocity / kG0;
	if (out) *out = o;
	return o.ThrustN;
}

static void Design()
{
	Title("1. Design point: throats sized for rated thrust at manifold = regulator setpoint, valve full open");
	const Config rk = Config::Rocket();
	const double pset = rk.RegSetpointBar * 1e5;
	// regulator outlet temperature at the start of a burst (isenthalpic from 379 bar / 293.15 K)
	const Eos::Props in = Eos::EvaluatePT(379e5, 293.15);
	double lo = 150, hi = 320;
	for (int k = 0; k < 80; ++k) { const double m = 0.5 * (lo + hi); (Eos::EvaluatePT(pset, m).h < in.h ? lo : hi) = m; }
	const double Treg = 0.5 * (lo + hi);
	std::printf("  Regulator setpoint %.0f bar (%.0f psi); outlet gas at start of burst %.1f K (JT from 379 bar / 293 K)\n", rk.RegSetpointBar, rk.RegSetpointBar * kPsiPerBar, Treg);
	std::printf("  nozzle        rated  | throat needed | chosen Dt  De    eps  | F@set   mdot   Isp_SL  Isp_vac | plenum  p_exit  T_exit  M_exit\n");
	for (int i : { 0, 4, 8 })
	{
		const NozzleSpec& n = rk.Nozzle[i];
		double a = 0.005, b = 0.08;
		for (int k = 0; k < 60; ++k) { const double m = 0.5 * (a + b); (NozzleThrustAt(rk, i, m, pset, Treg) < n.RatedThrustN ? a : b) = m; }
		double mdot, isp, ispv; NozzleOutput o, ov;
		const double F = NozzleThrustAt(rk, i, n.ThroatDiaM, pset, Treg, &mdot, &isp, &o);
		NozzleThrustAt(rk, i, n.ThroatDiaM, pset, Treg, nullptr, nullptr, nullptr);
		{
			Config c = rk; System s(c); ov = s.Evaluate(i, 1.0, pset, Treg, 1.0);   // vacuum (pa = 1 Pa)
			ispv = ov.ExitVelocity / kG0;
		}
		std::printf("  %-12s %6.0f N | %6.1f mm     | %5.1f mm %5.1f mm %4.2f | %5.2f kN %5.1f kg/s %5.1f s  %5.1f s | %5.1f bar %5.2f bar %5.1f K  %4.2f\n",
			n.Name, n.RatedThrustN, 0.5 * (a + b) * 1000, n.ThroatDiaM * 1000, n.ExitDiaM * 1000, n.AreaRatio(), F / 1000, mdot, isp, ispv,
			o.PlenumBar, o.ExitPressurePa / 1e5, o.PlumeTempK, o.ExitMach);
	}
	std::printf("  Bank totals at setpoint: rear 4 x 20 kN = 80 kN (Thrust/Rocket), lift 4 x 13 kN = 52 kN, yaw 2 x 4 kN (Rocket)\n");
	std::printf("  Regulator full-open effective area: Thrust %.0f mm2 (Cv ~ %.0f), Rocket %.0f mm2 (Cv ~ %.0f); isolation valves %.0f / %.0f mm2\n",
		Config::Thrust().RegAreaM2 * 1e6, Config::Thrust().RegAreaM2 * 1e6 / 17.0, rk.RegAreaM2 * 1e6, rk.RegAreaM2 * 1e6 / 17.0, rk.Tank[0].IsoAreaM2 * 1e6, rk.Tank[1].IsoAreaM2 * 1e6);

	// Isp vs stagnation temperature (rear nozzle, eps 4.5) - literature: N2 73 s measured / 80 s theoretical vacuum (Sutton)
	std::printf("  Rear nozzle Isp vs stagnation state (sea level / vacuum):");
	{
		System s(rk);
		for (double T0 : { 293.15, 250.0, 200.0, 150.0 })
		{
			const NozzleOutput a = s.Evaluate(0, 1.0, pset, T0), v = s.Evaluate(0, 1.0, pset, T0, 1.0);
			std::printf("  %3.0f K: %4.1f/%4.1f s", T0, a.ExitVelocity / kG0, v.ExitVelocity / kG0);
		}
		std::printf("\n  Ideal infinite-expansion limit at 293 K: sqrt(2 cp T)/g0 = %.1f s\n", std::sqrt(2 * 1039.0 * 293.15) / kG0);
	}
	// vessels
	System th(Config::Thrust()), rc(rk);
	std::printf("  Vessels (Type IV, HDPE liner 5 mm + CFRP, MEOP 690 bar): trunk 200 L r=%.0f mm L_cyl=%.2f m wall %.0f kg; seat 400 L r=%.0f mm L_cyl=%.2f m wall %.0f kg\n",
		rk.Tank[0].InnerRadiusM * 1000, rk.Tank[0].CylLengthM(), rc.TankWallMassKg(0), rk.Tank[1].InnerRadiusM * 1000, rk.Tank[1].CylLengthM(), rc.TankWallMassKg(1));
}

// ------------------------------------------------------------------------------------------------ helpers
struct BurstResult
{
	double tDropout = -1, t90 = -1, t10 = -1, impulse = 0, massUsed = 0, plateauN = 0, minTankT = 1e9, minLinerT = 1e9, minPlumeT = 1e9;
	double endP = 0, endT = 0, massErr = 0, energyErr = 0, frameMass = 0, frameImpulse = 0, usPerStep = 0;
};

static void PrintRow(const System& s, double t)
{
	const Telemetry& T = s.Tel();
	double F = 0, W = 0; double plume = 0; int sep = 0;
	for (int i = 0; i < 4; ++i) { F += s.Nozzle(i).ThrustN; W += s.Nozzle(i).MassFlowKgS; plume = s.Nozzle(i).PlumeTempK; sep += s.Nozzle(i).bSeparated; }
	const int last = T.NumTanks - 1;
	std::printf("  %6.3f | %6.2f | %6.1f %5.1f", t, F / 1000, T.Tank[0].PressureBar, T.Tank[0].TempK);
	if (T.NumTanks > 1) std::printf(" %6.1f %5.1f", T.Tank[last].PressureBar, T.Tank[last].TempK);
	std::printf(" | %6.1f | %6.1f %5.1f | %4.2f%s | %6.1f | %5.1f | %5.1f%s | %5.1f\n", T.HeaderBar, T.ManifoldBar, T.ManifoldK, T.RegOpening, T.bRegDropout ? "D" : " ",
		W, W > 0 ? F / (W * kG0) : 0.0, plume, sep ? "s" : " ", T.Tank[0].LinerMinK);
}

static BurstResult RearBurst(const Config& cfg, double fillBar, const char* label, bool print, System* keep = nullptr)
{
	System s(cfg);
	s.Fill(fillBar, 293.15);
	s.SetArmed(true);
	for (int k = 0; k < 1000; ++k) s.Step(kDt);   // arm: isolation valves travel, dome and manifold pressurise
	s.ResetCounters();
	BurstResult r;
	const double m0 = s.GasMassKg() + s.ManifoldMassKg();
	if (print)
	{
		std::printf("\n  -- %s: fill %.0f bar (%.0f psi) / 293 K, %.1f kg N2; armed manifold %.1f bar %.1f K, header %.1f bar --\n",
			label, fillBar, fillBar * kPsiPerBar, s.GasMassKg(), s.Tel().ManifoldBar, s.Tel().ManifoldK, s.Tel().HeaderBar);
		std::printf("    t[s] | F[kN] | tank p[bar] T[K]%s | hdr[bar] | reg out p[bar] T[K] | reg x | mdot | Isp_SL | plume T[K] | liner min[K]\n", cfg.NumTanks > 1 ? " (A)  tank B p, T " : "");
	}
	for (int i = 0; i < 4; ++i) s.Command(i, 1.0);
	const double marks[] = { 0.002, 0.005, 0.010, 0.020, 0.050, 0.100, 0.150, 0.200, 0.250, 0.300, 0.400, 0.500, 0.600, 0.800, 1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0 };
	int mk = 0;
	double t = 0, tw = 0;
	const double t0 = Now();
	int steps = 0;
	const double rated = 4 * cfg.Nozzle[0].RatedThrustN;
	while (t < 20.0)
	{
		s.Step(kDt); t += kDt; ++steps;
		double F = 0, W = 0;
		for (int i = 0; i < 4; ++i) { F += s.Nozzle(i).ThrustN; W += s.Nozzle(i).MassFlowKgS; r.minPlumeT = std::min(r.minPlumeT, s.Nozzle(i).PlumeTempK > 0 && s.Nozzle(i).ThrustN > 0 ? s.Nozzle(i).PlumeTempK : 1e9); }
		r.frameMass += W * kDt; r.frameImpulse += F * kDt;
		if (t > 0.02 && t < 0.06) r.plateauN = std::max(r.plateauN, F);
		if (r.tDropout < 0 && s.Tel().bRegDropout) r.tDropout = t;
		if (r.t90 < 0 && t > 0.03 && F < 0.9 * rated) r.t90 = t;
		if (r.t10 < 0 && t > 0.03 && F < 0.1 * rated) r.t10 = t;
		for (int k = 0; k < s.Tel().NumTanks; ++k) r.minTankT = std::min(r.minTankT, s.Tel().Tank[k].TempK), r.minLinerT = std::min(r.minLinerT, s.Tel().Tank[k].LinerMinK);
		if (print && mk < (int)(sizeof(marks) / sizeof(marks[0])) && t >= marks[mk] - 1e-9) { PrintRow(s, t); ++mk; }
		if (t > 0.05 && F < 0.002 * rated) { if (tw == 0) tw = t; if (t - tw > 0.05) break; }
	}
	r.usPerStep = (Now() - t0) / steps * 1e6;
	if (print) PrintRow(s, t);
	for (int i = 0; i < 4; ++i) s.Command(i, 0.0);
	for (int k = 0; k < 100; ++k) s.Step(kDt);
	r.impulse = s.Tel().TotalImpulseNs;
	r.massUsed = m0 - (s.GasMassKg() + s.ManifoldMassKg());
	r.endP = s.Tel().Tank[0].PressureBar; r.endT = s.Tel().Tank[0].TempK;
	r.massErr = s.MassBalanceErrorKg(); r.energyErr = s.EnergyBalanceErrorJ();
	if (print)
	{
		std::printf("  => regulated (dropout) at %.3f s | <90%% of 80 kN at %.3f s | <10%% (8 kN) at %.3f s | total impulse %.1f kN s from %.1f kg (mean Isp %.1f s)\n",
			r.tDropout, r.t90, r.t10, r.impulse / 1000, r.massUsed, r.impulse / (r.massUsed * kG0));
		std::printf("     plateau %.1f kN | gas min %.1f K, liner min %.1f K (%.0f C, rating -40 C), plume min %.1f K | residual %.1f kg at %.1f bar %.1f K | %.1f us per 1 ms step\n",
			r.plateauN / 1000, r.minTankT, r.minLinerT, r.minLinerT - 273.15, r.minPlumeT, s.GasMassKg() + s.ManifoldMassKg(), r.endP, r.endT, r.usPerStep);
	}
	if (keep) *keep = s;
	return r;
}

// ------------------------------------------------------------------------------------------------ main
int main()
{
	double tb = Now();
	(void)IsentropeTable::Get();
	std::printf("Isentrope table build: %.0f ms (48 x 32 stagnation states x 15 area ratios, once per process)\n", (Now() - tb) * 1000);

	EosCheck();
	Design();

	// ---------------------------------------------------------------- (a) + (b) rear bursts
	Title("(a)(b) Full-throttle rear-bank burst (4 nozzles, valves 100 %), sea level");
	System thrustAfter(Config::Thrust());
	BurstResult a1 = RearBurst(Config::Thrust(), 379, "Thrust 200 L", true, &thrustAfter);
	BurstResult a2 = RearBurst(Config::Rocket(), 379, "Rocket 600 L (200 + 400 on common header)", true);
	BurstResult b1 = RearBurst(Config::Thrust(), 690, "Thrust 200 L at rated 690 bar", true);
	BurstResult b2 = RearBurst(Config::Rocket(), 690, "Rocket 600 L at rated 690 bar", true);
	std::printf("\n  Summary                    | plateau | regulated | to 10%% | impulse  | gas used | mean Isp | gas min T | liner min T\n");
	auto sum = [](const char* n, const BurstResult& r) {
		std::printf("  %-26s | %5.1f kN | %6.3f s  | %5.2f s | %6.1f kNs | %6.1f kg | %5.1f s  | %6.1f K  | %6.1f K\n", n, r.plateauN / 1000, r.tDropout, r.t10, r.impulse / 1000, r.massUsed, r.impulse / (r.massUsed * kG0), r.minTankT, r.minLinerT);
	};
	sum("Thrust 200 L @ 379 bar", a1); sum("Rocket 600 L @ 379 bar", a2); sum("Thrust 200 L @ 690 bar", b1); sum("Rocket 600 L @ 690 bar", b2);

	// ---------------------------------------------------------------- (c) Rocket hover
	Title("(c) Rocket hover on the 4 corner lift nozzles (thrust = current weight, closed-loop valve command)");
	for (double fill : { 379.0, 690.0 })
	{
		System s(Config::Rocket());
		s.Fill(fill, 293.15); s.SetArmed(true);
		for (int k = 0; k < 1000; ++k) s.Step(kDt);
		s.ResetCounters();
		const double m0 = s.GasMassKg() + s.ManifoldMassKg();
		std::printf("\n  -- fill %.0f bar: car %.0f kg (dry %.0f + N2 %.1f), weight %.1f kN --\n", fill, kRocketDryKg + s.GasMassKg(), kRocketDryKg, s.GasMassKg(), (kRocketDryKg + s.GasMassKg()) * kG0 / 1000);
		std::printf("    t[s] | lift F[kN] need[kN] | stroke | tank A p T | tank B p T | reg out [bar] | reg x | Isp_SL | plume T\n");
		double t = 0, hoverStart = -1, hoverEnd = -1, nextPrint = 0, frameMass = 0, grace = 0;
		const double t0 = Now(); int steps = 0;
		while (t < 30.0)
		{
			const double need = (kRocketDryKg + s.GasMassKg() + s.ManifoldMassKg()) * kG0;
			for (int i = 4; i < 8; ++i) s.CommandThrust(i, need / 4.0);
			s.Step(kDt); t += kDt; ++steps;
			double F = 0, W = 0;
			for (int i = 4; i < 8; ++i) { F += s.Nozzle(i).ThrustN; W += s.Nozzle(i).MassFlowKgS; }
			frameMass += W * kDt;
			if (hoverStart < 0 && F >= 0.98 * need) hoverStart = t;
			if (hoverStart >= 0 && F < 0.98 * need) { grace += kDt; if (grace > 0.05) { hoverEnd = t - grace; break; } } else grace = 0;
			if (t >= nextPrint - 1e-9)
			{
				const Telemetry& T = s.Tel();
				std::printf("  %6.2f | %6.2f %6.2f | %5.3f | %5.1f %5.1f | %5.1f %5.1f | %6.1f%s | %4.2f | %5.1f | %5.1f\n", t, F / 1000, need / 1000, s.Nozzle(4).Valve,
					T.Tank[0].PressureBar, T.Tank[0].TempK, T.Tank[1].PressureBar, T.Tank[1].TempK, T.ManifoldBar, T.bRegDropout ? "D" : " ", T.RegOpening, W > 0 ? F / (W * kG0) : 0, s.Nozzle(4).PlumeTempK);
				nextPrint += 0.5;
			}
		}
		const double us = (Now() - t0) / steps * 1e6;
		const double used = m0 - (s.GasMassKg() + s.ManifoldMassKg());
		std::printf("  => hover from %.3f s to %.3f s: %.2f s of hover; gas used %.1f kg, lift impulse %.1f kN s (Isp %.1f s); tanks end %.1f / %.1f bar, %.1f / %.1f K; %.1f us/step\n",
			hoverStart, hoverEnd, hoverEnd - hoverStart, used, s.Tel().BankImpulseNs[BankLift] / 1000, s.Tel().BankImpulseNs[BankLift] / (used * kG0),
			s.Tel().Tank[0].PressureBar, s.Tel().Tank[1].PressureBar, s.Tel().Tank[0].TempK, s.Tel().Tank[1].TempK, us);
		std::printf("     conservation: frame-integrated nozzle mass %.4f kg vs tank+manifold loss %.4f kg (diff %.2e kg); internal mass balance %.2e kg, energy %.2e J\n",
			frameMass, used, frameMass - used, s.MassBalanceErrorKg(), s.EnergyBalanceErrorJ());
	}

	// ---------------------------------------------------------------- (d) valve response
	Title("(d) Valve response (EMA proportional valves; rear nozzle 1 alone, Thrust trim at regulated pressure)");
	{
		System base(Config::Thrust());
		base.Fill(379, 293.15); base.SetArmed(true);
		for (int k = 0; k < 1000; ++k) base.Step(kDt);
		// steady references
		System st = base; st.Command(0, 1.0); for (int k = 0; k < 200; ++k) st.Step(kDt);
		const double Ffull = st.Nozzle(0).ThrustN;
		System st5 = base; st5.Command(0, 0.5); for (int k = 0; k < 200; ++k) st5.Step(kDt);
		std::printf("  steady: full open %.2f kN (plenum %.1f bar); 50 %% stroke %.2f kN = %.0f %% (plenum %.1f bar, valve %s)\n", Ffull / 1000, st.Nozzle(0).PlenumBar,
			st5.Nozzle(0).ThrustN / 1000, 100 * st5.Nozzle(0).ThrustN / Ffull, st5.Nozzle(0).PlenumBar, st5.Nozzle(0).PlenumBar < 0.5 * st5.Tel().ManifoldBar ? "choked" : "subcritical");

		auto pulse = [&](System s, int noz, double width, bool print, double* Ipk)
		{
			double I = 0, t = 0;
			if (print) std::printf("   t[ms] cmd stroke  F[kN]  manifold[bar]\n");
			for (int k = 0; k < 60; ++k)
			{
				s.Command(noz, t < width - 1e-9 ? 1.0 : 0.0);
				s.Step(kDt); t += kDt;
				I += s.Nozzle(noz).ThrustN * kDt;
				if (print && (k < 36 && (k % 2 == 1 || k < 8))) std::printf("   %5.0f  %1.0f  %5.3f %6.2f  %6.1f\n", t * 1000, t - kDt < width - 1e-9 ? 1.0 : 0.0, s.Nozzle(noz).Valve, s.Nozzle(noz).ThrustN / 1000, s.Tel().ManifoldBar);
			}
			if (Ipk) *Ipk = I;
			return I;
		};
		std::printf("  20 ms command pulse (dead time 5 ms open / 4 ms close, 40 ms full-stroke slew, tau 6 ms):\n");
		double I20 = pulse(base, 0, 0.020, true, nullptr);
		const double I1 = pulse(base, 0, 0.001, false, nullptr);
		const double I50 = pulse(base, 0, 0.050, false, nullptr);
		std::printf("  impulse: 20 ms pulse %.1f N s (ideal square %.1f N s, %.0f %%); minimum impulse bit (1 ms command, stretched to 10 ms) %.1f N s; 50 ms pulse %.1f N s\n",
			I20, Ffull * 0.020, 100 * I20 / (Ffull * 0.020), I1, I50);
		// PWM
		for (double hz : { 10.0, 20.0 })
		{
			System s = base; s.SetValveMode(ValveMode::PWM);
			Config cfg = s.Cfg(); (void)cfg;
			System p(Config::Thrust());
			{
				Config c = Config::Thrust(); c.Mode = ValveMode::PWM; c.PwmHz = hz;
				p = System(c); p.Fill(379, 293.15); p.SetArmed(true); for (int k = 0; k < 1000; ++k) p.Step(kDt);
			}
			double sumF = 0, mn = 1e9, mx = 0; const int N = 1000;
			for (int k = 0; k < N; ++k) { p.Command(0, 0.5); p.Step(kDt); const double F = p.Nozzle(0).ThrustN; sumF += F; if (k > 200) { mn = std::min(mn, F); mx = std::max(mx, F); } }
			std::printf("  50 %% PWM at %2.0f Hz: mean %.2f kN = %.0f %% of full open, ripple %.2f..%.2f kN\n", hz, sumF / N / 1000, 100 * sumF / N / Ffull, mn / 1000, mx / 1000);
		}
		{
			System r(Config::Rocket()); r.Fill(379, 293.15); r.SetArmed(true); for (int k = 0; k < 1000; ++k) r.Step(kDt);
			System q = r; q.Command(8, 1.0); for (int k = 0; k < 200; ++k) q.Step(kDt);
			double Iy = pulse(r, 8, 0.020, false, nullptr), Iyb = pulse(r, 8, 0.001, false, nullptr);
			std::printf("  yaw nozzle (Rocket): steady %.2f kN; 20 ms pulse %.1f N s (%.0f %% of square); min impulse bit %.1f N s -> %.2f deg/s yaw-rate bit on Izz 2500 kg m2 at 2.2 m arm\n",
				q.Nozzle(8).ThrustN / 1000, Iy, 100 * Iy / (q.Nozzle(8).ThrustN * 0.02), Iyb, Iyb * 2.2 / 2500 * 57.2958);
		}
	}

	// ---------------------------------------------------------------- (e) recovery
	Title("(e) Tank cool-down and recovery after the Thrust-trim burst (valves shut, car parked at 20 C)");
	{
		System& s = thrustAfter;
		std::printf("   t after burst |  gas T[K]  p[bar] | liner inner[K] liner min[K] | overwrap outer[K] | h_in[W/m2K] | Q into gas[W]\n");
		double t = 0;
		const double marks[] = { 0, 1, 5, 10, 30, 60, 120, 300, 600, 1200, 1800, 3600 };
		for (double m : marks)
		{
			while (t < m - 1e-9) { s.Step(0.01); t += 0.01; }
			const TankTelemetry& k = s.Tel().Tank[0];
			std::printf("   %8.0f s    |  %6.1f  %6.1f  |   %6.1f       %6.1f    |    %6.1f        |  %6.1f     | %7.0f\n", t, k.TempK, k.PressureBar, k.LinerInnerK, k.LinerMinK, k.OverwrapOuterK, k.InnerHtc, k.WallHeatW);
		}
		std::printf("   conservation over burst + 1 h soak: mass %.2e kg, energy %.2e J (of %.3e J stored)\n", s.MassBalanceErrorKg(), s.EnergyBalanceErrorJ(), 0.0);
	}

	// ---------------------------------------------------------------- (f) conservation summary
	Title("(f) Conservation (exact bookkeeping; residuals are round-off)");
	auto cons = [](const char* n, const BurstResult& r) {
		std::printf("  %-24s internal mass %.2e kg, energy %.2e J | frame-integrated nozzle mass %.4f kg vs stored-mass loss %.4f kg (diff %.1e) | impulse frames %.1f vs internal %.1f N s\n",
			n, r.massErr, r.energyErr, r.frameMass, r.massUsed, r.frameMass - r.massUsed, r.frameImpulse, r.impulse);
	};
	cons("Thrust @ 379", a1); cons("Rocket @ 379", a2); cons("Thrust @ 690", b1); cons("Rocket @ 690", b2);
	return 0;
}
