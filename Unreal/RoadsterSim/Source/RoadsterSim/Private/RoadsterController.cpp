#include "RoadsterController.h"
#include "RoadsterPawn.h"
#include "RoadsterInterface.h"
#include "TrackActor.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/GameUserSettings.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

extern int32 RoadsterLogLevel();
extern void SetRoadsterLogLevel(int32 Level);

using namespace Roadster;

ARoadsterController::ARoadsterController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
}

void ARoadsterController::BeginPlay()
{
	Super::BeginPlay();
	SetMode(EUIMode::Title);
	// command line: -autodrive (autopilot from the start), -track=N (preset), -cam=N (camera mode)
	int32 TrackIdx = -1, CamIdx = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("track="), TrackIdx))
		if (ATrackActor* T = Track()) { T->LoadPreset(TrackIdx); T->Rebuild(); if (Car()) Car()->ResetToTrack(true); }
	if (FParse::Param(FCommandLine::Get(), TEXT("autodrive")))
	{
		bAutopilot = true; SetMode(EUIMode::Driving);
		if (RoadsterLogLevel() < 1) SetRoadsterLogLevel(1);
	}
	// -camat=x,y,z,yaw,pitch (metres, degrees): free camera at a fixed viewpoint (inspection shots)
	FString CamAt;
	if (FParse::Value(FCommandLine::Get(), TEXT("camat="), CamAt, false))
	{
		TArray<FString> V; CamAt.ParseIntoArray(V, TEXT(","));
		if (V.Num() >= 5)
		{
			SetMode(EUIMode::Builder);
			BuilderPos = FVector(FCString::Atof(*V[0]), FCString::Atof(*V[1]), FCString::Atof(*V[2])) * 100.f;
			BuilderRot = FRotator(FCString::Atof(*V[4]), FCString::Atof(*V[3]), 0.f);
			bShowHints = false;
		}
	}
	// -shots=8,9.5,11 : save screenshots at those play times (Saved/Shots)
	FString Shots;
	if (FParse::Value(FCommandLine::Get(), TEXT("shots="), Shots, false))
	{
		TArray<FString> Parts; Shots.ParseIntoArray(Parts, TEXT(","));
		for (const FString& S : Parts) ShotTimes.Add(FCString::Atof(*S));
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("cam="), CamIdx) && Car()) Car()->CameraMode = (ERoadsterCamera)FMath::Clamp(CamIdx, 0, 6);
}

ARoadsterPawn* ARoadsterController::Car() const { return Cast<ARoadsterPawn>(GetPawn()); }

ATrackActor* ARoadsterController::Track() const
{
	for (TActorIterator<ATrackActor> It(GetWorld()); It; ++It) return *It;
	return nullptr;
}

void ARoadsterController::SetMode(EUIMode NewMode)
{
	Mode = NewMode;
	const bool bCursor = Mode != EUIMode::Driving;
	bShowMouseCursor = bCursor;
	if (bCursor)
	{
		FInputModeGameAndUI M; M.SetHideCursorDuringCapture(false); M.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(M);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
	}
	if (ARoadsterPawn* P = Car())
	{
		P->bExternalCam = Mode == EUIMode::Builder;
		P->bSimPaused = Mode == EUIMode::Menu || Mode == EUIMode::Builder || Mode == EUIMode::Title;
		if (Mode == EUIMode::Builder)
		{
			const FVector C = P->GetActorLocation();
			BuilderPos = C + FVector(-3500, -3500, 2500);
			BuilderRot = (C - BuilderPos).Rotation();
		}
	}
}

