#include "TrackActor.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
	constexpr float M = 100.f;   // metres -> cm
	enum ESection { S_Asphalt = 0, S_Curb, S_Barrier, S_Tube, S_LED, S_Start, S_Count };

	struct FSectionBuf
	{
		TArray<FVector> V; TArray<int32> T; TArray<FVector> N; TArray<FVector2D> UV0, UV1; TArray<FProcMeshTangent> Tan;
		void Quad(const FVector& a0, const FVector& b0, const FVector& a1, const FVector& b1,
			const FVector& na0, const FVector& nb0, const FVector& na1, const FVector& nb1,
			FVector2D ua0, FVector2D ub0, FVector2D ua1, FVector2D ub1,
			FVector2D wa0 = FVector2D::ZeroVector, FVector2D wb0 = FVector2D::ZeroVector, FVector2D wa1 = FVector2D::ZeroVector, FVector2D wb1 = FVector2D::ZeroVector)
		{
			const int32 i = V.Num();
			V.Append({ a0, b0, a1, b1 });
			N.Append({ na0, nb0, na1, nb1 });
			UV0.Append({ ua0, ub0, ua1, ub1 });
			UV1.Append({ wa0, wb0, wa1, wb1 });
			const FVector Along = (a1 - a0).GetSafeNormal();
			for (int32 k = 0; k < 4; ++k) Tan.Add(FProcMeshTangent(Along, false));
			// front face towards the supplied normal (UE: cross(v1-v0, v2-v0) points at the viewer)
			const FVector G = FVector::CrossProduct(a1 - a0, b0 - a0);
			if (FVector::DotProduct(G, na0 + nb0) >= 0) T.Append({ i, i + 2, i + 1, i + 1, i + 2, i + 3 });
			else T.Append({ i, i + 1, i + 2, i + 1, i + 3, i + 2 });
		}
	};

	float Smooth(float t) { return t * t * (3.f - 2.f * t); }
	float WrapPi(float a) { return FMath::UnwindRadians(a); }
}

ATrackActor::ATrackActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->bUseAsyncCooking = true;
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Mesh->SetCollisionObjectType(ECC_WorldStatic);
	Mesh->SetMobility(EComponentMobility::Movable);

	Supports = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Supports"));
	Supports->SetupAttachment(Mesh);
	Supports->SetCollisionProfileName(TEXT("BlockAll"));
	Supports->SetCollisionObjectType(ECC_WorldStatic);
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"))) Supports->SetStaticMesh(Cube);

	const TCHAR* MatPaths[S_Count] = {
		TEXT("/Game/Materials/M_Track_Asphalt.M_Track_Asphalt"), TEXT("/Game/Materials/M_Track_Curb.M_Track_Curb"),
		TEXT("/Game/Materials/M_Track_Barrier.M_Track_Barrier"), TEXT("/Game/Materials/M_Track_Tube.M_Track_Tube"),
		TEXT("/Game/Materials/M_Track_LED.M_Track_LED"), TEXT("/Game/Materials/M_Track_StartLine.M_Track_StartLine") };
	for (int32 i = 0; i < S_Count; ++i) Mats.Add(LoadObject<UMaterialInterface>(nullptr, MatPaths[i]));
	if (UMaterialInterface* SM = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_Track_Support.M_Track_Support"))) Supports->SetMaterial(0, SM);

	LoadPreset(0);
}

void ATrackActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void ATrackActor::BeginPlay()
{
	Super::BeginPlay();
	if (Samples.Num() == 0) Rebuild();
}

// ============================================================================ presets
TArray<FString> ATrackActor::PresetNames()
{
	return { TEXT("Proving Ground"), TEXT("Ceiling Tunnel"), TEXT("Loop Gauntlet"), TEXT("Inverted Coaster"), TEXT("Drag Strip"), TEXT("Corkscrew Canyon") };
}

