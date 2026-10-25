#pragma once
// Tesla Roadster (2nd generation, revealed 15 Oct 2026) vehicle dynamics core.
// Pure C++17 with no engine types, so it is unit-tested outside Unreal (Tests/PhysicsHarness.cpp).
//
// Frames: world and body are both +X forward, +Y right, +Z up; metres, seconds, kilograms, newtons.
// Body origin is on the ground plane midway between the axles. The rigid-body state tracks the centre of mass.
//
// Provenance of every number: Research/roadster_specs.md. Tesla publishes almost no figures, so values
// tagged EXEC come from engineering-team interviews, and values tagged EST are engineering estimates
// chosen to reproduce the published performance.

#include <cmath>
#include <cstdint>
#include <functional>
#include <algorithm>
#include <array>
#include "ColdGasSystem.h"

namespace Roadster
{
	// ------------------------------------------------------------------ maths
	struct V3
	{
		double x = 0, y = 0, z = 0;
		V3() = default;
		V3(double X, double Y, double Z) : x(X), y(Y), z(Z) {}
		V3 operator+(const V3& o) const { return { x + o.x, y + o.y, z + o.z }; }
		V3 operator-(const V3& o) const { return { x - o.x, y - o.y, z - o.z }; }
		V3 operator-() const { return { -x, -y, -z }; }
		V3 operator*(double s) const { return { x * s, y * s, z * s }; }
		V3 operator/(double s) const { return { x / s, y / s, z / s }; }
		V3& operator+=(const V3& o) { x += o.x; y += o.y; z += o.z; return *this; }
		V3& operator-=(const V3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
		V3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
		double Dot(const V3& o) const { return x * o.x + y * o.y + z * o.z; }
		V3 Cross(const V3& o) const { return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x }; }
		double Len() const { return std::sqrt(x * x + y * y + z * z); }
		double Len2() const { return x * x + y * y + z * z; }
		V3 Norm() const { double l = Len(); return l > 1e-12 ? *this / l : V3(0, 0, 0); }
		V3 Mul(const V3& o) const { return { x * o.x, y * o.y, z * o.z }; }
	};

	struct Quat
	{
		double w = 1, x = 0, y = 0, z = 0;
		Quat() = default;
		Quat(double W, double X, double Y, double Z) : w(W), x(X), y(Y), z(Z) {}
		static Quat AxisAngle(const V3& axis, double angle)
		{
			V3 a = axis.Norm(); double s = std::sin(angle * 0.5);
			return { std::cos(angle * 0.5), a.x * s, a.y * s, a.z * s };
		}
		Quat operator*(const Quat& q) const
		{
			return { w * q.w - x * q.x - y * q.y - z * q.z,
			         w * q.x + x * q.w + y * q.z - z * q.y,
			         w * q.y - x * q.z + y * q.w + z * q.x,
			         w * q.z + x * q.y - y * q.x + z * q.w };
		}
		Quat Conj() const { return { w, -x, -y, -z }; }
		Quat Normalized() const { double l = std::sqrt(w * w + x * x + y * y + z * z); return { w / l, x / l, y / l, z / l }; }
		V3 Rotate(const V3& v) const
		{
			V3 u(x, y, z); V3 t = u.Cross(v) * 2.0;
			return v + t * w + u.Cross(t);
		}
		V3 Unrotate(const V3& v) const { return Conj().Rotate(v); }
		V3 Fwd() const { return Rotate({ 1, 0, 0 }); }
		V3 Right() const { return Rotate({ 0, 1, 0 }); }
		V3 Up() const { return Rotate({ 0, 0, 1 }); }
	};