void ARoadsterController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	ARoadsterPawn* P = Car();
	PlayTime += DeltaTime;
	if (ShotIndex < ShotTimes.Num() && PlayTime >= ShotTimes[ShotIndex])
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Shots/shot_%02d.png"), ShotIndex), false, false);
		++ShotIndex;
	}


	// mouse for the UI
	float MX = 0, MY = 0;
	if (GetMousePosition(MX, MY)) MousePos = FVector2D(MX, MY);
	bMouseDown = Down(EKeys::LeftMouseButton);
	bMouseClicked = Pressed(EKeys::LeftMouseButton);
	MouseWheel = Pressed(EKeys::MouseScrollUp) ? 1.f : (Pressed(EKeys::MouseScrollDown) ? -1.f : 0.f);
	ToastTime -= DeltaTime;

	// global keys
	if (Pressed(EKeys::Escape) || Pressed(EKeys::Gamepad_Special_Right))
	{
		if (Mode == EUIMode::Driving) SetMode(EUIMode::Menu);
		else if (Mode != EUIMode::Title) SetMode(EUIMode::Driving);
	}
	if (Mode == EUIMode::Title && (Pressed(EKeys::Enter) || Pressed(EKeys::SpaceBar) || Pressed(EKeys::Gamepad_FaceButton_Bottom))) SetMode(EUIMode::Driving);
	if (Pressed(EKeys::Tab) || Pressed(EKeys::Gamepad_Special_Left))
	{
		if (Mode == EUIMode::Driving) SetMode(EUIMode::Touchscreen);
		else if (Mode == EUIMode::Touchscreen) SetMode(EUIMode::Driving);
	}
	if (Pressed(EKeys::F2)) SetMode(Mode == EUIMode::Builder ? EUIMode::Driving : EUIMode::Builder);
	if (Pressed(EKeys::F1)) bShowHints = !bShowHints;

	if (!P) return;
	if (Mode == EUIMode::Builder) { BuilderInput(DeltaTime); P->Input = Inputs(); return; }
	if (Mode == EUIMode::Driving || Mode == EUIMode::Touchscreen)
	{
		DriveInput(DeltaTime);
		if (Mode == EUIMode::Driving && Pressed(EKeys::O)) { bAutopilot = !bAutopilot; ShowToast(bAutopilot ? TEXT("Autopilot ON (demo)") : TEXT("Autopilot OFF")); }
		if (bAutopilot) AutopilotInput(DeltaTime);
	}
	else P->Input = Inputs();
	LogTelemetry(DeltaTime);
}