void ATrackActor::LoadPreset(int32 Index)
{
	auto P = [](ETrackPiece T, float L, float A = 0, float R = 60, float B = 0, float O = 0, float W = 14, bool Walls = true)
	{
		FTrackPiece X; X.Type = T; X.Length = L; X.Angle = A; X.Radius = R; X.Bank = B; X.Offset = O; X.Width = W; X.bWalls = Walls; return X;
	};
	using E = ETrackPiece;
	Pieces.Reset();
	bCloseLoop = true;
	switch (Index)
	{
	default:
	case 0:   // Proving Ground: everything the car can do on one lap
		TrackName = TEXT("Proving Ground");
		Pieces = {
			P(E::Straight, 260, 0, 0, 0, 0, 16, false),
			P(E::Turn, 0, 90, 90, 12),
			P(E::Straight, 80, 0, 0, 0),
			P(E::Loop, 0, 0, 16, 0, 16),
			P(E::Straight, 90),
			P(E::Tube, 40, 0, 7.5f),
			P(E::BarrelRoll, 70, 180, 7.5f),
			P(E::Tube, 160, 0, 7.5f),
			P(E::BarrelRoll, 70, 180, 7.5f),
			P(E::Tube, 40, 0, 7.5f),
			P(E::Straight, 60),
			P(E::Turn, 0, 90, 70, 18),
			P(E::Straight, 120),
			P(E::Corkscrew, 140, 360, 5.f),
			P(E::Straight, 80),
			P(E::Pitch, 30, 14),
			P(E::Pitch, 18, -14),
			P(E::Gap, 38, 0, 0, 0, 2.5f),
			P(E::Pitch, 30, -10),
			P(E::Pitch, 30, 10),
			P(E::Straight, 60),
			P(E::Turn, 0, 90, 80, 14),
			P(E::Straight, 120),
		};
		break;
	case 1:   // Ceiling Tunnel: roll onto the roof of a tube, run a long inverted straight, roll back
		TrackName = TEXT("Ceiling Tunnel");
		Pieces = {
			P(E::Straight, 220, 0, 0, 0, 0, 16, false),
			P(E::Tube, 60, 0, 8.f),
			P(E::BarrelRoll, 80, 180, 8.f),
			P(E::Tube, 420, 0, 8.f),
			P(E::BarrelRoll, 80, 180, 8.f),
			P(E::Tube, 60, 0, 8.f),
			P(E::Straight, 200),
			P(E::Turn, 0, 180, 90, 20),
			P(E::Straight, 300),
			P(E::Turn, 0, 180, 90, 20),
		};
		break;
	case 2:   // Loop Gauntlet
		TrackName = TEXT("Loop Gauntlet");
		Pieces = {
			P(E::Straight, 300, 0, 0, 0, 0, 16, false),
			P(E::Loop, 0, 0, 12, 0, 15),
			P(E::Straight, 120),
			P(E::Loop, 0, 0, 18, 0, 15),
			P(E::Straight, 140),
			P(E::Loop, 0, 0, 25, 0, 16),
			P(E::Straight, 160),
			P(E::Turn, 0, 180, 70, 25),
			P(E::Straight, 700),
			P(E::Turn, 0, 180, 70, 25),
		};
		break;
	case 3:   // Inverted Coaster: open-air track that rolls over and stays upside down
		TrackName = TEXT("Inverted Coaster");
		Pieces = {
			P(E::Straight, 250, 0, 0, 0, 0, 16, false),
			P(E::Pitch, 60, 12),
			P(E::Pitch, 60, -12),
			P(E::Corkscrew, 90, 180, 4.5f),
			P(E::Straight, 160),
			P(E::Turn, 0, 90, 80, 0),
			P(E::Straight, 100),
			P(E::Corkscrew, 90, 180, 4.5f),
			P(E::Pitch, 60, -12),
			P(E::Pitch, 60, 12),
			P(E::Turn, 0, 90, 80, 15),
			P(E::Straight, 260),
			P(E::Turn, 0, 180, 90, 18),
		};
		break;
	case 4:   // Drag Strip: quarter mile + shutdown, timed by the 0-60 / 1/4-mile clock
		TrackName = TEXT("Drag Strip");
		bCloseLoop = false;
		Pieces = { P(E::Straight, 402.336f + 30.f, 0, 0, 0, 0, 22, false), P(E::Straight, 900, 0, 0, 0, 0, 22, true) };
		break;
	case 5:   // Corkscrew Canyon
		TrackName = TEXT("Corkscrew Canyon");
		Pieces = {
			P(E::Straight, 220, 0, 0, 0, 0, 16, false),
			P(E::Corkscrew, 120, 360, 5.f),
			P(E::Turn, 0, 60, 60, 30),
			P(E::Corkscrew, 120, -360, 5.f),
			P(E::Turn, 0, -60, 60, -30),
			P(E::Pitch, 40, 20),
			P(E::Loop, 0, 0, 15, 0, -15),
			P(E::Pitch, 40, -20),
			P(E::Turn, 0, 120, 70, 35),
			P(E::Straight, 200),
			P(E::Turn, 0, 120, 70, 35),
		};
		break;
	}
}

