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
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "RoadsterControlMath.h"
#include "AssetCompilingManager.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

bool RunRoadsterTrackVerification(ARoadsterController* Controller);
bool RunRoadsterRuntimePhysicsVerification(ARoadsterPawn* Pawn, FString& OutReport);

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
	LoadControlSettings();
	NativePad.Initialize();
	InputConnectionHandle = IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddWeakLambda(this,
		[this](EInputDeviceConnectionState State, FPlatformUserId User, FInputDeviceId Device)
		{
			if (State == EInputDeviceConnectionState::Disconnected && User == GetPlatformUserId()) bDeviceDisconnectedPending.Store(true);
		});
	bNativeWasConnected = NativePad.IsConnected();
	TArray<FString> SavedStunts;
	IFileManager::Get().FindFiles(SavedStunts, *(FPaths::ProjectSavedDir() / TEXT("Stunts/stunt_*.json")), true, false);
	SavedStunts.Sort();
	if (!SavedStunts.IsEmpty()) LastRecordingFile = FPaths::ProjectSavedDir() / TEXT("Stunts") / SavedStunts.Last();
	SetMode(EUIMode::Title);
}

void ARoadsterController::ConfigureCommandLine()
{
	// Possession can precede Pawn::BeginPlay. Apply scenarios and UI pages only once the
	// pawn's screens and default reset exist, so startup cannot silently erase the request.
	// command line: -autodrive (autopilot from the start), -track=N (preset), -cam=N (camera mode)
	int32 TrackIdx = -1, CamIdx = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("track="), TrackIdx))
		if (ATrackActor* T = Track()) { T->LoadPreset(TrackIdx); T->Rebuild(); if (Car()) Car()->ResetToTrack(true); }
	if (FParse::Param(FCommandLine::Get(), TEXT("xray")) && Car()) Car()->SetXRay(true);
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
	// Repeatable, real-renderer review modes. These do not change the ordinary launch flow.
	FParse::Value(FCommandLine::Get(), TEXT("exitafter="), ExitAfter);
	int32 ReviewQuality = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("reviewquality="), ReviewQuality))
	{
		if (UGameUserSettings* GS = UGameUserSettings::GetGameUserSettings())
		{
			GS->SetOverallScalabilityLevel(FMath::Clamp(ReviewQuality, 0, 3));
			GS->ApplyNonResolutionSettings();
		}
		if (IConsoleVariable* Percentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))) Percentage->Set(100.f, ECVF_SetByConsole);
	}
	FParse::Value(FCommandLine::Get(), TEXT("shotdir="), ShotDirectory, false);
	if (ShotDirectory.IsEmpty()) ShotDirectory = FPaths::ProjectSavedDir() / TEXT("Shots");
	IFileManager::Get().MakeDirectory(*ShotDirectory, true);
	if (FParse::Param(FCommandLine::Get(), TEXT("verifytracks")))
		RunRoadsterTrackVerification(this);
	FString Review;
	if (FParse::Value(FCommandLine::Get(), TEXT("review="), Review))
	{
		const bool bLabReview = Review == TEXT("lab") || Review == TEXT("labflight") || Review == TEXT("labcontrols");
		SetMode(Review == TEXT("builder") ? EUIMode::Builder : (Review == TEXT("screen") || bLabReview) ? EUIMode::Touchscreen : EUIMode::Driving);
		bShowHints = false;
		if (ARoadsterPawn* P = Car())
		{
			P->bSimPaused = Review == TEXT("builder");
			if (Review == TEXT("cockpit")) P->CameraMode = ERoadsterCamera::Cockpit;
			if (Review == TEXT("exterior"))
			{
				P->CameraMode = ERoadsterCamera::Orbit;
				P->Sim.Set.Aero = Roadster::AeroPreset::LowDrag;
			}
			if (Review == TEXT("screen") && P->Interface)
			{
				P->Interface->Tab = ECarTab::Vehicle;
				P->Interface->Page = EVehiclePage::Dynamics;
			}
			if (bLabReview && P->Interface)
			{
				URoadsterInterface::PrewarmSimulatorLab();
				P->bTrackMode = false;
				P->Interface->Tab = ECarTab::Vehicle;
				P->Interface->Page = EVehiclePage::SimulatorLab;
				P->Interface->SimulatorWorkspace = Review == TEXT("labflight") ? 1 : Review == TEXT("labcontrols") ? 2 : 0;
			}
			if (Review == TEXT("orbit") || Review == TEXT("upper")) P->StartRoadstarScenario(Review == TEXT("upper") ? 1 : 0);
			if (Review == TEXT("doors")) { P->CameraMode = ERoadsterCamera::Orbit; P->bDoorsOpen = true; }
		}
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("controlcheck"))) RunControlCheck();
	if (FParse::Param(FCommandLine::Get(), TEXT("simlabcheck")) && Car() && Car()->Interface)
	{
		FString Report;
		Car()->Interface->RunSimulatorLabVerification(Report);
		FFileHelper::SaveStringToFile(Report, *(FPaths::ProjectSavedDir() / TEXT("SimulatorLabCheck.txt")));
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("runtimephysicscheck")) && Car())
	{
		FString Report;
		RunRoadsterRuntimePhysicsVerification(Car(), Report);
		FFileHelper::SaveStringToFile(Report, *(FPaths::ProjectSavedDir() / TEXT("RuntimePhysicsCheck.txt")));
	}
}

void ARoadsterController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRecording) SaveRecording();
	CancelPilot();
	StopHapticsNow();
	IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().Remove(InputConnectionHandle);
	NativePad.Shutdown();
	Super::EndPlay(EndPlayReason);
}

bool ARoadsterController::Pressed(const FKey& K) const
{
	if (!bControlFocused) return false;
	return K.IsGamepadKey() && NativePad.IsConnected() ? NativePad.Pressed(K) : WasInputKeyJustPressed(K);
}
bool ARoadsterController::Down(const FKey& K) const
{
	if (!bControlFocused) return false;
	return K.IsGamepadKey() && NativePad.IsConnected() ? NativePad.Down(K) : IsInputKeyDown(K);
}
float ARoadsterController::Axis(const FKey& K) const
{
	if (!bControlFocused) return 0.f;
	return K.IsGamepadKey() && NativePad.IsConnected() ? NativePad.Axis(K) : GetInputAnalogKeyState(K);
}

ARoadsterPawn* ARoadsterController::Car() const { return Cast<ARoadsterPawn>(GetPawn()); }

ATrackActor* ARoadsterController::Track() const
{
	for (TActorIterator<ATrackActor> It(GetWorld()); It; ++It) return *It;
	return nullptr;
}