void ARoadsterController::DriveInput(float Dt)
{
	ARoadsterPawn* P = Car();
	const bool bKeys = Mode == EUIMode::Driving;   // the touchscreen keeps gamepad driving, mouse goes to the UI
	Inputs In;

	// ---- pedals and steering (keyboard smoothed, gamepad analogue)
	const bool bW = bKeys && (Down(EKeys::W) || Down(EKeys::Up));
	const bool bS = bKeys && (Down(EKeys::S) || Down(EKeys::Down));
	const float SteerKey = bKeys ? (float(Down(EKeys::D) || Down(EKeys::Right)) - float(Down(EKeys::A) || Down(EKeys::Left))) : 0.f;
	Throttle = FMath::FInterpConstantTo(Throttle, bW ? 1.f : 0.f, Dt, bW ? 4.f : 8.f);
	Brake = FMath::FInterpConstantTo(Brake, bS ? 1.f : 0.f, Dt, bS ? 6.f : 10.f);
	const float SteerRate = (FMath::Sign(SteerKey) != FMath::Sign(Steer) || SteerKey == 0.f) ? 5.f : 2.6f;
	Steer = FMath::FInterpConstantTo(Steer, SteerKey, Dt, SteerRate);
	const float PadT = Axis(EKeys::Gamepad_RightTriggerAxis), PadB = Axis(EKeys::Gamepad_LeftTriggerAxis);
	const float PadS = Axis(EKeys::Gamepad_LeftX);
	In.Throttle = FMath::Max(Throttle, PadT);
	In.Brake = FMath::Max(Brake, PadB);
	In.Steer = FMath::Abs(PadS) > 0.08f ? FMath::Sign(PadS) * FMath::Pow((FMath::Abs(PadS) - 0.08f) / 0.92f, 1.6f) : Steer;
	In.Handbrake = (bKeys && Down(EKeys::SpaceBar)) || Down(EKeys::Gamepad_FaceButton_Right) ? 1.f : 0.f;

	// ---- Tesla-style reverse: hold brake at a standstill to engage, press accelerator at a standstill to leave
	const float V = P->Sim.T.ForwardSpeed;
	if (!bReverseLatch)
	{
		if (FMath::Abs(V) < 0.4f && In.Brake > 0.5f && In.Throttle < 0.05f) BrakeHoldAtRest += Dt; else BrakeHoldAtRest = 0.f;
		if (BrakeHoldAtRest > 0.45f) { bReverseLatch = true; BrakeHoldAtRest = 0.f; }
	}
	else if (In.Throttle > 0.1f && V > -1.0f) bReverseLatch = false;
	In.bWantReverse = bReverseLatch;

	// ---- thrusters, BOOST (yoke button), lift and yaw nozzles
	In.Boost = (bKeys && (Down(EKeys::LeftShift) || Down(EKeys::RightShift))) || Down(EKeys::Gamepad_FaceButton_Bottom) ? 1.f : 0.f;
	In.Lift = (bKeys && (Down(EKeys::LeftControl) || Down(EKeys::RightControl))) || Down(EKeys::Gamepad_FaceButton_Left) ? 1.f : 0.f;
	In.Yaw = bKeys ? float(Down(EKeys::E)) - float(Down(EKeys::Q)) : 0.f;
	In.Yaw += float(Down(EKeys::Gamepad_DPad_Right)) - float(Down(EKeys::Gamepad_DPad_Left));
	if (P->Sim.T.bAirborne)
	{
		// in the air the pedals and steering become pitch / roll for the corner nozzles (Rocket)
		In.Pitch = In.Throttle - In.Brake + Axis(EKeys::Gamepad_RightY);
		In.Roll = In.Steer;
	}
	In.bDRS = bKeys && Down(EKeys::X);
	P->Input = In;

	// ---- camera look
	FRotator Look = P->LookOffset;
	const float RX = Axis(EKeys::Gamepad_RightX), RY = Axis(EKeys::Gamepad_RightY);
	if (bKeys && Down(EKeys::RightMouseButton))
	{
		float DX = 0, DY = 0; GetInputMouseDelta(DX, DY);
		Look.Yaw += DX * 2.f; Look.Pitch = FMath::Clamp(Look.Pitch + DY * 2.f, -60.f, 60.f);
	}
	else if (FMath::Abs(RX) > 0.15f || FMath::Abs(RY) > 0.15f)
	{
		Look.Yaw = RX * 150.f; Look.Pitch = RY * 45.f;
	}
	else if (P->CameraMode != ERoadsterCamera::Orbit)
	{
		Look = FMath::RInterpTo(Look, FRotator::ZeroRotator, Dt, 3.f);
	}
	if (P->CameraMode == ERoadsterCamera::Orbit && bKeys && Down(EKeys::RightMouseButton)) { float DX = 0, DY = 0; GetInputMouseDelta(DX, DY); Look = FRotator(DY * 3.f, DX * 3.f, 0); }
	P->LookOffset = Look;

	if (!bKeys)
	{
		if (Pressed(EKeys::Gamepad_FaceButton_Top)) P->CycleCamera();
		return;
	}
	// ---- one-shot keys
	Settings& Set = P->Sim.Set;
	if (Pressed(EKeys::C) || Pressed(EKeys::Gamepad_FaceButton_Top)) P->CycleCamera();
	if (Pressed(EKeys::V)) { P->CameraMode = P->CameraMode == ERoadsterCamera::Cockpit ? ERoadsterCamera::Chase : ERoadsterCamera::Cockpit; P->LookOffset = FRotator::ZeroRotator; }
	if (Pressed(EKeys::R) || Pressed(EKeys::Gamepad_LeftThumbstick)) { P->ResetToTrack(false); ShowToast(TEXT("Reset to track")); }
	if (Pressed(EKeys::BackSpace)) { P->ResetToTrack(true); ShowToast(TEXT("Back to the start line")); }
	if (Pressed(EKeys::F) || Pressed(EKeys::Gamepad_RightShoulder))
	{
		Set.Aero = AeroPreset::Custom;
		Set.Fan = (FanMode)(((int32)Set.Fan + 1) % 5);
		ShowToast(FString::Printf(TEXT("Fan: %s"), *FString(FanModeName(Set.Fan))));
	}
	if (Pressed(EKeys::RightBracket) || Pressed(EKeys::Gamepad_DPad_Up))
	{
		Set.Aero = AeroPreset::Custom; Set.Fan = FanMode::Custom;
		Set.FanCustomKPa = FMath::Min(P->Sim.S.FanMaxPressureKPa, Set.FanCustomKPa + 0.5);
		ShowToast(FString::Printf(TEXT("Underbody pressure  -%.1f kPa"), Set.FanCustomKPa));
	}
	if (Pressed(EKeys::LeftBracket) || Pressed(EKeys::Gamepad_DPad_Down))
	{
		Set.Aero = AeroPreset::Custom; Set.Fan = FanMode::Custom;
		Set.FanCustomKPa = FMath::Max(0.0, Set.FanCustomKPa - 0.5);
		ShowToast(FString::Printf(TEXT("Underbody pressure  -%.1f kPa"), Set.FanCustomKPa));
	}
	if (Pressed(EKeys::Z)) { Set.Aero = (AeroPreset)(((int32)Set.Aero + 1) % 4); ShowToast(FString::Printf(TEXT("Aero: %s"), *FString(AeroPresetName(Set.Aero)))); }
	if (Pressed(EKeys::T) || Pressed(EKeys::Gamepad_LeftShoulder))
	{
		P->bTrackMode = !P->bTrackMode;
		Set.Dynamics = P->bTrackMode ? DynamicsPreset::Track : DynamicsPreset::Sport;
		Set.bESC = Set.bTCS = !P->bTrackMode;
		P->LapTime = 0;
		ShowToast(P->bTrackMode ? TEXT("Track Mode  ·  ESC OFF  ·  TCS OFF") : TEXT("Track Mode off"));
	}
	if (Pressed(EKeys::H)) { P->ExteriorLights = (P->ExteriorLights + 1) % 4; }
	if (Pressed(EKeys::M)) { P->SetTrim((Trim)(((int32)P->Sim.S.TrimLevel + 1) % 3)); ShowToast(FString::Printf(TEXT("Augment: %s"), *FString(TrimName(P->Sim.S.TrimLevel)))); }
	if (Pressed(EKeys::N)) { Set.bInfiniteGas = !Set.bInfiniteGas; ShowToast(Set.bInfiniteGas ? TEXT("Unlimited nitrogen (demo)") : TEXT("Real tank")); }
	if (Pressed(EKeys::B)) { P->Sim.RefillTank(); P->Sim.Recharge(); ShowToast(TEXT("Pit: nitrogen refilled, battery charged")); }
	if (Pressed(EKeys::G)) { bSlowMo = !bSlowMo; P->TimeScale = bSlowMo ? 0.25f : 1.f; ShowToast(bSlowMo ? TEXT("Slow motion 1/4") : TEXT("Real time")); }
	if (Pressed(EKeys::P)) { P->bSimPaused = !P->bSimPaused; }
	const FKey Num[6] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six };
	for (int32 i = 0; i < 6; ++i)
		if (Pressed(Num[i]))
			if (ATrackActor* T = Track()) { T->LoadPreset(i); T->Rebuild(); P->ResetToTrack(true); ShowToast(T->TrackName); }
}