// ============================================================================ sampling
void ATrackActor::BuildSamples()
{
	Samples.Reset();
	FVector Ax = FVector::ZeroVector;     // axis / path centre (m)
	FQuat Qp = FQuat::Identity;           // path frame
	float Roll = 0.f;                     // road roll about the path X axis (rad)
	float H = 0.f;                        // road sits H metres "below" the axis (tube radius / corkscrew height)
	float S = 0.f;

	auto RoadQ = [&]() { return Qp * FQuat(FVector::ForwardVector, Roll); };
	auto Emit = [&](int32 Piece, float Width, float TubeR, bool bWalls, bool bGap)
	{
		FTrackSample X;
		const FQuat Qr = RoadQ();
		X.Q = Qr;
		X.P = (Ax - Qr.GetUpVector() * H) * M;
		X.Axis = Ax * M;
		X.QPath = Qp;
		X.S = S; X.Width = Width; X.TubeR = TubeR; X.bWalls = bWalls; X.bGap = bGap; X.Piece = Piece;
		Samples.Add(X);
	};

	if (Pieces.Num() == 0) return;
	Emit(0, Pieces[0].Width, Pieces[0].Type == ETrackPiece::Tube || Pieces[0].Type == ETrackPiece::BarrelRoll ? Pieces[0].Radius : 0.f, Pieces[0].bWalls, false);

	for (int32 i = 0; i < Pieces.Num(); ++i)
	{
		const FTrackPiece& Pc = Pieces[i];
		const bool bTubeLike = Pc.Type == ETrackPiece::Tube || Pc.Type == ETrackPiece::BarrelRoll;
		const bool bAxisPiece = bTubeLike || Pc.Type == ETrackPiece::Corkscrew;
		const float NewH = bAxisPiece ? FMath::Max(2.f, Pc.Radius) : 0.f;
		if (!FMath::IsNearlyEqual(NewH, H))
		{
			// re-centre the axis so the road position stays continuous
			const FVector Road = Ax - RoadQ().GetUpVector() * H;
			H = NewH;
			Ax = Road + RoadQ().GetUpVector() * H;
		}

		float L = FMath::Max(1.f, Pc.Length);
		float YawRate = 0, PitchRate = 0, RollRate = 0;
		FVector Side = FVector::ZeroVector;
		switch (Pc.Type)
		{
		case ETrackPiece::Turn: L = FMath::Max(5.f, FMath::Abs(FMath::DegreesToRadians(Pc.Angle)) * Pc.Radius); YawRate = FMath::DegreesToRadians(Pc.Angle) / L; break;
		case ETrackPiece::Loop: L = 2 * PI * FMath::Max(5.f, Pc.Radius); PitchRate = 2 * PI / L; break;
		case ETrackPiece::Pitch: PitchRate = FMath::DegreesToRadians(Pc.Angle) / L; break;
		case ETrackPiece::Corkscrew:
		case ETrackPiece::BarrelRoll: RollRate = FMath::DegreesToRadians(Pc.Angle) / L; break;
		default: break;
		}
		const float StartRoll = Roll;
		const float BaseRoll = FMath::RoundToFloat(StartRoll / PI) * PI;
		const bool bBanks = Pc.Type == ETrackPiece::Straight || Pc.Type == ETrackPiece::Turn || Pc.Type == ETrackPiece::Pitch || Pc.Type == ETrackPiece::Tube;
		// positive Bank tilts the surface towards the inside of a right-hand turn (road normal leans to +Y)
		const float TargetRoll = BaseRoll - FMath::DegreesToRadians(Pc.Bank);
		const int32 N = FMath::Max(2, FMath::CeilToInt(L / (Pc.Type == ETrackPiece::Loop ? 0.6f : 1.f)));
		const float Ds = L / N;
		const FVector LoopSide = Qp.GetRightVector();
		for (int32 k = 1; k <= N; ++k)
		{
			const float t = k / float(N);
			// midpoint integration: half rotation, advance, half rotation
			auto Rotate = [&](float D)
			{
				if (YawRate != 0) Qp = Qp * FQuat(FVector::UpVector, YawRate * D);
				if (PitchRate != 0) Qp = Qp * FQuat(FVector::RightVector, -PitchRate * D);   // nose up
				Qp.Normalize();
			};
			Rotate(Ds * 0.5f);
			Ax += Qp.GetForwardVector() * Ds;
			if (Pc.Type == ETrackPiece::Loop) Ax += LoopSide * (Pc.Offset / N);
			if (Pc.Type == ETrackPiece::Gap) Ax.Z -= Pc.Offset / N;
			Rotate(Ds * 0.5f);
			if (RollRate != 0) Roll += RollRate * Ds;
			else if (bBanks) Roll = FMath::Lerp(StartRoll, TargetRoll, Smooth(t));
			S += Ds;
			Emit(i, Pc.Width, bTubeLike ? Pc.Radius : 0.f, Pc.bWalls, Pc.Type == ETrackPiece::Gap);
		}
	}

	// optional connector back to the start (cubic Hermite, frames slerped)
	if (bCloseLoop && Samples.Num() > 4)
	{
		const FTrackSample E = Samples.Last();
		const FTrackSample& S0 = Samples[0];
		const FVector P0 = E.P / M, P1 = S0.P / M;
		const float D = FVector::Dist(P0, P1);
		if (D > 3.f && D < 4000.f)
		{
			const FVector T0 = E.Q.GetForwardVector() * D * 1.1f, T1 = S0.Q.GetForwardVector() * D * 1.1f;
			const int32 N = FMath::CeilToInt(D * 1.3f);
			FVector Prev = P0;
			for (int32 k = 1; k <= N; ++k)
			{
				const float t = k / float(N), t2 = t * t, t3 = t2 * t;
				const FVector Pt = (2 * t3 - 3 * t2 + 1) * P0 + (t3 - 2 * t2 + t) * T0 + (-2 * t3 + 3 * t2) * P1 + (t3 - t2) * T1;
				const FVector Dir = (Pt - Prev).GetSafeNormal();
				const FVector UpI = FQuat::Slerp(E.Q, S0.Q, Smooth(t)).GetUpVector();
				const FVector Right = FVector::CrossProduct(UpI, Dir).GetSafeNormal();
				const FVector Up = FVector::CrossProduct(Dir, Right).GetSafeNormal();
				FTrackSample X;
				X.Q = FRotationMatrix::MakeFromXZ(Dir, Up).ToQuat();
				X.P = Pt * M; X.Axis = X.P; X.QPath = X.Q;
				S += FVector::Dist(Pt, Prev);
				X.S = S; X.Width = FMath::Lerp(E.Width, S0.Width, t); X.TubeR = 0; X.bWalls = true; X.Piece = Pieces.Num();
				Samples.Add(X);
				Prev = Pt;
			}
		}
	}

	Centerline.Reset();
	const FTransform T = GetActorTransform();
	for (int32 i = 0; i < Samples.Num(); i += 4) Centerline.Add(T.TransformPosition(Samples[i].P));
}