void ARoadsterController::SetMode(EUIMode NewMode)
{
	if (Mode != NewMode)
	{
		if (NewMode != EUIMode::Driving)
		{
			CancelPilot();
			if (bRecording) SaveRecording();
			StopHapticsNow();
		}
		else
		{
			bBlockPadActionsUntilRelease = Down(EKeys::Gamepad_FaceButton_Bottom) || Down(EKeys::Gamepad_FaceButton_Right);
			bBlockSpaceUntilRelease = Down(EKeys::SpaceBar);
		}
	}
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
	const bool bWasFocused = bControlFocused;
	bControlFocused = FApp::HasFocus();
	NativePad.Tick(DeltaTime, bControlFocused);
	const bool bNativeConnected = NativePad.IsConnected();
	if (bDeviceDisconnectedPending.Exchange(false) || (bNativeWasConnected && !bNativeConnected) || (bWasFocused && !bControlFocused))
	{
		CancelPilot(TEXT("Pilot sequence cancelled"));
		if (bRecording) SaveRecording();
		StopHapticsNow();
	}
	bNativeWasConnected = bNativeConnected;
	if (!Down(EKeys::Gamepad_FaceButton_Bottom) && !Down(EKeys::Gamepad_FaceButton_Right)) bBlockPadActionsUntilRelease = false;
	if (!Down(EKeys::SpaceBar)) bBlockSpaceUntilRelease = false;
	ARoadsterPawn* P = Car();
	if (!bCommandLineConfigured && P && P->Interface && P->HasActorBegunPlay() && Track() && Track()->HasActorBegunPlay())
	{
		bCommandLineConfigured = true;
		ConfigureCommandLine();
		UE_LOG(LogTemp, Display, TEXT("ROADSTER_REVIEW configured after pawn and track initialization"));
	}
	// Review runs (shots / timed exit) hold their clock while shaders, meshes and textures are still compiling,
	// so captures from a cold cache never show placeholder materials (capped at 15 minutes).
	const bool bReviewRun = ExitAfter > 0.f || ShotIndex < ShotTimes.Num();
	if (bReviewRun && CompileWaitSeconds < 900.f && FAssetCompilingManager::Get().GetNumRemainingAssets() > 0)
	{
		if (CompileWaitSeconds == 0.f) UE_LOG(LogTemp, Display, TEXT("ROADSTER_REVIEW waiting for compilation"));
		CompileWaitSeconds += DeltaTime;
	}
	else
	{
		PlayTime += DeltaTime;
	}
	if (ExitAfter > 0.f && PlayTime >= 5.f) { MeasuredFrameTime += DeltaTime; ++MeasuredFrames; ReviewFrameTimes.Add(DeltaTime * 1000.f); }
	if (ExitAfter > 0.f && PlayTime >= ExitAfter)
	{
		int32 W = 0, H = 0; GetViewportSize(W, H);
		ReviewFrameTimes.Sort();
		const float P95 = ReviewFrameTimes.IsEmpty() ? 0.f : ReviewFrameTimes[FMath::Min(ReviewFrameTimes.Num() - 1, FMath::FloorToInt(ReviewFrameTimes.Num() * 0.95f))];
		const IConsoleVariable* Percentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
		const FString Report = FString::Printf(TEXT("Frames=%d\nElapsedSeconds=%.3f\nAverageFPS=%.2f\nP95FrameMilliseconds=%.2f\nResolution=%dx%d\nScreenPercentage=%.1f\nCompileWaitSeconds=%.1f\nNote=Short review run, excludes first 5 seconds; screenshots can cause hitches.\n"),
			MeasuredFrames, MeasuredFrameTime, MeasuredFrames / FMath::Max(MeasuredFrameTime, 0.001f), P95, W, H, Percentage ? Percentage->GetFloat() : -1.f, CompileWaitSeconds);
		FFileHelper::SaveStringToFile(Report, *(ShotDirectory / TEXT("performance.txt")));
		UE_LOG(LogTemp, Display, TEXT("ROADSTER_REVIEW %s"), *Report.Replace(TEXT("\n"), TEXT(" ")));
		ExitAfter = 0.f;
		ConsoleCommand(TEXT("quit"));
	}
	if (ShotIndex < ShotTimes.Num() && PlayTime >= ShotTimes[ShotIndex])
	{
		FScreenshotRequest::RequestScreenshot(ShotDirectory / FString::Printf(TEXT("shot_%02d.png"), ShotIndex), false, false);
		++ShotIndex;
	}


	UpdateCursor(DeltaTime);
	ToastTime -= DeltaTime;

	// global keys
	if (Pressed(EKeys::Escape) || Pressed(EKeys::Gamepad_Special_Right))
	{
		CancelPilot();
		if (Mode == EUIMode::Driving) SetMode(EUIMode::Menu);
		else if (Mode != EUIMode::Title) SetMode(EUIMode::Driving);
	}
	if (Mode == EUIMode::Title && (Pressed(EKeys::Enter) || Pressed(EKeys::SpaceBar) || (!bVirtualCursor && Pressed(EKeys::Gamepad_FaceButton_Bottom)))) SetMode(EUIMode::Driving);
	if (Pressed(EKeys::Tab) || Pressed(EKeys::Gamepad_Special_Left))
	{
		if (Mode == EUIMode::Driving) SetMode(EUIMode::Touchscreen);
		else if (Mode == EUIMode::Touchscreen) SetMode(EUIMode::Driving);
	}
	if (Pressed(EKeys::F2)) SetMode(Mode == EUIMode::Builder ? EUIMode::Driving : EUIMode::Builder);
	if (Pressed(EKeys::F1)) bShowHints = !bShowHints;
	if (Mode != EUIMode::Driving && Mode != EUIMode::Title && Pressed(EKeys::Gamepad_FaceButton_Right)) SetMode(EUIMode::Driving);

	if (!P) return;
	if (Mode == EUIMode::Builder) { BuilderInput(DeltaTime); P->Input = Inputs(); return; }
	if (Mode == EUIMode::Driving || Mode == EUIMode::Touchscreen)
	{
		DriveInput(DeltaTime);
		if (Mode == EUIMode::Driving && Pressed(EKeys::O)) { CancelPilot(); bAutopilot = !bAutopilot; ShowToast(bAutopilot ? TEXT("Autopilot ON (demo)") : TEXT("Autopilot OFF")); }
		if (bAutopilot) AutopilotInput(DeltaTime);
		if (Mode == EUIMode::Driving) ApplyPilotSequence(DeltaTime);
	}
	else P->Input = Inputs();
	UpdateHaptics(DeltaTime);
	LogTelemetry(DeltaTime);
}