void ARoadsterController::BuilderInput(float Dt)
{
	// fly camera: right mouse to look, WASD/QE to move, Shift for speed
	if (Down(EKeys::RightMouseButton))
	{
		float DX = 0, DY = 0; GetInputMouseDelta(DX, DY);
		BuilderRot.Yaw += DX * 3.f;
		BuilderRot.Pitch = FMath::Clamp(BuilderRot.Pitch + DY * 3.f, -89.f, 89.f);
	}
	const float Speed = (Down(EKeys::LeftShift) ? 9000.f : 3000.f) * Dt;
	const FVector F = BuilderRot.Vector(), R = FRotationMatrix(BuilderRot).GetScaledAxis(EAxis::Y);
	if (Down(EKeys::W)) BuilderPos += F * Speed;
	if (Down(EKeys::S)) BuilderPos -= F * Speed;
	if (Down(EKeys::D)) BuilderPos += R * Speed;
	if (Down(EKeys::A)) BuilderPos -= R * Speed;
	if (Down(EKeys::E)) BuilderPos.Z += Speed;
	if (Down(EKeys::Q)) BuilderPos.Z -= Speed;
	if (ARoadsterPawn* P = Car()) { P->ExtCamPos = BuilderPos; P->ExtCamRot = BuilderRot; }
}