// ============================================================================ mesh
void ATrackActor::BuildMesh()
{
	FSectionBuf B[S_Count];
	const int32 Num = Samples.Num();
	for (int32 i = 0; i + 1 < Num; ++i)
	{
		const FTrackSample& A0 = Samples[i];
		const FTrackSample& A1 = Samples[i + 1];
		if (A1.bGap) continue;
		const float W2 = A1.Width * 0.5f;
		if (A1.TubeR <= 0.f && A0.TubeR <= 0.f)
		{
			// ---- open road: deck, curbs, barriers or sides, underside
			auto L = [&](const FTrackSample& Sm, float y, float z) { return Sm.P + Sm.Q.RotateVector(FVector(0, y, z) * M); };
			auto Nrm = [&](const FTrackSample& Sm, const FVector& n) { return Sm.Q.RotateVector(n); };
			auto Strip = [&](int32 Sec, float ya, float za, float yb, float zb, const FVector& n, float vScale = 0.125f)
			{
				const FVector na0 = Nrm(A0, n), na1 = Nrm(A1, n);
				B[Sec].Quad(L(A0, ya, za), L(A0, yb, zb), L(A1, ya, za), L(A1, yb, zb), na0, na0, na1, na1,
					FVector2D(A0.S * 0.125f, ya * vScale), FVector2D(A0.S * 0.125f, yb * vScale), FVector2D(A1.S * 0.125f, ya * vScale), FVector2D(A1.S * 0.125f, yb * vScale),
					FVector2D(A0.S, (ya + W2) / (2 * W2)), FVector2D(A0.S, (yb + W2) / (2 * W2)), FVector2D(A1.S, (ya + W2) / (2 * W2)), FVector2D(A1.S, (yb + W2) / (2 * W2)));
			};
			const float C = 1.0f, Th = 0.7f, WallH = 1.05f, WallT = 0.45f;
			const float Ext = W2 + C;
			Strip(S_Asphalt, -W2, 0, W2, 0, FVector::UpVector);
			Strip(S_Curb, -Ext, 0.04f, -W2, 0, FVector::UpVector, 1.f);
			Strip(S_Curb, W2, 0, Ext, 0.04f, FVector::UpVector, 1.f);
			if (A1.bWalls)
			{
				Strip(S_Barrier, -Ext, 0.04f, -Ext, WallH, FVector::RightVector, 0.5f);
				Strip(S_Barrier, -Ext, WallH, -Ext - WallT, WallH, FVector::UpVector, 0.5f);
				Strip(S_Barrier, -Ext - WallT, WallH, -Ext - WallT, -Th, -FVector::RightVector, 0.5f);
				Strip(S_Barrier, Ext, WallH, Ext, 0.04f, -FVector::RightVector, 0.5f);
				Strip(S_Barrier, Ext + WallT, WallH, Ext, WallH, FVector::UpVector, 0.5f);
				Strip(S_Barrier, Ext + WallT, -Th, Ext + WallT, WallH, FVector::RightVector, 0.5f);
				Strip(S_Barrier, Ext + WallT, -Th, -Ext - WallT, -Th, -FVector::UpVector, 0.5f);
			}
			else
			{
				Strip(S_Barrier, -Ext, 0.04f, -Ext, -Th, -FVector::RightVector, 0.5f);
				Strip(S_Barrier, Ext, -Th, Ext, 0.04f, FVector::RightVector, 0.5f);
				Strip(S_Barrier, Ext, -Th, -Ext, -Th, -FVector::UpVector, 0.5f);
			}
		}
		else
		{
			// ---- tube: inner surface (asphalt band follows the road roll), outer shell, LED strips
			const float R = FMath::Max(A0.TubeR, A1.TubeR);
			const int32 NT = 72;
			auto Roll = [](const FTrackSample& Sm)
			{
				// road roll relative to the path frame
				const FQuat Rel = Sm.QPath.Inverse() * Sm.Q;
				FVector Ax; float Ang; Rel.ToAxisAndAngle(Ax, Ang);
				return Ax.X >= 0 ? Ang : -Ang;
			};
			const float R0 = Roll(A0), R1 = Roll(A1);
			const float HalfBand = (W2 / R);
			auto TubeP = [&](const FTrackSample& Sm, float Phi, float Rad) { return Sm.Axis + Sm.QPath.RotateVector(FVector(0, FMath::Sin(Phi), -FMath::Cos(Phi)) * Rad * M); };
			auto TubeN = [&](const FTrackSample& Sm, float Phi) { return Sm.QPath.RotateVector(FVector(0, -FMath::Sin(Phi), FMath::Cos(Phi))); };
			for (int32 k = 0; k < NT; ++k)
			{
				const float P0 = 2 * PI * k / NT, P1 = 2 * PI * (k + 1) / NT, Pm = (P0 + P1) * 0.5f;
				const bool bRoad = FMath::Abs(WrapPi(Pm - 0.5f * (R0 + R1))) < HalfBand;
				const int32 Sec = bRoad ? S_Asphalt : S_Tube;
				const float v0 = bRoad ? (WrapPi(P0 - R0) * R + W2) / (2 * W2) : 0.f, v1 = bRoad ? (WrapPi(P1 - R0) * R + W2) / (2 * W2) : 0.f;
				B[Sec].Quad(TubeP(A0, P0, R), TubeP(A0, P1, R), TubeP(A1, P0, R), TubeP(A1, P1, R),
					TubeN(A0, P0), TubeN(A0, P1), TubeN(A1, P0), TubeN(A1, P1),
					FVector2D(A0.S * 0.125f, P0 * R * 0.125f), FVector2D(A0.S * 0.125f, P1 * R * 0.125f), FVector2D(A1.S * 0.125f, P0 * R * 0.125f), FVector2D(A1.S * 0.125f, P1 * R * 0.125f),
					FVector2D(A0.S, v0), FVector2D(A0.S, v1), FVector2D(A1.S, v0), FVector2D(A1.S, v1));
				// outer shell
				const float Ro = R + 0.6f;
				B[S_Tube].Quad(TubeP(A0, P1, Ro), TubeP(A0, P0, Ro), TubeP(A1, P1, Ro), TubeP(A1, P0, Ro),
					-TubeN(A0, P1), -TubeN(A0, P0), -TubeN(A1, P1), -TubeN(A1, P0),
					FVector2D(A0.S * 0.125f, P1 * Ro * 0.125f), FVector2D(A0.S * 0.125f, P0 * Ro * 0.125f), FVector2D(A1.S * 0.125f, P1 * Ro * 0.125f), FVector2D(A1.S * 0.125f, P0 * Ro * 0.125f));
			}
			// LED light lines at 4 fixed angles (top, sides, between) - path frame, so they stay straight while the road twists
			for (float Phi : { PI, PI * 0.5f, PI * 1.5f, PI * 0.75f, PI * 1.25f })
			{
				const float d = 0.12f / R;
				B[S_LED].Quad(TubeP(A0, Phi - d, R - 0.03f), TubeP(A0, Phi + d, R - 0.03f), TubeP(A1, Phi - d, R - 0.03f), TubeP(A1, Phi + d, R - 0.03f),
					TubeN(A0, Phi), TubeN(A0, Phi), TubeN(A1, Phi), TubeN(A1, Phi),
					FVector2D(0, 0), FVector2D(1, 0), FVector2D(0, 1), FVector2D(1, 1));
			}
			// portal rings where a tube begins or ends
			const bool bStart = i == 0 || Samples[i].TubeR <= 0.f || (i > 0 && Samples[i - 1].TubeR <= 0.f);
			const bool bEnd = i + 2 >= Num || Samples[i + 2].TubeR <= 0.f;
			for (int32 e = 0; e < 2; ++e)
			{
				if ((e == 0 && !bStart) || (e == 1 && !bEnd)) continue;
				const FTrackSample& Sm = e == 0 ? A0 : A1;
				const FVector Fn = Sm.QPath.GetForwardVector() * (e == 0 ? -1.f : 1.f);
				for (int32 k = 0; k < NT; ++k)
				{
					const float P0 = 2 * PI * k / NT, P1 = 2 * PI * (k + 1) / NT;
					B[S_Barrier].Quad(TubeP(Sm, P0, R), TubeP(Sm, P1, R), TubeP(Sm, P0, R + 0.6f), TubeP(Sm, P1, R + 0.6f), Fn, Fn, Fn, Fn,
						FVector2D(0, 0), FVector2D(1, 0), FVector2D(0, 1), FVector2D(1, 1));
				}
			}
		}
	}
	// chequered start line across the first metres
	if (Num > 4)
	{
		const FTrackSample& A0 = Samples[0];
		const FTrackSample& A1 = Samples[FMath::Min(3, Num - 1)];
		const float W2 = A0.Width * 0.5f;
		auto L = [&](const FTrackSample& Sm, float y) { return Sm.P + Sm.Q.RotateVector(FVector(0, y, 0.015f) * M); };
		const FVector n0 = A0.Q.GetUpVector(), n1 = A1.Q.GetUpVector();
		B[S_Start].Quad(L(A0, -W2), L(A0, W2), L(A1, -W2), L(A1, W2), n0, n0, n1, n1, FVector2D(0, 0), FVector2D(0, A0.Width / 1.f), FVector2D(3, 0), FVector2D(3, A0.Width / 1.f));
	}

	Mesh->ClearAllMeshSections();
	for (int32 s = 0; s < S_Count; ++s)
	{
		if (B[s].V.Num() == 0) continue;
		Mesh->CreateMeshSection(s, B[s].V, B[s].T, B[s].N, B[s].UV0, B[s].UV1, TArray<FVector2D>(), TArray<FVector2D>(), TArray<FColor>(), B[s].Tan, s != S_LED && s != S_Start);
		if (Mats.IsValidIndex(s) && Mats[s]) Mesh->SetMaterial(s, Mats[s]);
	}
}