void ARoadsterController::DriveInput(float Dt)
{
	ARoadsterPawn* P = Car();
	Settings& Set = P->Sim.Set;
	const bool bKeys = Mode == EUIMode::Driving && bControlFocused;
	const bool bPad = bKeys && !bBlockPadActionsUntilRelease;
	const bool bFlight = P->Sim.T.bAirborne || Set.bHoverHold;
	const float Speed = (float)P->Sim.T.Speed;
	auto PadDown = [&](const FKey& K) { return bPad && Down(K); };
	auto PadPressed = [&](const FKey& K) { return bPad && Pressed(K); };
	auto PadAxis = [&](const FKey& K) { return bPad ? Axis(K) : 0.f; };
	const float LX = RoadsterControls::Curve(PadAxis(EKeys::Gamepad_LeftX), SteeringDeadzone, StickExponent);
	const float LY = RoadsterControls::Curve(PadAxis(EKeys::Gamepad_LeftY), SteeringDeadzone, StickExponent);
	const float RX = RoadsterControls::Curve(PadAxis(EKeys::Gamepad_RightX), CameraDeadzone, 1.2f);
	const float RY = RoadsterControls::Curve(PadAxis(EKeys::Gamepad_RightY), CameraDeadzone, 1.2f);
	const float RT = RoadsterControls::Trigger(PadAxis(EKeys::Gamepad_RightTriggerAxis));
	const float LT = RoadsterControls::Trigger(PadAxis(EKeys::Gamepad_LeftTriggerAxis));
	const bool W = bKeys && (Down(EKeys::W) || Down(EKeys::Up));
	const bool S = bKeys && (Down(EKeys::S) || Down(EKeys::Down));
	const float SteerKey = bKeys ? float(Down(EKeys::D) || Down(EKeys::Right)) - float(Down(EKeys::A) || Down(EKeys::Left)) : 0.f;
	const float PitchKey = float(S) - float(W);
	const float YawKey = bKeys ? float(Down(EKeys::E)) - float(Down(EKeys::Q)) : 0.f;
	const bool BoostHeld = (bKeys && (Down(EKeys::LeftShift) || Down(EKeys::RightShift))) || PadDown(EKeys::Gamepad_FaceButton_Bottom);
	const bool LiftHeld = (bKeys && ((!bBlockSpaceUntilRelease && Down(EKeys::SpaceBar)) || Down(EKeys::LeftControl) || Down(EKeys::RightControl))) || PadDown(EKeys::Gamepad_FaceButton_Left);
	const bool BrakeAttitude = (bKeys && (Down(EKeys::LeftAlt) || Down(EKeys::RightAlt))) || PadDown(EKeys::Gamepad_FaceButton_Right);

	bManualPilotInput = W || S || SteerKey != 0.f || YawKey != 0.f || FMath::Abs(LX) > .08f || FMath::Abs(LY) > .08f || RT > .08f || LT > .08f || BoostHeld || LiftHeld || BrakeAttitude ||
		PadDown(EKeys::Gamepad_LeftShoulder) || PadDown(EKeys::Gamepad_RightShoulder) || PadDown(EKeys::Gamepad_DPad_Up) || PadDown(EKeys::Gamepad_DPad_Down) ||
		(bKeys && (Down(EKeys::PageUp) || Down(EKeys::PageDown)));
	Inputs In;
	if (!bFlight)
	{
		Throttle = FMath::FInterpConstantTo(Throttle, W ? 1.f : 0.f, Dt, W ? 5.f : 10.f);
		Brake = FMath::FInterpConstantTo(Brake, S ? 1.f : 0.f, Dt, S ? 7.f : 12.f);
		const bool Returning = SteerKey == 0.f || FMath::Sign(SteerKey) != FMath::Sign(Steer);
		Steer = RoadsterControls::Smooth(Steer, SteerKey, RoadsterControls::SteeringRate(Speed, true, Returning), Dt);
		PadSteer = RoadsterControls::Smooth(PadSteer, LX, RoadsterControls::SteeringRate(Speed, false, FMath::Abs(LX) < .01f), Dt);
		In.Throttle = FMath::Max(Throttle, RT);
		In.Brake = FMath::Max(Brake, LT);
		const bool bPadSteering = FMath::Abs(LX) > .001f || (SteerKey == 0.f && FMath::Abs(PadSteer) > .001f);
		In.Steer = (bPadSteering ? PadSteer : Steer) * RoadsterControls::SteeringAuthority(Speed);
		In.Handbrake = BrakeAttitude ? 1.f : 0.f;
		const float V = (float)P->Sim.T.ForwardSpeed;
		if (!bReverseLatch)
		{
			if (FMath::Abs(V) < .4f && In.Brake > .5f && In.Throttle < .05f) BrakeHoldAtRest += Dt;
			else BrakeHoldAtRest = 0.f;
			if (BrakeHoldAtRest > .45f) { bReverseLatch = true; BrakeHoldAtRest = 0.f; }
		}
		else if (In.Throttle > .1f && V > -1.f) bReverseLatch = false;
		In.bWantReverse = bReverseLatch;
	}
	else
	{
		// Flight axes never borrow the pedals or camera stick.
		Throttle = Brake = Steer = PadSteer = 0.f;
		In.Pitch = FMath::Clamp(PitchKey - LY, -1.f, 1.f); // pull back = nose up
		In.Roll = FMath::Clamp(SteerKey + LX, -1.f, 1.f);
		In.Yaw = FMath::Clamp(YawKey + float(PadDown(EKeys::Gamepad_RightShoulder)) - float(PadDown(EKeys::Gamepad_LeftShoulder)), -1.f, 1.f);
		In.bAttitudeBrake = BrakeAttitude;
		if (Set.bHoverHold)
		{
			// Triggers adjust the geometric-elevation target; the physics PID
			// owns valve authority and consumes finite nitrogen.
			Set.HoverAltitudeM += double(RT - LT) * Dt * 5.;
		}
		else In.Lift = FMath::Max(0.f, RT - LT);
	}
	if (!bFlight) In.Yaw = YawKey;
	In.Lift = LiftHeld ? 1.f : In.Lift;
	In.Boost = BoostHeld ? 1.f : 0.f;
	In.bDRS = bKeys && Down(EKeys::X);
	P->Input = In;

	// A right stick always looks around, including during flips and hover.
	FRotator Look = P->LookOffset;
	if (bKeys && Down(EKeys::RightMouseButton))
	{
		float DX = 0, DY = 0; GetInputMouseDelta(DX, DY);
		if (P->CameraMode == ERoadsterCamera::Orbit) Look = FRotator(DY * 2.f * CameraSensitivity, DX * 2.f * CameraSensitivity, 0);
		else { Look.Yaw += DX * 1.5f * CameraSensitivity; Look.Pitch = FMath::Clamp(Look.Pitch + DY * 1.5f * CameraSensitivity, -70.f, 70.f); }
	}
	else if (FMath::Abs(RX) > .001f || FMath::Abs(RY) > .001f)
	{
		if (P->CameraMode == ERoadsterCamera::Orbit) Look = FRotator(RY * 90.f * Dt * CameraSensitivity, RX * 150.f * Dt * CameraSensitivity, 0);
		else { Look.Yaw = RX * 140.f * CameraSensitivity; Look.Pitch = RY * 50.f * CameraSensitivity; }
	}
	else if (P->CameraMode == ERoadsterCamera::Orbit) Look = FRotator::ZeroRotator;
	else Look = FMath::RInterpTo(Look, FRotator::ZeroRotator, Dt, 4.f);
	P->LookOffset = Look;
	if (!bKeys) return;

	if (Pressed(EKeys::C) || PadPressed(EKeys::Gamepad_FaceButton_Top)) P->CycleCamera();
	if (Pressed(EKeys::V)) { P->CameraMode = P->CameraMode == ERoadsterCamera::Cockpit ? ERoadsterCamera::Chase : ERoadsterCamera::Cockpit; P->LookOffset = FRotator::ZeroRotator; }
	if (Pressed(EKeys::R) || PadPressed(EKeys::Gamepad_LeftThumbstick))
	{
		CancelPilot(); if (bRecording) SaveRecording();
		Set.bHoverHold = false; P->ResetToTrack(false); ShowToast(TEXT("Reset to track"));
	}
	if (Pressed(EKeys::BackSpace))
	{
		CancelPilot(); if (bRecording) SaveRecording();
		Set.bHoverHold = false; P->ResetToTrack(true); ShowToast(TEXT("Back to the start line"));
	}
	if (Pressed(EKeys::H) || PadPressed(EKeys::Gamepad_RightThumbstick)) { CancelPilot(); ToggleHover(); }
	if (Set.bHoverHold)
	{
		const float HeightAxis = float(Down(EKeys::PageUp) || PadDown(EKeys::Gamepad_DPad_Up)) - float(Down(EKeys::PageDown) || PadDown(EKeys::Gamepad_DPad_Down));
		Set.HoverAltitudeM = FMath::Clamp(Set.HoverAltitudeM + HeightAxis * Dt * 4., -1000., 10000000.);
	}
	if (Pressed(EKeys::F10))
	{
		CancelPilot(); Set.bHoverHold = false; Set.bRoadstarMode = !Set.bRoadstarMode;
		ShowToast(Set.bRoadstarMode ? TEXT("Roadstar free flight ON") : TEXT("Roadstar free flight OFF"));
	}
	if (Pressed(EKeys::F6)) { CancelPilot(); SelectedStuntPreset = (SelectedStuntPreset + 1) % 4; ShowToast(TEXT("Stunt: ") + StuntPresetName() + TEXT("  (J to play)")); }
	if (Pressed(EKeys::J)) StartPreset();
	if (Pressed(EKeys::F7)) ToggleRecording();
	if (Pressed(EKeys::F8)) ReplayLastRecording();
	if (Pressed(EKeys::F5)) { bHapticsEnabled = !bHapticsEnabled; SaveControlSettings(); if (!bHapticsEnabled) StopHapticsNow(); ShowToast(bHapticsEnabled ? TEXT("Controller vibration ON") : TEXT("Controller vibration OFF")); }
	if (Pressed(EKeys::F) || (!bFlight && PadPressed(EKeys::Gamepad_RightShoulder)))
	{
		Set.Aero = AeroPreset::Custom; Set.Fan = (FanMode)(((int32)Set.Fan + 1) % 5);
		ShowToast(FString::Printf(TEXT("Fan: %s"), *FString(FanModeName(Set.Fan))));
	}
	if (Pressed(EKeys::RightBracket) || (!Set.bHoverHold && PadPressed(EKeys::Gamepad_DPad_Up)))
	{
		Set.Aero = AeroPreset::Custom; Set.Fan = FanMode::Custom; Set.FanCustomKPa = FMath::Min(P->Sim.S.FanMaxPressureKPa, Set.FanCustomKPa + .5);
		ShowToast(FString::Printf(TEXT("Underbody pressure -%.1f kPa"), Set.FanCustomKPa));
	}
	if (Pressed(EKeys::LeftBracket) || (!Set.bHoverHold && PadPressed(EKeys::Gamepad_DPad_Down)))
	{
		Set.Aero = AeroPreset::Custom; Set.Fan = FanMode::Custom; Set.FanCustomKPa = FMath::Max(0., Set.FanCustomKPa - .5);
		ShowToast(FString::Printf(TEXT("Underbody pressure -%.1f kPa"), Set.FanCustomKPa));
	}
	if (Pressed(EKeys::Z)) { Set.Aero = (AeroPreset)(((int32)Set.Aero + 1) % 4); ShowToast(FString::Printf(TEXT("Aero: %s"), *FString(AeroPresetName(Set.Aero)))); }
	if (Pressed(EKeys::T) || (!bFlight && PadPressed(EKeys::Gamepad_LeftShoulder)))
	{
		P->bTrackMode = !P->bTrackMode; Set.Dynamics = P->bTrackMode ? DynamicsPreset::Track : DynamicsPreset::Sport;
		Set.bESC = Set.bTCS = !P->bTrackMode; P->LapTime = 0;
		ShowToast(P->bTrackMode ? TEXT("Track Mode - ESC OFF - TCS OFF") : TEXT("Track Mode off"));
	}
	if (Pressed(EKeys::L)) P->ExteriorLights = (P->ExteriorLights + 1) % 4;
	if (Pressed(EKeys::U) || PadPressed(EKeys::Gamepad_DPad_Left))
	{
		if (P->Sim.T.Speed < 0.5 && !P->Sim.T.bAirborne)
		{
			P->bDoorsOpen = !P->bDoorsOpen;
			ShowToast(P->bDoorsOpen ? TEXT("Doors opening") : TEXT("Doors closing"));
		}
		else ShowToast(TEXT("Stop before opening the doors"));
	}
	if (Pressed(EKeys::I)) { P->SetXRay(!P->bXRay); ShowToast(P->bXRay ? TEXT("Engineering view") : TEXT("Engineering view off")); }
	if (Pressed(EKeys::M)) { CancelPilot(); Set.bHoverHold = false; P->SetTrim((Trim)(((int32)P->Sim.S.TrimLevel + 1) % 3)); ShowToast(FString::Printf(TEXT("Augment: %s"), *FString(TrimName(P->Sim.S.TrimLevel)))); }
	if (Pressed(EKeys::N)) { Set.bInfiniteGas = !Set.bInfiniteGas; ShowToast(Set.bInfiniteGas ? TEXT("Unlimited nitrogen (demo)") : TEXT("Real tank")); }
	if (Pressed(EKeys::B)) { P->Sim.RefillTank(); P->Sim.Recharge(); ShowToast(TEXT("Pit: nitrogen refilled, battery charged")); }
	if (Pressed(EKeys::G)) { bSlowMo = !bSlowMo; P->TimeScale = bSlowMo ? .25f : 1.f; ShowToast(bSlowMo ? TEXT("Slow motion 1/4") : TEXT("Real time")); }
	if (Pressed(EKeys::P)) { CancelPilot(); P->bSimPaused = !P->bSimPaused; if (P->bSimPaused) StopHapticsNow(); }
	const FKey Num[6] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six };
	for (int32 I = 0; I < 6; ++I)
		if (Pressed(Num[I])) if (ATrackActor* T = Track())
		{
			CancelPilot(); if (bRecording) SaveRecording(); Set.bHoverHold = false;
			T->LoadPreset(I); T->Rebuild(); P->ResetToTrack(true); ShowToast(T->TrackName);
		}
}

