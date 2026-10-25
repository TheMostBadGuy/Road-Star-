#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
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

private:
	int32 AutoIndex = 0;
	TArray<float> ShotTimes;
	float PlayTime = 0.f;
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

	void DriveInput(float Dt);
	void BuilderInput(float Dt);
	bool Pressed(const FKey& K) const { return WasInputKeyJustPressed(K); }
	bool Down(const FKey& K) const { return IsInputKeyDown(K); }
	float Axis(const FKey& K) const { return GetInputAnalogKeyState(K); }
};