	inline double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
	inline double Lerp(double a, double b, double t) { return a + (b - a) * t; }
	inline double Sign(double v) { return v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0); }
	inline double MoveTo(double cur, double target, double maxDelta)
	{
		if (std::fabs(target - cur) <= maxDelta) return target;
		return cur + Sign(target - cur) * maxDelta;
	}

	constexpr double kG = 9.80665;
	constexpr double kPi = 3.14159265358979323846;
	constexpr double kMph = 0.44704;
	constexpr double kPsiPerBar = 14.5038;
	constexpr double kPsiPerKPa = 0.145038;
	constexpr double kN2GasConstant = 296.8;   // J/(kg K)

	// ------------------------------------------------------------------ world query
	struct Hit
	{
		bool bHit = false;
		double Dist = 0;        // distance from start to the contact surface along dir
		double CenterDist = 0;  // distance travelled by the sweep sphere centre
		V3 Point;
		V3 Normal{ 0, 0, 1 };
	};
	// Sweep a sphere of radius r from start along unit dir for len metres.
	using TraceFn = std::function<Hit(const V3& start, const V3& dir, double len, double radius)>;

	// ------------------------------------------------------------------ enums (names match the car's UI)
	enum class Trim : uint8_t { Performance, Thrust, Rocket };
	enum class FanMode : uint8_t { Off, Braking, LowDrag, MaxGrip, Custom };
	enum class Accel : uint8_t { Chill, Standard, Sport };
	enum class Handling : uint8_t { Comfort, Balance, Sport };
	enum class FrontWing : uint8_t { LowDrag, HighDownforce };
	enum class RearWing : uint8_t { Stowed, LowDrag, HighDownforce };
	enum class AeroLoad : uint8_t { LowDrag, Balance, MaxGrip };
	enum class DynamicsPreset : uint8_t { Comfort, Balance, Sport, Custom, Track };
	enum class AeroPreset : uint8_t { Auto, LowDrag, HighDownforce, Custom };

	inline const char* TrimName(Trim t) { switch (t) { case Trim::Performance: return "Performance"; case Trim::Thrust: return "Thrust"; default: return "Rocket"; } }
	inline const char* FanModeName(FanMode m) { switch (m) { case FanMode::Off: return "Off"; case FanMode::Braking: return "Braking"; case FanMode::LowDrag: return "Low Drag"; case FanMode::MaxGrip: return "Max Grip"; default: return "Custom"; } }
	inline const char* AccelName(Accel a) { switch (a) { case Accel::Chill: return "Chill"; case Accel::Standard: return "Standard"; default: return "Sport"; } }
	inline const char* HandlingName(Handling a) { switch (a) { case Handling::Comfort: return "Comfort"; case Handling::Balance: return "Balance"; default: return "Sport"; } }
	inline const char* FrontWingName(FrontWing w) { return w == FrontWing::LowDrag ? "Low Drag" : "High Downforce"; }
	inline const char* RearWingName(RearWing w) { switch (w) { case RearWing::Stowed: return "Stowed"; case RearWing::LowDrag: return "Low Drag"; default: return "High Downforce"; } }
	inline const char* AeroLoadName(AeroLoad a) { switch (a) { case AeroLoad::LowDrag: return "Low Drag"; case AeroLoad::Balance: return "Balance"; default: return "Max Grip"; } }
	inline const char* AeroPresetName(AeroPreset a) { switch (a) { case AeroPreset::Auto: return "Auto"; case AeroPreset::LowDrag: return "Low Drag"; case AeroPreset::HighDownforce: return "High Downforce"; default: return "Custom"; } }
	inline const char* DynamicsName(DynamicsPreset d) { switch (d) { case DynamicsPreset::Comfort: return "Comfort"; case DynamicsPreset::Balance: return "Balance"; case DynamicsPreset::Sport: return "Sport"; case DynamicsPreset::Custom: return "Custom"; default: return "Track"; } }

	// ------------------------------------------------------------------ specification
	struct Spec
	{
		// Chassis (dimensions: DERIVED from Tesla's side photo, wheelbase per Wikipedia)
		double DryMassKg = 1700;          // EXEC target (<1,700 kg), Performance trim
		double CGHeight = 0.42;           // EST
		double CGForward = -0.226;        // EST 42/58 front/rear
		V3 Inertia{ 520, 2250, 2500 };    // EST (roll, pitch, yaw)
		double Wheelbase = 2.825;
		double TrackFront = 1.70;
		double TrackRear = 1.68;
		double Length = 4.38;
		double Width = 2.00;
		double Height = 1.17;
		double FrontOverhang = 0.82;
		double RearOverhang = 0.735;
		double RideHeight = 0.120;        // road mode sill clearance
		double LowRideDrop = 0.025;       // "Track Mode lowers the car" (OFFICIAL), amount EST

		// Tires: 295/30 R20 front, 335/30 R21 rear, Pirelli P Zero Trofeo (PRESS / photo)
		double WheelRadiusFront = 0.3425;
		double WheelRadiusRear = 0.3672;
		double TireWidthFront = 0.295;
		double TireWidthRear = 0.335;
		double WheelInertiaFront = 1.35;
		double WheelInertiaRear = 1.85;
		double PeakMu = 1.45;             // EST, Trofeo-class semi-slick
		double SlideMuRatio = 0.80;
		double PeakSlipRatio = 0.10;
		double PeakSlipAngleDeg = 7.0;
		double LoadSensitivity = 0.085;
		double RelaxationLength = 0.30;
		double OptimalHotPressureKPa = 193;   // 28 psi hot, as on Tesla's Track Mode screen
		double DefaultColdPressureKPa = 159;  // 23 psi cold
		double OptimalTireTempC = 85;         // ~185 F, matches UI frames (176-182 F)
		double RollingResistance = 0.0105;
		double AmbientC = 24;

		// Suspension (rates at the wheel) - double wishbone, adaptive (EXEC); rates EST
		double SuspensionTravel = 0.11;
		double StaticCompression = 0.060;
		double SpringRateFront = 70000;
		double SpringRateRear = 82000;
		double DampingBump = 5200;
		double DampingRebound = 7800;
		double AntiRollFront = 42000;
		double AntiRollRear = 28000;
		double BumpStopRate = 900000;
		double ProgressiveRate = 1.6;
		double HeaveGain = 500000;        // active ride-height (heave) leveller, N per metre-second
		double HeaveMaxPushN = 30000;
		double HeaveMaxPullN = 8000;

		// Steering: steer-by-wire (OFFICIAL), yoke
		double MaxSteerDeg = 32;
		double Ackermann = 0.6;

		// Powertrain: two rear motors, one per wheel, RWD with torque vectoring (EXEC)
		double RearMotorPeakKW = 520;     // each; ">1,000 kW" total (EXEC)
		double MotorTorqueNm = 760;       // each, motor side; ">1,400 Nm" total (EXEC), ~1,500 hp production
		double GearRatio = 8.3;           // EST, sized for <1.6 s 0-60 (Performance)
		double MotorMaxRPM = 25000;       // EST
		double DrivetrainEfficiency = 0.93;
		double BatteryKWh = 120;          // DERIVED from the 10-min charge claim
		double BatteryMaxDischargeKW = 1250;
		double MaxRegenKW = 350;
		double TopSpeedMps = 105.0;       // ">230 mph" shown at the reveal (limiter on wheel speed)
		double AuxLoadKW = 1.2;

		// Brakes: carbon-ceramic (EXEC); torques EST
		double MaxBrakeTorqueFront = 5400;   // per wheel at 100% front share x2
		double MaxBrakeTorqueRear = 4600;

		// Body aero (EST; Tesla has published no Cd)
		double FrontalArea = 1.92;
		double Cd = 0.30;
		double BodyCl = -0.05;
		double AirDensity = 1.20;
		// Active aero increments (EST): lift coefficient x area, and drag area
		double FrontWingClA_Low = -0.08, FrontWingClA_High = -0.38, FrontWingCdA_High = 0.04;
		double RearWingClA_Low = -0.30, RearWingClA_High = -0.95, RearWingCdA_Low = 0.05, RearWingCdA_High = 0.17;
		double HoodVentClA = -0.06, HoodVentCdA = 0.015;
		double BargeClA = -0.10, BargeCdA = 0.02;

		// Suction system: 4 axial fans in the rear diffuser, ~20 kW each (EXEC), sealed skirts (patent)
		double FanCount = 4;
		double FanMaxPressureKPa = 8.0;   // EST: ~1.5x the Rocket's weight - holds it on curved ceilings (OFFICIAL claim), G-meter scaled to 3.5 g (UI)
		double FanEffectiveArea = 4.2;    // EST: hexagonal sealed floor under the pack
		double SkirtDeployMax = 0.22;     // compliant skirts reach past ride height to the surface (patent), and follow curved floors
		double SkirtResidualGap = 0.006;
		double FanSealGapScale = 0.022;   // gap at which seal efficiency halves
		double FanMaxRPM = 16000;         // ">15,000 rpm" (EXEC)
		double FanSpoolTime = 0.30;
		double FanMaxPowerKW = 80;        // 4 x 20 kW (EXEC)
		double FanCentreForward = -0.20;
		double FanMaxReach = 0.55;

		// Cold-gas thrusters (Thrust / Rocket trims)
		double TankFullBar = 379;          // 5,500 psi as shown full ("100% - Boost Ready") in Tesla's UI
		double GasTempK = 293;
		// Rated thrusts are normalisers for telemetry/plumes; delivered thrust comes from ColdGasSystem.h
		double RearThrustFullN = 80000;    // ">80 kN" from the 4 rear nozzles (EXEC): 4 x 19.9 kN at 150 bar manifold
		double LiftThrustFullN = 13000;    // per corner lift nozzle (Rocket), 12.9 kN rated
		double YawThrustFullN = 4000;      // per front yaw nozzle (Rocket)
		double ExhaustVelocity = 600;      // sea-level effective, fresh gas (Isp ~61 s); blowdown mean is lower
		// Nozzle exits in the body frame (x fwd, y RIGHT, z up; metres), from Tesla's photos:
		// rear bank on the shelf above the fan box (Gravity/Augments), lift nozzles behind each wheel pointing down
		// (World/Gravity), yaw jets at the front corners firing sideways
		V3 RearNozzlePos[4] = { { -2.12, -0.37, 0.585 }, { -2.12, -0.123, 0.585 }, { -2.12, 0.123, 0.585 }, { -2.12, 0.37, 0.585 } };
		V3 LiftNozzlePos[4] = { { 0.95, -0.80, 0.12 }, { 0.95, 0.80, 0.12 }, { -1.95, -0.665, 0.20 }, { -1.95, 0.665, 0.20 } };   // FL FR RL RR
		V3 YawNozzlePos[2] = { { 1.95, -0.75, 0.30 }, { 1.95, 0.75, 0.30 } };   // left-firing (exits left), right-firing

		// Trim-dependent (filled by ApplyTrim)
		Trim TrimLevel = Trim::Rocket;
		double TankVolumeL = 600;
		double TrimExtraMassKg = 200;
		bool bHasRearThrusters = true;
		bool bHasLiftThrusters = true;
		int Seats = 1;

		void ApplyTrim(Trim t)
		{
			TrimLevel = t;
			switch (t)
			{
			case Trim::Performance: TankVolumeL = 0;   TrimExtraMassKg = 0;   bHasRearThrusters = false; bHasLiftThrusters = false; Seats = 2; break;
			// augment hardware mass (gas is separate, TankKg): 690 bar Type-IV vessels (96 kg / 200 L, 187 kg / 400 L,
			// Mirai-class mass efficiency) + valves, regulator, lines and nozzles; Rocket drops the passenger seat
			case Trim::Thrust:      TankVolumeL = 200; TrimExtraMassKg = 135; bHasRearThrusters = true;  bHasLiftThrusters = false; Seats = 2; break;
			case Trim::Rocket:      TankVolumeL = 600; TrimExtraMassKg = 330; bHasRearThrusters = true;  bHasLiftThrusters = true;  Seats = 1; break;
			}
		}
	};

	// ------------------------------------------------------------------ driver inputs
	struct Inputs
	{
		double Throttle = 0;   // 0..1
		double Brake = 0;      // 0..1
		double Steer = 0;      // -1..1 (positive = right)
		double Handbrake = 0;  // 0..1
		double Boost = 0;      // 0..1 rear thrusters (yoke BOOST button)
		double Lift = 0;       // 0..1 corner lift nozzles (Rocket)
		double Pitch = 0;      // -1..1 air control (Rocket), + nose up
		double Roll = 0;       // -1..1 air control (Rocket), + roll right
		double Yaw = 0;        // -1..1 front yaw nozzles (Rocket)
		bool bWantReverse = false;
		bool bDRS = false;     // yoke DRS button: stows the rear wing while held
	};

	// Driver-adjustable settings, mirroring the Vehicle > Dynamics and Track Mode > Setup screens
	struct Settings
	{
		DynamicsPreset Dynamics = DynamicsPreset::Sport;
		Accel Acceleration = Accel::Sport;
		Handling RideHandling = Handling::Sport;
		bool bLowSuspension = false;
		AeroPreset Aero = AeroPreset::Auto;
		FrontWing FWing = FrontWing::HighDownforce;
		RearWing RWing = RearWing::HighDownforce;
		AeroLoad Load = AeroLoad::MaxGrip;
		FanMode Fan = FanMode::MaxGrip;
		double FanCustomKPa = 4.0;         // "control the pressures": underbody suction target
		bool bHoodVentOpen = false;
		bool bBargeDeployed = true;
		int AeroBalance = 1;               // -3..+3, front% = 50 - 2n
		int BrakeBalance = -1;             // -3..+3, front% = 60 - 2n
		bool bESC = true;
		bool bTCS = true;
		double RegenLevel = 1.0;
		double ColdPressureKPa[4] = { 159, 159, 159, 159 };
		double BoostLevel = 1.0;           // valve authority 0.25..1
		bool bThrustersArmed = true;
		bool bFlightAssist = true;         // auto-level with corner nozzles when airborne (Rocket)
		bool bInfiniteGas = false;
		bool bInfiniteBattery = false;
		bool bActiveSuspension = true;
		bool bLaunchControl = true;
		bool bUnrestrictedTopSpeed = false;

		double AeroFrontShare() const { return (50.0 - 2.0 * AeroBalance) / 100.0; }
		double BrakeFrontShare() const { return (60.0 - 2.0 * BrakeBalance) / 100.0; }
	};

	// ------------------------------------------------------------------ runtime state
	struct WheelState
	{
		V3 Hardpoint;              // body frame, relative to body origin
		double Radius = 0.35, Inertia = 1.4, Width = 0.25;
		bool bFront = true, bLeft = true;

		double Omega = 0, Spin = 0, Steer = 0;
		double Length = 0, LengthPrev = 0, LengthVel = 0;   // hardpoint -> wheel centre
		double Compression = 0;
		bool bContact = false;
		V3 ContactPoint, ContactNormal{ 0, 0, 1 };
		double Load = 0, Fx = 0, Fy = 0;
		double Kappa = 0, Alpha = 0;
		double SlideSpeed = 0, GripUsage = 0;
		double DriveTorque = 0, BrakeTorque = 0;
		double TCScale = 1, ABSScale = 1;
		double PressureKPa = 159, SurfaceTempC = 24, CarcassTempC = 24, BrakeTempC = 24, Wear = 0;
		double GripFactor = 1;
	};

	struct BodyProbe { V3 P; double R; };

	struct Telemetry
	{
		double Speed = 0, ForwardSpeed = 0;
		V3 AccelG;                 // specific force in body frame (g), x fwd, y right, z up
		double PowerKW = 0, MotorPowerKW = 0, RegenKW = 0, FanPowerKW = 0;
		double FanRPM = 0, FanPressureKPa = 0, FanTargetKPa = 0, FanDownforceN = 0, FanSeal = 0, FanGap = 0, SkirtDeploy = 0;
		double WingDownforceN = 0, DragN = 0, WingAngle = 0;  // WingAngle 0 stowed .. 1 high downforce
		double FrontWingAngle = 0;
		double TankBar = 0, TankKg = 0, TankPercent = 0, ThrustN = 0;
		double ThrustRear = 0, ThrustYawL = 0, ThrustYawR = 0;
		double ThrustCorner[4] = { 0, 0, 0, 0 };   // 0..1 per corner nozzle (FL, FR, RL, RR)
		double SOC = 1, BatteryKWh = 120, RangeKm = 0, OdometerKm = 0;
		double MotorRPM = 0, MotorTempC = 40, BatteryTempC = 35;
		double RearTorque = 0;
		double HeavePreloadN = 0;
		double UpDotWorld = 1;
		int WheelsInContact = 0;
		bool bReverse = false, bTC = false, bABS = false, bESC = false, bLaunch = false, bBodyContact = false, bAirborne = false;
		double AirTime = 0, LastAirTime = 0;
		double BrakePedal = 0, RegenFrac = 0, AccelPedal = 0;
		double TotalDownforceN() const { return FanDownforceN + WingDownforceN; }

		// Drag-strip timer
		bool bRunActive = false;
		double RunTime = 0, RunDist = 0;
		double T60 = -1, T60Rollout = -1, T100kmh = -1, T100mph = -1, TQuarter = -1, QuarterTrapMph = -1;
		double TopSpeed = 0, PeakG = 0;
	};

	// ================================================================== vehicle
	class Vehicle
	{
	public:
		Spec S;
		Settings Set;
		std::array<WheelState, 4> Wheels;   // FL, FR, RL, RR
		std::array<BodyProbe, 16> Probes;
		int NumProbes = 0;

		// Rigid body (centre of mass)
		V3 X, V, W;
		Quat Q;

		Telemetry T;
		double EnergyKWh = 120;
		double TankKg = 0;
		ColdGas::System Gas;    // valve-level cold-gas system (copyable; the isentrope table is a shared static)
		double FanRPM = 0;
		double SkirtDeploy = 0;   // 0..1
		double WingAngle = 1, FrontWingAngle = 1;
		double HeavePreload = 0;
		double SimTime = 0;
		bool bReverseGear = false;
		uint64_t StepCount = 0;

		V3 CGBody() const { return { S.CGForward, 0, S.CGHeight - (Set.bLowSuspension ? S.LowRideDrop : 0.0) }; }
		double TotalMass() const { return S.DryMassKg + S.TrimExtraMassKg + TankKg; }
		/** Volume-weighted vessel pressure (real-gas nitrogen, ColdGasSystem.h): the UI's "Thruster xxxx PSI". */
		double TankBar() const { return S.TankVolumeL > 0 ? Gas.Tel().DisplayBar : 0.0; }
		double SOC() const { return EnergyKWh / S.BatteryKWh; }
		double TargetCompression() const { return S.StaticCompression; }

		V3 BodyOriginWorld() const { return X - Q.Rotate(CGBody()); }
		V3 BodyToWorld(const V3& pBody) const { return X + Q.Rotate(pBody - CGBody()); }

		double MinLen() const { return 0.10; }
		double MaxLen() const { return MinLen() + S.SuspensionTravel; }

		void Init(Trim trim = Trim::Rocket)
		{
			S.ApplyTrim(trim);
			const double hw_f = S.TrackFront * 0.5, hw_r = S.TrackRear * 0.5;
			const double xf = S.Wheelbase * 0.5, xr = -S.Wheelbase * 0.5;
			auto setup = [&](int i, double x, double y, bool front, bool left)
			{
				WheelState& w = Wheels[i];
				w = WheelState();
				w.bFront = front; w.bLeft = left;
				w.Radius = front ? S.WheelRadiusFront : S.WheelRadiusRear;
				w.Inertia = front ? S.WheelInertiaFront : S.WheelInertiaRear;
				w.Width = front ? S.TireWidthFront : S.TireWidthRear;
				const double staticLen = MaxLen() - S.StaticCompression;
				w.Hardpoint = V3(x, y, w.Radius + staticLen);
				w.Length = w.LengthPrev = staticLen;
				w.PressureKPa = Set.ColdPressureKPa[i];
				w.SurfaceTempC = w.CarcassTempC = w.BrakeTempC = S.AmbientC;
			};
			setup(0, xf, -hw_f, true, true);
			setup(1, xf, hw_f, true, false);
			setup(2, xr, -hw_r, false, true);
			setup(3, xr, hw_r, false, false);

			// Collision probes around the body shell (body frame), radius in metres
			const double fx = xf + S.FrontOverhang, rx = xr - S.RearOverhang;
			NumProbes = 0;
			auto probe = [&](double x, double y, double z, double r) { Probes[NumProbes++] = { V3(x, y, z), r }; };
			probe(fx - 0.14, 0.00, 0.32, 0.14);
			probe(fx - 0.30, -0.74, 0.34, 0.16);
			probe(fx - 0.30, 0.74, 0.34, 0.16);
			probe(rx + 0.16, 0.00, 0.50, 0.16);
			probe(rx + 0.26, -0.76, 0.52, 0.17);
			probe(rx + 0.26, 0.76, 0.52, 0.17);
			probe(0.25, -0.88, 0.50, 0.13);
			probe(0.25, 0.88, 0.50, 0.13);
			probe(-0.75, -0.90, 0.56, 0.13);
			probe(-0.75, 0.90, 0.56, 0.13);
			probe(-0.25, -0.40, S.Height - 0.13, 0.12);
			probe(-0.25, 0.40, S.Height - 0.13, 0.12);
			probe(-1.05, 0.00, S.Height - 0.22, 0.14);
			probe(0.85, 0.00, 0.16, 0.09);
			probe(-0.85, 0.00, 0.16, 0.09);
			probe(0.00, 0.00, 0.15, 0.09);

			EnergyKWh = S.BatteryKWh;
			const bool fitted = S.TankVolumeL > 0;
			Gas = ColdGas::System(fitted ? S.TankVolumeL : 200.0);   // 200 L -> Thrust, 600 L -> Rocket (200 + 400 on a common header)
			RefillTank();
			FanRPM = 0; SkirtDeploy = 0; HeavePreload = 0;
			T = Telemetry();
		}

		void RefillTank()
		{
			// armed fill: lines charged to lock-up, "Boost Ready" at 5,497 psi
			Gas.SetArmed(S.TankVolumeL > 0 && Set.bThrustersArmed);
			Gas.Fill(S.TankFullBar, S.GasTempK);
			TankKg = S.TankVolumeL > 0 ? Gas.GasMassKg() + Gas.ManifoldMassKg() : 0.0;
		}
		void Recharge() { EnergyKWh = S.BatteryKWh; }

		// Place the car resting on a surface: body origin on the surface, oriented by q.
		void Place(const V3& bodyOriginWorld, const Quat& q, double speedForward = 0)
		{
			Q = q.Normalized();
			X = bodyOriginWorld + Q.Rotate(CGBody());
			V = Q.Fwd() * speedForward;
			W = V3();
			for (auto& w : Wheels)
			{
				w.Omega = speedForward / w.Radius;
				w.Kappa = w.Alpha = 0;
				w.Length = w.LengthPrev = MaxLen() - S.StaticCompression;
				w.TCScale = w.ABSScale = 1;
			}
			bReverseGear = false;
			T.bRunActive = false;

			// Pre-balance the active suspension for the new orientation, as if the car had driven there:
			// fans spooled (if enabled), skirts down, springs carrying weight-normal + suction.
			const bool fanOn = Set.Fan == FanMode::MaxGrip || Set.Fan == FanMode::Custom || Set.Aero == AeroPreset::Auto || Set.Aero == AeroPreset::HighDownforce;
			const double kpa = Set.Fan == FanMode::Custom && Set.Aero == AeroPreset::Custom ? Set.FanCustomKPa : S.FanMaxPressureKPa;
			FanRPM = fanOn ? S.FanMaxRPM * std::sqrt(Clamp(kpa / S.FanMaxPressureKPa, 0, 1)) : 0.0;
			SkirtDeploy = fanOn ? 1.0 : 0.0;
			const double suction = fanOn ? kpa * 1000.0 * S.FanEffectiveArea * 0.93 : 0.0;
			const double normal = TotalMass() * kG * Q.Up().z + suction;
			double springAtStatic = 0;
			for (int i = 0; i < 4; ++i)
			{
				const bool front = Wheels[i].bFront;
				const double k = front ? S.SpringRateFront : S.SpringRateRear;
				const double frontShare = 0.5 - S.CGForward / S.Wheelbase;
				const double corner = (front ? frontShare : 1.0 - frontShare) * (S.DryMassKg + S.TrimExtraMassKg) * kG * 0.5;
				springAtStatic += std::max(0.0, corner - k * S.StaticCompression) + k * S.StaticCompression * (1.0 + (S.ProgressiveRate - 1.0) * S.StaticCompression / S.SuspensionTravel);
			}
			HeavePreload = Set.bActiveSuspension ? Clamp((normal - springAtStatic) / 4.0, -S.HeaveMaxPullN, S.HeaveMaxPushN) : 0.0;
		}

		// ---------------------------------------------------------- one fixed step
		void Step(double h, const Inputs& in, const TraceFn& trace)
		{
			SimTime += h; ++StepCount;
			const V3 fwd = Q.Fwd(), right = Q.Right(), up = Q.Up();
			const double mass = TotalMass();
			const V3 gravity(0, 0, -kG);
			V3 F = gravity * mass;
			V3 Tq;   // world torque about CoM
			auto addForceAt = [&](const V3& force, const V3& pointWorld) { F += force; Tq += (pointWorld - X).Cross(force); };

			const V3 velBody = Q.Unrotate(V);
			const double vFwd = velBody.x;
			const double speed = V.Len();
			const V3 wBody = Q.Unrotate(W);
			const double prevAccelFwd = T.AccelG.x * kG;

			// ---- dynamics presets (Vehicle > Dynamics)
			switch (Set.Dynamics)
			{
			case DynamicsPreset::Comfort: Set.Acceleration = Accel::Chill; Set.RideHandling = Handling::Comfort; Set.bLowSuspension = false; break;
			case DynamicsPreset::Balance: Set.Acceleration = Accel::Standard; Set.RideHandling = Handling::Balance; Set.bLowSuspension = false; break;
			case DynamicsPreset::Sport: Set.Acceleration = Accel::Sport; Set.RideHandling = Handling::Sport; Set.bLowSuspension = false; break;
			case DynamicsPreset::Track: Set.Acceleration = Accel::Sport; Set.RideHandling = Handling::Sport; Set.bLowSuspension = true; break;
			case DynamicsPreset::Custom: break;
			}
			double torqueScale = 1, powerScale = 1, throttleGamma = 1.0;
			switch (Set.Acceleration)
			{
			case Accel::Chill: torqueScale = 0.40; powerScale = 0.35; throttleGamma = 1.6; break;
			case Accel::Standard: torqueScale = 0.75; powerScale = 0.70; throttleGamma = 1.25; break;
			case Accel::Sport: torqueScale = 1.00; powerScale = 1.00; throttleGamma = 1.0; break;
			}
			double dampScale = 1, arbScale = 1, tvGain = 1;
			switch (Set.RideHandling)
			{
			case Handling::Comfort: dampScale = 0.72; arbScale = 0.75; tvGain = 0.6; break;
			case Handling::Balance: dampScale = 0.88; arbScale = 0.9; tvGain = 0.85; break;
			case Handling::Sport: dampScale = 1.0; arbScale = 1.0; tvGain = 1.0; break;
			}

			// ---- gear selection. The controller latches bWantReverse (brake held at a standstill);
			// in reverse the brake pedal drives backwards and the accelerator brakes.
			if (in.bWantReverse && std::fabs(vFwd) < 1.0) bReverseGear = true;
			if (!in.bWantReverse) bReverseGear = false;
			const double pedalDrive = bReverseGear ? in.Brake : in.Throttle;
			const double pedalBrake = bReverseGear ? in.Throttle : in.Brake;

			// ---- steer-by-wire: speed-sensitive ratio and Ackermann
			const double steerLimit = S.MaxSteerDeg * kPi / 180.0 / (1.0 + std::pow(std::fabs(vFwd) / 30.0, 2.0) * 1.6);
			const double steer = Clamp(in.Steer, -1, 1) * std::max(steerLimit, 2.5 * kPi / 180.0);
			{
				const double L = S.Wheelbase, t = S.TrackFront;
				double inner = steer, outer = steer;
				if (std::fabs(steer) > 1e-4)
				{
					const double Rturn = L / std::tan(std::fabs(steer));
					const double ackIn = std::atan(L / std::max(0.5, Rturn - t * 0.5));
					const double ackOut = std::atan(L / (Rturn + t * 0.5));
					inner = Sign(steer) * Lerp(std::fabs(steer), ackIn, S.Ackermann);
					outer = Sign(steer) * Lerp(std::fabs(steer), ackOut, S.Ackermann);
				}
				Wheels[0].Steer = steer > 0 ? outer : inner;
				Wheels[1].Steer = steer > 0 ? inner : outer;
				Wheels[2].Steer = Wheels[3].Steer = 0;
			}

			// ---- yaw-rate reference (bicycle model) for torque vectoring / ESC
			const double yawRate = wBody.z;
			double yawRef = 0;
			if (std::fabs(vFwd) > 2.0)
			{
				const double vch = 30.0;
				yawRef = vFwd * steer / (S.Wheelbase * (1.0 + (vFwd * vFwd) / (vch * vch)));
				const double latCap = (S.PeakMu * kG + T.TotalDownforceN() * S.PeakMu / mass) * 0.95;
				yawRef = Clamp(yawRef, -latCap / std::fabs(vFwd), latCap / std::fabs(vFwd));
			}
			const double yawErr = yawRef - yawRate;
			const double bodySlip = std::fabs(vFwd) > 3 ? std::atan2(velBody.y, std::fabs(vFwd)) : 0.0;

			// ---- aero preset logic (Track Mode > Setup: Auto / Low Drag / High Downforce / Custom)
			FrontWing fw = Set.FWing; RearWing rw = Set.RWing; FanMode fan = Set.Fan; bool vent = Set.bHoodVentOpen, barge = Set.bBargeDeployed;
			switch (Set.Aero)
			{
			case AeroPreset::LowDrag: fw = FrontWing::LowDrag; rw = RearWing::Stowed; fan = FanMode::LowDrag; vent = false; barge = false; break;
			case AeroPreset::HighDownforce: fw = FrontWing::HighDownforce; rw = RearWing::HighDownforce; fan = FanMode::MaxGrip; vent = true; barge = true; break;
			case AeroPreset::Auto:
			{
				// Fans carry the load at low speed and hand over to the wings above ~60 mph (EXEC),
				// but stay at Max Grip whenever the car is on a wall or ceiling.
				// any tilt or roll rate (banked wall, tube roll, loop) spools the fans before grip is needed
				const bool steep = up.z < 0.95 || std::fabs(wBody.x) > 0.25 || std::fabs(wBody.y) > 0.35;
				const bool launchPhase = pedalDrive > 0.6 && std::fabs(vFwd) < 50.0;
				fan = (std::fabs(vFwd) < 27.0 || steep || launchPhase || pedalBrake > 0.3 || std::fabs(T.AccelG.y) > 0.8) ? FanMode::MaxGrip : FanMode::LowDrag;
				fw = FrontWing::HighDownforce;
				rw = (std::fabs(vFwd) > 20.0 || pedalBrake > 0.4) ? RearWing::HighDownforce : RearWing::LowDrag;
				if (std::fabs(vFwd) > 75.0 && std::fabs(steer) < 0.02 && pedalBrake < 0.05) rw = RearWing::LowDrag;
				vent = std::fabs(vFwd) > 30.0; barge = true;
				break;
			}
			case AeroPreset::Custom: break;
			}
			if (in.bDRS) rw = RearWing::Stowed;
			// Aero Load overrides how hard the system works
			double loadScale = Set.Load == AeroLoad::LowDrag ? 0.45 : (Set.Load == AeroLoad::Balance ? 0.75 : 1.0);

			// ---- body + active aero
			{
				const double q = 0.5 * S.AirDensity * speed * speed;
				const double fwdFrac = Clamp(vFwd / std::max(speed, 0.01), 0, 1);
				const double wingTarget = rw == RearWing::Stowed ? 0.0 : (rw == RearWing::LowDrag ? 0.45 : 1.0);
				WingAngle = MoveTo(WingAngle, wingTarget, h * 1.8);
				FrontWingAngle = MoveTo(FrontWingAngle, fw == FrontWing::HighDownforce ? 1.0 : 0.0, h * 2.5);
				const double rearClA = WingAngle < 0.45 ? Lerp(0.0, S.RearWingClA_Low, WingAngle / 0.45) : Lerp(S.RearWingClA_Low, S.RearWingClA_High, (WingAngle - 0.45) / 0.55);
				const double rearCdA = WingAngle < 0.45 ? Lerp(0.0, S.RearWingCdA_Low, WingAngle / 0.45) : Lerp(S.RearWingCdA_Low, S.RearWingCdA_High, (WingAngle - 0.45) / 0.55);
				const double frontClA = Lerp(S.FrontWingClA_Low, S.FrontWingClA_High, FrontWingAngle);
				const double extraClA = (vent ? S.HoodVentClA : 0.0) + (barge ? S.BargeClA : 0.0);
				const double extraCdA = (vent ? S.HoodVentCdA : 0.0) + (barge ? S.BargeCdA : 0.0) + FrontWingAngle * S.FrontWingCdA_High;
				const double CdA = S.Cd * S.FrontalArea + rearCdA + extraCdA;
				const V3 drag = speed > 0.01 ? V * (-(0.5 * S.AirDensity * CdA * speed)) : V3();
				addForceAt(drag, BodyToWorld({ -0.2, 0, 0.55 }));
				const double totalClA = (S.BodyCl * S.FrontalArea + rearClA + frontClA + extraClA) * loadScale;
				const double down = -q * totalClA * fwdFrac * fwdFrac;   // positive = downforce
				const double fShare = Set.AeroFrontShare();
				addForceAt(-up * (down * fShare), BodyToWorld({ S.Wheelbase * 0.5, 0, 0.4 }));
				addForceAt(-up * (down * (1.0 - fShare)), BodyToWorld({ -S.Wheelbase * 0.5, 0, 0.4 }));
				T.DragN = drag.Len();
				T.WingDownforceN = down;
				T.WingAngle = WingAngle; T.FrontWingAngle = FrontWingAngle;
			}

			// ---- suction fans + deployable skirts
			double fanPowerKW = 0;
			{
				double targetKPa = 0;
				switch (fan)
				{
				case FanMode::Off: targetKPa = 0; break;
				case FanMode::Braking: targetKPa = pedalBrake > 0.05 ? S.FanMaxPressureKPa * Clamp(pedalBrake * 1.6, 0, 1) : 0.0; break;
				case FanMode::LowDrag: targetKPa = S.FanMaxPressureKPa * 0.12; break;  // base-drag bleed (function not published)
				case FanMode::MaxGrip: targetKPa = S.FanMaxPressureKPa; break;
				case FanMode::Custom: targetKPa = Clamp(Set.FanCustomKPa, 0, S.FanMaxPressureKPa); break;
				}
				if (fan != FanMode::Custom) targetKPa *= loadScale;
				// A sealed floor still needs electric fan power. With a depleted battery, let
				// the rotor coast down through the same spool response instead of supplying
				// unlimited suction at zero stored energy. Demo battery mode remains powered.
				if (EnergyKWh <= 0.0 && !Set.bInfiniteBattery) targetKPa = 0.0;
				// skirts deploy whenever the fans are asked for meaningful suction
				SkirtDeploy = MoveTo(SkirtDeploy, targetKPa > 0.8 ? 1.0 : 0.0, h * 4.0);

				const double floorH = S.RideHeight - (Set.bLowSuspension ? S.LowRideDrop : 0.0);
				const V3 floorPt = BodyToWorld({ S.FanCentreForward, 0, floorH + 0.05 });
				const Hit fh = trace(floorPt, -up, S.FanMaxReach + 0.05, 0.0);
				double gap = 10.0, seal = 0.0;
				if (fh.bHit)
				{
					const double clearance = fh.Dist - 0.05;
					gap = std::max(S.SkirtResidualGap, clearance - S.SkirtDeployMax * SkirtDeploy);
					seal = 1.0 / (1.0 + std::pow(gap / S.FanSealGapScale, 2.0));
				}
				const double rpmTarget = S.FanMaxRPM * std::sqrt(Clamp(targetKPa / S.FanMaxPressureKPa, 0, 1));
				FanRPM += (rpmTarget - FanRPM) * Clamp(h / S.FanSpoolTime, 0, 1);
				const double rpmFrac = FanRPM / S.FanMaxRPM;
				const double dP = S.FanMaxPressureKPa * 1000.0 * rpmFrac * rpmFrac * seal;   // Pa
				const double downforce = dP * S.FanEffectiveArea;
				addForceAt(-up * downforce, BodyToWorld({ S.FanCentreForward, 0, 0.1 }));
				// fan law: power ~ rpm^3, a sealed fan moves less air
				fanPowerKW = S.FanMaxPowerKW * rpmFrac * rpmFrac * rpmFrac * (0.55 + 0.45 * (1.0 - seal));
				T.FanRPM = FanRPM; T.FanPressureKPa = dP / 1000.0; T.FanTargetKPa = targetKPa;
				T.FanDownforceN = downforce; T.FanSeal = seal; T.FanGap = fh.bHit ? gap : -1;
				T.FanPowerKW = fanPowerKW; T.SkirtDeploy = SkirtDeploy;
			}

			// ---- wheel contact (needed by flight assist and suspension)
			double compression[4] = { 0, 0, 0, 0 };
			Hit hits[4];
			int contactsNow = 0;
			for (int i = 0; i < 4; ++i)
			{
				WheelState& w = Wheels[i];
				const V3 hp = BodyToWorld(w.Hardpoint);
				const double reach = MaxLen() + w.Radius;
				hits[i] = trace(hp, -up, reach + 0.02, 0.04);
				const Hit& hit = hits[i];
				w.LengthPrev = w.Length;
				if (hit.bHit && hit.Dist - w.Radius < MaxLen())
				{
					w.bContact = true; ++contactsNow;
					w.Length = Clamp(hit.Dist - w.Radius, MinLen() - 0.06, MaxLen());
					w.ContactPoint = hit.Point; w.ContactNormal = hit.Normal;
				}
				else
				{
					w.bContact = false;
					w.Length = std::min(MaxLen(), w.Length + 0.6 * h);
				}
				w.LengthVel = (w.Length - w.LengthPrev) / h;
				compression[i] = MaxLen() - w.Length;
				w.Compression = compression[i];
			}
			const bool airborne = contactsNow == 0;

			// ---- cold-gas thrusters: valve-level model (ColdGasSystem.h). The attitude controller requests thrust per
			// nozzle; the gas model (vessels, regulator, valves, nozzles) decides what the hardware actually delivers.
			{
				const bool fitted = S.TankVolumeL > 0;
				Gas.SetArmed(fitted && Set.bThrustersArmed);
				const double auth = Clamp(Set.BoostLevel, 0.25, 1.0);   // "Valve authority": caps every thrust request

				// rear bank: BOOST asks for a fraction of rated thrust; the model gives what the manifold can deliver
				for (int k = 0; k < 4; ++k)
					Gas.CommandThrust(k, S.bHasRearThrusters ? Clamp(in.Boost, 0, 1) * auth * Gas.Cfg().Nozzle[k].RatedThrustN : 0.0);

				// corner lift + front yaw pair (Rocket). Allocation is about the CG with the real nozzle positions:
				// the collective share is moment-neutral, roll/pitch corrections are differential.
				double corner[4] = { 0, 0, 0, 0 }, yawL = 0, yawR = 0;
				if (S.bHasLiftThrusters && Gas.Tel().bArmed)
				{
					const double base = Clamp(in.Lift, 0, 1);
					V3 tauB;
					const bool assist = Set.bFlightAssist && (airborne || base > 0.05);
					const V3 upB = Q.Unrotate(V3(0, 0, 1));          // world up in body frame
					if (assist)
					{
						const double pitchCmd = Clamp(in.Pitch, -1, 1) * 0.5, rollCmd = Clamp(in.Roll, -1, 1) * 0.6;
						V3 want = (upB + V3(-std::sin(pitchCmd), std::sin(rollCmd), 0) * 1.0).Norm();
						const V3 err = V3(0, 0, 1).Cross(want);       // rotate body +Z towards want
						tauB = V3(err.x * 70000.0 - wBody.x * 9000.0, err.y * 70000.0 - wBody.y * 22000.0, 0);
					}
					else if (airborne)
						tauB = V3(in.Roll * 18000.0, -in.Pitch * 30000.0, 0);
					const V3 cg = CGBody();
					double xr[4], yr[4], sxx = 0, syy = 0;
					for (int k = 0; k < 4; ++k) { xr[k] = S.LiftNozzlePos[k].x - cg.x; yr[k] = S.LiftNozzlePos[k].y; sxx += xr[k] * xr[k]; syy += yr[k] * yr[k]; }
					const double xf = 0.5 * (xr[0] + xr[1]), xb = 0.5 * (xr[2] + xr[3]);
					const double wf = -xb / (xf - xb), wb = xf / (xf - xb);   // moment-neutral split of the collective
					double cap[4], capMean = 0;
					for (int k = 0; k < 4; ++k) { cap[k] = Gas.MaxThrustN(ColdGas::LiftFL + k) * auth; capMean += 0.25 * cap[k]; }
					const double a = tauB.x / syy, b = -tauB.y / sxx;
					for (int k = 0; k < 4; ++k)
						corner[k] = Clamp(base * capMean * 2.0 * (k < 2 ? wf : wb) + a * yr[k] + b * xr[k], 0, cap[k]);
					if (base < 0.02) for (double& c : corner) if (c < 0.05 * capMean) c = 0;   // deadband on pure trim, saves gas
					const double yawCmd = Clamp(in.Yaw, -1, 1) + (airborne && assist ? Clamp(-wBody.z * 0.8, -1, 1) : 0.0);
					if (yawCmd > 0.02) yawR = yawCmd * Gas.MaxThrustN(ColdGas::YawLeftFiring) * auth;
					if (yawCmd < -0.02) yawL = -yawCmd * Gas.MaxThrustN(ColdGas::YawRightFiring) * auth;
				}
				for (int k = 0; k < 4; ++k) Gas.CommandThrust(ColdGas::LiftFL + k, corner[k]);
				Gas.CommandThrust(ColdGas::YawLeftFiring, yawR);    // nozzle 8: jet exits to the left -> nose pushed right
				Gas.CommandThrust(ColdGas::YawRightFiring, yawL);   // nozzle 9: jet exits to the right -> nose pushed left

				if (Set.bInfiniteGas && Gas.Tel().GasFraction < 0.995) Gas.TopUp();   // demo mode (books stay balanced)
				Gas.Step(h, ColdGas::kPatm, S.AmbientC + 273.15);

				// apply the thrust actually delivered at the nozzle exits
				double rear = 0, lift = 0;
				for (int k = 0; k < 4; ++k)
				{
					const double Fn = Gas.Nozzle(k).ThrustN;
					if (Fn > 0) addForceAt(fwd * Fn, BodyToWorld(S.RearNozzlePos[k]));
					rear += Fn;
				}
				for (int k = 0; k < 4; ++k)
				{
					const double Fn = Gas.Nozzle(ColdGas::LiftFL + k).ThrustN;
					T.ThrustCorner[k] = Fn / S.LiftThrustFullN;
					if (Fn <= 0) continue;
					addForceAt(up * Fn, BodyToWorld(S.LiftNozzlePos[k]));
					lift += Fn;
				}
				// yaw jets: nozzle 8 exhausts to the car's left (force to the right), nozzle 9 the opposite
				const double FyR = Gas.Nozzle(ColdGas::YawLeftFiring).ThrustN, FyL = Gas.Nozzle(ColdGas::YawRightFiring).ThrustN;
				if (FyR > 0) addForceAt(right * FyR, BodyToWorld(S.YawNozzlePos[0]));
				if (FyL > 0) addForceAt(-right * FyL, BodyToWorld(S.YawNozzlePos[1]));

				TankKg = fitted ? Gas.GasMassKg() + Gas.ManifoldMassKg() : 0.0;
				T.ThrustRear = rear / S.RearThrustFullN;
				T.ThrustYawR = FyR / S.YawThrustFullN; T.ThrustYawL = FyL / S.YawThrustFullN;
				T.ThrustN = rear + lift + FyR + FyL;
				T.TankBar = fitted ? Gas.Tel().DisplayBar : 0.0;          // volume-weighted vessel pressure -> "Thruster xxxx PSI"
				T.TankKg = TankKg;
				T.TankPercent = fitted ? Gas.Tel().GasFraction : 0.0;      // usable gas (above the 10 bar residual) vs a full fill
			}
			// ---- powertrain torque request (RWD, one motor per rear wheel)
			const double throttle = std::pow(Clamp(pedalDrive, 0, 1), throttleGamma);
			const double dirSign = bReverseGear ? -1.0 : 1.0;
			const double rr = S.WheelRadiusRear;
			const double wheelTorqueMax = S.MotorTorqueNm * S.GearRatio * S.DrivetrainEfficiency;
			double rearReq[2] = { throttle * wheelTorqueMax * torqueScale, throttle * wheelTorqueMax * torqueScale };
			auto powerCap = [&](double reqNm, double omegaWheel)
			{
				const double w = std::max(std::fabs(omegaWheel), 1.0);
				return std::min(reqNm, S.RearMotorPeakKW * 1000.0 * powerScale * S.DrivetrainEfficiency / w);
			};
			rearReq[0] = powerCap(rearReq[0], Wheels[2].Omega);
			rearReq[1] = powerCap(rearReq[1], Wheels[3].Omega);
			const double motorOmegaMax = S.MotorMaxRPM * 2 * kPi / 60.0 / S.GearRatio;
			const double vLimit = bReverseGear ? 12.0 : (Set.bUnrestrictedTopSpeed ? 1e9 : S.TopSpeedMps);
			auto rpmFade = [&](double omega)
			{
				const double limitOmega = std::min(motorOmegaMax, vLimit / rr);
				return Clamp((limitOmega - std::fabs(omega)) / (limitOmega * 0.006), 0, 1);
			};
			rearReq[0] *= rpmFade(Wheels[2].Omega);
			rearReq[1] *= rpmFade(Wheels[3].Omega);

			// torque vectoring across the two rear motors
			if (throttle > 0.02 && std::fabs(vFwd) > 3.0)
			{
				const double sum = rearReq[0] + rearReq[1];
				const double bias = Clamp((yawErr * 0.9 + steer * 0.5) * tvGain, -0.4, 0.4);   // +: more torque left -> yaw right
				rearReq[0] = sum * (0.5 + bias * 0.5);
				rearReq[1] = sum * (0.5 - bias * 0.5);
			}

			// launch control: brake + full accelerator at a standstill pre-loads, release to launch
			T.bLaunch = Set.bLaunchControl && !bReverseGear && Set.Acceleration == Accel::Sport && std::fabs(vFwd) < 0.5 && in.Brake > 0.5 && in.Throttle > 0.5;

			// battery power limit (electrical)
			{
				const double mechKW = (std::fabs(rearReq[0] * Wheels[2].Omega) + std::fabs(rearReq[1] * Wheels[3].Omega)) / 1000.0;
				const double elecKW = mechKW / S.DrivetrainEfficiency + fanPowerKW + S.AuxLoadKW;
				const double avail = (EnergyKWh > 0.2 || Set.bInfiniteBattery ? S.BatteryMaxDischargeKW : 0.0) * Clamp(0.55 + 0.45 * SOC() / 0.3, 0, 1);
				// At rest mechanical power is zero even with a torque request. An empty
				// battery must also remove that stalled torque, or it repeatedly nudges
				// the car forward whenever wheel speed falls below the power threshold.
				if (avail <= 0.0)
				{
					rearReq[0] = rearReq[1] = 0.0;
				}
				else if (elecKW > avail && mechKW > 1e-3)
				{
					const double s = Clamp((avail - fanPowerKW - S.AuxLoadKW) * S.DrivetrainEfficiency / mechKW, 0, 1);
					rearReq[0] *= s; rearReq[1] *= s;
				}
			}

			// regen on lift-off (rear motors only)
			double regenTorque[4] = { 0, 0, 0, 0 };
			double regenFrac = 0;
			if (throttle < 0.02 && std::fabs(vFwd) > 1.5 && !T.bLaunch)
			{
				const double targetDecel = 0.25 * kG * Set.RegenLevel * Clamp((std::fabs(vFwd) - 1.5) / 4.0, 0, 1);
				double total = targetDecel * mass * rr;
				const double kwCap = S.MaxRegenKW * 1000.0 / std::max(std::fabs(vFwd) / rr, 1.0);
				total = std::min(total, kwCap);
				regenTorque[2] = regenTorque[3] = total * 0.5;
				regenFrac = total / std::max(1.0, 0.25 * kG * mass * rr);
			}

			// ---- active ride-height control (heave only; roll and pitch stay passive).
			// Holds ride height under suction in any orientation so the skirt seal survives on walls and
			// ceilings. "Adaptive suspension" is official; the control law is an estimate.
			{
				double errSum = 0; int n = 0;
				for (int i = 0; i < 4; ++i) if (Wheels[i].bContact) { errSum += compression[i] - TargetCompression(); ++n; }
				if (n >= 2 && Set.bActiveSuspension)
					HeavePreload = Clamp(HeavePreload + h * S.HeaveGain * errSum / n, -S.HeaveMaxPullN, S.HeaveMaxPushN);
				else
					HeavePreload = MoveTo(HeavePreload, 0.0, h * 20000.0);
				T.HeavePreloadN = HeavePreload;
			}

			// ---- per wheel
			int contacts = 0; bool anyTC = false, anyABS = false, anyESC = false;
			double motorMechKW = 0, regenKW = 0;
			const double brakeFront = Set.BrakeFrontShare();
			for (int i = 0; i < 4; ++i)
			{
				WheelState& w = Wheels[i];
				const bool front = w.bFront;
				const double k = front ? S.SpringRateFront : S.SpringRateRear;
				const double arb = (front ? S.AntiRollFront : S.AntiRollRear) * arbScale;
				const int other = (i % 2 == 0) ? i + 1 : i - 1;
				const double R = w.Radius;

				const double maxBrake = front ? S.MaxBrakeTorqueFront * brakeFront / 0.6 : S.MaxBrakeTorqueRear * (1.0 - brakeFront) / 0.4;
				double brakeT = Clamp(pedalBrake, 0, 1) * maxBrake;
				const double handT = front ? 0.0 : Clamp(in.Handbrake, 0, 1) * S.MaxBrakeTorqueRear * 1.3;

				double driveT = front ? 0.0 : rearReq[i - 2];
				if (bReverseGear) driveT *= 0.2;
				driveT *= dirSign;
				if (T.bLaunch) { driveT = 0.0; brakeT = std::max(brakeT, maxBrake); }

				// ESC: brake the outside front wheel when the car rotates more than the driver asked
				double escBrake = 0;
				if (Set.bESC && std::fabs(vFwd) > 6.0 && front)
				{
					const double over = -Sign(yawRef) * yawErr;
					const double excess = std::max(std::fabs(bodySlip) - 0.10, over - 0.25);
					const bool yawingRight = yawRate > 0;
					if (excess > 0 && (yawingRight ? w.bLeft : !w.bLeft)) { escBrake = 2400 * Clamp(excess * 5.0, 0, 1); anyESC = true; }
				}

				w.Load = 0; w.Fx = w.Fy = 0; w.SlideSpeed = 0; w.GripUsage = 0;
				const double regenT = std::fabs(regenTorque[i]);
				auto applyBrake = [&](double omega, double torque, double inertia)
				{
					const double d = h * torque / inertia;
					return std::fabs(omega) <= d ? 0.0 : omega - Sign(omega) * d;
				};

				if (w.bContact)
				{
					++contacts;
					const double c = compression[i];
					const double prog = 1.0 + (S.ProgressiveRate - 1.0) * Clamp(c / S.SuspensionTravel, 0, 1);
					const double frontShare = 0.5 - S.CGForward / S.Wheelbase;
					const double staticCorner = (front ? frontShare : 1.0 - frontShare) * (S.DryMassKg + S.TrimExtraMassKg) * kG * 0.5;
					const double preload = std::max(0.0, staticCorner - k * S.StaticCompression) + HeavePreload;
					const double fs = k * c * prog + preload;
					const double fd = (w.LengthVel < 0 ? -S.DampingBump * w.LengthVel : -S.DampingRebound * w.LengthVel) * dampScale;
					const double fa = arb * (compression[i] - compression[other]);
					const double fb = w.Length < MinLen() ? S.BumpStopRate * (MinLen() - w.Length) - 4000.0 * w.LengthVel : 0.0;
					const double fz = std::max(0.0, fs + fd + fa + fb);
					w.Load = fz;

					const V3 cp = hits[i].Point;
					addForceAt(up * fz, cp);

					const V3 n = w.ContactNormal;
					const V3 wf = fwd * std::cos(w.Steer) + right * std::sin(w.Steer);
					const V3 tf = (wf - n * wf.Dot(n)).Norm();
					const V3 ts = n.Cross(tf).Norm();
					const V3 vc = V + W.Cross(cp - X);
					const double vx = vc.Dot(tf), vy = vc.Dot(ts);

					// lateral slip with relaxation length: spring-like at standstill (no creep on banks)
					const double sigma = S.RelaxationLength;
					w.Alpha = Clamp((w.Alpha + h * (-vy) / sigma) / (1.0 + h * std::fabs(vx) / sigma), -4, 4);

					// grip: load sensitivity, pressure, temperature, wear
					const double nominal = (S.DryMassKg + S.TrimExtraMassKg) * kG * 0.25;
					const double loadMu = 1.0 - S.LoadSensitivity * Clamp(fz / nominal - 1.0, -0.6, 6.0);
					const double pErr = (w.PressureKPa - S.OptimalHotPressureKPa) / S.OptimalHotPressureKPa;
					const double pressMu = Clamp(1.0 - 0.55 * pErr * pErr - (pErr < 0 ? 0.06 * -pErr : 0.0), 0.6, 1.0);
					const double tErr = (w.SurfaceTempC - S.OptimalTireTempC) / 60.0;
					const double tempMu = Clamp(1.0 - 0.10 * tErr * tErr, 0.85, 1.0);
					const double wearMu = 1.0 - 0.15 * w.Wear;
					w.GripFactor = loadMu * pressMu * tempMu * wearMu;
					const double fmax = S.PeakMu * w.GripFactor * fz;

					const double pressStiff = std::pow(Clamp(w.PressureKPa / S.OptimalHotPressureKPa, 0.5, 1.8), 0.35);
					const double kPeak = S.PeakSlipRatio / pressStiff;
					const double aPeak = std::tan(S.PeakSlipAngleDeg * kPi / 180.0) / pressStiff * (front ? 1.0 : 0.92);
					const double C = Clamp(2.0 - 2.0 * std::asin(Clamp(S.SlideMuRatio, 0.3, 0.999)) / kPi, 1.05, 1.9);
					const double B = std::tan(kPi / (2.0 * C));
					// combined-slip magic formula, normalised so the peak sits at rho = 1
					auto tire = [&](double kap, double& ofx, double& ofy)
					{
						const double nx = kap / kPeak, ny = w.Alpha / aPeak;
						const double rho = std::sqrt(nx * nx + ny * ny);
						if (rho < 1e-9) { ofx = ofy = 0; return; }
						const double f = fmax * std::sin(C * std::atan(B * rho));
						ofx = f * nx / rho; ofy = f * ny / rho;
					};

					// TCS / ABS: cap torque at what the tire can transmit (EV motor torque control is fast)
					double fyNow = 0, fxNow = 0; tire((w.Omega * R - vx) / std::max(std::fabs(vx), 1.0), fxNow, fyNow);
					const double fxAvail = std::sqrt(std::max(0.0, fmax * fmax - fyNow * fyNow));
					if ((Set.bTCS || T.bLaunch) && std::fabs(driveT) > 1.0)
					{
						const double allowance = Set.bESC ? 1.05 : 1.15;
						const double cap = R * fxAvail * allowance + 0.5 * w.Inertia * std::fabs(prevAccelFwd) / R;
						double target = Clamp(cap / std::fabs(driveT), 0, 1);
						// slip feedback: once past the peak the sliding force is lower than the cap, so pull
						// torque below the current tire force until the wheel comes back under the peak
						const double slipNow = (w.Omega * R - vx) / std::max(std::fabs(vx), 1.0) * dirSign;
						if (slipNow > kPeak * (Set.bESC ? 1.2 : 1.5))
							target = std::min(target, Clamp(R * std::fabs(fxNow) * 0.75 / std::fabs(driveT), 0, 1));
						w.TCScale = target < w.TCScale ? target : MoveTo(w.TCScale, target, h * 4.0);
						if (target < 0.98) anyTC = true;
					}
					else w.TCScale = 1.0;
					driveT *= w.TCScale;
					// ABS caps friction brake + regen together (regen is blended by the stability system,
					// otherwise lifting mid-corner would lock the driven rear wheels)
					const double blended = brakeT + regenT;
					if (blended > 1 && std::fabs(vx) > 1.5)
					{
						const double cap = R * fxAvail * 1.05;
						double target = Clamp(cap / blended, 0.0, 1.0);
						// slip feedback (as for TCS): a wheel already past the peak sees only the sliding force,
						// so release below it until the wheel spins back up
						const double slipNow = (w.Omega * R - vx) / std::max(std::fabs(vx), 1.0) * Sign(vx);
						if (slipNow < -kPeak * 1.4)
							target = std::min(target, Clamp(R * std::fabs(fxNow) * 0.6 / blended, 0.0, 1.0));
						w.ABSScale = target;
						if (w.ABSScale < 0.98 && brakeT > 1) anyABS = true;
					}
					else w.ABSScale = 1.0;
					const double regenUsed = regenT * w.ABSScale;
					const double totalBrake = brakeT * w.ABSScale + handT + escBrake + regenUsed;

					// implicit wheel spin integration (backward Euler on the tire's longitudinal stiffness)
					const double vd = std::max(std::fabs(vx), 1.0);
					const double k0 = (w.Omega * R - vx) / vd;
					double fx0, fy0, fx1, fy1;
					tire(k0, fx0, fy0); tire(k0 + 1e-3, fx1, fy1);
					const double slope = std::max(0.0, (fx1 - fx0) / 1e-3);
					const double Ieff = w.Inertia + h * R * R * slope / vd;
					double omega = w.Omega + h * (driveT - R * fx0) / Ieff;
					omega = applyBrake(omega, totalBrake, Ieff);
					const double k1 = (omega * R - vx) / vd;
					double fx, fy; tire(k1, fx, fy);
					if (slope > 0) fx = Clamp(fx0 + slope * (k1 - k0), -fmax, fmax);
					w.Omega = omega; w.Kappa = k1;

					// low-speed contact damping (settles parking and wall-hanging oscillation)
					const double fade = Clamp(1.0 - std::fabs(vx) / 4.0, 0, 1);
					fy -= 9000.0 * vy * fade;
					const double fl = std::sqrt(fx * fx + fy * fy);
					if (fl > fmax * 1.02 && fl > 1e-6) { const double s = fmax * 1.02 / fl; fx *= s; fy *= s; }

					const double crr = S.RollingResistance * std::sqrt(S.OptimalHotPressureKPa / std::max(80.0, w.PressureKPa));
					fx -= Clamp(vx * 4.0, -1, 1) * crr * fz;

					w.Fx = fx; w.Fy = fy;
					addForceAt(tf * fx + ts * fy, cp);
					const double rim = w.Omega * R;
					w.SlideSpeed = std::sqrt((rim - vx) * (rim - vx) + vy * vy);
					w.GripUsage = fmax > 1 ? std::sqrt(fx * fx + fy * fy) / fmax : 0;

					// tire thermal model: patch slip power + hysteresis in, convection out
					const double slipPower = std::fabs((rim - vx) * fx) + std::fabs(vy * fy);
					const double hyst = crr * fz * std::fabs(vx) * 3.0;
					const double cool = (18.0 + 1.6 * std::fabs(vx)) * (w.SurfaceTempC - S.AmbientC);
					w.SurfaceTempC += h * (0.35 * slipPower + hyst - cool - 40.0 * (w.SurfaceTempC - w.CarcassTempC)) / 5000.0;
					w.CarcassTempC += h * (40.0 * (w.SurfaceTempC - w.CarcassTempC) + 0.6 * hyst - 3.0 * (w.CarcassTempC - S.AmbientC)) / 26000.0;
					w.Wear = std::min(1.0, w.Wear + h * slipPower * 2.0e-9);
					w.BrakeTorque = totalBrake - regenUsed;
					regenKW += regenUsed * std::fabs(w.Omega) / 1000.0;
				}
				else
				{
					w.Alpha *= 0.98;
					w.TCScale = 1.0; w.ABSScale = 1.0;
					double omega = w.Omega + h * driveT / w.Inertia;
					omega = applyBrake(omega, brakeT + regenT, w.Inertia);
					// ABS also governs an unloaded wheel: it keeps it turning near road speed, so it does not
					// touch down locked (only the handbrake may lock a wheel in the air)
					const double roadOmega = std::fabs(vFwd) / R;
					if (std::fabs(vFwd) > 3.0 && in.Handbrake < 0.1 && std::fabs(omega) < 0.85 * roadOmega)
						omega = Sign(vFwd) * 0.85 * roadOmega;
					omega = applyBrake(omega, handT, w.Inertia);
					w.Omega = omega * (1.0 - 0.15 * h);
					w.SurfaceTempC += h * (-(8.0 + 1.0 * speed) * (w.SurfaceTempC - S.AmbientC)) / 5000.0;
					w.BrakeTorque = brakeT + handT;
				}
				w.DriveTorque = driveT;
				w.Spin = std::fmod(w.Spin + w.Omega * h, 2 * kPi);

				// hot pressure from carcass temperature (Gay-Lussac, constant volume)
				w.PressureKPa = (Set.ColdPressureKPa[i] + 101.3) * (w.CarcassTempC + 273.15) / (S.AmbientC + 273.15) - 101.3;

				// brake heat (carbon-ceramic disc + caliper lump)
				w.BrakeTempC += h * (std::fabs(w.Omega) * w.BrakeTorque * 0.9 / 7000.0 - (0.4 + 0.02 * speed) * (w.BrakeTempC - S.AmbientC) / 60.0);

				motorMechKW += driveT * w.Omega / 1000.0;
				if (!w.bContact) regenKW += regenT * std::fabs(w.Omega) / 1000.0;
			}

			// ---- body collision probes: penalty contact for bumpers, roof, sills and floor
			T.bBodyContact = false;
			for (int p = 0; p < NumProbes; ++p)
			{
				const V3 c = BodyToWorld(Probes[p].P);
				const V3 d = c - X;
				const double len = d.Len();
				if (len < 1e-3) continue;
				const V3 dir = d / len;
				const Hit hit = trace(X, dir, len, Probes[p].R);
				if (!hit.bHit) continue;
				const V3 n = hit.Normal;
				double pen = (len - hit.CenterDist) * std::max(0.2, -dir.Dot(n));
				if (pen <= 0) continue;
				pen = std::min(pen, 0.25);
				const V3 vp = V + W.Cross(c - X);
				const double vn = vp.Dot(n);
				const double fn = std::max(0.0, 2.4e6 * pen - (vn < 0 ? 6.0e4 * vn : 1.0e4 * vn));
				const V3 vt = vp - n * vn;
				const double vtl = vt.Len();
				V3 ft;
				if (vtl > 1e-3) ft = vt * (-std::min(0.30 * fn, 5000.0 * vtl) / vtl);
				addForceAt(n * fn + ft, c - n * Probes[p].R);
				T.bBodyContact = true;
			}

			// ---- integrate rigid body (semi-implicit Euler, quaternion attitude)
			const V3 accel = F / mass;
			V += accel * h;
			X += V * h;
			{
				const V3 tb = Q.Unrotate(Tq);
				V3 wb = Q.Unrotate(W);
				const V3 I = S.Inertia;
				const V3 Iw = wb.Mul(I);
				const V3 gyro = wb.Cross(Iw);
				const V3 dwb((tb.x - gyro.x) / I.x, (tb.y - gyro.y) / I.y, (tb.z - gyro.z) / I.z);
				wb += dwb * h;
				W = Q.Rotate(wb);
				const double wl = W.Len();
				if (wl > 1e-9) Q = (Quat::AxisAngle(W / wl, wl * h) * Q).Normalized();
			}

			// ---- energy and thermal
			const double elecKW = (motorMechKW > 0 ? motorMechKW / S.DrivetrainEfficiency : motorMechKW * S.DrivetrainEfficiency)
				- regenKW * S.DrivetrainEfficiency + fanPowerKW + S.AuxLoadKW;
			if (!Set.bInfiniteBattery) EnergyKWh = Clamp(EnergyKWh - elecKW * h / 3600.0, 0, S.BatteryKWh);
			else EnergyKWh = S.BatteryKWh;
			T.MotorTempC += h * (std::fabs(motorMechKW) * 0.012 - (T.MotorTempC - 40.0) * 0.02);
			T.BatteryTempC += h * (std::fabs(elecKW) * 0.0012 - (T.BatteryTempC - 35.0) * 0.004);

			// ---- telemetry
			const V3 specific = (accel - gravity);
			const V3 aBody = Q.Unrotate(specific) / kG;
			const double a = 1.0 - std::exp(-h / 0.05);
			T.AccelG = T.AccelG + (aBody - T.AccelG) * a;
			T.Speed = V.Len(); T.ForwardSpeed = Q.Unrotate(V).x;
			T.MotorPowerKW = motorMechKW; T.RegenKW = regenKW; T.PowerKW = elecKW;
			T.SOC = SOC(); T.BatteryKWh = EnergyKWh; T.RangeKm = EnergyKWh / 0.24;
			T.OdometerKm += T.Speed * h / 1000.0;
			T.MotorRPM = 0.5 * (Wheels[2].Omega + Wheels[3].Omega) * S.GearRatio * 60.0 / (2 * kPi);
			T.RearTorque = Wheels[2].DriveTorque + Wheels[3].DriveTorque;
			T.UpDotWorld = Q.Up().z;
			T.WheelsInContact = contacts;
			T.bReverse = bReverseGear; T.bTC = anyTC; T.bABS = anyABS; T.bESC = anyESC;
			T.BrakePedal = pedalBrake; T.AccelPedal = pedalDrive; T.RegenFrac = Clamp(regenFrac, 0, 1);
			if (contacts == 0) { T.AirTime += h; T.bAirborne = T.AirTime > 0.08; }
			else { if (T.AirTime > 0.3) T.LastAirTime = T.AirTime; T.AirTime = 0; T.bAirborne = false; }
			UpdateRunTimer(h, in);
		}

		void ResetRunTimer() { T.bRunActive = false; T.RunTime = T.RunDist = 0; T.T60 = T.T60Rollout = T.T100kmh = T.T100mph = T.TQuarter = T.QuarterTrapMph = -1; RolloutTime = -1; }

	private:
		bool bArmed = true;
		double RolloutTime = -1;
		void UpdateRunTimer(double h, const Inputs& in)
		{
			const double v = std::fabs(T.ForwardSpeed);
			T.TopSpeed = std::max(T.TopSpeed, T.Speed);
			const double gl = std::sqrt(T.AccelG.x * T.AccelG.x + T.AccelG.y * T.AccelG.y);
			if (T.WheelsInContact > 0) T.PeakG = std::max(T.PeakG, gl);
			if (!T.bRunActive)
			{
				if (v < 0.3) bArmed = true;
				if (bArmed && v > 0.05 && (in.Throttle > 0.1 || in.Boost > 0.1) && !bReverseGear)
				{
					ResetRunTimer(); T.bRunActive = true; bArmed = false;
				}
				return;
			}
			T.RunTime += h; T.RunDist += v * h;
			if (RolloutTime < 0 && T.RunDist >= 0.3048) RolloutTime = T.RunTime;   // 1-ft rollout, as US drag strips time it
			if (T.T60 < 0 && v >= 60 * kMph) { T.T60 = T.RunTime; T.T60Rollout = T.RunTime - std::max(0.0, RolloutTime); }
			if (T.T100kmh < 0 && v >= 100 / 3.6) T.T100kmh = T.RunTime;
			if (T.T100mph < 0 && v >= 100 * kMph) T.T100mph = T.RunTime;
			if (T.TQuarter < 0 && T.RunDist >= 402.336) { T.TQuarter = T.RunTime; T.QuarterTrapMph = v / kMph; }
			if (in.Brake > 0.3 || T.RunTime > 60 || (T.TQuarter > 0 && v < 1.0)) T.bRunActive = false;
		}
	};
}