FString ARoadsterController::GetControlDeviceLabel() const
{
	return NativePad.IsConnected() ? NativePad.DeviceName() : TEXT("Keyboard / mouse or standard gamepad");
}

FString ARoadsterController::StuntPresetName() const { return UTF8_TO_TCHAR(RoadsterControls::PresetName(SelectedStuntPreset)); }
FString ARoadsterController::GetStuntStatus() const
{
	if (bRecording) return FString::Printf(TEXT("RECORDING %.1fs  |  F7 save"), SequenceTime);
	if (bReplaying) return FString::Printf(TEXT("INPUT REPLAY %.1fs  |  move controls to cancel"), SequenceTime);
	if (bPresetPlaying) return FString::Printf(TEXT("%s %.1fs  |  move controls to cancel"), *StuntPresetName(), SequenceTime);
	return TEXT("Stunt: ") + StuntPresetName() + TEXT("  |  F6 select, J play");
}

void ARoadsterController::LoadControlSettings()
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Config/Windows/RoadsterControls.ini");
	FConfigFile File;
	if (FPaths::FileExists(Path))
	{
		File.Read(Path);
		File.GetFloat(TEXT("Controls"), TEXT("SteeringDeadzone"), SteeringDeadzone);
		File.GetFloat(TEXT("Controls"), TEXT("StickExponent"), StickExponent);
		File.GetFloat(TEXT("Controls"), TEXT("CameraSensitivity"), CameraSensitivity);
		File.GetFloat(TEXT("Controls"), TEXT("CameraDeadzone"), CameraDeadzone);
		File.GetBool(TEXT("Controls"), TEXT("Haptics"), bHapticsEnabled);
	}
	SteeringDeadzone = RoadsterControls::Bounded(SteeringDeadzone, .10f, 0.f, .4f);
	StickExponent = RoadsterControls::Bounded(StickExponent, 1.5f, .5f, 3.f);
	CameraSensitivity = RoadsterControls::Bounded(CameraSensitivity, 1.f, .2f, 3.f);
	CameraDeadzone = RoadsterControls::Bounded(CameraDeadzone, .12f, 0.f, .4f);
	if (!FPaths::FileExists(Path)) SaveControlSettings();
}