void ATrackActor::BuildSupportsAndLights()
{
	Supports->ClearInstances();
	for (UPointLightComponent* L : Lights) if (L) L->DestroyComponent();
	Lights.Reset();

	const FTransform T = GetActorTransform();
	float NextSupport = 0.f, NextLight = 0.f;
	for (const FTrackSample& Sm : Samples)
	{
		if (Sm.bGap) continue;
		if (Sm.TubeR <= 0.f && Sm.S >= NextSupport)
		{
			const FVector Up = Sm.Q.GetUpVector();
			if (Up.Z > 0.6f)
			{
				const FVector Bottom = T.TransformPosition(Sm.P - Up * 70.f);
				if (Bottom.Z > 250.f)
				{
					const float Hgt = Bottom.Z;
					// engine cylinder: 100 cm tall, 100 cm diameter, centred
					Supports->AddInstance(FTransform(FRotator::ZeroRotator, FVector(Bottom.X, Bottom.Y, Hgt * 0.5f), FVector(1.4f, 1.4f, Hgt / 100.f)), true);
					NextSupport = Sm.S + 24.f;
				}
			}
		}
		if (Sm.TubeR > 0.f && Sm.S >= NextLight && Lights.Num() < 64)
		{
			UPointLightComponent* L = NewObject<UPointLightComponent>(this);
			L->SetupAttachment(Mesh);
			L->RegisterComponent();
			L->SetRelativeLocation(Sm.Axis);
			L->SetIntensityUnits(ELightUnits::Candelas);
			L->SetIntensity(180.f);
			L->SetAttenuationRadius(Sm.TubeR * M * 3.f);
			L->SetLightColor(FLinearColor(0.85f, 0.92f, 1.f));
			L->SetCastShadows(false);
			Lights.Add(L);
			NextLight = Sm.S + 35.f;
		}
	}
}

