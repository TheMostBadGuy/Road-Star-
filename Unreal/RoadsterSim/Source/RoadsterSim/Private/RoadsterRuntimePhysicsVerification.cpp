#include "RoadsterPawn.h"
#include "TrackActor.h"
#include "ProceduralMeshComponent.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include <limits>

// Opt-in integration diagnostics. Every Vehicle::Step collision query below goes through the
// canonical Pawn::MakeTrace and the real UWorld. No replacement plane/tube trace or pose correction.
namespace
{
	using Roadster::V3;
	using Roadster::Quat;
	using Roadster::Vehicle;
	using Roadster::Inputs;
	V3 RuntimeMetres(const FVector& V) { return {V.X * .01, V.Y * .01, V.Z * .01}; }
	Quat RuntimeQuat(const FQuat& Q) { return {Q.W, Q.X, Q.Y, Q.Z}; }
	bool RuntimeFinite(const Vehicle& C)
	{
		return std::isfinite(C.X.Len()) && std::isfinite(C.V.Len()) && std::isfinite(C.W.Len())
			&& std::isfinite(C.Q.w) && std::isfinite(C.Q.x) && std::isfinite(C.Q.y) && std::isfinite(C.Q.z)
			&& std::isfinite(C.EnergyKWh) && std::isfinite(C.TankKg) && C.V.Len() < 1000000.0;
	}
	bool SameVector(const V3& A, const V3& B) { return A.x == B.x && A.y == B.y && A.z == B.z; }
	bool SameInputs(const Inputs& A, const Inputs& B)
	{
		return A.Throttle == B.Throttle && A.Brake == B.Brake && A.Steer == B.Steer && A.Handbrake == B.Handbrake
			&& A.Boost == B.Boost && A.Lift == B.Lift && A.Pitch == B.Pitch && A.Roll == B.Roll && A.Yaw == B.Yaw
			&& A.bWantReverse == B.bWantReverse && A.bDRS == B.bDRS && A.bAttitudeBrake == B.bAttitudeBrake
			&& A.bPilotStunt == B.bPilotStunt;
	}
	struct FRuntimeFrame
	{
		V3 P, N, Right;
		Quat Q;
		double Width = 0, TubeRadius = 0;
		int32 Piece = -1;
	};
	bool RuntimeFrame(ATrackActor* Track, const TFunction<bool(const FTrackSample&)>& Accept,
		bool bMostInverted, FRuntimeFrame& Out)
	{
		const auto& Samples = Track->GetSamples();
		int32 Best = -1;
		double Score = -std::numeric_limits<double>::infinity();
		double MinS = std::numeric_limits<double>::infinity(), MaxS = -std::numeric_limits<double>::infinity();
		for (const auto& Sample : Samples)
			if (!Sample.bGap && Accept(Sample)) { MinS = FMath::Min(MinS, double(Sample.S)); MaxS = FMath::Max(MaxS, double(Sample.S)); }
		const double MidS = .5 * (MinS + MaxS);
		for (int32 i = 3; i + 3 < Samples.Num(); ++i)
		{
			const auto& A = Samples[i]; const auto& B = Samples[i + 1];
			if (A.bGap || B.bGap || A.Piece != B.Piece || !Accept(A) || !Accept(B)) continue;
			const double Candidate = bMostInverted ? -A.Q.GetUpVector().Z : -FMath::Abs(double(A.S) - MidS);
			if (Candidate > Score) { Best = i; Score = Candidate; }
		}
		if (Best < 0) return false;
		const auto& A = Samples[Best]; const auto& B = Samples[Best + 1];
		const FTransform& Transform = Track->GetActorTransform();
		const FQuat Q = Transform.GetRotation() * FQuat::Slerp(A.Q, B.Q, .37f).GetNormalized();
		Out.P = RuntimeMetres(Transform.TransformPosition(FMath::Lerp(A.P, B.P, .37f)));
		Out.N = RuntimeMetres(Q.GetUpVector() * 100.0);
		Out.Right = RuntimeMetres(Q.GetRightVector() * 100.0);
		Out.Q = RuntimeQuat(Q); Out.Width = A.Width; Out.TubeRadius = A.TubeR; Out.Piece = A.Piece;
		return true;
	}
	Vehicle RuntimeCar(const FRuntimeFrame& Frame, bool bFan)
	{
		Vehicle C; C.Init(Roadster::Trim::Rocket);
		C.Set.Aero = Roadster::AeroPreset::Custom;
		C.Set.Fan = bFan ? Roadster::FanMode::MaxGrip : Roadster::FanMode::Off;
		C.Set.bRoadstarMode = false; C.Set.bHoverHold = false;
		C.Set.bFlightAssist = false; C.Set.bLandingAssist = false;
		C.Set.bInfiniteGas = false; C.Set.bInfiniteBattery = false;
		C.Set.bThrustersArmed = false;
		C.Place(Frame.P + Frame.N * .03, Frame.Q);
		return C;
	}
	struct FRuntimeStats
	{
		bool bFinite = true;
		int32 Steps = 0, SteadySteps = 0, SteadyFourContacts = 0, SteadyFiveFanHits = 0;
		int32 FirstWheelStep = -1, FirstBodyStep = -1;
		double FirstWheelUp = -2, FirstBodyUp = -2;
		double MinUp = 1, MaxGap = 0, MaxSpeed = 0, MaxNormalSpeed = 0, MaxThrust = 0;
		int64 Queries = 0, FanRays = 0, FanHits = 0, BodyHits = 0;
	};
}