void ARoadsterController::SaveControlSettings()
{
	SteeringDeadzone = RoadsterControls::Bounded(SteeringDeadzone, .10f, 0.f, .4f);
	StickExponent = RoadsterControls::Bounded(StickExponent, 1.5f, .5f, 3.f);
	CameraSensitivity = RoadsterControls::Bounded(CameraSensitivity, 1.f, .2f, 3.f);
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Config/Windows/RoadsterControls.ini");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	FConfigFile File;
	File.SetFloat(TEXT("Controls"), TEXT("SteeringDeadzone"), SteeringDeadzone);
	File.SetFloat(TEXT("Controls"), TEXT("StickExponent"), StickExponent);
	File.SetFloat(TEXT("Controls"), TEXT("CameraSensitivity"), CameraSensitivity);
	File.SetFloat(TEXT("Controls"), TEXT("CameraDeadzone"), CameraDeadzone);
	File.SetBool(TEXT("Controls"), TEXT("Haptics"), bHapticsEnabled);
	File.Dirty = true;
	if (!File.Write(Path)) UE_LOG(LogTemp, Warning, TEXT("Could not save controller tuning to %s"), *Path);
}

void ARoadsterController::UpdateCursor(float Dt)
{
	const bool bUI = Mode != EUIMode::Driving;
	float MX = 0, MY = 0;
	if (GetMousePosition(MX, MY))
	{
		const FVector2D Physical(MX, MY);
		if (!Physical.Equals(PreviousPhysicalMouse, .5f)) { MousePos = Physical; bVirtualCursor = false; }
		PreviousPhysicalMouse = Physical;
	}
	if (bUI && bControlFocused)
	{
		int32 Width = 0, Height = 0; GetViewportSize(Width, Height);
		const float X = RoadsterControls::Curve(Axis(EKeys::Gamepad_LeftX), SteeringDeadzone, 1.2f) + float(Down(EKeys::Gamepad_DPad_Right)) - float(Down(EKeys::Gamepad_DPad_Left));
		const float Y = -RoadsterControls::Curve(Axis(EKeys::Gamepad_LeftY), SteeringDeadzone, 1.2f) + float(Down(EKeys::Gamepad_DPad_Down)) - float(Down(EKeys::Gamepad_DPad_Up));
		if (FMath::Abs(X) > .001f || FMath::Abs(Y) > .001f)
		{
			if (!bVirtualCursor && MousePos.IsNearlyZero()) MousePos = FVector2D(Width * .5f, Height * .5f);
			bVirtualCursor = true;
			const float CursorSpeed = FMath::Max(Height, 720) * .7f;
			MousePos += FVector2D(X, Y) * CursorSpeed * FMath::Min(Dt, .05f);
			MousePos.X = FMath::Clamp(MousePos.X, 0., FMath::Max(1., double(Width - 1)));
			MousePos.Y = FMath::Clamp(MousePos.Y, 0., FMath::Max(1., double(Height - 1)));
			SetMouseLocation(FMath::RoundToInt(MousePos.X), FMath::RoundToInt(MousePos.Y));
			PreviousPhysicalMouse = MousePos;
		}
	}
	bMouseDown = Down(EKeys::LeftMouseButton) || (bUI && Down(EKeys::Gamepad_FaceButton_Bottom));
	bMouseClicked = Pressed(EKeys::LeftMouseButton) || (bUI && Pressed(EKeys::Gamepad_FaceButton_Bottom));
	MouseWheel = Pressed(EKeys::MouseScrollUp) ? 1.f : (Pressed(EKeys::MouseScrollDown) ? -1.f : 0.f);
	if (bUI) MouseWheel += RoadsterControls::Curve(Axis(EKeys::Gamepad_RightY), CameraDeadzone, 1.f) * Dt * 8.f;
}

