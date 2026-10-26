#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "RoadsterPhysics.h"
#include "RoadsterPawn.generated.h"

class UStaticMeshComponent;
class UCameraComponent;
class USceneCaptureComponent2D;
class UAudioComponent;
class USpotLightComponent;
class UPointLightComponent;
class UCanvasRenderTarget2D;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;
class UCanvas;
class URoadsterInterface;
class ATrackActor;

UENUM()
enum class ERoadsterCamera : uint8 { Chase, ChaseFar, Cockpit, Hood, Wheel, Orbit, Cinematic };

USTRUCT()
struct FXRaySaved
{
	GENERATED_BODY()
	UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> Materials;
};

UCLASS()
class ROADSTERSIM_API ARoadsterPawn : public APawn
{
	GENERATED_BODY()

public:
	ARoadsterPawn();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult) override;

	// ------------------------------------------------------------ simulation
	Roadster::Vehicle Sim;
	Roadster::Inputs Input;
	float TimeScale = 1.f;
	bool bSimPaused = false;

	void SetTrim(Roadster::Trim NewTrim);
	void StartRoadstarScenario(int32 Scenario);
	/** Put the car on the nearest track sample (or upright where it is) and stop it. */
	void ResetToTrack(bool bToStart = false);
	void PlaceAt(const FTransform& BodyOriginWorld, float SpeedMps = 0.f);
	FTransform BodyTransform() const;

	// ------------------------------------------------------------ cameras
	ERoadsterCamera CameraMode = ERoadsterCamera::Chase;
	FRotator LookOffset = FRotator::ZeroRotator;   // mouse / right-stick look
	/** Select a view with a camera cut; leaves driving and simulation state unchanged. */
	void SetCameraMode(ERoadsterCamera NewMode);
	void CycleCamera();
	bool IsInterior() const { return CameraMode == ERoadsterCamera::Cockpit; }
	/** Free camera driven by the controller (track builder / photo mode). */
	bool bExternalCam = false;
	FVector ExtCamPos = FVector::ZeroVector;
	FRotator ExtCamRot = FRotator::ZeroRotator;

	// ------------------------------------------------------------ engineering X-ray view
	/** Body and trim become an edge-lit translucent ghost so the cold-gas system, fans and chassis show. */
	bool bXRay = false;
	float SpeedFeelIntensity = 1.f;
	void SetXRay(bool bOn);
	/** Authored hinge demonstration; travel and timing are simulator estimates. */
	bool bDoorsOpen = false;
	float GetHeadlightOpenFraction() const { return HeadlightOpenFraction; }
	bool AreHeadlightsRequested() const { return bHeadlightsRequested; }

	// ------------------------------------------------------------ car state toggles (UI)
	int32 ExteriorLights = 3;      // Off / DRL / On / Auto
	bool bTrackMode = false;
	/** Set only while a validated PlanetX host owns the planet visual. */
	bool bPlanetXVisualActive = false;
	float LapTime = 0.f, LastLap = -1.f, BestLap = -1.f;
	int32 LapCount = 0;
	TArray<float> LapHistory;

	// ------------------------------------------------------------ in-car screens
	UPROPERTY(Transient) TObjectPtr<UCanvasRenderTarget2D> ScreenRT;
	UPROPERTY(Transient) TObjectPtr<UCanvasRenderTarget2D> ForwardRT_A;
	UPROPERTY(Transient) TObjectPtr<UCanvasRenderTarget2D> ForwardRT_B;
	UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> CarViewRT;
	UPROPERTY(Transient) TObjectPtr<URoadsterInterface> Interface;

	/** Touch input routed from the full-screen overlay (virtual 1920x1200 coordinates). */
	ATrackActor* FindTrack() const;