void ATrackActor::Rebuild()
{
	BuildSamples();
	BuildMesh();
	BuildSupportsAndLights();
	bHavePrev = false;
}

// ============================================================================ queries
bool ATrackActor::GetStartFrame(FTransform& Out) const
{
	if (Samples.Num() < 30) return false;
	const FTrackSample& Sm = Samples[20];
	Out = FTransform(Sm.Q, Sm.P + Sm.Q.GetUpVector() * 3.f) * GetActorTransform();
	return true;
}

bool ATrackActor::GetFrameAtPiece(int32 PieceIndex, FTransform& Out) const
{
	for (const FTrackSample& Sm : Samples)
		if (Sm.Piece == PieceIndex && !Sm.bGap)
		{
			Out = FTransform(Sm.Q, Sm.P + Sm.Q.GetUpVector() * 3.f) * GetActorTransform();
			return true;
		}
	return false;
}

bool ATrackActor::FindNearestFrame(const FVector& WorldPos, FTransform& Out) const
{
	if (Samples.Num() == 0) return false;
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldPos);
	int32 Best = -1; float BestD = FLT_MAX;
	for (int32 i = 0; i < Samples.Num(); ++i)
	{
		if (Samples[i].bGap) continue;
		const float D = FVector::DistSquared(Samples[i].P, Local);
		if (D < BestD) { BestD = D; Best = i; }
	}
	if (Best < 0 || BestD > FMath::Square(150.f * M)) return false;
	// step back a little so the car is not dropped on a jump lip
	Best = FMath::Max(0, Best - 5);
	while (Best > 0 && Samples[Best].bGap) --Best;
	const FTrackSample& Sm = Samples[Best];
	Out = FTransform(Sm.Q, Sm.P + Sm.Q.GetUpVector() * 3.f) * GetActorTransform();
	return true;
}