void ARoadsterController::ToggleHover()
{
	ARoadsterPawn* P = Car();
	if (!P) return;
	if (!P->Sim.S.bHasLiftThrusters) { ShowToast(TEXT("Hover requires the Rocket augment")); return; }
	P->Sim.Set.bHoverHold = !P->Sim.Set.bHoverHold;
	if (P->Sim.Set.bHoverHold)
	{
		P->Sim.Set.HoverAltitudeM = P->Sim.T.AltitudeM;
		ShowToast(FString::Printf(TEXT("Hover hold %.1f m  |  triggers / D-pad adjust height"), P->Sim.Set.HoverAltitudeM));
	}
	else ShowToast(TEXT("Hover hold OFF"));
}

void ARoadsterController::StopHapticsNow()
{
	NativePad.Rumble(0.f, 0.f, 0);
	if (LowFeedbackHandle) PlayDynamicForceFeedback(0.f, 0.f, true, false, true, false, EDynamicForceFeedbackAction::Stop, LowFeedbackHandle);
	if (HighFeedbackHandle) PlayDynamicForceFeedback(0.f, 0.f, false, true, false, true, EDynamicForceFeedbackAction::Stop, HighFeedbackHandle);
	LowFeedbackHandle = HighFeedbackHandle = 0;
	HapticLow = HapticHigh = HapticTimer = 0.f;
}

void ARoadsterController::UpdateHaptics(float Dt)
{
	ARoadsterPawn* P = Car();
	if (!bHapticsEnabled || !bControlFocused || Mode != EUIMode::Driving || !P || P->bSimPaused)
	{
		if (HapticLow > 0.f || HapticHigh > 0.f || LowFeedbackHandle || HighFeedbackHandle) StopHapticsNow();
		return;
	}
	float Slip = 0.f;
	for (const auto& Wheel : P->Sim.Wheels) if (Wheel.bContact) Slip = FMath::Max(Slip, (float)FMath::Clamp(Wheel.SlideSpeed / 12., 0., 1.));
	const auto& T = P->Sim.T;
	const float Accel = FMath::Clamp((float)FMath::Abs(T.AccelG.x) * .12f, 0.f, .32f);
	const float Boost = T.ThrustRear > .01 ? .18f : 0.f;
	const float Collision = T.bBodyContact ? .16f : 0.f;
	HapticLow = RoadsterControls::Smooth(HapticLow, FMath::Clamp(Accel + Boost + Collision, 0.f, .5f), 12.f, Dt);
	HapticHigh = RoadsterControls::Smooth(HapticHigh, FMath::Clamp(Slip * .28f + Collision, 0.f, .4f), 16.f, Dt);
	HapticTimer -= Dt;
	if (HapticTimer > 0.f) return;
	HapticTimer = .06f;
	if (NativePad.IsConnected())
	{
		if (LowFeedbackHandle || HighFeedbackHandle)
		{
			if (LowFeedbackHandle) PlayDynamicForceFeedback(0.f, 0.f, true, false, true, false, EDynamicForceFeedbackAction::Stop, LowFeedbackHandle);
			if (HighFeedbackHandle) PlayDynamicForceFeedback(0.f, 0.f, false, true, false, true, EDynamicForceFeedbackAction::Stop, HighFeedbackHandle);
			LowFeedbackHandle = HighFeedbackHandle = 0;
		}
		NativePad.Rumble(HapticLow, HapticHigh, 150);
	}
	else
	{
		LowFeedbackHandle = PlayDynamicForceFeedback(HapticLow, .15f, true, false, true, false, LowFeedbackHandle ? EDynamicForceFeedbackAction::Update : EDynamicForceFeedbackAction::Start, LowFeedbackHandle);
		HighFeedbackHandle = PlayDynamicForceFeedback(HapticHigh, .15f, false, true, false, true, HighFeedbackHandle ? EDynamicForceFeedbackAction::Update : EDynamicForceFeedbackAction::Start, HighFeedbackHandle);
	}
}

void ARoadsterController::CancelPilot(const FString& Reason)
{
	const bool bWasActive = bReplaying || bPresetPlaying;
	if (bReplaying) if (ARoadsterPawn* P = Car()) P->Sim.Set.bHoverHold = false;
	if (bPresetPlaying) if (ARoadsterPawn* P = Car()) P->Sim.Set.bFlightAssist = bSavedFlightAssist;
	bReplaying = bPresetPlaying = false;
	ReplayIndex = 0;
	if (bWasActive && !Reason.IsEmpty()) ShowToast(Reason);
}

void ARoadsterController::StartPreset()
{
	ARoadsterPawn* P = Car();
	if (!P || !P->Sim.S.bHasLiftThrusters) { ShowToast(TEXT("Stunt jets require the Rocket augment")); return; }
	if (bRecording) SaveRecording();
	CancelPilot();
	bAutopilot = false;
	bSavedFlightAssist = P->Sim.Set.bFlightAssist;
	P->Sim.Set.bFlightAssist = false;
	P->Sim.Set.bHoverHold = false;
	bPresetPlaying = true;
	SequenceTime = 0.f;
	PresetPhysicsTime = 0.0;
	ShowToast(StuntPresetName() + TEXT(" valve sequence  |  move controls to cancel"));
}

void ARoadsterController::ToggleRecording()
{
	if (bRecording) { SaveRecording(); return; }
	CancelPilot();
	RecordedInputs.Reset(); SequenceTime = 0.f; bRecording = true;
	ShowToast(TEXT("Recording pilot inputs  |  F7 saves  |  180s limit"));
}

