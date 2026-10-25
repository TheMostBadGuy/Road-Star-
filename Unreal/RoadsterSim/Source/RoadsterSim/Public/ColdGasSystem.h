#pragma once
// =====================================================================================================================
// ColdGasSystem.h - valve-level cold-gas (nitrogen) propulsion model for the 2026 Tesla Roadster simulator.
// =====================================================================================================================
// Pure C++17, header-only, no engine types, deterministic. Built to run beside Roadster::Vehicle at 1 kHz; it
// sub-steps internally (0.25 ms while gas is moving, 20 ms when everything is shut) for the fast feed dynamics.
// Validation: Tests/ColdGasHarness.cpp. Wiring into RoadsterPhysics.h: Tests/coldgas_integration.md.
//
// SYSTEM (per trim)
//   Thrust: 200 L Type-IV COPV (trunk)                         -> isolation valve -> HP header -> regulator -> manifold
//   Rocket: 200 L (trunk) + 400 L (seat) COPVs, each with its own isolation valve on a common HP header (crossfeed)
//   Manifold (line + accumulator volume) -> per-nozzle feed line (K-factor loss) -> electric valve -> nozzle plenum
//   -> converging-diverging nozzle. Nozzles: 0-3 rear bank, 4-7 corner lift (FL, FR, RL, RR), 8-9 yaw
//   (8 = left-firing: jet exits to the left, pushes the nose right; 9 = right-firing).
//
// PHYSICS AND SOURCES
//  * Nitrogen EOS: Peng-Robinson [Peng & Robinson, Ind. Eng. Chem. Fundam. 15 (1976) 59] with a constant Peneloux-type
//    volume translation c = -1.54e-4 m3/kg fitted here to NIST data. Critical constants Tc = 126.192 K,
//    pc = 3.3958 MPa, rho_c = 313.3 kg/m3, acentric factor 0.0372, M = 28.0134 g/mol [Span, Lemmon, Jacobsen, Wagner &
//    Yokozeki, J. Phys. Chem. Ref. Data 29 (2000) 1361]. The translated EOS is one Helmholtz function
//    A(T,v) = A_PR(T, v + c), so p, u, h, s, cv, cp, w and mu_JT are mutually consistent. Ideal part: 5/2 R plus an
//    Einstein vibrational term (theta = 3353 K). Validated against NIST WebBook SRD 69 (Span 2000 reference EOS) over
//    200-350 K and 1-700 bar: density within +-1.3 %, enthalpy within +-2.4 kJ/kg, JT coefficient within +-0.03 K/bar
//    (the harness reprints the comparison). Plain PR is 6-12 % too dense there, so the translation matters.
//    Saturation ancillaries (p_sat, rho', rho''): Span et al. (2000). Transport: dilute-gas Sutherland viscosity and a
//    power-law conductivity plus density-only residual polynomials (Jossi-Stiel-Thodos form) fitted to NIST
//    (+-2 % outside the near-critical region).
//  * Tanks: lumped gas (mass m, internal energy U) with an exact mass and energy balance:
//    dm/dt = -W, dU/dt = -W h + Q_wall. Gas state comes from (v = V/m, u = U/m) through the EOS. Below the dome the
//    vessel holds vapour + condensate in equilibrium (ancillary saturation densities, PR energies), and vapour is
//    drawn off. Wall: 1-D transient conduction through a 5 mm HDPE liner and the CFRP overwrap (10 finite volumes,
//    cylindrical areas). Inside, turbulent natural convection Nu = 0.13 Ra^(1/3) [McAdams, Heat Transmission (1954);
//    Incropera & DeWitt]. Outside, 6 W/m2K to the trunk air. The liner's rated minimum is -40 C (ISO 19881 / UN GTR 13
//    service range). Relief at 1.1 x MEOP. The isolation valve shuts automatically below the residual pressure
//    (default 10 bar, Type-IV liner practice of keeping 0.5-2 MPa).
//  * Valves, regulator and lines: IEC 60534-2-1 compressible-flow sizing form W = A Y sqrt(2 rho1 dp),
//    Y = 1 - x/(3 xT), choked at x = xT. It is regularised to laminar (linear) flow for dp below ~2e-4 p, so
//    equalisation stays well posed. Line losses are lumped as K-factors in series with the valve,
//    1/A_eq^2 = 1/A_v^2 + K/A_line^2.
//  * Dome-loaded regulator: an electronic pilot sets the dome pressure (first-order, 50 ms). The poppet follows
//    x* = clamp((p_lockup - p_out)/band) with a 4 ms lag. Lock-up is +2 %, droop to full-open is -6 %, and the
//    supply-pressure effect is 0.5 %. When the poppet saturates open, outlet = inlet - flow loss: that is regulator
//    dropout, after which the system runs as unregulated blowdown. The regulator is isenthalpic, so the
//    Joule-Thomson cooling comes out of the manifold energy balance. No reverse flow.
//  * Manifold/accumulator: lumped gas volume (m, U), integrated with a linearly-implicit pressure predictor
//    (stable through regulator dropout and equalisation). The HP header between the tanks and the regulator is
//    massless and solved algebraically, so mass and energy are conserved exactly.
//  * Nozzle valves: electromechanical (EMA) poppet/pintle valves. Opening and closing dead times, a first-order
//    stroke with a rate limit, a seat deadband and a minimum on-time (which sets the minimum impulse bit).
//    Proportional (default) or PWM. See the integration note for why proportional is the default.
//  * Nozzles: choked flow and exit state come from a real-gas isentrope table, built once at start-up by
//    integrating the EOS along s = s0 for every stagnation state (p0, T0) on a 48 x 32 grid
//    (0.9-800 bar, 70-380 K). The table holds the critical flow function C* = G* sqrt(R T0)/p0 (cf. ISO 9300) and
//    p/p0, V/sqrt(R T0) and T/T0 at 15 area ratios. The expansion is frozen and metastable: no condensation, the
//    standard cold-gas assumption. The supersaturation ratio is reported for plume visuals. Thrust is
//    F = lambda*eta_v*W*Ve + (pe - pa)*Ae, with Summerfield separation (wall p < 0.4 pa) for heavily overexpanded
//    flow and a subsonic branch once the throat unchokes [Sutton & Biblarz, Rocket Propulsion Elements, ch. 3].
//    Valve and throat are solved in series (bracketed Illinois) for the plenum pressure. Throttling across the valve
//    is isenthalpic: T_plenum = T_manifold - mu_JT*dp.
//
// HONESTY NOTES (numbers from Tests/ColdGasHarness.cpp): these nozzles give Isp 61-62 s at sea level from 283-293 K
// stagnation. The loss-free vacuum limit is 71 s at eps 25 (Sutton: 73 s measured, 80 s theoretical at infinite
// expansion). Because the tank gas cools adiabatically during a burst (to ~100 K, the dew line), the mean Isp over a
// full blowdown is only 44-48 s. 80 kN costs ~135 kg/s, so the 200 L / 379 bar charge (70.5 kg) holds 80 kN for
// ~0.13 s before regulator dropout. Thrust falls below 8 kN at 0.8 s, and the burst delivers 29 kN*s in total
// (Rocket 600 L: 0.48 s regulated, 2.3 s to 10 %, 89 kN*s). Tesla's "about 5 s of thrust" at ">80 kN" (~400 kN*s)
// is not achievable with this hardware. The model delivers what the gas can.
// =====================================================================================================================

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace Roadster
{
namespace ColdGas
{
	// ------------------------------------------------------------------------------------------- constants
	constexpr double kRu = 8.314462618;          // J/(mol K), CODATA 2018
	constexpr double kMolarMass = 0.0280134;     // kg/mol N2 (Span et al. 2000)
	constexpr double kR = kRu / kMolarMass;      // 296.80 J/(kg K)
	constexpr double kTc = 126.192;              // K
	constexpr double kPc = 3.3958e6;             // Pa
	constexpr double kRhoc = 313.3;              // kg/m3
	constexpr double kTtriple = 63.151;          // K
	constexpr double kOmega = 0.0372;            // acentric factor
	constexpr double kThetaVib = 3353.0;         // K, N2 vibrational temperature (fundamental 2330 cm-1)
	constexpr double kG0 = 9.80665;
	constexpr double kPatm = 101325.0;
	constexpr double kBarPa = 1.0e5;
	constexpr double kPsiPerBar = 14.5037738;
	constexpr double kPiCG = 3.14159265358979323846;
	constexpr double kAmbientK = 293.15;

	inline double Clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

	// =========================================================================================== N2 thermodynamics
	namespace Eos
	{
		constexpr double kB = 0.07780 * kR * kTc / kPc;                                   // m3/kg
		constexpr double kAc = 0.45724 * kR * kR * kTc * kTc / kPc;                      // Pa m6/kg2
		constexpr double kKappa = 0.37464 + 1.54226 * kOmega - 0.26992 * kOmega * kOmega;
		constexpr double kShift = -1.54e-4;   // m3/kg volume translation, v_PR = v + kShift (fit to NIST, this work)
		constexpr double kS2 = 1.4142135623730951;
		constexpr double kVMin = kB - kShift + 1e-9;                                      // smallest admissible v

		struct Alpha { double a, da, d2a; };
		inline Alpha AlphaT(double T)
		{
			const double sTTc = std::sqrt(T * kTc), f = 1.0 + kKappa * (1.0 - std::sqrt(T / kTc));
			return { kAc * f * f, -kAc * kKappa * f / sTTc, kAc * kKappa * (1.0 + kKappa) / (2.0 * T * sTTc) };
		}
		inline double LogTerm(double V) { return std::log((V + (1.0 + kS2) * kB) / (V + (1.0 - kS2) * kB)); }
		inline double Den(double V) { return V * V + 2.0 * kB * V - kB * kB; }

		inline double CvIdeal(double T)
		{
			const double x = kThetaVib / T;
			if (x > 200.0) return 2.5 * kR;
			const double e = std::exp(-x);
			return kR * (2.5 + x * x * e / ((1.0 - e) * (1.0 - e)));
		}
		inline double UIdealRaw(double T) { const double x = kThetaVib / T; return kR * (2.5 * T + (x > 200.0 ? 0.0 : kThetaVib / std::expm1(x))); }
		inline double SIdealRaw(double T)
		{
			const double x = kThetaVib / T;
			return kR * (2.5 * std::log(T) + (x > 200.0 ? 0.0 : x / std::expm1(x) - std::log(-std::expm1(-x))));
		}

		inline double Pressure(double T, double v) { const double V = v + kShift; return kR * T / (V - kB) - AlphaT(T).a / Den(V); }
		inline double URaw(double T, double v)
		{
			const double V = v + kShift; const Alpha A = AlphaT(T);
			return UIdealRaw(T) + (T * A.da - A.a) / (2.0 * kS2 * kB) * LogTerm(V);
		}
		inline double SRaw(double T, double v)
		{
			const double V = v + kShift; const Alpha A = AlphaT(T);
			return SIdealRaw(T) + kR * std::log(V - kB) + A.da / (2.0 * kS2 * kB) * LogTerm(V);
		}
		inline double Cv(double T, double v) { return CvIdeal(T) + T * AlphaT(T).d2a / (2.0 * kS2 * kB) * LogTerm(v + kShift); }
		inline double DPDv(double T, double v)
		{
			const double V = v + kShift, D = Den(V);
			return -kR * T / ((V - kB) * (V - kB)) + AlphaT(T).a * (2.0 * V + 2.0 * kB) / (D * D);
		}

		// Specific volume from (p, T): analytic cubic in Z, largest root (vapour/supercritical) unless liquid requested.
		inline double VFromPT(double p, double T, bool liquid = false)
		{
			const Alpha al = AlphaT(T);
			const double RT = kR * T, A = al.a * p / (RT * RT), B = kB * p / RT;
			const double c2 = -(1.0 - B), c1 = A - 3.0 * B * B - 2.0 * B, c0 = -(A * B - B * B - B * B * B);
			const double P = c1 - c2 * c2 / 3.0, Q = 2.0 * c2 * c2 * c2 / 27.0 - c2 * c1 / 3.0 + c0;
			const double disc = Q * Q / 4.0 + P * P * P / 27.0;
			double Z;
			if (disc > 0.0)
			{
				const double sd = std::sqrt(disc);
				Z = std::cbrt(-Q / 2.0 + sd) + std::cbrt(-Q / 2.0 - sd) - c2 / 3.0;
			}
			else
			{
				const double r = std::sqrt(-P / 3.0), phi = std::acos(Clampd(-Q / (2.0 * r * r * r), -1.0, 1.0));
				double zmax = -1e30, zmin = 1e30;
				for (int k = 0; k < 3; ++k)
				{
					const double z = 2.0 * r * std::cos((phi - 2.0 * kPiCG * k) / 3.0) - c2 / 3.0;
					if (z > B) { zmax = std::max(zmax, z); zmin = std::min(zmin, z); }
				}
				Z = liquid ? zmin : zmax;
				if (Z > 1e29 || Z < -1e29) Z = std::max(1.0, B * 1.01);
			}
			double V = std::max(Z * RT / p, kB * 1.000001);
			for (int it = 0; it < 3; ++it)   // Newton polish
			{
				const double D = Den(V), f = RT / (V - kB) - al.a / D - p;
				const double df = -RT / ((V - kB) * (V - kB)) + al.a * (2.0 * V + 2.0 * kB) / (D * D);
				if (df >= 0.0) break;
				const double Vn = V - f / df;
				V = Vn > kB ? Vn : 0.5 * (V + kB);
			}
			return std::max(V - kShift, kVMin);
		}

		// Reference offsets so that u, h, s match the NIST WebBook default for N2 (u(300 K, 1 bar) = 222.17 kJ/kg,
		// s = 6.8457 kJ/kg K). Differences are unaffected.
		struct RefOffsets { double u, s; };
		inline const RefOffsets& Ref()
		{
			static const RefOffsets r = [] { const double v = VFromPT(1.0e5, 300.0); return RefOffsets{ 222170.0 - URaw(300.0, v), 6845.7 - SRaw(300.0, v) }; }();
			return r;
		}
		inline double U(double T, double v) { return URaw(T, v) + Ref().u; }
		inline double S(double T, double v) { return SRaw(T, v) + Ref().s; }

		// Temperature from (u, v); cv > 0 everywhere, so Newton is monotone.
		inline double TFromUV(double u, double v, double Tguess)
		{
			double T = Clampd(Tguess, 5.0, 3000.0);
			for (int it = 0; it < 40; ++it)
			{
				const double dT = (u - U(T, v)) / Cv(T, v);
				T = Clampd(T + Clampd(dT, -0.5 * T, 0.5 * T), 2.0, 5000.0);
				if (std::fabs(dT) < 1e-10 * T) break;
			}
			return T;
		}

		struct Props { double T, v, rho, p, u, h, s, cv, cp, w, dpdT, dpdv, muJT, beta; };
		inline Props Evaluate(double T, double v)
		{
			Props o{};
			o.T = T; o.v = v; o.rho = 1.0 / v;
			const double V = v + kShift; const Alpha A = AlphaT(T); const double L = LogTerm(V), D = Den(V);
			o.p = kR * T / (V - kB) - A.a / D;
			o.u = UIdealRaw(T) + (T * A.da - A.a) / (2.0 * kS2 * kB) * L + Ref().u;
			o.h = o.u + o.p * v;
			o.s = SIdealRaw(T) + kR * std::log(V - kB) + A.da / (2.0 * kS2 * kB) * L + Ref().s;
			o.cv = CvIdeal(T) + T * A.d2a / (2.0 * kS2 * kB) * L;
			o.dpdT = kR / (V - kB) - A.da / D;
			o.dpdv = -kR * T / ((V - kB) * (V - kB)) + A.a * (2.0 * V + 2.0 * kB) / (D * D);
			const double dvdT = -o.dpdT / std::min(o.dpdv, -1e-30);
			o.cp = o.cv + T * o.dpdT * dvdT;
			o.w = std::sqrt(std::max(1.0, -v * v * o.dpdv * o.cp / o.cv));
			o.muJT = (T * dvdT - v) / o.cp;   // K/Pa
			o.beta = dvdT / v;
			return o;
		}
		inline Props EvaluatePT(double p, double T) { return Evaluate(T, VFromPT(p, T)); }

		// ---- saturation ancillaries (Span et al. 2000)
		inline double PSat(double T)
		{
			T = Clampd(T, kTtriple, kTc);
			const double t = 1.0 - T / kTc;
			return kPc * std::exp(kTc / T * (-6.12445284 * t + 1.2632722 * std::pow(t, 1.5) - 0.765910082 * std::pow(t, 2.5) - 1.77570564 * std::pow(t, 5.0)));
		}
		inline double RhoLiqSat(double T)
		{
			T = Clampd(T, kTtriple, kTc);
			const double t = 1.0 - T / kTc;
			return kRhoc * std::exp(1.48654237 * std::pow(t, 0.3294) - 0.280476066 * std::pow(t, 2.0 / 3.0) + 0.0894143085 * std::pow(t, 8.0 / 3.0) - 0.119879866 * std::pow(t, 35.0 / 6.0));
		}
		inline double RhoVapSat(double T)
		{
			T = Clampd(T, kTtriple, kTc);
			const double t = 1.0 - T / kTc;
			return kRhoc * std::exp(kTc / T * (-1.70127164 * std::pow(t, 0.34) - 3.70402649 * std::pow(t, 5.0 / 6.0) + 1.29859383 * std::pow(t, 7.0 / 6.0)
				- 0.561424977 * std::pow(t, 13.0 / 6.0) - 2.68505381 * std::pow(t, 14.0 / 3.0)));
		}
		// Saturation temperature from pressure (tabulated inverse of PSat, linear in ln p).
		inline double TSat(double p)
		{
			struct Tab { double lnp0, dlnp; double T[256]; };
			static const Tab tab = []
			{
				Tab t{};
				t.lnp0 = std::log(PSat(kTtriple)); t.dlnp = (std::log(kPc) - t.lnp0) / 255.0;
				for (int k = 0; k < 256; ++k)
				{
					const double target = t.lnp0 + k * t.dlnp;
					double lo = kTtriple, hi = kTc;
					for (int it = 0; it < 70; ++it) { const double m = 0.5 * (lo + hi); (std::log(PSat(m)) < target ? lo : hi) = m; }
					t.T[k] = 0.5 * (lo + hi);
				}
				return t;
			}();
			if (p >= kPc) return kTc;
			const double x = (std::log(std::max(p, 1.0)) - tab.lnp0) / tab.dlnp;
			if (x <= 0.0) return kTtriple;
			const int i = std::min(254, (int)x);
			const double f = x - i;
			return tab.T[i] + f * (tab.T[i + 1] - tab.T[i]);
		}
		// Supersaturation reference below the triple point (Clausius-Clapeyron with the sublimation enthalpy ~ 225 kJ/kg).
		inline double PSatExtended(double T)
		{
			if (T >= kTtriple) return PSat(T);
			const double ptp = PSat(kTtriple);
			return ptp * std::exp(-225000.0 / kR * (1.0 / std::max(T, 5.0) - 1.0 / kTtriple));
		}

		// ---- transport (fit to NIST WebBook, Lemmon & Jacobsen 2004 correlations): Pa s, W/(m K)
		inline double Viscosity(double T, double rho)
		{
			const double mu0 = 1.663e-5 * std::pow(T / 273.15, 1.5) * (273.15 + 107.0) / (T + 107.0);
			return mu0 + rho * (1.18018e-8 + rho * (7.61902e-11 + rho * (-1.22546e-13 + rho * 2.57157e-16)));
		}
		inline double Conductivity(double T, double rho)
		{
			const double k0 = 0.025968 * std::pow(T / 300.0, 0.89);
			return k0 + rho * (3.34059e-5 + rho * (1.26580e-7 + rho * (-1.72350e-10 + rho * 2.81916e-13)));
		}
	}

	// Lowest stagnation temperature the nozzle table admits (stays on the vapour side of the dome).
	inline double TFloor(double p) { return p < 0.999 * kPc ? Eos::TSat(p) + 1.0 : kTc + 1.0; }

	// IEC 60534-2-1 style compressible restriction, W in kg/s (A = effective area incl. discharge coefficient).
	inline double Orifice(double A, double p1, double rho1, double p2, double xT)
	{
		if (A <= 0.0 || p1 <= p2 || rho1 <= 0.0) return 0.0;
		const double dp = p1 - p2;
		const double lam = 2.0e-4 * p1 + 500.0;   // laminar regularisation band (Pa)
		if (dp < lam)
		{
			const double xl = std::min(lam / p1, xT);
			return A * (1.0 - xl / (3.0 * xT)) * std::sqrt(2.0 * rho1 * p1 * xl) * (dp / lam);
		}
		const double x = std::min(dp / p1, xT);
		return A * (1.0 - x / (3.0 * xT)) * std::sqrt(2.0 * rho1 * p1 * x);
	}

	// =========================================================================================== real-gas isentropes
	class IsentropeTable
	{
	public:
		static constexpr int NP = 48, NT = 32, NE = 15;
		static constexpr double kEps[NE] = { 1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0, 12.0, 16.0, 20.0, 25.0 };
		static constexpr double kPmin = 0.9e5, kPmax = 8.0e7, kTmin = 70.0, kTmax = 380.0;

		static const IsentropeTable& Get() { static const IsentropeTable t; return t; }

		struct Cell { int o; double w00, w01, w10, w11; double p0, T0; };
		Cell Locate(double p0, double T0) const
		{
			Cell c{};
			p0 = Clampd(p0, kPmin, kPmax);
			T0 = Clampd(std::max(T0, TFloor(p0)), kTmin, kTmax);
			const double x = (std::log(p0) - LnPmin) / DLnP, y = (T0 - kTmin) / DT;
			const int i = std::min(NP - 2, std::max(0, (int)x)), j = std::min(NT - 2, std::max(0, (int)y));
			const double fx = Clampd(x - i, 0.0, 1.0), fy = Clampd(y - j, 0.0, 1.0);
			c.o = i * NT + j;
			c.w00 = (1 - fx) * (1 - fy); c.w01 = (1 - fx) * fy; c.w10 = fx * (1 - fy); c.w11 = fx * fy;
			c.p0 = p0; c.T0 = T0;
			return c;
		}
		double Cstar(const Cell& c) const { return Bi(Cs, c, 1, 0); }
		double MuJT(const Cell& c) const { return Bi(Mu, c, 1, 0); }
		// p/p0, V/sqrt(R T0), T/T0 at area-ratio sample k
		void Sample(const Cell& c, int k, double& pr, double& vr, double& tr) const
		{
			pr = Bi(Pr, c, NE, k); vr = Bi(Vr, c, NE, k); tr = Bi(Tr, c, NE, k);
		}
		// Interpolated in ln(eps) between samples k and k+1 with weight f.
		void SampleEps(const Cell& c, int k, double f, double& pr, double& vr, double& tr) const
		{
			double p1, v1, t1, p2, v2, t2;
			Sample(c, k, p1, v1, t1);
			if (f <= 0.0 || k + 1 >= NE) { pr = p1; vr = v1; tr = t1; return; }
			Sample(c, k + 1, p2, v2, t2);
			pr = std::exp(std::log(p1) + f * (std::log(p2) - std::log(p1))); vr = v1 + f * (v2 - v1); tr = t1 + f * (t2 - t1);
		}
		static void EpsIndex(double eps, int& k, double& f)
		{
			eps = Clampd(eps, 1.0, kEps[NE - 1]);
			k = 0;
			while (k < NE - 2 && kEps[k + 1] <= eps) ++k;
			f = (std::log(eps) - std::log(kEps[k])) / (std::log(kEps[k + 1]) - std::log(kEps[k]));
			f = Clampd(f, 0.0, 1.0);
		}

	private:
		double LnPmin = 0, DLnP = 0, DT = 0;
		std::vector<float> Cs, Mu, Pr, Vr, Tr;

		double Bi(const std::vector<float>& a, const Cell& c, int stride, int k) const
		{
			const int o = c.o;
			return c.w00 * a[o * stride + k] + c.w01 * a[(o + 1) * stride + k] + c.w10 * a[(o + NT) * stride + k] + c.w11 * a[(o + NT + 1) * stride + k];
		}

		IsentropeTable()
		{
			LnPmin = std::log(kPmin); DLnP = (std::log(kPmax) - LnPmin) / (NP - 1); DT = (kTmax - kTmin) / (NT - 1);
			Cs.assign(NP * NT, 0.f); Mu.assign(NP * NT, 0.f);
			Pr.assign(NP * NT * NE, 0.f); Vr.assign(NP * NT * NE, 0.f); Tr.assign(NP * NT * NE, 0.f);
			for (int i = 0; i < NP; ++i)
				for (int j = 0; j < NT; ++j)
					BuildPoint(i, j);
		}

		// March the isentrope from stagnation (p0, T0) in ln v; locate the mass-flux maximum (throat), then record the
		// supersonic branch at each area ratio eps = G*/G.
		void BuildPoint(int i, int j)
		{
			const int idx = i * NT + j;
			const double p0 = std::exp(LnPmin + i * DLnP);
			const double T0 = std::max(kTmin + j * DT, TFloor(p0));
			const double v0 = Eos::VFromPT(p0, T0);
			const Eos::Props P0 = Eos::Evaluate(T0, v0);
			const double s0 = P0.s, h0 = P0.h, sRT = std::sqrt(kR * T0);
			Mu[idx] = (float)P0.muJT;
			struct S { double x, p, T, vel, G; };
			S w[3]{}; int nw = 0; bool peak = false; double Gs = 0.0; S prev{}; double epsPrev = 1.0; int k = 1;
			auto store = [&](int kk, double p, double T, double vel)
			{
				const int o = idx * NE + kk;
				Pr[o] = (float)(p / p0); Vr[o] = (float)(vel / sRT); Tr[o] = (float)(T / T0);
			};
			auto capture = [&](const S& s)
			{
				const double eps = Gs / std::max(s.G, 1e-300);
				while (k < NE && kEps[k] <= eps)
				{
					const double den = std::log(eps) - std::log(epsPrev);
					const double f = den > 1e-14 ? (std::log(kEps[k]) - std::log(epsPrev)) / den : 1.0;
					store(k, std::exp(std::log(prev.p) + f * (std::log(s.p) - std::log(prev.p))), prev.T + f * (s.T - prev.T), prev.vel + f * (s.vel - prev.vel));
					++k;
				}
				prev = s; epsPrev = std::max(eps, 1.0);
			};
			auto setThroat = [&](const S& th)
			{
				Gs = th.G; peak = true;
				store(0, th.p, th.T, th.vel);
				Cs[idx] = (float)(Gs * sRT / p0);
				prev = th; epsPrev = 1.0;
			};
			auto consume = [&](const S& s)
			{
				if (peak) { capture(s); return; }
				if (nw < 3) w[nw++] = s; else { w[0] = w[1]; w[1] = w[2]; w[2] = s; }
				if (nw == 3 && w[2].G < w[1].G)
				{
					// parabola through the last three samples (Newton form) for the mass-flux peak
					const double d01 = (w[1].G - w[0].G) / (w[1].x - w[0].x), d12 = (w[2].G - w[1].G) / (w[2].x - w[1].x);
					const double a = (d12 - d01) / (w[2].x - w[0].x);
					double xs = w[1].x, Gpk = w[1].G;
					if (a < 0.0)
					{
						xs = Clampd(0.5 * (w[0].x + w[1].x) - d01 / (2.0 * a), w[0].x, w[2].x);
						Gpk = w[0].G + d01 * (xs - w[0].x) + a * (xs - w[0].x) * (xs - w[1].x);
					}
					const S& A = xs < w[1].x ? w[0] : w[1];
					const S& B = xs < w[1].x ? w[1] : w[2];
					const double f = (xs - A.x) / (B.x - A.x);
					S th{ xs, std::exp(std::log(A.p) + f * (std::log(B.p) - std::log(A.p))), A.T + f * (B.T - A.T), A.vel + f * (B.vel - A.vel), std::max(Gpk, w[1].G) };
					setThroat(th);
					if (w[1].x > xs) capture(w[1]);
					capture(w[2]);
				}
			};
			// real-gas march, with an ideal-gas continuation if the metastable state reaches the spinodal
			const double dx = 0.015;
			double lnv = std::log(v0), T = T0;
			bool ideal = false;
			S last{ lnv, p0, T0, 0.0, 0.0 };
			for (int it = 0; it < 12000 && k < NE; ++it)
			{
				S s{};
				if (!ideal)
				{
					const double v = std::exp(lnv + dx);
					double Tn = T * std::exp(-0.4 * dx);
					for (int m = 0; m < 30; ++m)
					{
						const double dT = (s0 - Eos::S(Tn, v)) * Tn / Eos::Cv(Tn, v);
						Tn += Clampd(dT, -0.3 * Tn, 0.3 * Tn);
						if (std::fabs(dT) < 1e-11 * Tn) break;
					}
					bool ok = std::isfinite(Tn) && Tn > 6.0 && Eos::DPDv(Tn, v) < 0.0;
					double p = 0.0, hh = 0.0;
					if (ok) { p = Eos::Pressure(Tn, v); hh = Eos::U(Tn, v) + p * v; ok = p > 0.0 && std::isfinite(hh); }
					if (!ok) { ideal = true; continue; }
					lnv += dx; T = Tn;
					const double vel = std::sqrt(std::max(0.0, 2.0 * (h0 - hh)));
					s = { lnv, p, T, vel, vel / v };
				}
				else
				{
					const double cp = Eos::CvIdeal(last.T) + kR;
					const double Tn = last.T * 0.99;
					if (Tn < 1.0) break;
					const double p = last.p * std::pow(Tn / last.T, cp / kR);
					const double vel = std::sqrt(last.vel * last.vel + 2.0 * cp * (last.T - Tn));
					const double v = kR * Tn / p;
					s = { std::log(v), p, Tn, vel, vel / v };
				}
				last = s;
				consume(s);
			}
			if (!peak)
			{
				S best = w[0];
				for (int q = 1; q < nw; ++q) if (w[q].G > best.G) best = w[q];
				if (best.G <= 0.0) best = last;
				setThroat(best);
			}
			for (; k < NE; ++k) store(k, prev.p, prev.T, prev.vel);
		}
	};

	// =========================================================================================== configuration
	enum class ValveMode : uint8_t { Proportional, PWM };
	enum NozzleId : int { RearA = 0, RearB = 1, RearC = 2, RearD = 3, LiftFL = 4, LiftFR = 5, LiftRL = 6, LiftRR = 7, YawLeftFiring = 8, YawRightFiring = 9, kNumNozzles = 10 };
	enum BankId : int { BankRear = 0, BankLift = 1, BankYaw = 2 };

	struct Material { double k, rho, cp; };   // W/(m K), kg/m3, J/(kg K)

	struct ValveSpec
	{
		double DeadOpenS = 0.005, DeadCloseS = 0.004;   // command-to-motion dead time (bus + driver + backlash)
		double TauOpenS = 0.006, TauCloseS = 0.005;     // position-loop first-order lag
		double SlewOpen = 25.0, SlewClose = 40.0;       // stroke/s: 40 ms full open, 25 ms full close (spring-assisted)
		double MinStroke = 0.02;                        // seat deadband (proportional mode)
		double MinOnTimeS = 0.010;                      // shortest command pulse the controller issues
		double AreaRatio = 3.0;                         // full-open effective flow area / throat area
		double xT = 0.60;                               // IEC 60534 choked pressure-drop ratio (balanced poppet)
	};

	struct NozzleSpec
	{
		bool bPresent = false;
		int Bank = BankRear;
		const char* Name = "";
		double ThroatDiaM = 0.0, ExitDiaM = 0.0;
		double Cd = 0.98;          // throat discharge coefficient (rounded throat, high Re)
		double Lambda = 0.983;     // divergence loss, 15 deg conical: (1 + cos 15)/2
		double EtaV = 0.985;       // boundary-layer velocity coefficient
		double LineDiaM = 0.05, LineK = 1.5;
		double RatedThrustN = 0.0; // design thrust at manifold = setpoint, valve fully open
		ValveSpec Valve;
		double ThroatArea() const { return 0.25 * kPiCG * ThroatDiaM * ThroatDiaM; }
		double ExitArea() const { return 0.25 * kPiCG * ExitDiaM * ExitDiaM; }
		double AreaRatio() const { return ThroatDiaM > 0 ? (ExitDiaM * ExitDiaM) / (ThroatDiaM * ThroatDiaM) : 1.0; }
		double LineArea() const { return 0.25 * kPiCG * LineDiaM * LineDiaM; }
	};

	struct TankSpec
	{
		bool bPresent = false;
		const char* Name = "";
		double VolumeL = 0.0;
		double InnerRadiusM = 0.2;
		double LinerThkM = 0.005;      // HDPE
		double OverwrapThkM = 0.023;   // CFRP, netting estimate for MEOP x 2.25 burst at ~2.0 GPa composite strength
		double MEOPBar = 690.0;        // "rated about 10,000 psi"
		double ReliefBar = 759.0;      // 1.1 x MEOP
		double ReliefAreaM2 = 3.0e-5;
		double IsoAreaM2 = 4.0e-3;     // effective flow area of the full-bore isolation ball valve
		double IsoTravelS = 0.25;      // motorised quarter-turn travel time
		double CylLengthM() const
		{
			const double V = VolumeL * 1e-3, r = InnerRadiusM;
			return std::max(0.0, (V - 4.0 / 3.0 * kPiCG * r * r * r) / (kPiCG * r * r));
		}
	};

	struct Config
	{
		int NumTanks = 1;
		TankSpec Tank[2];
		NozzleSpec Nozzle[kNumNozzles];
		Material Liner{ 0.42, 950.0, 1900.0 };      // HDPE
		Material Overwrap{ 0.60, 1550.0, 850.0 };   // T700-class CFRP, through-thickness
		double LinerMinK = 233.15;                  // -40 C rated minimum service temperature
		double OuterHtc = 6.0;                      // W/(m2 K) trunk air natural convection
		double BuoyancyG = kG0;
		// regulator (dome loaded, electronically piloted)
		double RegSetpointBar = 150.0;
		double RegLockupFrac = 0.02, RegDroopFrac = 0.06, RegSPE = 0.005;
		double RegTauS = 0.004, DomeTauS = 0.05;
		double DomeRampBarS = 300.0;                // electronic pilot soft-start: dome slews at most this fast
		double RegAreaM2 = 3.8e-3;                  // full-open effective area (Cv ~ 225)
		double RegXt = 0.60;
		double IsoXt = 0.30;                        // ball valves recover pressure poorly
		double ManifoldVolumeL = 4.0;
		double ManifoldWallKg = 12.0;               // stainless lines + accumulator shell (thermal mass)
		double ManifoldWallAreaM2 = 0.6;            // wetted area
		double ManifoldLineDiaM = 0.06;             // hydraulic diameter for forced convection
		double ManifoldAmbientWK = 2.0;             // lagged lines: conductance to ambient (W/K)
		double MinTankBar = 10.0;                   // automatic isolation below this residual pressure
		double SubstepS = 0.00025, IdleSubstepS = 0.02;
		double SeparationRatio = 0.40;              // Summerfield criterion, p_wall/p_a at separation
		ValveMode Mode = ValveMode::Proportional;
		double PwmHz = 10.0;

		static void Rear(NozzleSpec& n, const char* name)
		{
			n.bPresent = true; n.Bank = BankRear; n.Name = name;
			n.ThroatDiaM = 0.0350; n.ExitDiaM = 0.0720;     // 72 mm bell exit (inner, measured from Tesla's photo), eps 4.23
			n.LineDiaM = 0.060; n.LineK = 1.2; n.RatedThrustN = 20000.0;
		}
		static void Lift(NozzleSpec& n, const char* name)
		{
			n.bPresent = true; n.Bank = BankLift; n.Name = name;
			n.ThroatDiaM = 0.0285; n.ExitDiaM = 0.0560;     // eps 3.86
			n.LineDiaM = 0.050; n.LineK = 2.0; n.RatedThrustN = 13000.0;
		}
		static void Yaw(NozzleSpec& n, const char* name)
		{
			n.bPresent = true; n.Bank = BankYaw; n.Name = name;
			n.ThroatDiaM = 0.0158; n.ExitDiaM = 0.0310;     // eps 3.85
			n.LineDiaM = 0.030; n.LineK = 2.0; n.RatedThrustN = 4000.0;
			n.Valve.DeadOpenS = 0.003; n.Valve.DeadCloseS = 0.003; n.Valve.TauOpenS = 0.003; n.Valve.TauCloseS = 0.003;
			n.Valve.SlewOpen = 60.0; n.Valve.SlewClose = 80.0; n.Valve.MinOnTimeS = 0.006;
		}
		static Config Thrust()
		{
			Config c;
			c.NumTanks = 1;
			TankSpec& a = c.Tank[0];
			a.bPresent = true; a.Name = "Trunk 200 L"; a.VolumeL = 200.0; a.InnerRadiusM = 0.200; a.OverwrapThkM = 0.023; a.IsoAreaM2 = 4.0e-3;
			Rear(c.Nozzle[0], "Rear 1"); Rear(c.Nozzle[1], "Rear 2"); Rear(c.Nozzle[2], "Rear 3"); Rear(c.Nozzle[3], "Rear 4");
			c.RegAreaM2 = 3.8e-3; c.ManifoldVolumeL = 4.0;
			return c;
		}
		static Config Rocket()
		{
			Config c = Thrust();
			c.NumTanks = 2;
			TankSpec& b = c.Tank[1];
			b.bPresent = true; b.Name = "Seat 400 L"; b.VolumeL = 400.0; b.InnerRadiusM = 0.250; b.OverwrapThkM = 0.029; b.IsoAreaM2 = 5.0e-3;
			Lift(c.Nozzle[4], "Lift FL"); Lift(c.Nozzle[5], "Lift FR"); Lift(c.Nozzle[6], "Lift RL"); Lift(c.Nozzle[7], "Lift RR");
			Yaw(c.Nozzle[8], "Yaw L-firing"); Yaw(c.Nozzle[9], "Yaw R-firing");
			c.RegAreaM2 = 6.2e-3; c.ManifoldVolumeL = 9.0;
			c.ManifoldWallKg = 26.0; c.ManifoldWallAreaM2 = 1.4;
			return c;
		}
		// 200 L -> Thrust, 600 L -> Rocket (200 + 400); anything else -> a single vessel of that size feeding the rear bank.
		static Config ForTankVolumeL(double litres)
		{
			if (litres >= 590.0 && litres <= 610.0) return Rocket();
			Config c = Thrust();
			if (litres > 0.0 && std::fabs(litres - 200.0) > 1.0)
			{
				c.Tank[0].VolumeL = litres;
				c.Tank[0].InnerRadiusM = 0.2 * std::cbrt(litres / 200.0);
				c.Tank[0].OverwrapThkM = 0.023 * c.Tank[0].InnerRadiusM / 0.2;
			}
			return c;
		}
	};

	// =========================================================================================== outputs
	struct NozzleOutput
	{
		double ThrustN = 0;          // frame-averaged thrust along the nozzle axis
		double MassFlowKgS = 0;      // frame-averaged
		double ExitVelocity = 0;     // effective exhaust velocity F/W (m/s)
		double GasExitVelocity = 0;  // jet velocity at the exit plane (m/s)
		double PlumeTempK = 0;       // static temperature at the exit plane (frozen expansion)
		double ExitPressurePa = 0;
		double ExitMach = 0;
		double Supersaturation = 0;  // p_exit / p_sat(T_exit): >1 means the jet is fogging (N2 and entrained moisture)
		double PlenumBar = 0, PlenumK = 0;
		double Valve = 0;            // actual valve stroke 0..1
		double ThrustFrac = 0;       // ThrustN / rated
		bool bChoked = false, bSeparated = false;
	};

	struct TankTelemetry
	{
		double PressureBar = 0, TempK = 0, MassKg = 0, DensityKgM3 = 0;
		double LinerInnerK = 0, LinerMinK = 0, OverwrapOuterK = 0;
		double InnerHtc = 0, WallHeatW = 0;      // W/(m2 K), heat into gas (W)
		double VapourFraction = 1;               // < 1 when nitrogen has condensed in the vessel
		double IsoOpen = 0;
		bool bTwoPhase = false, bLinerBelowRating = false, bReliefOpen = false, bCutoff = false;
	};

	struct Telemetry
	{
		int NumTanks = 0;
		TankTelemetry Tank[2];
		double HeaderBar = 0;
		double DomeBar = 0, RegOpening = 0, RegOutletBar = 0, RegOutletK = 0;
		bool bRegDropout = false, bRegLockedUp = false;
		double ManifoldBar = 0, ManifoldK = 0;
		double ValvePos[kNumNozzles] = {};
		double TotalThrustN = 0, TotalMassFlowKgS = 0;
		double TotalImpulseNs = 0, BankImpulseNs[3] = { 0, 0, 0 };
		double MassExpelledKg = 0, MassVentedKg = 0;
		double GasMassKg = 0, UsableGasKg = 0, GasFraction = 0;   // GasFraction: usable / usable at fill (UI "%")
		double DisplayBar = 0, DisplayPsi = 0;                     // volume-weighted vessel pressure (UI "Thruster PSI")
		double SimTime = 0;
		bool bArmed = false;
	};

	// =========================================================================================== system
	class System
	{
	public:
		static constexpr int kWallNodes = 10;

		explicit System(const Config& cfg = Config::Thrust()) : C(cfg)
		{
			(void)IsentropeTable::Get();
			NumTanks = std::max(1, std::min(2, C.NumTanks));
			for (int i = 0; i < kNumNozzles; ++i)
			{
				const NozzleSpec& n = C.Nozzle[i];
				if (!n.bPresent) continue;
				IsentropeTable::EpsIndex(n.AreaRatio(), Nz[i].kE, Nz[i].fE);
				// subsonic isentropic pressure ratio at the exit area ratio: throat unchokes below pa / prSub
				const double eps = n.AreaRatio();
				double lo = 1e-4, hi = 1.0;
				for (int it = 0; it < 80; ++it)
				{
					const double M = 0.5 * (lo + hi);
					const double ar = (1.0 / M) * std::pow((2.0 / 2.4) * (1.0 + 0.2 * M * M), 3.0);
					(ar > eps ? lo : hi) = M;
				}
				const double Ms = 0.5 * (lo + hi);
				Nz[i].prSub = std::pow(1.0 + 0.2 * Ms * Ms, -3.5);
			}
			for (int t = 0; t < NumTanks; ++t) BuildWall(t);
			Man.V = C.ManifoldVolumeL * 1e-3;
			Fill(379.0, kAmbientK);
		}
		explicit System(double tankVolumeL) : System(Config::ForTankVolumeL(tankVolumeL)) {}

		// ------------------------------------------------------------------ commands
		// Charge every vessel to (bar, K) with the walls soaked at the same temperature. The manifold is vented to
		// ambient; if the system is armed the regulator re-pressurises it within ~0.3 s. Resets the counters.
		void Fill(double bar, double kelvin)
		{
			FillBar = bar; FillK = kelvin;
			for (int t = 0; t < NumTanks; ++t)
			{
				Gas& g = Tk[t];
				g.V = C.Tank[t].VolumeL * 1e-3;
				const double v = Eos::VFromPT(bar * kBarPa, kelvin);
				g.m = g.V / v; g.U = g.m * Eos::U(kelvin, v); g.T = kelvin;
				Solve(g);
				for (int k = 0; k < kWallNodes; ++k) Wall[t].T[k] = kelvin;
				Cutoff[t] = false; Relief[t] = false;
				FillM[t] = g.m; FillU[t] = g.U;
			}
			{
				// disarmed: lines vented to ambient. Armed (pit refill with the system live): lines charged to
				// regulator lock-up at the fill temperature, valves settled, so the car is "Boost Ready" immediately.
				const double plock = C.RegSetpointBar * kBarPa * (1.0 + C.RegLockupFrac);
				const double pm = bArmed ? std::min(plock, bar * kBarPa) : kPatm;
				const double v = Eos::VFromPT(pm, kelvin);
				Man.m = Man.V / v; Man.U = Man.m * Eos::U(kelvin, v); Man.T = kelvin;
				Solve(Man);
				ManWallT = kelvin;
				if (bArmed)
				{
					for (int t = 0; t < NumTanks; ++t) IsoPos[t] = IsoCmd[t] ? 1.0 : 0.0;
					Dome = DomeCmd = C.RegSetpointBar * kBarPa; RegX = 0.0;
				}
			}
			for (auto& n : NzOut) n = NozzleOutput();
			for (int i = 0; i < kNumNozzles; ++i) { Nz[i].prevW = 0; Nz[i].lastPlenum = 0; }
			HdrP = Tk[0].p;
			UsableFull = UsableGasKg();
			ResetCounters();
			UpdateTelemetry(kPatm);
		}
		void ResetCounters()
		{
			M0 = TotalGasMass(); E0 = TotalGasEnergy() + TotalWallEnergy();
			Mout = Mvent = Hout = Hvent = Qamb = 0;
			Impulse = 0; BankImpulse[0] = BankImpulse[1] = BankImpulse[2] = 0;
		}
		// "Unlimited gas" demo mode: restore every vessel (gas and walls) to the last Fill() state without venting the
		// manifold or touching valves. The added mass/energy is booked as an inflow, so the conservation checks hold.
		void TopUp()
		{
			const double e0 = TotalGasEnergy() + TotalWallEnergy(), m0 = TotalGasMass();
			for (int t = 0; t < NumTanks; ++t)
			{
				Tk[t].m = FillM[t]; Tk[t].U = FillU[t]; Tk[t].T = FillK;
				Solve(Tk[t]);
				for (int k = 0; k < kWallNodes; ++k) Wall[t].T[k] = FillK;
				Cutoff[t] = false;
			}
			M0 += TotalGasMass() - m0; E0 += TotalGasEnergy() + TotalWallEnergy() - e0;
		}
		void SetArmed(bool armed) { bArmed = armed; }
		void SetIsolation(int tank, bool open) { if (tank >= 0 && tank < 2) IsoCmd[tank] = open; }
		void SetSetpointBar(double bar) { C.RegSetpointBar = std::max(0.0, bar); }
		void SetValveMode(ValveMode m) { C.Mode = m; }
		// Proportional mode: valve stroke 0..1. PWM mode: duty 0..1.
		void Command(int i, double u)
		{
			if (i < 0 || i >= kNumNozzles || !C.Nozzle[i].bPresent) return;
			Vs[i].Cmd = std::isfinite(u) ? Clampd(u, 0.0, 1.0) : 0.0;
		}
		// Thrust request (N). Inverts the valve/line/throat model at the present manifold state.
		void CommandThrust(int i, double newtons)
		{
			if (i < 0 || i >= kNumNozzles || !C.Nozzle[i].bPresent) return;
			if (C.Mode == ValveMode::PWM)
			{
				const double fmax = MaxThrustN(i);
				Command(i, fmax > 1.0 ? newtons / fmax : 0.0);
			}
			else Command(i, StrokeForThrust(i, newtons));
		}

		// ------------------------------------------------------------------ simulation
		void Step(double dt, double ambientPa = kPatm, double ambientK = kAmbientK)
		{
			if (!(dt > 0.0)) return;
			for (int i = 0; i < kNumNozzles; ++i) { Nz[i].accF = 0; Nz[i].accW = 0; }
			const double hmax = IsIdle(ambientPa) ? C.IdleSubstepS : C.SubstepS;
			const int n = std::max(1, (int)std::ceil(dt / hmax - 1e-9));
			const double h = dt / n;
			for (int s = 0; s < n; ++s) Substep(h, ambientPa, ambientK);
			for (int i = 0; i < kNumNozzles; ++i)
			{
				NozzleOutput& o = NzOut[i];
				o.ThrustN = Nz[i].accF / dt;
				o.MassFlowKgS = Nz[i].accW / dt;
				o.ExitVelocity = o.MassFlowKgS > 1e-9 ? o.ThrustN / o.MassFlowKgS : 0.0;
				o.ThrustFrac = C.Nozzle[i].RatedThrustN > 0 ? o.ThrustN / C.Nozzle[i].RatedThrustN : 0.0;
				o.Valve = Vs[i].Pos;
			}
			UpdateTelemetry(ambientPa);
		}

		// ------------------------------------------------------------------ queries
		const NozzleOutput& Nozzle(int i) const { return NzOut[Clampi(i)]; }
		const Telemetry& Tel() const { return T; }
		const Config& Cfg() const { return C; }
		bool HasNozzle(int i) const { return i >= 0 && i < kNumNozzles && C.Nozzle[i].bPresent; }
		double SimTime() const { return Time; }

		// Thrust a nozzle would give fully open at the present manifold state (no droop allowance).
		double MaxThrustN(int i, double pa = kPatm) const
		{
			if (!HasNozzle(i)) return 0.0;
			return EvalNozzle(i, 1.0, Man.p, Man.T, Man.rho, pa).ThrustN;
		}
		// Valve stroke that gives `newtons` at the present manifold state (bracketed secant on the full model).
		double StrokeForThrust(int i, double newtons, double pa = kPatm) const
		{
			if (!HasNozzle(i) || newtons <= 0.0) return 0.0;
			const double fmax = EvalNozzle(i, 1.0, Man.p, Man.T, Man.rho, pa).ThrustN;
			if (fmax <= 1e-6 || newtons >= fmax) return fmax > 1e-6 ? 1.0 : 0.0;
			double a = 0.0, fa = -newtons, b = 1.0, fb = fmax - newtons, x = newtons / fmax;
			int side = 0;
			for (int it = 0; it < 8; ++it)
			{
				const double fx = EvalNozzle(i, x, Man.p, Man.T, Man.rho, pa).ThrustN - newtons;
				if (std::fabs(fx) < 2e-3 * newtons) break;
				if (fx > 0) { b = x; fb = fx; if (side == 1) fa *= 0.5; side = 1; }
				else { a = x; fa = fx; if (side == -1) fb *= 0.5; side = -1; }
				x = (a * fb - b * fa) / (fb - fa);
			}
			return Clampd(x, 0.0, 1.0);
		}
		// Sea-level specific impulse a rear nozzle would deliver right now, fully open (s).
		double CurrentIspS(double pa = kPatm) const
		{
			int i = 0; while (i < kNumNozzles && !C.Nozzle[i].bPresent) ++i;
			if (i >= kNumNozzles) return 0.0;
			double pt = 0.0, Tt = kAmbientK;
			for (int t = 0; t < NumTanks; ++t) if (Tk[t].p > pt) { pt = Tk[t].p; Tt = Tk[t].T; }
			const double p = std::min(pt, std::max(Dome, C.RegSetpointBar * kBarPa));
			if (p <= pa * 1.01) return 0.0;
			const Eos::Props P = Eos::EvaluatePT(p, std::max(Tt, TFloor(p)));
			const NozzleOutput o = EvalNozzle(i, 1.0, p, P.T, P.rho, pa);
			return o.MassFlowKgS > 0 ? o.ThrustN / (o.MassFlowKgS * kG0) : 0.0;
		}
		double GasMassKg() const { double m = 0; for (int t = 0; t < NumTanks; ++t) m += Tk[t].m; return m; }
		double UsableGasKg() const
		{
			double m = 0;
			for (int t = 0; t < NumTanks; ++t)
			{
				if (Cutoff[t]) continue;
				const double rmin = 1.0 / Eos::VFromPT(C.MinTankBar * kBarPa, std::max(Tk[t].T, TFloor(C.MinTankBar * kBarPa)));
				m += std::max(0.0, Tk[t].m - rmin * Tk[t].V);
			}
			return m;
		}
		double RemainingImpulseNs(double pa = kPatm) const { return UsableGasKg() * CurrentIspS(pa) * kG0; }
		// Tsiolkovsky with the present Isp: what the remaining usable gas could add to a vehicle of this total mass.
		double DeltaVBudget(double vehicleMassKg, double pa = kPatm) const
		{
			const double mu = UsableGasKg();
			if (vehicleMassKg <= mu + 1.0) return 0.0;
			return CurrentIspS(pa) * kG0 * std::log(vehicleMassKg / (vehicleMassKg - mu));
		}
		// Conservation diagnostics (should be round-off): mass in + out vs stored, and the first-law balance.
		double MassBalanceErrorKg() const { return (TotalGasMass() + Mout + Mvent) - M0; }
		double EnergyBalanceErrorJ() const { return (TotalGasEnergy() + TotalWallEnergy() + Hout + Hvent - Qamb) - E0; }
		double EnergyThroughputJ() const { return std::fabs(Hout) + std::fabs(Hvent) + std::fabs(Qamb); }
		double ManifoldMassKg() const { return Man.m; }
		double TankMassKg(int t) const { return Tk[t].m; }
		double TankWallMassKg(int t) const { return WallMass[t]; }
		double TankLinerMassKg(int t) const { return LinerMass[t]; }
		double FillPressureBar() const { return FillBar; }

		// Single-point nozzle evaluation for an arbitrary manifold state (used by the harness for design checks).
		NozzleOutput Evaluate(int i, double stroke, double manifoldPa, double manifoldK, double pa = kPatm) const
		{
			const Eos::Props P = Eos::EvaluatePT(manifoldPa, manifoldK);
			NozzleOutput o = EvalNozzle(i, stroke, manifoldPa, manifoldK, P.rho, pa);
			o.ExitVelocity = o.MassFlowKgS > 0 ? o.ThrustN / o.MassFlowKgS : 0;
			return o;
		}

	private:
		// ------------------------------------------------------------------ state
		struct Gas
		{
			double m = 0, U = 0, V = 0;
			double T = kAmbientK, p = kPatm, rho = 1.0, h = 0, w = 350, cp = 1040, beta = 1.0 / kAmbientK, quality = 1;
			bool bTwoPhase = false;
		};
		struct WallModel
		{
			double T[kWallNodes]{}, Cap[kWallNodes]{}, G[kWallNodes]{};   // G[k]: conductance node k -> k+1
			double Ain = 0, HalfRin = 0, Gout = 0;                         // inner area, half-node resistance (m2 K/W * A), outer conductance
			int NL = 4;
		};
		struct ValveState
		{
			double Cmd = 0, Out = 0, Pos = 0, Held = 0, OnStart = -1;
			double PwmStart = -1, PwmOn = 0;
			double EvT[32]{}, EvV[32]{}; int EvHead = 0, EvCount = 0; double EvBase = 0;
		};
		struct NozzleCache { int kE = 0; double fE = 0, prSub = 0.99, prevW = 0, accF = 0, accW = 0, lastPlenum = 0; };

		Config C;
		int NumTanks = 1;
		Gas Tk[2], Man;
		WallModel Wall[2];
		double WallMass[2] = { 0, 0 }, LinerMass[2] = { 0, 0 };
		double IsoPos[2] = { 0, 0 };
		bool IsoCmd[2] = { true, true }, Cutoff[2] = { false, false }, Relief[2] = { false, false };
		double TankQ[2] = { 0, 0 }, TankHtc[2] = { 0, 0 };
		bool bArmed = false;
		double Dome = 0, DomeCmd = 0, RegX = 0, HdrP = 0, HdrWreg = 0, HdrWabs = 0;
		double ManWallT = kAmbientK, ManQ = 0;
		static constexpr double kSteelCp = 500.0;
		ValveState Vs[kNumNozzles];
		NozzleCache Nz[kNumNozzles];
		NozzleOutput NzOut[kNumNozzles];
		Telemetry T;
		double Time = 0, FillBar = 379, FillK = kAmbientK, UsableFull = 1, FillM[2] = { 0, 0 }, FillU[2] = { 0, 0 };
		double M0 = 0, E0 = 0, Mout = 0, Mvent = 0, Hout = 0, Hvent = 0, Qamb = 0;
		double Impulse = 0, BankImpulse[3] = { 0, 0, 0 };

		static int Clampi(int i) { return i < 0 ? 0 : (i >= kNumNozzles ? kNumNozzles - 1 : i); }

		double TotalGasMass() const { double m = Man.m; for (int t = 0; t < NumTanks; ++t) m += Tk[t].m; return m; }
		double TotalGasEnergy() const { double e = Man.U; for (int t = 0; t < NumTanks; ++t) e += Tk[t].U; return e; }
		double TotalWallEnergy() const
		{
			double e = C.ManifoldWallKg * kSteelCp * ManWallT;
			for (int t = 0; t < NumTanks; ++t) for (int k = 0; k < kWallNodes; ++k) e += Wall[t].Cap[k] * Wall[t].T[k];
			return e;
		}

		// ------------------------------------------------------------------ thermodynamic state of a control volume
		static void Solve(Gas& g)
		{
			const double v = g.V / std::max(g.m, 1e-12), u = g.U / std::max(g.m, 1e-12);
			double T1 = Eos::TFromUV(u, std::max(v, Eos::kVMin), g.T);
			// phase boundary at this specific volume: dew temperature (v > v_c) or bubble temperature (v < v_c)
			const double rho = 1.0 / v;
			double Tb = kTtriple;
			bool inDome = false;
			if (T1 < kTc && rho > Eos::RhoVapSat(kTtriple) && rho < Eos::RhoLiqSat(kTtriple))
			{
				const double Tq = std::max(T1, kTtriple);   // ancillary densities are monotone: one test at T1 decides
				inDome = T1 < kTtriple || (rho < kRhoc ? Eos::RhoVapSat(Tq) < rho : Eos::RhoLiqSat(Tq) > rho);
			}
			if (inDome)
			{
				double lo = kTtriple, hi = kTc - 1e-4;
				const bool vap = rho < kRhoc;
				for (int it = 0; it < 48; ++it)
				{
					const double m = 0.5 * (lo + hi);
					const bool inside = vap ? Eos::RhoVapSat(m) < rho : Eos::RhoLiqSat(m) > rho;   // m still below the boundary
					(inside ? lo : hi) = m;
				}
				Tb = 0.5 * (lo + hi);
			}
			const bool two = inDome && T1 < Tb;
			if (!two)
			{
				const Eos::Props P = Eos::Evaluate(T1, std::max(v, Eos::kVMin));
				g.T = T1; g.p = P.p; g.rho = P.rho; g.h = P.h; g.w = P.w; g.cp = P.cp; g.beta = P.beta; g.quality = 1; g.bTwoPhase = false;
				return;
			}
			// vapour + condensate in equilibrium: solve u = x u''(T) + (1-x) u'(T), x from the volume constraint
			auto umix = [&](double T, double& x, double& vv)
			{
				vv = 1.0 / Eos::RhoVapSat(T); const double vl = 1.0 / Eos::RhoLiqSat(T);
				x = Clampd((v - vl) / std::max(vv - vl, 1e-12), 0.0, 1.0);
				return x * Eos::U(T, vv) + (1.0 - x) * Eos::U(T, std::max(vl, Eos::kVMin));
			};
			double a = kTtriple, b = Tb, x = 1, vv = v;
			double fa = umix(a, x, vv) - u, fb = umix(b, x, vv) - u, Tm = T1;
			if (fa >= 0) Tm = a; else if (fb <= 0) Tm = b;
			else
			{
				int side = 0;
				for (int it = 0; it < 60; ++it)
				{
					Tm = (a * fb - b * fa) / (fb - fa);
					const double fm = umix(Tm, x, vv) - u;
					if (std::fabs(fm) < 1e-9 * std::fabs(u) + 1e-6 || b - a < 1e-10) break;
					if (fm > 0) { b = Tm; fb = fm; if (side == 1) fa *= 0.5; side = 1; }
					else { a = Tm; fa = fm; if (side == -1) fb *= 0.5; side = -1; }
				}
			}
			umix(Tm, x, vv);
			const Eos::Props Pv = Eos::Evaluate(Tm, vv);
			g.T = Tm; g.p = Eos::PSat(Tm); g.rho = 1.0 / v; g.h = Pv.u + g.p * vv;   // vapour is drawn off
			g.w = Pv.w; g.cp = Pv.cp; g.beta = Pv.beta; g.quality = x; g.bTwoPhase = true;
		}

		// ------------------------------------------------------------------ vessel wall (1-D, liner + overwrap)
		void BuildWall(int t)
		{
			const TankSpec& ts = C.Tank[t];
			WallModel& w = Wall[t];
			const double L = ts.CylLengthM();
			auto area = [&](double r) { return 2.0 * kPiCG * r * L + 4.0 * kPiCG * r * r; };
			auto vol = [&](double r) { return kPiCG * r * r * L + 4.0 / 3.0 * kPiCG * r * r * r; };
			const double fr[kWallNodes] = { 0.05, 0.15, 0.30, 0.50, 0.04, 0.08, 0.13, 0.20, 0.25, 0.30 };
			double dx[kWallNodes]; const Material* mat[kWallNodes];
			for (int k = 0; k < kWallNodes; ++k)
			{
				dx[k] = k < w.NL ? fr[k] * ts.LinerThkM : fr[k] * ts.OverwrapThkM;
				mat[k] = k < w.NL ? &C.Liner : &C.Overwrap;
			}
			double r = ts.InnerRadiusM;
			w.Ain = area(r);
			WallMass[t] = 0; LinerMass[t] = 0;
			for (int k = 0; k < kWallNodes; ++k)
			{
				const double r2 = r + dx[k], dv = vol(r2) - vol(r);
				w.Cap[k] = mat[k]->rho * mat[k]->cp * dv;
				WallMass[t] += mat[k]->rho * dv;
				if (k < w.NL) LinerMass[t] += mat[k]->rho * dv;
				if (k + 1 < kWallNodes)
					w.G[k] = area(r2) / (dx[k] / (2.0 * mat[k]->k) + dx[k + 1] / (2.0 * mat[k + 1]->k));
				r = r2;
			}
			w.HalfRin = dx[0] / (2.0 * mat[0]->k);
			const double Aout = area(r);
			w.Gout = 1.0 / (1.0 / (C.OuterHtc * Aout) + dx[kWallNodes - 1] / (2.0 * mat[kWallNodes - 1]->k * Aout));
		}
		// Turbulent natural convection inside the vessel: h = 0.13 k (g beta dT rho^2 cp / (mu k))^(1/3).
		double InnerHtc(const Gas& g, double dT) const
		{
			const double k = Eos::Conductivity(g.T, g.rho), mu = Eos::Viscosity(g.T, g.rho);
			const double grp = C.BuoyancyG * std::fabs(g.beta) * std::fabs(dT) * g.rho * g.rho * g.cp / (mu * k);
			return std::max(2.0, 0.13 * k * std::cbrt(grp));
		}

		// ------------------------------------------------------------------ HP header (massless): tanks -> regulator
		struct HeaderSol { double p = 0, Wreg = 0, hMix = 0, W[2] = { 0, 0 }, pmax = 0; };
		double IsoArea(int t) const
		{
			const double s = IsoPos[t];
			return C.Tank[t].IsoAreaM2 * s * s * (3.0 - 2.0 * s);
		}
		HeaderSol SolveHeader(double pm) const
		{
			HeaderSol S;
			int src = -1; double pmax = -1, pmin = 1e30;
			double A[2] = { 0, 0 };
			for (int t = 0; t < NumTanks; ++t)
			{
				A[t] = IsoArea(t);
				if (A[t] <= 0) continue;
				if (Tk[t].p > pmax) { pmax = Tk[t].p; src = t; }
				pmin = std::min(pmin, Tk[t].p);
			}
			if (src < 0) { S.p = pm; S.hMix = Tk[0].h; S.pmax = pm; return S; }
			S.pmax = pmax;
			const double rhoSrc = Tk[src].rho, pSrc = Tk[src].p;
			const double Ar = C.RegAreaM2 * RegX;
			double W[2] = { 0, 0 };
			auto g = [&](double ph)
			{
				const double rhoH = rhoSrc * ph / pSrc;
				double sum = 0;
				for (int t = 0; t < NumTanks; ++t)
				{
					if (A[t] <= 0) { W[t] = 0; continue; }
					W[t] = Tk[t].p >= ph ? Orifice(A[t], Tk[t].p, Tk[t].rho, ph, C.IsoXt) : -Orifice(A[t], ph, rhoH, Tk[t].p, C.IsoXt);
					sum += W[t];
				}
				return sum - Orifice(Ar, ph, rhoH, pm, C.RegXt);
			};
			double lo = std::min(pmin, pm), hi = pmax;
			double flo = g(lo), fhi = g(hi), ph = hi;
			if (fhi >= 0.0) ph = hi;
			else if (flo <= 0.0) ph = lo;
			else
			{
				int side = 0;
				for (int it = 0; it < 80; ++it)
				{
					ph = (lo * fhi - hi * flo) / (fhi - flo);
					const double f = g(ph);
					if (std::fabs(f) < 1e-12 || hi - lo < 1e-11 * hi + 1e-4) break;
					if (f < 0) { hi = ph; fhi = f; if (side == 1) flo *= 0.5; side = 1; }
					else { lo = ph; flo = f; if (side == -1) fhi *= 0.5; side = -1; }
				}
			}
			g(ph);
			S.p = ph;
			double wpos = 0, hpos = 0, sum = 0;
			for (int t = 0; t < NumTanks; ++t)
			{
				S.W[t] = W[t]; sum += W[t];
				if (W[t] > 0) { wpos += W[t]; hpos += W[t] * Tk[t].h; }
			}
			S.Wreg = std::max(0.0, sum);
			if (sum < 0.0)   // tiny negative residual from the root tolerance: give it back to the source tank
			{
				S.W[src] -= sum; S.Wreg = 0.0;
			}
			S.hMix = wpos > 0 ? hpos / wpos : Tk[src].h;
			return S;
		}

		// ------------------------------------------------------------------ nozzle: valve + line + plenum + CD nozzle
		NozzleOutput EvalNozzle(int i, double stroke, double pm, double Tm, double rhom, double pa) const
		{
			NozzleOutput o; o.Valve = stroke;
			const NozzleSpec& nz = C.Nozzle[i];
			if (!nz.bPresent || stroke <= 1e-9 || pm <= pa * (1.0 + 1e-6) || rhom <= 0.0) return o;
			const IsentropeTable& tab = IsentropeTable::Get();
			const NozzleCache& nc = Nz[i];
			const double At = nz.ThroatArea(), Ae = nz.ExitArea(), Al = nz.LineArea();
			const double Av = stroke * nz.Valve.AreaRatio * At;
			const double Aeq = 1.0 / std::sqrt(1.0 / (Av * Av) + nz.LineK / (Al * Al));
			const double mu = tab.MuJT(tab.Locate(pm, Tm));
			const double xT = nz.Valve.xT;
			struct TF { double W = 0, T0 = 0; IsentropeTable::Cell cell{}; bool choked = false; };
			auto throat = [&](double pc, TF& r)
			{
				const double Tc = Tm - mu * (pm - pc);
				r.cell = tab.Locate(pc, Tc); r.T0 = r.cell.T0;
				if (pc * nc.prSub >= pa)
				{
					r.W = nz.Cd * At * tab.Cstar(r.cell) * pc / std::sqrt(kR * r.T0); r.choked = true;
				}
				else
				{
					const double Te = r.T0 * std::pow(pa / pc, 2.0 / 7.0);
					const double ve = std::sqrt(std::max(0.0, 7.0 * kR * (r.T0 - Te)));
					r.W = nz.Cd * Ae * pa / (kR * Te) * ve; r.choked = false;
				}
				return r.W;
			};
			TF tf;
			// plenum pressure: valve flow (decreasing in pc) = throat flow (increasing in pc)
			double lo = pa, hi = pm;
			double flo = Orifice(Aeq, pm, rhom, lo, xT), fhi = -throat(hi, tf);
			double pc = hi;
			if (flo <= 0.0) pc = lo;
			else
			{
				int side = 0;
				for (int it = 0; it < 60; ++it)
				{
					pc = (lo * fhi - hi * flo) / (fhi - flo);
					const double f = Orifice(Aeq, pm, rhom, pc, xT) - throat(pc, tf);
					if (std::fabs(f) < 1e-10 * (tf.W + 1e-9) || hi - lo < 1e-10 * pm) break;
					if (f < 0) { hi = pc; fhi = f; if (side == 1) flo *= 0.5; side = 1; }
					else { lo = pc; flo = f; if (side == -1) fhi *= 0.5; side = -1; }
				}
			}
			const double W = throat(pc, tf);
			if (W <= 0.0) return o;
			const double T0 = tf.T0, sRT = std::sqrt(kR * T0);
			double F = 0, pe = pa, ve = 0, Te = T0;
			bool sep = false;
			if (tf.choked)
			{
				double pr, vr, tr;
				tab.SampleEps(tf.cell, nc.kE, nc.fE, pr, vr, tr);
				pe = pr * pc; ve = vr * sRT; Te = tr * T0;
				const double psep = C.SeparationRatio * pa;
				if (pe >= psep) F = nz.Lambda * nz.EtaV * W * ve + (pe - pa) * Ae;
				else
				{
					// flow separates where the wall pressure falls to psep; the separated skirt carries ambient pressure
					double p1, v1, t1; tab.Sample(tf.cell, 0, p1, v1, t1);
					double epsS = 1, prS = p1, vrS = v1, trS = t1;
					for (int k = 1; k <= std::min(nc.kE + 1, IsentropeTable::NE - 1); ++k)
					{
						double p2, v2, t2; tab.Sample(tf.cell, k, p2, v2, t2);
						if (p2 * pc < psep)
						{
							const double f = Clampd((std::log(psep / pc) - std::log(p1)) / (std::log(p2) - std::log(p1)), 0.0, 1.0);
							const double le1 = std::log(IsentropeTable::kEps[k - 1]), le2 = std::log(IsentropeTable::kEps[k]);
							epsS = std::exp(le1 + f * (le2 - le1)); prS = psep / pc; vrS = v1 + f * (v2 - v1); trS = t1 + f * (t2 - t1);
							break;
						}
						p1 = p2; v1 = v2; t1 = t2;
					}
					epsS = std::min(epsS, nz.AreaRatio());
					pe = prS * pc; ve = vrS * sRT; Te = trS * T0;
					F = nz.Lambda * nz.EtaV * W * ve + (pe - pa) * epsS * At;
					sep = true;
				}
			}
			else
			{
				Te = T0 * std::pow(pa / pc, 2.0 / 7.0);
				ve = std::sqrt(std::max(0.0, 7.0 * kR * (T0 - Te)));
				pe = pa; F = nz.EtaV * W * ve;
			}
			o.ThrustN = std::max(0.0, F);
			o.MassFlowKgS = W;
			o.GasExitVelocity = ve;
			o.ExitVelocity = o.ThrustN / W;
			o.PlumeTempK = Te;
			o.ExitPressurePa = pe;
			o.ExitMach = ve / std::sqrt(1.4 * kR * std::max(Te, 1.0));
			o.Supersaturation = Te < kTc ? pe / Eos::PSatExtended(Te) : 0.0;
			o.PlenumBar = pc / kBarPa; o.PlenumK = T0;
			o.ThrustFrac = nz.RatedThrustN > 0 ? o.ThrustN / nz.RatedThrustN : 0.0;
			o.bChoked = tf.choked; o.bSeparated = sep;
			return o;
		}

		// ------------------------------------------------------------------ actuators
		static double Delayed(const ValveState& v, double tq)
		{
			double val = v.EvBase;
			for (int n = 0; n < v.EvCount; ++n)
			{
				const int idx = (v.EvHead - v.EvCount + n + 64) & 31;
				if (v.EvT[idx] <= tq + 1e-12) val = v.EvV[idx]; else break;
			}
			return val;
		}
		static void PushEvent(ValveState& v, double t, double val)
		{
			if (v.EvCount == 32) { v.EvBase = v.EvV[(v.EvHead - 32 + 64) & 31]; --v.EvCount; }
			v.EvT[v.EvHead] = t; v.EvV[v.EvHead] = val; v.EvHead = (v.EvHead + 1) & 31; ++v.EvCount;
		}
		void UpdateValve(int i, double t0, double h)
		{
			ValveState& v = Vs[i];
			const ValveSpec& sp = C.Nozzle[i].Valve;
			double out = 0;
			if (C.Mode == ValveMode::Proportional)
			{
				const double u = v.Cmd < sp.MinStroke ? 0.0 : v.Cmd;
				if (u > 0) { out = u; v.Held = u; }
				else if (v.Out > 0 && t0 - v.OnStart < sp.MinOnTimeS - 1e-12) out = v.Held;   // stretch to the minimum on-time
			}
			else
			{
				const double period = 1.0 / std::max(C.PwmHz, 0.1);
				if (v.Cmd <= 1e-6)
				{
					v.PwmStart = -1;
					if (v.Out > 0 && t0 - v.OnStart < sp.MinOnTimeS - 1e-12) out = 1.0;
				}
				else
				{
					if (v.PwmStart < 0 || t0 - v.PwmStart >= period - 1e-12)
					{
						v.PwmStart = v.PwmStart < 0 ? t0 : v.PwmStart + period * std::floor((t0 - v.PwmStart) / period + 1e-9);
						double ton = v.Cmd * period;
						if (v.Cmd >= 1.0 - 1e-9) ton = period;
						else if (ton < sp.MinOnTimeS) ton = ton >= 0.5 * sp.MinOnTimeS ? sp.MinOnTimeS : 0.0;
						v.PwmOn = ton;
					}
					out = t0 - v.PwmStart < v.PwmOn - 1e-12 ? 1.0 : 0.0;
					if (out == 0.0 && v.Out > 0 && t0 - v.OnStart < sp.MinOnTimeS - 1e-12) out = 1.0;
				}
			}
			if (out != v.Out)
			{
				if (out > 0 && v.Out <= 0) v.OnStart = t0;
				v.Out = out;
				PushEvent(v, t0, out);
			}
			const double co = Delayed(v, t0 - sp.DeadOpenS), cc = Delayed(v, t0 - sp.DeadCloseS);
			const double e = sp.DeadCloseS < sp.DeadOpenS ? std::min(co, cc) : std::max(co, cc);
			const double tau = e > v.Pos ? sp.TauOpenS : sp.TauCloseS;
			double d = (e - v.Pos) * (1.0 - std::exp(-h / tau));
			d = Clampd(d, -sp.SlewClose * h, sp.SlewOpen * h);
			v.Pos = Clampd(v.Pos + d, 0.0, 1.0);
			if (v.Pos < 1e-7 && e <= 0.0) v.Pos = 0.0;
		}
		double RegulatorTarget() const
		{
			if (Dome < 0.5 * kBarPa) return 0.0;
			const double plock = Dome * (1.0 + C.RegLockupFrac) + C.RegSPE * (FillBar * kBarPa - HdrP);
			const double band = std::max((C.RegLockupFrac + C.RegDroopFrac) * Dome, 0.5 * kBarPa);
			return Clampd((plock - Man.p) / band, 0.0, 1.0);
		}
		// Nothing can move gas: all nozzle valves shut and uncommanded, no header flow, actuators settled. The large
		// idle sub-step then only carries the (slow) wall conduction and gas/wall heat exchange.
		bool IsIdle(double pa) const
		{
			(void)pa;
			for (int i = 0; i < kNumNozzles; ++i) if (C.Nozzle[i].bPresent && (Vs[i].Cmd > 0 || Vs[i].Out > 0 || Vs[i].Pos > 0)) return false;
			if (HdrWreg > 1e-9 || HdrWabs > 1e-7) return false;
			bool anyOpen = false;
			for (int t = 0; t < NumTanks; ++t)
			{
				const double tgt = IsoTarget(t) ? 1.0 : 0.0;
				if (std::fabs(IsoPos[t] - tgt) > 0.0 || Relief[t]) return false;
				anyOpen = anyOpen || IsoPos[t] > 0.0;
			}
			if (anyOpen && (RegX > 1e-9 || RegulatorTarget() > 0.0)) return false;
			const double domeT = bArmed ? C.RegSetpointBar * kBarPa : 0.0;
			return std::fabs(Dome - domeT) <= 1e-6 * std::max(domeT, kBarPa);
		}
		bool IsoTarget(int t) const { return bArmed && IsoCmd[t] && !Cutoff[t]; }

		// ------------------------------------------------------------------ one sub-step
		void Substep(double h, double pa, double Tamb)
		{
			const double t0 = Time;
			// actuators
			for (int t = 0; t < NumTanks; ++t)
			{
				if (bArmed && Tk[t].p < C.MinTankBar * kBarPa && C.MinTankBar > 0) Cutoff[t] = true;
				const double tgt = IsoTarget(t) ? 1.0 : 0.0, rate = h / std::max(C.Tank[t].IsoTravelS, 1e-3);
				IsoPos[t] = tgt > IsoPos[t] ? std::min(tgt, IsoPos[t] + rate) : std::max(tgt, IsoPos[t] - rate);
			}
			const double domeT = bArmed ? C.RegSetpointBar * kBarPa : 0.0;
			const double ramp = C.DomeRampBarS * kBarPa * h;
			DomeCmd = domeT > DomeCmd ? std::min(domeT, DomeCmd + ramp) : std::max(domeT, DomeCmd - 4.0 * ramp);   // vents faster than it loads
			Dome += (DomeCmd - Dome) * (1.0 - std::exp(-h / C.DomeTauS));
			if (std::fabs(Dome - domeT) < 1e-9 * kBarPa && DomeCmd == domeT) Dome = domeT;
			for (int i = 0; i < kNumNozzles; ++i) if (C.Nozzle[i].bPresent) UpdateValve(i, t0, h);
			RegX += (RegulatorTarget() - RegX) * (1.0 - std::exp(-h / C.RegTauS));
			if (RegX < 1e-12) RegX = 0.0;

			// gas <- wall heat
			for (int t = 0; t < NumTanks; ++t)
			{
				const double dT = Wall[t].T[0] - Tk[t].T;
				const double hin = InnerHtc(Tk[t], dT);
				const double G = 1.0 / (1.0 / (hin * Wall[t].Ain) + Wall[t].HalfRin / Wall[t].Ain);
				TankHtc[t] = hin; TankQ[t] = G * dT;
			}
			// manifold gas <- line/accumulator walls: natural convection, or Dittus-Boelter with the through-flow
			{
				const double dT = ManWallT - Man.T;
				const double k = Eos::Conductivity(Man.T, Man.rho), mu = Eos::Viscosity(Man.T, Man.rho);
				const double D = C.ManifoldLineDiaM, Wthru = std::max(HdrWreg, 0.0);
				const double Re = 4.0 * Wthru / (kPiCG * D * mu), Pr = mu * Man.cp / k;
				const double hf = 0.023 * std::pow(std::max(Re, 1.0), 0.8) * std::pow(Pr, 0.4) * k / D;
				const double hin = std::max(InnerHtc(Man, dT), hf);
				ManQ = hin * C.ManifoldWallAreaM2 * dT;
				// keep the explicit exchange stable for a small gas volume
				const double capG = std::max(Man.m * Man.cp, 1e-6), capW = C.ManifoldWallKg * kSteelCp;
				const double lim = 0.5 * std::fabs(dT) / h / (1.0 / capG + 1.0 / capW);
				ManQ = Clampd(ManQ, -lim, lim);
			}

			// linearly-implicit manifold pressure predictor
			const double c2 = Man.w * Man.w, kV = c2 / Man.V;
			const HeaderSol h0 = SolveHeader(Man.p);
			const double dp = 10.0 + 1e-5 * Man.p;
			const HeaderSol h1 = SolveHeader(Man.p + dp);
			const double dWr = (h1.Wreg - h0.Wreg) / dp;
			double Wo = 0, dWo = 0;
			for (int i = 0; i < kNumNozzles; ++i) { Wo += Nz[i].prevW; dWo += Nz[i].prevW / std::max(Man.p, 1.0); }
			const double dpPred = h * kV * (h0.Wreg - Wo) / (1.0 + h * kV * (dWo - dWr));
			const double pPred = Clampd(Man.p + dpPred, 0.3 * Man.p, std::max(Man.p, h0.pmax));
			const double rhoPred = std::max(1e-6, Man.rho + (pPred - Man.p) / c2);
			const HeaderSol hs = SolveHeader(pPred);
			HdrP = hs.p; HdrWreg = hs.Wreg;
			HdrWabs = 0; for (int t = 0; t < NumTanks; ++t) HdrWabs += std::fabs(hs.W[t]);

			// nozzles
			double Wsum = 0;
			for (int i = 0; i < kNumNozzles; ++i)
			{
				Nz[i].prevW = 0;
				if (!C.Nozzle[i].bPresent) continue;
				if (Vs[i].Pos <= 0.0)
				{
					NozzleOutput& o = NzOut[i];
					const double keepT = o.PlumeTempK;
					o = NozzleOutput(); o.PlumeTempK = keepT > 0 ? keepT : Man.T;
					continue;
				}
				NozzleOutput o = EvalNozzle(i, Vs[i].Pos, pPred, Man.T, rhoPred, pa);
				Nz[i].prevW = o.MassFlowKgS;
				Nz[i].accF += o.ThrustN * h; Nz[i].accW += o.MassFlowKgS * h;
				Impulse += o.ThrustN * h; BankImpulse[C.Nozzle[i].Bank] += o.ThrustN * h;
				Wsum += o.MassFlowKgS;
				const double keepF = NzOut[i].ThrustN, keepW = NzOut[i].MassFlowKgS;
				NzOut[i] = o; NzOut[i].ThrustN = keepF; NzOut[i].MassFlowKgS = keepW;   // averaged values set in Step()
			}

			// integrate the vessels (mass, energy, relief)
			for (int t = 0; t < NumTanks; ++t)
			{
				Gas& g = Tk[t];
				const double W = hs.W[t];
				g.m -= h * W;
				g.U -= h * W * (W > 0 ? g.h : hs.hMix);
				g.U += h * TankQ[t];
				if (g.p > C.Tank[t].ReliefBar * kBarPa) Relief[t] = true;
				if (g.p < 0.95 * C.Tank[t].ReliefBar * kBarPa) Relief[t] = false;
				if (Relief[t])
				{
					const double Wv = Orifice(C.Tank[t].ReliefAreaM2, g.p, g.rho, pa, 0.53);
					g.m -= h * Wv; g.U -= h * Wv * g.h;
					Mvent += h * Wv; Hvent += h * Wv * g.h;
				}
			}
			// manifold
			const double hMan = Man.h;
			Man.m += h * (hs.Wreg - Wsum);
			Man.U += h * (hs.Wreg * hs.hMix - Wsum * hMan + ManQ);
			Mout += h * Wsum; Hout += h * Wsum * hMan;
			{
				const double qa = C.ManifoldAmbientWK * (Tamb - ManWallT);
				ManWallT += h * (qa - ManQ) / (C.ManifoldWallKg * kSteelCp);
				Qamb += h * qa;
			}

			// vessel walls
			for (int t = 0; t < NumTanks; ++t)
			{
				WallModel& w = Wall[t];
				double dE[kWallNodes];
				for (int k = 0; k < kWallNodes; ++k) dE[k] = 0;
				dE[0] -= TankQ[t];
				for (int k = 0; k + 1 < kWallNodes; ++k) { const double q = w.G[k] * (w.T[k] - w.T[k + 1]); dE[k] -= q; dE[k + 1] += q; }
				const double qa = w.Gout * (Tamb - w.T[kWallNodes - 1]);
				dE[kWallNodes - 1] += qa; Qamb += h * qa;
				for (int k = 0; k < kWallNodes; ++k) w.T[k] += h * dE[k] / w.Cap[k];
			}

			for (int t = 0; t < NumTanks; ++t) Solve(Tk[t]);
			Solve(Man);
			Time = t0 + h;
		}

		void UpdateTelemetry(double pa)
		{
			(void)pa;
			T.NumTanks = NumTanks;
			double pv = 0, vv = 0;
			for (int t = 0; t < NumTanks; ++t)
			{
				TankTelemetry& o = T.Tank[t];
				const Gas& g = Tk[t];
				o.PressureBar = g.p / kBarPa; o.TempK = g.T; o.MassKg = g.m; o.DensityKgM3 = g.rho;
				o.LinerInnerK = Wall[t].T[0];
				double mn = 1e9; for (int k = 0; k < Wall[t].NL; ++k) mn = std::min(mn, Wall[t].T[k]);
				o.LinerMinK = mn; o.OverwrapOuterK = Wall[t].T[kWallNodes - 1];
				o.InnerHtc = TankHtc[t]; o.WallHeatW = TankQ[t];
				o.VapourFraction = g.quality; o.bTwoPhase = g.bTwoPhase;
				o.bLinerBelowRating = mn < C.LinerMinK; o.bReliefOpen = Relief[t]; o.bCutoff = Cutoff[t];
				o.IsoOpen = IsoPos[t];
				pv += g.p * g.V; vv += g.V;
			}
			T.HeaderBar = HdrP / kBarPa;
			T.DomeBar = Dome / kBarPa; T.RegOpening = RegX;
			T.ManifoldBar = Man.p / kBarPa; T.ManifoldK = Man.T;
			T.RegOutletBar = T.ManifoldBar; T.RegOutletK = Man.T;
			T.bRegDropout = Dome > kBarPa && RegX > 0.97 && Man.p < Dome * (1.0 - C.RegDroopFrac);
			T.bRegLockedUp = Dome > kBarPa && RegX < 1e-3 && Man.p >= Dome;
			double F = 0, W = 0;
			for (int i = 0; i < kNumNozzles; ++i) { T.ValvePos[i] = Vs[i].Pos; F += NzOut[i].ThrustN; W += NzOut[i].MassFlowKgS; }
			T.TotalThrustN = F; T.TotalMassFlowKgS = W;
			T.TotalImpulseNs = Impulse; for (int b = 0; b < 3; ++b) T.BankImpulseNs[b] = BankImpulse[b];
			T.MassExpelledKg = Mout; T.MassVentedKg = Mvent;
			T.GasMassKg = GasMassKg(); T.UsableGasKg = UsableGasKg();
			T.GasFraction = UsableFull > 1e-9 ? Clampd(T.UsableGasKg / UsableFull, 0.0, 1.0) : 0.0;
			T.DisplayBar = vv > 0 ? pv / vv / kBarPa : 0.0; T.DisplayPsi = T.DisplayBar * kPsiPerBar;
			T.SimTime = Time; T.bArmed = bArmed;
		}
	};
}
}