protected:
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> StaticParts;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ThrustersRear;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ThrustersRocket;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> COPV;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> COPVTrunk;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ColdGasPlumbing;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> FanBox;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> SeatPassenger;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Wing;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> HoodVent;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> BargeL;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> BargeR;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Skirts;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> WingStruts;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> AdditionalHingedParts;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SkirtParts;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Yoke;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ScreenDisplay;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ForwardA;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> ForwardB;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> FrontLights;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> TailLights;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Fans;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Wheels;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Discs;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Calipers;
	/** Pushrod suspension parts (SuspensionRig.h order), articulated from wheel travel and steering. */
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> SuspParts;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> PlumesRear;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> PlumesLift;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> PlumesYaw;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Smoke;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;
	UPROPERTY() TObjectPtr<USceneCaptureComponent2D> CarCapture;
	UPROPERTY() TArray<TObjectPtr<USpotLightComponent>> Headlights;
	UPROPERTY() TObjectPtr<UPointLightComponent> BrakeGlow;
	UPROPERTY() TObjectPtr<UPointLightComponent> CabinGlow;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> EarthVisual;
	UPROPERTY() TObjectPtr<UAudioComponent> AudioMotor;
	UPROPERTY() TObjectPtr<UAudioComponent> AudioFan;
	UPROPERTY() TObjectPtr<UAudioComponent> AudioThrust;
	UPROPERTY() TObjectPtr<UAudioComponent> AudioWind;
	UPROPERTY() TObjectPtr<UAudioComponent> AudioTire;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TailMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HeadMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PlumeMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> XRayMID;
	/** Original materials of the components the X-ray view ghosts (restored when it is switched off). */
	UPROPERTY() TMap<TObjectPtr<UStaticMeshComponent>, FXRaySaved> XRaySaved;

	// [TEAM HOOKUP: components] overlay / VFX component pointers (Coordination/TEAM.md). Add UPROPERTY lines here.

	friend class URoadsterThermalOverlay;
	friend class URoadsterFlowOverlay;
	friend class URoadsterPhysicsLab;
	friend class URoadsterJetVFX;
	friend bool RunRoadsterRuntimePhysicsVerification(ARoadsterPawn* Pawn, FString& OutReport);

private:
	double Accumulator = 0.0;
	double DebugAfterReset = -1.0;
	FVector CamPos = FVector::ZeroVector;
	FVector CameraFollowPos = FVector::ZeroVector;
	FVector CameraPreviousAnchor = FVector::ZeroVector;
	FVector CamUp = FVector::UpVector;
	FRotator CamRot = FRotator::ZeroRotator;
	float CamFov = 75.f;
	float CameraAccelG = 0.f;
	float CameraClearDistance = 0.f;
	ERoadsterCamera CameraPreviousMode = ERoadsterCamera::Chase;
	bool bPreviousExternalCam = false;
	bool bCameraFallbackActive = false;
	bool bCameraWasObstructed = false;
	float OrbitYaw = 200.f, OrbitPitch = -12.f;
	float CaptureTimer = 0.f, ScreenTimer = 0.f;
	bool bCarPreviewCaptured = false;
	float CinematicTimer = 0.f;
	float DoorOpenFraction = 0.f;
	float VentOpenFraction = 0.f;
	float BargeOpenFraction = 0.f;
	float HeadlightOpenFraction = 0.f;
	float AutoHeadlightTimer = 0.f;
	bool bAutoHeadlights = false;
	bool bHeadlightsRequested = false;
	FVector CinematicPos = FVector::ZeroVector;
	bool bCamInit = false;

	struct FPuff { FVector Pos; FVector Vel; float Age; float Life; float Size; float Growth; float Alpha; };
	TArray<FPuff> Puffs;

	Roadster::TraceFn MakeTrace();
	void StepSimulation(float DeltaSeconds);
	void UpdateVisuals(float DeltaSeconds);
	/** Pose every suspension part; returns the upright (hub carrier) transform per corner for wheels and brakes. */
	void UpdateSuspension(FTransform OutUpright[4]);
	void UpdateAudio(float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	void UpdateSmoke(float DeltaSeconds);
	void UpdateLaps(float DeltaSeconds);
	void EmitPuff(const FVector& Pos, const FVector& Vel, float Size, float Life, float Growth, float Alpha);
	void ApplyTrimVisibility();
	UStaticMeshComponent* AddPart(const TCHAR* MeshName, USceneComponent* Parent = nullptr, FName Name = NAME_None);

	UFUNCTION() void DrawScreen(UCanvas* Canvas, int32 Width, int32 Height);
	UFUNCTION() void DrawForwardA(UCanvas* Canvas, int32 Width, int32 Height);
	UFUNCTION() void DrawForwardB(UCanvas* Canvas, int32 Width, int32 Height);

	FTransform WingStowed, WingDeployed;
	FTransform VentClosed;
	FTransform BargeLStowed, BargeRStowed;
	FTransform SkirtsUp;
	FTransform YokeBase;
};