void ARoadsterController::AutopilotInput(float Dt)
{
	ARoadsterPawn* P = Car();
	ATrackActor* T = Track();
	if (!P || !T) return;
	const TArray<FTrackSample>& S = T->GetSamples();
	if (S.Num() < 10) return;
	const FTransform TT = T->GetActorTransform();
	const FVector Local = TT.InverseTransformPosition(P->GetActorLocation());

	// nearest sample: windowed search around the last index, global fallback
	auto Search = [&](int32 From, int32 To)
	{
		int32 Best = -1; float BestD = FLT_MAX;
		for (int32 k = From; k <= To; ++k)
		{
			const int32 i = (k % S.Num() + S.Num()) % S.Num();
			const float D = FVector::DistSquared(S[i].P, Local);
			if (D < BestD) { BestD = D; Best = i; }
		}
		return TPair<int32, float>(Best, BestD);
	};
	TPair<int32, float> R = Search(AutoIndex - 20, AutoIndex + 250);
	if (R.Value > FMath::Square(4000.f)) R = Search(0, S.Num() - 1);
	AutoIndex = R.Key;

	const double V = P->Sim.T.Speed;
	auto Ahead = [&](float Metres)
	{
		const float Target = S[AutoIndex].S + Metres;
		int32 j = AutoIndex;
		for (int32 n = 0; n < S.Num(); ++n)
		{
			const int32 k = (AutoIndex + n) % S.Num();
			if (k < AutoIndex && !T->bCloseLoop) return S.Num() - 1;
			const float Sk = k >= AutoIndex ? S[k].S : S[k].S + T->GetLengthM();
			if (Sk >= Target) return k;
			j = k;
		}
		return j;
	};

	// pure pursuit steering in the car's own frame (works on walls, loops and ceilings)
	const float Look = 9.f + (float)V * 0.7f;
	const FTrackSample& TS = S[Ahead(Look)];
	const FVector TargetW = TT.TransformPosition(TS.P + TS.Q.GetUpVector() * 40.f);
	const FVector L = P->GetActorTransform().InverseTransformPosition(TargetW) / 100.f;
	const double Dist2 = FMath::Max(1.0, (double)(L.X * L.X + L.Y * L.Y));
	const double Kappa = 2.0 * L.Y / Dist2;
	const double Delta = FMath::Atan(P->Sim.S.Wheelbase * Kappa);
	const double SteerLimit = FMath::Max(P->Sim.S.MaxSteerDeg * PI / 180.0 / (1.0 + FMath::Pow(V / 30.0, 2.0) * 1.6), 2.5 * PI / 180.0);
	Roadster::Inputs& In = P->Input;
	In.Steer = FMath::Clamp(Delta / SteerLimit, -1.0, 1.0);

	// speed target from turn curvature ahead (in the road plane), with a floor before jump gaps
	double VTarget = 75.0 * AutoSpeedScale;
	bool bGapAhead = false;
	const int32 i0 = AutoIndex;
	for (float d = 10.f; d < 40.f + V * 3.5f; d += 10.f)
	{
		const FTrackSample& A0 = S[Ahead(d - 10.f)];
		const FTrackSample& A1 = S[Ahead(d)];
		if (A1.bGap) bGapAhead = true;
		// arrive at inverted sections at a speed the reduced normal load can manage, braking while upright
		if (A1.Q.GetUpVector().Z < -0.5) VTarget = FMath::Min(VTarget, FMath::Sqrt(40.0 * 40.0 + 2.0 * 8.0 * FMath::Max(0.f, d - 20.f)));
		const FVector N = A1.Q.GetUpVector();
		const FVector F0 = FVector::VectorPlaneProject(A0.Q.GetForwardVector(), N).GetSafeNormal();
		const FVector F1 = A1.Q.GetForwardVector();
		const double AngYaw = FMath::Acos(FMath::Clamp(FVector::DotProduct(F0, F1), -1.f, 1.f));
		const double AngAll = FMath::Acos(FMath::Clamp(FVector::DotProduct(A0.Q.GetForwardVector(), F1), -1.f, 1.f));
		const double AngPitch = FMath::Sqrt(FMath::Max(0.0, AngAll * AngAll - AngYaw * AngYaw));
		// lateral grip ~1.5 g; dips / loops limited to ~3 g of extra load; crests must stay under ~0.9 g of lift-off
		const bool bConvex = FVector::DotProduct(F1 - A0.Q.GetForwardVector(), A0.Q.GetUpVector()) < 0.f;
		const double Curv = FMath::Max(AngYaw / 10.0, AngPitch / 10.0 * (bConvex ? 1.6 : 0.5));
		if (Curv > 1e-4)
		{
			const double VCap = FMath::Sqrt(1.5 * 9.81 / Curv);
			// allow braking distance
			const double VAllowed = FMath::Sqrt(VCap * VCap + 2.0 * 9.0 * FMath::Max(0.f, d - 15.f));
			VTarget = FMath::Min(VTarget, VAllowed);
		}
	}
	if (bGapAhead) VTarget = FMath::Clamp(VTarget, 30.0, 32.0);
	// on walls and ceilings the tires carry only (suction - weight): keep speed and throttle modest so
	// the friction circle still has lateral grip to hold the car against the slope
	const double UpZ = P->Sim.T.UpDotWorld;
	const bool bInverted = UpZ < -0.7;
	if (bInverted) VTarget = FMath::Min(VTarget, 38.0);
	const double Err = VTarget - V;
	In.Throttle = FMath::Clamp(Err * (bInverted ? 0.12 : 0.35), 0.0, bInverted ? 0.5 : 1.0);
	In.Brake = FMath::Clamp(-Err * 0.12, 0.0, bInverted ? 0.12 : 1.0);
	In.Handbrake = 0; In.bWantReverse = false;
	(void)i0;
	// stuck (crashed / beached): reset to the track after 3 s
	static float StuckTime = 0.f;
	StuckTime = V < 1.0 ? StuckTime + Dt : 0.f;
	if (StuckTime > 3.f) { StuckTime = 0.f; P->ResetToTrack(false); }
	static float DbgT = 0.f; DbgT += Dt;
	if (DbgT > 0.1f && RoadsterLogLevel() >= 2)
	{
		DbgT = 0.f;
		const FTrackSample& Cur = S[AutoIndex];
		const FVector Off = Local - Cur.P;
		const float Lat = FVector::DotProduct(Off, Cur.Q.GetRightVector()) / 100.f;
		const FVector CarF = TT.InverseTransformVector(P->GetActorForwardVector());
		const float HeadErr = FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(CarF, Cur.Q.GetRightVector()), FVector::DotProduct(CarF, Cur.Q.GetForwardVector())));
		const auto& W = P->Sim.Wheels;
		UE_LOG(LogTemp, Display, TEXT("TELD s=%.0f lat=%.2f head=%.1f steer=%.2f tgtY=%.1f v=%.1f vt=%.1f aF=%.3f aR=%.3f kR=%.2f yaw=%.3f"),
			Cur.S, Lat, HeadErr, In.Steer, L.Y, V, VTarget, 0.5 * (W[0].Alpha + W[1].Alpha), 0.5 * (W[2].Alpha + W[3].Alpha), 0.5 * (W[2].Kappa + W[3].Kappa), P->Sim.Q.Unrotate(P->Sim.W).z);
	}
}

