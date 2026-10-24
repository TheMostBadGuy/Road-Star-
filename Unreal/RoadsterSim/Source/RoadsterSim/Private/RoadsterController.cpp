#include "RoadsterController.h"
#include "RoadsterPawn.h"
#include "RoadsterInterface.h"
#include "TrackActor.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/GameUserSettings.h"

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
	if (Mode == EUIMode::Driving || Mode == EUIMode::Touchscreen) DriveInput(DeltaTime);
	else P->Input = Inputs();
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