void ARoadsterController::SaveRecording()
{
	bRecording = false;
	if (RecordedInputs.Num() < 2) { ShowToast(TEXT("Recording too short to save")); return; }
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 1);
	Root->SetStringField(TEXT("type"), TEXT("pilot-input-sequence"));
	Root->SetStringField(TEXT("note"), TEXT("Replay starts at the current pose; inputs do not guarantee the same trajectory."));
	TArray<TSharedPtr<FJsonValue>> Frames;
	for (const FRecordedInput& Frame : RecordedInputs)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("t"), Frame.Time);
		const Inputs& I = Frame.Input;
		O->SetNumberField(TEXT("throttle"), I.Throttle); O->SetNumberField(TEXT("brake"), I.Brake); O->SetNumberField(TEXT("steer"), I.Steer);
		O->SetNumberField(TEXT("handbrake"), I.Handbrake); O->SetNumberField(TEXT("boost"), I.Boost); O->SetNumberField(TEXT("lift"), I.Lift);
		O->SetNumberField(TEXT("pitch"), I.Pitch); O->SetNumberField(TEXT("roll"), I.Roll); O->SetNumberField(TEXT("yaw"), I.Yaw);
		O->SetBoolField(TEXT("reverse"), I.bWantReverse); O->SetBoolField(TEXT("drs"), I.bDRS); O->SetBoolField(TEXT("attitudeBrake"), I.bAttitudeBrake);
		O->SetBoolField(TEXT("hover"), Frame.bHover); O->SetBoolField(TEXT("roadstar"), Frame.bRoadstar); O->SetNumberField(TEXT("hoverAltitude"), Frame.HoverAltitude);
		Frames.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("frames"), Frames);
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("Stunts");
	IFileManager::Get().MakeDirectory(*Folder, true);
	const FDateTime Now = FDateTime::UtcNow();
	const FString Filename = Folder / FString::Printf(TEXT("stunt_%s_%03d_%s.json"), *Now.ToString(TEXT("%Y%m%d_%H%M%S")), Now.GetMillisecond(), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
	FString JSON; const auto Writer = TJsonWriterFactory<>::Create(&JSON);
	if (FJsonSerializer::Serialize(Root, Writer) && FFileHelper::SaveStringToFile(JSON, *Filename))
	{
		LastRecordingFile = Filename;
		ShowToast(FString::Printf(TEXT("Saved %.1fs pilot inputs  |  F8 replay"), RecordedInputs.Last().Time));
		UE_LOG(LogTemp, Display, TEXT("ROADSTER_STUNT saved %s frames=%d"), *Filename, RecordedInputs.Num());
	}
	else ShowToast(TEXT("Could not save the pilot recording"));
}

void ARoadsterController::ReplayLastRecording()
{
	if (bRecording) SaveRecording();
	CancelPilot();
	FString JSON;
	if (LastRecordingFile.IsEmpty() || IFileManager::Get().FileSize(*LastRecordingFile) > 32 * 1024 * 1024 || !FFileHelper::LoadFileToString(JSON, *LastRecordingFile))
	{ ShowToast(TEXT("No saved pilot recording  |  F7 to record")); return; }
	TSharedPtr<FJsonObject> Root;
	const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JSON), Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("frames"), Frames) || Frames->Num() < 2 || Frames->Num() > 18000)
	{ ShowToast(TEXT("Pilot recording is invalid or exceeds the limit")); return; }
	double Version = 0;
	if (!Root->TryGetNumberField(TEXT("version"), Version) || Version != 1.) { ShowToast(TEXT("Unsupported pilot recording version")); return; }
	TArray<FRecordedInput> Loaded;
	float PreviousTime = -1.f;
	for (const auto& Value : *Frames)
	{
		if (!Value.IsValid() || Value->Type != EJson::Object) { ShowToast(TEXT("Invalid pilot frame")); return; }
		const TSharedPtr<FJsonObject> O = Value->AsObject();
		FRecordedInput Frame;
		double Time = 0;
		if (!O->TryGetNumberField(TEXT("t"), Time) || !FMath::IsFinite(Time) || Time < 0 || Time > 180.1 || Time < PreviousTime) { ShowToast(TEXT("Invalid pilot timeline")); return; }
		Frame.Time = (float)Time; PreviousTime = Frame.Time;
		auto Number = [&](const TCHAR* Name, double Low, double High) { double V = 0; O->TryGetNumberField(Name, V); return FMath::IsFinite(V) ? FMath::Clamp(V, Low, High) : 0.; };
		Inputs& I = Frame.Input;
		I.Throttle = Number(TEXT("throttle"), 0, 1); I.Brake = Number(TEXT("brake"), 0, 1); I.Steer = Number(TEXT("steer"), -1, 1);
		I.Handbrake = Number(TEXT("handbrake"), 0, 1); I.Boost = Number(TEXT("boost"), 0, 1); I.Lift = Number(TEXT("lift"), 0, 1);
		I.Pitch = Number(TEXT("pitch"), -1, 1); I.Roll = Number(TEXT("roll"), -1, 1); I.Yaw = Number(TEXT("yaw"), -1, 1);
		O->TryGetBoolField(TEXT("reverse"), I.bWantReverse); O->TryGetBoolField(TEXT("drs"), I.bDRS); O->TryGetBoolField(TEXT("attitudeBrake"), I.bAttitudeBrake);
		O->TryGetBoolField(TEXT("hover"), Frame.bHover); O->TryGetBoolField(TEXT("roadstar"), Frame.bRoadstar); Frame.HoverAltitude = Number(TEXT("hoverAltitude"), -1000, 10000000);
		Loaded.Add(Frame);
	}
	if (Loaded[0].Time > .1f || Loaded.Last().Time <= 0.f) { ShowToast(TEXT("Pilot timeline must begin at zero")); return; }
	RecordedInputs = MoveTemp(Loaded); SequenceTime = 0.f; ReplayIndex = 0; bReplaying = true; bAutopilot = false;
	ShowToast(TEXT("Input replay from current pose  |  move controls to cancel"));
}

void ARoadsterController::ApplyPilotSequence(float Dt)
{
	ARoadsterPawn* P = Car();
	if (!P || P->bSimPaused || !bControlFocused) return;
	if ((bReplaying || bPresetPlaying) && bManualPilotInput) CancelPilot(TEXT("Manual control - sequence cancelled"));
	const float Step = FMath::Min(Dt, .05f) * P->TimeScale;
	if (bPresetPlaying) return; // advanced inside each Pawn physics substep
	if (bReplaying)
	{
		while (ReplayIndex + 1 < RecordedInputs.Num() && RecordedInputs[ReplayIndex + 1].Time <= SequenceTime) ++ReplayIndex;
		const FRecordedInput& Frame = RecordedInputs[ReplayIndex];
		P->Input = Frame.Input; P->Sim.Set.bHoverHold = Frame.bHover; P->Sim.Set.bRoadstarMode = Frame.bRoadstar; P->Sim.Set.HoverAltitudeM = Frame.HoverAltitude;
		SequenceTime += Step;
		if (SequenceTime > RecordedInputs.Last().Time + .05f) { CancelPilot(TEXT("Input replay complete")); P->Input = Inputs(); P->Sim.Set.bHoverHold = false; }
	}
	else if (bRecording)
	{
		FRecordedInput Frame; Frame.Time = SequenceTime; Frame.Input = P->Input; Frame.bHover = P->Sim.Set.bHoverHold;
		Frame.HoverAltitude = P->Sim.Set.HoverAltitudeM; Frame.bRoadstar = P->Sim.Set.bRoadstarMode; RecordedInputs.Add(Frame);
		SequenceTime += Step;
		if (SequenceTime >= 180.f || RecordedInputs.Num() >= 18000) SaveRecording();
	}
}