void ARoadsterController::LogTelemetry(float Dt)
{
	if (RoadsterLogLevel() < 1) return;
	TelemetryTimer += Dt;
	if (TelemetryTimer < 1.f) return;
	TelemetryTimer = 0.f;
	ARoadsterPawn* P = Car();
	if (!P) return;
	const Roadster::Telemetry& T = P->Sim.T;
	int32 Piece = -1;
	if (ATrackActor* Tr = Track())
		if (Tr->GetSamples().IsValidIndex(AutoIndex)) Piece = Tr->GetSamples()[AutoIndex].Piece;
	UE_LOG(LogTemp, Display, TEXT("TEL mode=%d auto=%d piece=%d v=%.1f up=%.2f wheels=%d fan=%.1fkN air=%d body=%d pos=(%.0f,%.0f,%.0f) fps=%.0f"),
		(int32)Mode, bAutopilot ? 1 : 0, Piece, T.Speed, T.UpDotWorld, T.WheelsInContact, T.FanDownforceN / 1000.0, T.bAirborne ? 1 : 0, T.bBodyContact ? 1 : 0,
		P->GetActorLocation().X / 100.f, P->GetActorLocation().Y / 100.f, P->GetActorLocation().Z / 100.f, 1.f / FMath::Max(Dt, 1e-4f));
}