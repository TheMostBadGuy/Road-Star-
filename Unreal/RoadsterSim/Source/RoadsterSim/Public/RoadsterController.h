#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RoadsterGamepad.h"
#include "RoadsterPhysics.h"
#include "RoadsterController.generated.h"

class ARoadsterPawn;
class ATrackActor;

UENUM()
enum class EUIMode : uint8 { Title, Driving, Touchscreen, Menu, Builder, Help };

UCLASS()
class ROADSTERSIM_API ARoadsterController : public APlayerController
{
	GENERATED_BODY()

public:
	ARoadsterController();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PlayerTick(float DeltaTime) override;

	EUIMode Mode = EUIMode::Title;
	void SetMode(EUIMode NewMode);

	ARoadsterPawn* Car() const;
	ATrackActor* Track() const;

	// mouse state for the immediate-mode UI (canvas pixels)
	FVector2D MousePos = FVector2D::ZeroVector;
	bool bMouseDown = false, bMouseClicked = false;
	float MouseWheel = 0.f;

	// builder camera
	FVector BuilderPos = FVector(-6000, -6000, 4500);
	FRotator BuilderRot = FRotator(-30, 45, 0);
	bool bShowHints = true;
	bool bSlowMo = false;
	FString Toast; float ToastTime = 0.f;
	void ShowToast(const FString& S, float Secs = 2.5f) { Toast = S; ToastTime = Secs; }

	/** Autopilot: follows the track centreline (testing + attract/demo mode). */
	bool bAutopilot = false;
	float AutoSpeedScale = 1.f;

	// Saved locally in Saved/Config/Windows/RoadsterControls.ini.
	float SteeringDeadzone = .10f, StickExponent = 1.5f, CameraSensitivity = 1.f;
	bool bHapticsEnabled = true;
	void SaveControlSettings();
	FString GetControlDeviceLabel() const;
	FString GetStuntStatus() const;
	FString StuntPresetName() const;
	bool IsRecording() const { return bRecording; }
	bool IsReplaying() const { return bReplaying; }
	bool IsStuntActive() const { return bPresetPlaying; }
	bool IsVirtualCursorActive() const { return bVirtualCursor; }
	/** Sample finite valve presets on the same clock as rigid-body integration. */
	void ApplyPhysicsPreset(double StepSeconds);

private:
	bool bCommandLineConfigured = false;
	void ConfigureCommandLine();
	int32 AutoIndex = 0;
	TArray<float> ShotTimes;
	float PlayTime = 0.f;
	float CompileWaitSeconds = 0.f;
	int32 ShotIndex = 0;
	float ExitAfter = 0.f;
	float MeasuredFrameTime = 0.f;
	int32 MeasuredFrames = 0;
	TArray<float> ReviewFrameTimes;
	FString ShotDirectory;
	float TelemetryTimer = 0.f;
	void AutopilotInput(float Dt);
	void LogTelemetry(float Dt);
	float Steer = 0.f, Throttle = 0.f, Brake = 0.f;
	float BrakeHoldAtRest = 0.f;
	bool bReverseLatch = false;
	FRoadsterGamepad NativePad;
	bool bControlFocused = true, bNativeWasConnected = false;
	FDelegateHandle InputConnectionHandle;
	TAtomic<bool> bDeviceDisconnectedPending{false};
	bool bVirtualCursor = false, bBlockPadActionsUntilRelease = false;
	bool bBlockSpaceUntilRelease = false;
	bool bManualPilotInput = false;
	FVector2D PreviousPhysicalMouse = FVector2D::ZeroVector;
	float PadSteer = 0.f, HapticLow = 0.f, HapticHigh = 0.f, HapticTimer = 0.f;
	float CameraDeadzone = .12f;
	void LoadControlSettings();
	void UpdateCursor(float Dt);
	void UpdateHaptics(float Dt);
	void StopHapticsNow();
	FDynamicForceFeedbackHandle LowFeedbackHandle = 0, HighFeedbackHandle = 0;
	void ToggleHover();
	void CancelPilot(const FString& Reason = TEXT(""));
	void ApplyPilotSequence(float Dt);
	void StartPreset();
	void ToggleRecording();
	void SaveRecording();
	void ReplayLastRecording();
	void RunControlCheck();
	struct FRecordedInput
	{
		float Time = 0.f;
		Roadster::Inputs Input;
		bool bHover = false, bRoadstar = false;
		double HoverAltitude = 0;
	};
	TArray<FRecordedInput> RecordedInputs;
	FString LastRecordingFile;
	bool bRecording = false, bReplaying = false, bPresetPlaying = false, bSavedFlightAssist = true;
	float SequenceTime = 0.f;
	double PresetPhysicsTime = 0.0;
	int32 ReplayIndex = 0, SelectedStuntPreset = 0;

	void DriveInput(float Dt);
	void BuilderInput(float Dt);
	bool Pressed(const FKey& K) const;
	bool Down(const FKey& K) const;
	float Axis(const FKey& K) const;
};