void ARoadsterController::ApplyPhysicsPreset(double StepSeconds)
{
	ARoadsterPawn* P = Car();
	if (!bPresetPlaying || !P || P->bSimPaused || !bControlFocused) return;
	RoadsterControls::FValveRequest Request;
	if (RoadsterControls::SamplePreset(SelectedStuntPreset, static_cast<float>(PresetPhysicsTime), Request))
	{
		P->Input = Inputs();
		P->Input.Lift = Request.Lift; P->Input.Pitch = Request.Pitch; P->Input.Roll = Request.Roll;
		P->Input.Yaw = Request.Yaw; P->Input.Boost = Request.Boost; P->Input.bAttitudeBrake = Request.AttitudeBrake;
		PresetPhysicsTime += StepSeconds;
		SequenceTime = static_cast<float>(PresetPhysicsTime);
	}
	else { CancelPilot(TEXT("Valve sequence complete")); P->Input = Inputs(); }
}

void ARoadsterController::RunControlCheck()
{
	int32 Checks = 0, Failed = 0;
	TArray<FString> FailureDetails;
	auto Check = [&](bool Pass, const TCHAR* Label = TEXT("unlabeled assertion"))
	{
		++Checks;
		if (!Pass) { ++Failed; FailureDetails.Add(FString::Printf(TEXT("%d: %s"), Checks, Label)); }
	};
	Check(RoadsterControls::Curve(.05f, .1f, 1.5f) == 0.f);
	Check(RoadsterControls::Curve(1.f, .1f, 1.5f) == 1.f);
	Check(RoadsterControls::Curve(-1.f, .1f, 1.5f) == -1.f);
	Check(RoadsterControls::SteeringAuthority(100.f) < RoadsterControls::SteeringAuthority(10.f));
	Check(RoadsterControls::Trigger(0.f) == 0.f && RoadsterControls::Trigger(1.f) == 1.f);
	for (int32 Preset = 0; Preset < 4; ++Preset)
	{
		RoadsterControls::FValveRequest Request;
		Check(RoadsterControls::SamplePreset(Preset, 0.f, Request) && Request.Lift == 1.f);
		Check(!RoadsterControls::SamplePreset(Preset, 10.f, Request) && Request.Lift == 0.f);
	}
	// Exercise the real persistence/playback path without moving or teleporting
	// the vehicle. A unique, locally generated test file is deleted afterwards.
	if (ARoadsterPawn* P = Car())
	{
		const Inputs SavedInput = P->Input;
		const Settings SavedSettings = P->Sim.Set;
		const bool SavedPaused = P->bSimPaused;
		const bool SavedFocus = bControlFocused;
		P->bSimPaused = false;
		// This deterministic check simulates both focus states; the production focus guard stays active.
		bControlFocused = true;
		TArray<FRecordedInput> SavedFrames = RecordedInputs;
		const FString SavedLast = LastRecordingFile, SavedToast = Toast;
		const float SavedToastTime = ToastTime;
		const bool SavedAutopilot = bAutopilot;
		RecordedInputs.Reset();
		FRecordedInput First; First.Time = 0.f; First.Input.Lift = 1.25; First.Input.Pitch = -.5; First.Input.bAttitudeBrake = true;
		FRecordedInput Last; Last.Time = .1f;
		RecordedInputs.Add(First); RecordedInputs.Add(Last);
		SaveRecording();
		const FString TestFile = LastRecordingFile;
		const bool bTestSaved = TestFile != SavedLast && FPaths::FileExists(TestFile);
		Check(bTestSaved);
		if (bTestSaved)
		{
			ReplayLastRecording(); Check(bReplaying);
			bManualPilotInput = false;
			ApplyPilotSequence(.016f);
			Check(P->Input.Lift == 1. && P->Input.Pitch == -.5 && P->Input.bAttitudeBrake, TEXT("focused replay bounds and applies saved inputs"));
			const float FocusedTime = SequenceTime;
			bControlFocused = false;
			ApplyPilotSequence(.016f);
			Check(SequenceTime == FocusedTime && bReplaying, TEXT("unfocused replay does not advance"));
			bControlFocused = true;
			bManualPilotInput = true;
			ApplyPilotSequence(.016f); Check(!bReplaying, TEXT("manual control cancels focused replay"));
			FFileHelper::SaveStringToFile(TEXT("{\"version\":1,\"frames\":[{\"t\":0.1},{\"t\":0.05}]}"), *TestFile);
			ReplayLastRecording(); Check(!bReplaying);
			Check(IFileManager::Get().Delete(*TestFile));
		}
		P->Input = SavedInput; P->Sim.Set = SavedSettings;
		P->bSimPaused = SavedPaused;
		bControlFocused = SavedFocus;
		RecordedInputs = MoveTemp(SavedFrames); LastRecordingFile = SavedLast;
		Toast = SavedToast; ToastTime = SavedToastTime; bAutopilot = SavedAutopilot;
		bRecording = bReplaying = bPresetPlaying = bManualPilotInput = false; SequenceTime = 0.f; ReplayIndex = 0;
	}
	else Check(false);
	const FString Report = FString::Printf(TEXT("Status=%s\nChecks=%d\nFailed=%d\nDevice=%s\nNativeSDLAvailable=%d\nNativeSonyConnected=%d\nNote=Control math, finite valve requests, JSON save/load, bounded replay, manual cancellation and invalid timeline rejection checked in Unreal; no physical controller or flight trajectory verified.\n"), Failed ? TEXT("FAIL") : TEXT("PASS"), Checks, Failed, *GetControlDeviceLabel(), NativePad.IsAvailable() ? 1 : 0, NativePad.IsConnected() ? 1 : 0);
	const FString FullReport = Report + TEXT("FocusTest=Simulated focused/unfocused replay states; native window focus remains production guarded.\n") + FString::Join(FailureDetails, TEXT("\n"));
	FFileHelper::SaveStringToFile(FullReport, *(FPaths::ProjectSavedDir() / TEXT("ControlCheck.txt")));
	UE_LOG(LogTemp, Display, TEXT("ROADSTER_CONTROL_CHECK %s"), *Report.Replace(TEXT("\n"), TEXT(" ")));
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