bool ATrackActor::CrossedStartLine(const FVector& WorldPos)
{
	if (Samples.Num() < 2) return false;
	const FTrackSample& Sm = Samples[0];
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldPos) - Sm.P;
	const float Along = FVector::DotProduct(Local, Sm.Q.GetForwardVector());
	const float Lat = FMath::Abs(FVector::DotProduct(Local, Sm.Q.GetRightVector()));
	const float Vert = FMath::Abs(FVector::DotProduct(Local, Sm.Q.GetUpVector()));
	bool bCross = false;
	if (bHavePrev && PrevStartDist < 0.f && Along >= 0.f && Lat < Sm.Width * M && Vert < 800.f) bCross = true;
	PrevStartDist = Along; bHavePrev = true;
	return bCross;
}

// ============================================================================ persistence
static FString TrackDir() { return FPaths::ProjectSavedDir() / TEXT("Tracks"); }

bool ATrackActor::SaveToFile(const FString& Name) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("name"), Name);
	Root->SetBoolField(TEXT("closeLoop"), bCloseLoop);
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FTrackPiece& P : Pieces)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		FJsonObjectConverter::UStructToJsonObject(FTrackPiece::StaticStruct(), &P, O);
		Arr.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("pieces"), Arr);
	FString Out;
	const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root, W);
	return FFileHelper::SaveStringToFile(Out, *(TrackDir() / (Name + TEXT(".json"))));
}

bool ATrackActor::LoadFromFile(const FString& Name)
{
	FString In;
	if (!FFileHelper::LoadFileToString(In, *(TrackDir() / (Name + TEXT(".json"))))) return false;
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(In), Root) || !Root.IsValid()) return false;
	TArray<FTrackPiece> NewPieces;
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("pieces")))
	{
		FTrackPiece P;
		if (FJsonObjectConverter::JsonObjectToUStruct(V->AsObject().ToSharedRef(), &P)) NewPieces.Add(P);
	}
	if (NewPieces.Num() == 0) return false;
	Pieces = NewPieces;
	TrackName = Root->GetStringField(TEXT("name"));
	Root->TryGetBoolField(TEXT("closeLoop"), bCloseLoop);
	Rebuild();
	return true;
}

TArray<FString> ATrackActor::SavedTracks()
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(TrackDir() / TEXT("*.json")), true, false);
	for (FString& F : Files) F = FPaths::GetBaseFilename(F);
	return Files;
}