bool RunRoadsterRuntimePhysicsVerification(ARoadsterPawn* Pawn, FString& OutReport)
{
	if (!Pawn || !Pawn->GetWorld() || !Pawn->GetWorld()->GetPhysicsScene())
	{
		OutReport = TEXT("FAIL prerequisite: live Pawn, UWorld and physics scene are required.");
		return false;
	}
	ATrackActor* OriginalTrack = Pawn->FindTrack();
	if (!OriginalTrack)
	{
		OutReport = TEXT("FAIL prerequisite: the playable world's canonical TrackActor is missing.");
		return false;
	}
	const double StartTime = FPlatformTime::Seconds();
	const Vehicle LiveBefore = Pawn->Sim;
	const Inputs InputBefore = Pawn->Input;
	const FTransform PoseBefore = Pawn->GetActorTransform();
	const FString OriginalName = OriginalTrack->TrackName;
	const int32 OriginalPieces = OriginalTrack->Pieces.Num(), OriginalSelected = OriginalTrack->SelectedPiece;
	const double OriginalLength = OriginalTrack->GetLengthM();
	const bool bOriginalClosed = OriginalTrack->bCloseLoop;
	TArray<FString> Lines, Csv;
	TArray<FString> LoopDebug = { TEXT("t_s,gap_m,vN_mps,contacts,FL_c,FL_len,FL_N,FR_c,FR_len,FR_N,RL_c,RL_len,RL_N,RR_c,RR_len,RR_N,probe_c,probe_rl,probe_rr,probe_fl,probe_fr") };
	int32 Failures = 0;
	int64 Queries = 0;
	auto Check = [&](const FString& Name, bool bPass, const FString& Detail)
	{
		Lines.Add(FString(bPass ? TEXT("PASS | ") : TEXT("FAIL | ")) + Name + TEXT(" | ") + Detail);
		if (!bPass) ++Failures;
	};
	Lines.Add(TEXT("Actual UWorld collision + Vehicle::Step(h=0.001 s); finite N2 and finite battery; default hardware.") );
	Lines.Add(TEXT("Canonical transient TrackActor generated collision is isolated 5 km away and 1 km above the main track. Original track is never rebuilt."));
	Lines.Add(TEXT("Body impact diagnostics measure the simplified collision probes, not exact visible mesh clearance or crashworthiness."));
	Csv.Add(TEXT("scenario,t_s,body_x_m,body_y_m,body_z_m,speed_mps,vertical_mps,up_z,wheel_contacts,fan_N,seal,battery_kWh,gas_kg,body_contact"));

	// MakeTrace captures this one setting. Disable its analytic planet fallback while capturing,
	// then restore it immediately; all subsequent diagnostic queries must hit real world geometry.
	const bool bPlanetBefore = Pawn->Sim.Set.bRoadstarMode;
	Pawn->Sim.Set.bRoadstarMode = false;
	const Roadster::TraceFn CanonicalTrace = Pawn->MakeTrace();
	Pawn->Sim.Set.bRoadstarMode = bPlanetBefore;
	FRuntimeStats* ActiveStats = nullptr;
	const Roadster::TraceFn Trace = [&](const V3& P, const V3& D, double Length, double Radius)
	{
		++Queries;
		const Roadster::Hit Hit = CanonicalTrace(P, D, Length, Radius);
		if (ActiveStats)
		{
			++ActiveStats->Queries;
			if (Radius <= .0001 && FMath::Abs(Length - .8) < 1e-8)
			{
				++ActiveStats->FanRays;
				if (Hit.bHit) ++ActiveStats->FanHits;
			}
			if (Radius > .0401 && Hit.bHit) ++ActiveStats->BodyHits;
		}
		return Hit;
	};

	UWorld* World = Pawn->GetWorld();
	const FTransform TestTransform(FQuat::Identity, FVector(500000, 0, 100000));
	ATrackActor* TestTrack = World->SpawnActorDeferred<ATrackActor>(ATrackActor::StaticClass(), TestTransform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestTrack)
	{
		OutReport = TEXT("FAIL prerequisite: cannot spawn an isolated transient canonical TrackActor.");
		return false;
	}
	TestTrack->SetFlags(RF_Transient);
	ON_SCOPE_EXIT
	{
		if (IsValid(TestTrack)) { TestTrack->SetActorEnableCollision(false); TestTrack->Destroy(); }
	};
	UProceduralMeshComponent* Mesh = TestTrack->FindComponentByClass<UProceduralMeshComponent>();
	if (!Mesh)
	{
		OutReport = TEXT("FAIL prerequisite: canonical TrackActor has no ProceduralMeshComponent.");
		return false;
	}
	Mesh->bUseAsyncCooking = false;
	TestTrack->LoadPreset(1);
	TestTrack->FinishSpawning(TestTransform);
	auto LoadPreset = [&](int32 Preset)
	{
		TestTrack->LoadPreset(Preset); TestTrack->Rebuild();
	};
	auto GeometryProbe = [&](const FString& Name, const FRuntimeFrame& Frame)
	{
		const auto Hit = Trace(Frame.P + Frame.N * .5, -Frame.N, 1.0, 0.0);
		const double Alignment = Hit.Normal.Dot(Frame.N);
		const bool bHit = Hit.bHit && FMath::Abs(Hit.Dist - .5) < .03 && Alignment > .98;
		Check(Name + TEXT(" real surface prerequisite"), bHit,
			FString::Printf(TEXT("piece=%d hit=%d distance=%.6f m expected=0.5 m normalDot=%.6f"), Frame.Piece, Hit.bHit, Hit.Dist, Alignment));
		return bHit;
	};
	auto Record = [&](const FString& Name, double Time, const Vehicle& Car)
	{
		const V3 P = Car.BodyOriginWorld();
		Csv.Add(FString::Printf(TEXT("%s,%.3f,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f,%d,%.8f,%.8f,%.10f,%.10f,%d"),
			*Name, Time, P.x, P.y, P.z, Car.V.Len(), Car.V.z, Car.Q.Up().z,
			Car.T.WheelsInContact, Car.T.FanDownforceN, Car.T.FanSeal, Car.EnergyKWh, Car.TankKg, Car.T.bBodyContact));
	};
	auto Run = [&](const FString& Name, Vehicle& Car, const Inputs& Input, int32 Steps, const FRuntimeFrame& Frame)
	{
		FRuntimeStats Stats;
		ActiveStats = &Stats;
		for (int32 i = 0; i < Steps; ++i)
		{
			const int64 HitsBefore = Stats.FanHits;
			Car.Step(.001, Input, Trace);
			++Stats.Steps;
			Stats.bFinite &= RuntimeFinite(Car);
			Stats.MinUp = FMath::Min(Stats.MinUp, Car.Q.Up().Dot(Frame.N));
			Stats.MaxGap = FMath::Max(Stats.MaxGap, (Car.BodyOriginWorld() - Frame.P).Dot(Frame.N));
			Stats.MaxSpeed = FMath::Max(Stats.MaxSpeed, Car.V.Len());
			Stats.MaxNormalSpeed = FMath::Max(Stats.MaxNormalSpeed, Car.V.Dot(Frame.N));
			Stats.MaxThrust = FMath::Max(Stats.MaxThrust, Car.T.ThrustN);
			if (Stats.FirstWheelStep < 0 && Car.T.WheelsInContact > 0)
			{
				Stats.FirstWheelStep = i; Stats.FirstWheelUp = Car.Q.Up().Dot(Frame.N);
			}
			if (Stats.FirstBodyStep < 0 && Car.T.bBodyContact)
			{
				Stats.FirstBodyStep = i; Stats.FirstBodyUp = Car.Q.Up().Dot(Frame.N);
			}
			if (i >= 500)
			{
				++Stats.SteadySteps;
				if (Car.T.WheelsInContact == 4) ++Stats.SteadyFourContacts;
				if (Stats.FanHits - HitsBefore == 5) ++Stats.SteadyFiveFanHits;
			}
			if (i % 100 == 0 || i == Steps - 1)
				Record(Name, (i + 1) * .001, Car);
			if (Name.StartsWith(TEXT("loop apex fan-on")) && i < 400 && i % 10 == 0)
			{
				FString Row = FString::Printf(TEXT("%.3f,%.5f,%.5f,%d"), (i + 1) * .001,
					(Car.BodyOriginWorld() - Frame.P).Dot(Frame.N), Car.V.Dot(Frame.N), Car.T.WheelsInContact);
				for (const auto& W : Car.Wheels) Row += FString::Printf(TEXT(",%d,%.4f,%.0f"), W.bContact ? 1 : 0, W.Length, W.Load);
				for (double C : Car.T.FanProbeClearance) Row += FString::Printf(TEXT(",%.4f"), C);
				LoopDebug.Add(Row);
			}
			if (!Stats.bFinite) break;
		}
		ActiveStats = nullptr;
		return Stats;
	};
	auto Adhesion = [&](const FString& Name, const FRuntimeFrame& Frame, bool bComparisons)
	{
		if (!GeometryProbe(Name, Frame)) return;
		Vehicle Car = RuntimeCar(Frame, true);
		Inputs Hold; Hold.Brake = .2;
		const double EnergyBefore = Car.EnergyKWh;
		const auto On = Run(Name + TEXT(" fan-on"), Car, Hold, 4000, Frame);
		const double ContactFraction = On.SteadySteps > 0 ? double(On.SteadyFourContacts) / On.SteadySteps : 0;
		const double FiveFraction = On.SteadySteps > 0 ? double(On.SteadyFiveFanHits) / On.SteadySteps : 0;
		const double Gap = (Car.BodyOriginWorld() - Frame.P).Dot(Frame.N);
		Check(Name + TEXT(" stationary inverted adhesion"), On.bFinite && Frame.N.z < -.98 && Car.Q.Up().Dot(Frame.N) > .98
			&& ContactFraction > .95 && FiveFraction > .95 && Gap > -.08 && Gap < .35
			&& Car.T.FanDownforceN > Car.TotalMass() * Roadster::kG * FMath::Abs(Frame.N.z),
			FString::Printf(TEXT("4-wheel fraction=%.4f five-hit fraction=%.4f gap=%.5f m upDot=%.6f fan=%.1f N weight=%.1f N rays=%lld"),
				ContactFraction, FiveFraction, Gap, Car.Q.Up().Dot(Frame.N), Car.T.FanDownforceN, Car.TotalMass() * Roadster::kG, On.FanRays));
		Check(Name + TEXT(" finite electrical work and no propulsion"), Car.EnergyKWh < EnergyBefore && Car.EnergyKWh > 0 && On.MaxThrust < 1e-8,
			FString::Printf(TEXT("battery spent=%.8f kWh max jet thrust=%.8f N"), EnergyBefore - Car.EnergyKWh, On.MaxThrust));
		if (!bComparisons) return;
		Vehicle FanOff = Car;
		FanOff.Set.Fan = Roadster::FanMode::Off;
		const auto Off = Run(Name + TEXT(" fan-off"), FanOff, Hold, 1500, Frame);
		Check(Name + TEXT(" fan-off comparison detaches"), Off.bFinite && Off.MaxGap > 1.0 && Off.MaxNormalSpeed > 2.0
			&& FanOff.T.FanDownforceN < Car.T.FanDownforceN * .02,
			FString::Printf(TEXT("maximum separation=%.5f m falling speed=%.5f m/s remaining fan=%.3f N"), Off.MaxGap, Off.MaxNormalSpeed, FanOff.T.FanDownforceN));
		Vehicle Empty = Car;
		Empty.EnergyKWh = 0.0;
		const auto NoBattery = Run(Name + TEXT(" depleted-battery"), Empty, Hold, 1500, Frame);
		Check(Name + TEXT(" depleted battery cannot hold ceiling"), NoBattery.bFinite && NoBattery.MaxGap > 1.0
			&& Empty.T.FanDownforceN < Car.T.FanDownforceN * .02 && Empty.EnergyKWh == 0.0,
			FString::Printf(TEXT("separation=%.5f m fan=%.3f N battery=%.9f kWh"), NoBattery.MaxGap, Empty.T.FanDownforceN, Empty.EnergyKWh));
	};

	FRuntimeFrame Ceiling;
	const bool bCeiling = RuntimeFrame(TestTrack, [](const FTrackSample& S) { return S.Piece == 3 && S.TubeR > 0 && S.Q.GetUpVector().Z < -.999; }, false, Ceiling);
	Check(TEXT("ceiling test frame exists"), bCeiling, TEXT("Ceiling Tunnel preset, long inverted straight"));
	if (bCeiling) Adhesion(TEXT("tube ceiling"), Ceiling, true);
	LoadPreset(2);
	FRuntimeFrame Loop;
	const bool bLoop = RuntimeFrame(TestTrack, [](const FTrackSample& S) { return S.Piece == 1 && S.Q.GetUpVector().Z < -.98; }, true, Loop);
	Check(TEXT("loop test frame exists"), bLoop, TEXT("Loop Gauntlet preset, first 20 m-radius apex"));
	if (bLoop) Adhesion(TEXT("loop apex"), Loop, false);

	LoadPreset(4);
	FRuntimeFrame Floor;
	const bool bFloor = RuntimeFrame(TestTrack, [](const FTrackSample& S) { return S.Piece == 0 && S.Q.GetUpVector().Z > .999; }, false, Floor);
	Check(TEXT("floor test frame exists"), bFloor, TEXT("Drag Strip preset, first straight"));
	if (bFloor && GeometryProbe(TEXT("drag floor"), Floor))
	{
		Vehicle Settled = RuntimeCar(Floor, true);
		Settled.Set.bThrustersArmed = true;
		Inputs Hold; Hold.Brake = .2;
		const auto Warm = Run(TEXT("floor settle"), Settled, Hold, 2000, Floor);
		Check(TEXT("floor settles on all four wheels"), Warm.bFinite && Settled.T.WheelsInContact == 4 && Settled.V.Len() < .1
			&& Settled.Q.Up().Dot(Floor.N) > .99,
			FString::Printf(TEXT("contacts=%d speed=%.6f m/s upDot=%.8f"), Settled.T.WheelsInContact, Settled.V.Len(), Settled.Q.Up().Dot(Floor.N)));
		Vehicle Motor = Settled, Boost = Settled;
		Inputs Accelerate; Accelerate.Throttle = 1.0;
		const double StartEnergy = Motor.EnergyKWh, StartGas = Motor.TankKg;
		const auto MotorStats = Run(TEXT("motor acceleration"), Motor, Accelerate, 1000, Floor);
		Accelerate.Boost = 1.0;
		const auto BoostStats = Run(TEXT("boost acceleration"), Boost, Accelerate, 1000, Floor);
		const double MotorForward = Motor.V.Dot(Floor.Q.Fwd()), BoostForward = Boost.V.Dot(Floor.Q.Fwd());
		Check(TEXT("settled motor drive accelerates using finite battery"), MotorStats.bFinite && MotorForward > 1.0 && Motor.EnergyKWh < StartEnergy,
			FString::Printf(TEXT("speed=%.6f m/s battery spent=%.8f kWh contacts=%d"), MotorForward, StartEnergy - Motor.EnergyKWh, Motor.T.WheelsInContact));
		Check(TEXT("finite rear boost adds forward momentum and expels propellant"), BoostStats.bFinite && BoostForward > MotorForward
			&& Boost.TotalMass() * BoostForward > Motor.TotalMass() * MotorForward && Boost.TankKg < StartGas - .1
			&& Boost.Gas.Tel().TotalImpulseNs > 100.0,
			FString::Printf(TEXT("motor=%.6f boosted=%.6f m/s momentum gain=%.3f N s expelled=%.6f kg jet impulse=%.3f N s"),
				MotorForward, BoostForward, Boost.TotalMass() * BoostForward - Motor.TotalMass() * MotorForward,
				StartGas - Boost.TankKg, Boost.Gas.Tel().TotalImpulseNs));
	}
	auto Landing = [&](const FString& Name, const FRuntimeFrame& Frame)
	{
		if (!GeometryProbe(Name, Frame)) return;
		Vehicle Car = RuntimeCar(Frame, false);
		Car.Set.bThrustersArmed = true; Car.Set.bLandingAssist = true; Car.Set.bFlightAssist = true;
		Car.Place(Frame.P + V3(0, 0, 8), Frame.Q * Quat::AxisAngle(V3(1, 0, 0), Roadster::kPi));
		const double GasBefore = Car.TankKg;
		// 8 s, no driver input: on the 8 deg landing deck the car touches down rolling ~4.7 m/s downhill and Hold-mode
		// regen (0.25 g against 0.14 g of slope) needs ~4.5 s to stop it; 5 s ended the run while it was still braking.
		const auto Stats = Run(Name, Car, Inputs(), 8000, Frame);
		const double Up = Car.Q.Up().Dot(Frame.N);
		Check(Name + TEXT(" actual geometry recovery"), Stats.bFinite && Up > .98 && Car.T.WheelsInContact == 4 && Car.V.Len() < 1.0
			&& Stats.FirstWheelStep >= 0 && Stats.FirstWheelUp > .90 && (Stats.FirstBodyStep < 0 || Stats.FirstBodyUp > .5)
			&& Car.TankKg < GasBefore && Car.TankKg > 0,
			FString::Printf(TEXT("roll=180 deg drop=8 m first wheel t=%.3f s upDot=%.7f first body t=%.3f s upDot=%.7f final upDot=%.7f contacts=%d speed=%.5f m/s spent gas=%.6f kg bodyHits=%lld"),
				(Stats.FirstWheelStep + 1) * .001, Stats.FirstWheelUp, (Stats.FirstBodyStep + 1) * .001, Stats.FirstBodyUp,
				Up, Car.T.WheelsInContact, Car.V.Len(), GasBefore - Car.TankKg, Stats.BodyHits));
	};
	if (bFloor) Landing(TEXT("flat deck landing"), Floor);
	LoadPreset(0);
	FRuntimeFrame Ramp;
	const bool bRamp = RuntimeFrame(TestTrack, [TestTrack](const FTrackSample& S)
	{
		return TestTrack->Pieces.IsValidIndex(S.Piece) && TestTrack->Pieces[S.Piece].Type == ETrackPiece::Pitch
			&& S.Q.GetUpVector().Z > .95 && S.Q.GetUpVector().Z < .99;
	}, false, Ramp);
	Check(TEXT("inclined landing surface exists"), bRamp, TEXT("Proving Ground generated kicker/landing pitch geometry"));
	if (bRamp) Landing(TEXT("inclined deck landing"), Ramp);

	// Fast explicit sweeps check real geometry continuity. Actual Vehicle impacts separately expose
	// the difference between a continuous query and its current radial body penalty-contact model.
	LoadPreset(4);
	FRuntimeFrame Wall;
	const bool bWall = RuntimeFrame(TestTrack, [](const FTrackSample& S) { return S.Piece == 1 && S.bWalls; }, false, Wall);
	if (bWall)
	{
		const double WallSide = Wall.Width * .5 + 1.0;
		const V3 ProbeStart = Wall.P + Wall.N * .55 + Wall.Right * (WallSide - 4.0);
		const auto Swept = Trace(ProbeStart, Wall.Right, 12.0, .15);
		Check(TEXT("canonical high-speed barrier sphere sweep"), Swept.bHit && FMath::Abs(Swept.CenterDist - 3.85) < .03
			&& Swept.Normal.Dot(-Wall.Right) > .98,
			FString::Printf(TEXT("12 m sweep, r=0.15 m, hit=%d center distance=%.6f m expected=3.85 m"), Swept.bHit, Swept.CenterDist));
		for (double Speed : {40.0, 120.0})
		{
			Vehicle Car = RuntimeCar(Wall, false);
			Car.Place(Wall.P + Wall.N * .03 + Wall.Right * (WallSide - 4.0), Wall.Q);
			Car.V = Wall.Right * Speed;
			double MaxSide = -1e30, MaxProbePenetration = 0;
			bool bFinite = true, bBodyContact = false, bFullCrossing = false;
			FRuntimeStats ImpactStats; ActiveStats = &ImpactStats;
			for (int32 i = 0; i < 500; ++i)
			{
				Car.Step(.001, Inputs(), Trace);
				bFinite &= RuntimeFinite(Car); bBodyContact |= Car.T.bBodyContact;
				const double Side = (Car.X - Wall.P).Dot(Wall.Right);
				MaxSide = FMath::Max(MaxSide, Side);
				for (int32 p = 0; p < Car.NumProbes; ++p)
				{
					const V3 Position = Car.BodyToWorld(Car.Probes[p].P);
					const double Height = (Position - Wall.P).Dot(Wall.N);
					if (Height > .04 && Height < 1.05)
						MaxProbePenetration = FMath::Max(MaxProbePenetration, (Position - Wall.P).Dot(Wall.Right) + Car.Probes[p].R - WallSide);
				}
				if (Side > WallSide + .45 + 1.1 && (Car.X - Wall.P).Dot(Wall.N) < 1.05) bFullCrossing = true;
				if (i % 10 == 0 || i == 499) Record(FString::Printf(TEXT("barrier %.0f mps"), Speed), (i + 1) * .001, Car);
				if (!bFinite) break;
			}
			ActiveStats = nullptr;
			Check(FString::Printf(TEXT("Vehicle barrier impact %.0f m/s"), Speed), bFinite && bBodyContact && !bFullCrossing,
				FString::Printf(TEXT("body contact=%d low full crossing=%d max COM side=%.5f m wall=%.2f m max probe penetration=%.5f m final lateral speed=%.5f m/s body hits=%lld"),
					bBodyContact, bFullCrossing, MaxSide, WallSide, MaxProbePenetration, Car.V.Dot(Wall.Right), ImpactStats.BodyHits));
			Lines.Add(FString::Printf(TEXT("LIMITATION | %.0f m/s barrier probe-envelope penetration=%.5f m; visible mesh clearance is unverified."), Speed, MaxProbePenetration));
		}
	}
	else Check(TEXT("barrier test frame exists"), false, TEXT("Drag Strip second straight with walls is missing"));
	LoadPreset(1);
	if (bCeiling)
	{
		const V3 Start = Ceiling.P + Ceiling.N * 4.0;
		const auto Swept = Trace(Start, -Ceiling.N, 12.0, .15);
		Check(TEXT("canonical high-speed ceiling sphere sweep"), Swept.bHit && FMath::Abs(Swept.CenterDist - 3.85) < .03
			&& Swept.Normal.Dot(Ceiling.N) > .98,
			FString::Printf(TEXT("12 m sweep, r=0.15 m, hit=%d center distance=%.6f m expected=3.85 m"), Swept.bHit, Swept.CenterDist));
		for (double Speed : {40.0, 120.0})
		{
			Vehicle Car = RuntimeCar(Ceiling, false);
			Car.Place(Ceiling.P + Ceiling.N * 5.0, Quat());
			Car.V = -Ceiling.N * Speed;
			bool bFinite = true, bContact = false;
			double MaxOutside = -1e30;
			FRuntimeStats ImpactStats; ActiveStats = &ImpactStats;
			for (int32 i = 0; i < 350; ++i)
			{
				Car.Step(.001, Inputs(), Trace);
				bFinite &= RuntimeFinite(Car); bContact |= Car.T.bBodyContact;
				MaxOutside = FMath::Max(MaxOutside, -(Car.X - Ceiling.P).Dot(Ceiling.N));
				if (i % 10 == 0 || i == 349) Record(FString::Printf(TEXT("ceiling %.0f mps"), Speed), (i + 1) * .001, Car);
				if (!bFinite) break;
			}
			ActiveStats = nullptr;
			Check(FString::Printf(TEXT("Vehicle ceiling impact %.0f m/s"), Speed), bFinite && bContact && MaxOutside < 1.5,
				FString::Printf(TEXT("body contact=%d max COM outside roof=%.5f m final vertical speed=%.5f m/s body hits=%lld"),
					bContact, MaxOutside, Car.V.z, ImpactStats.BodyHits));
		}
	}

	const auto& Live = Pawn->Sim;
	const bool bLiveSame = SameVector(Live.X, LiveBefore.X) && SameVector(Live.V, LiveBefore.V) && SameVector(Live.W, LiveBefore.W)
		&& Live.Q.w == LiveBefore.Q.w && Live.Q.x == LiveBefore.Q.x && Live.Q.y == LiveBefore.Q.y && Live.Q.z == LiveBefore.Q.z
		&& Live.SimTime == LiveBefore.SimTime && Live.StepCount == LiveBefore.StepCount && Live.EnergyKWh == LiveBefore.EnergyKWh
		&& Live.TankKg == LiveBefore.TankKg && Live.Gas.GasMassKg() == LiveBefore.Gas.GasMassKg()
		&& Live.Set.bRoadstarMode == LiveBefore.Set.bRoadstarMode && SameInputs(Pawn->Input, InputBefore)
		&& Pawn->GetActorTransform().Equals(PoseBefore, 0.0);
	Check(TEXT("live vehicle and original track preserved"), bLiveSame && OriginalTrack->TrackName == OriginalName
		&& OriginalTrack->Pieces.Num() == OriginalPieces && OriginalTrack->SelectedPiece == OriginalSelected
		&& OriginalTrack->bCloseLoop == bOriginalClosed && OriginalTrack->GetLengthM() == OriginalLength,
		TEXT("live pose, momentum, clock, resources, input, Earth setting and original track unchanged; only transient test track rebuilt"));
	Lines.Add(FString::Printf(TEXT("RESULT: %d failures; %lld canonical UWorld queries; elapsed %.3f seconds. No real-vehicle calibration or exact visual-envelope claim."),
		Failures, Queries, FPlatformTime::Seconds() - StartTime));
	OutReport = FString::Join(Lines, TEXT("\n"));
	FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../../Verification-20261026/RuntimePhysics"));
	FPaths::CollapseRelativeDirectories(Directory);
	IFileManager::Get().MakeDirectory(*Directory, true);
	FFileHelper::SaveStringToFile(OutReport, *(Directory / TEXT("RuntimePhysicsVerification.txt")));
	FFileHelper::SaveStringToFile(FString::Join(Csv, TEXT("\n")), *(Directory / TEXT("runtime-vehicle-world.csv")));
	FFileHelper::SaveStringToFile(FString::Join(LoopDebug, TEXT("\n")), *(Directory / TEXT("runtime-loop-apex-detail.csv")));
	UE_LOG(LogTemp, Display, TEXT("ROADSTER_RUNTIME_PHYSICS\n%s"), *OutReport);
	return Failures == 0;
}
