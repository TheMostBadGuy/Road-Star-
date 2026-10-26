#include "RoadsterPawn.h"
#include "RoadsterRig.h"
#include "SuspensionRig.h"
#include "RoadsterInterface.h"
#include "RoadsterController.h"
#include "TrackActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Components/SpotLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/AudioComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundWave.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Kismet/GameplayStatics.h"
#include <cmath>

using namespace Roadster;

// Feature switches for profiling: 1 screens, 2 car-view capture, 4 smoke, 8 HUD (bit set = disabled)
static TAutoConsoleVariable<int32> CVarRoadsterDisable(TEXT("roadster.Disable"), 0, TEXT("Bitmask: 1 screens, 2 car capture, 4 smoke, 8 hud"));
CSV_DEFINE_CATEGORY(Roadster, true);
int32 RoadsterDisableMask() { return CVarRoadsterDisable.GetValueOnGameThread(); }
// Diagnostic logging: 0 off, 1 telemetry + resets (default with -autodrive), 2 verbose (autopilot, reset settle, respawn search)
static TAutoConsoleVariable<int32> CVarRoadsterLog(TEXT("roadster.Log"), 0, TEXT("0 off, 1 telemetry, 2 verbose"));
int32 RoadsterLogLevel() { return CVarRoadsterLog.GetValueOnGameThread(); }
void SetRoadsterLogLevel(int32 Level) { CVarRoadsterLog->Set(Level, ECVF_SetByCommandline); }

static TAutoConsoleVariable<int32> CVarRoadsterCameraDebug(TEXT("roadster.CameraDebug"), 0, TEXT("1 logs player camera mode, boom clearance and cockpit fallback transitions"));

// BEGIN ROADSTER_CAMERA_MOTION -- compiled directly by the portable camera harness.
namespace RoadsterCameraMotion
{
	inline double Alpha(double DeltaSeconds, double Rate)
	{
		if (Rate <= 0.0) return 1.0;
		return 1.0 - std::exp(-Rate * (DeltaSeconds > 0.0 ? DeltaSeconds : 0.0));
	}
	template <typename V>
	V FollowRelative(const V& Current, const V& PreviousAnchor, const V& Anchor, const V& DesiredOffset, double DeltaSeconds)
	{
		const double A = Alpha(DeltaSeconds, 7.0);
		return Anchor + (Current - PreviousAnchor) * (1.0 - A) + DesiredOffset * A;
	}
	inline double ObstacleDistance(double DesiredDistance, double HitDistance, double Margin)
	{
		const double D = HitDistance - Margin;
		return D < 0.0 ? 0.0 : (D < DesiredDistance ? D : DesiredDistance);
	}
	inline bool NeedsCockpit(bool bStartPenetrating, double AllowedDistance, double CarClearance)
	{
		return bStartPenetrating || AllowedDistance < CarClearance;
	}
	inline double ReleaseDistance(double PreviousDistance, double AllowedDistance, double DeltaSeconds, bool bReset, bool bWasFallback)
	{
		if (bReset || bWasFallback || PreviousDistance <= 0.0 || AllowedDistance < PreviousDistance) return AllowedDistance;
		return PreviousDistance + (AllowedDistance - PreviousDistance) * Alpha(DeltaSeconds, 8.0);
	}
	inline bool ResetView(bool bInitialized, int PreviousMode, int Mode, bool bWasExternal, bool bExternal)
	{
		return !bInitialized || PreviousMode != Mode || bWasExternal != bExternal;
	}
}
// END ROADSTER_CAMERA_MOTION

namespace
{
	struct FCameraBoomQuery
	{
		float AllowedDistance = 0.f;
		bool bObstructed = false;
		bool bStartPenetrating = false;
	};

	FCameraBoomQuery QueryCameraBoom(UWorld* World, const AActor* Car, const FVector& Anchor, const FVector& Desired)
	{
		constexpr float Radius = 12.f, Skin = 3.f; // cm: near-plane clearance, including the small speed cue
		FCameraBoomQuery Result;
		Result.AllowedDistance = FVector::Dist(Anchor, Desired);
		if (!World || Result.AllowedDistance <= UE_SMALL_NUMBER) return Result;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RoadsterCameraBoom), true, Car);
		Params.bFindInitialOverlaps = true;
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		FHitResult Hit;
		if (World->SweepSingleByObjectType(Hit, Anchor, Desired, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius), Params))
		{
			Result.bObstructed = true;
			Result.bStartPenetrating = Hit.bStartPenetrating;
			Result.AllowedDistance = float(RoadsterCameraMotion::ObstacleDistance(Result.AllowedDistance, Hit.Distance, Skin));
		}
		// Also preserve line of sight against thin/complex occluders, using the same world objects.
		if (World->LineTraceSingleByObjectType(Hit, Anchor, Desired, Objects, Params))
		{
			Result.bObstructed = true;
			Result.bStartPenetrating |= Hit.bStartPenetrating;
			Result.AllowedDistance = float(RoadsterCameraMotion::ObstacleDistance(Result.AllowedDistance, Hit.Distance, Radius + Skin));
		}
		return Result;
	}
}

namespace
{
	constexpr float M2CM = 100.f;
	// Blender (x fwd, y LEFT, z up, metres) -> Unreal (x fwd, y RIGHT, z up, cm)
	FVector B2U(double x, double y, double z) { return FVector(x * M2CM, -y * M2CM, z * M2CM); }
	FVector ToU(const V3& v) { return FVector(v.x, v.y, v.z) * M2CM; }
	V3 ToM(const FVector& v) { return V3(v.X / M2CM, v.Y / M2CM, v.Z / M2CM); }
	void ApplyRigNozzles(Spec& S)
	{
		const char* Names[] = { "Rear_R2", "Rear_R1", "Rear_L1", "Rear_L2", "Lift_FL", "Lift_FR", "Lift_RL", "Lift_RR", "Yaw_L_firing", "Yaw_R_firing" };
		for (int32 i = 0; i < 10; ++i)
			if (const auto* N = RoadsterRig::FindNozzle(Names[i]))
			{
				const V3 P(N->X, -N->Y, N->Z);
				if (i < 4) S.RearNozzlePos[i] = P;
				else if (i < 8) S.LiftNozzlePos[i - 4] = P;
				else S.YawNozzlePos[i - 8] = P;
			}
	}
	FQuat ToUQ(const Quat& q) { return FQuat(q.x, q.y, q.z, q.w); }
	Quat ToMQ(const FQuat& q) { return Quat(q.W, q.X, q.Y, q.Z); }

	template <typename T>
	T* LoadAsset(const FString& Path) { return Cast<T>(StaticLoadObject(T::StaticClass(), nullptr, *Path)); }
}

ARoadsterPawn::ARoadsterPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Body = AddPart(TEXT("Roadster_Body"));
	for (const TCHAR* N : { TEXT("Side_Details"), TEXT("Splitter"), TEXT("Plate"), TEXT("Plate_Text"), TEXT("Rear_Diffuser"),
		TEXT("Interior"), TEXT("Seat_Driver"), TEXT("Screen_Bezel"), TEXT("Sill_Plates"), TEXT("Charge_Port"), TEXT("Winglets") })
		StaticParts.Add(AddPart(N));
	FrontLights = AddPart(TEXT("Front_Lights"));
	TailLights = AddPart(TEXT("Tail_Lights"));
	ThrustersRear = AddPart(TEXT("Thrusters_Rear"));
	ThrustersRocket = AddPart(TEXT("Thrusters_Rocket"));
	COPV = AddPart(TEXT("COPV_Rocket"));
	COPVTrunk = AddPart(TEXT("COPV_Trunk"));          // 200 L vessel (Thrust and Rocket)
	ColdGasPlumbing = AddPart(TEXT("ColdGas_Plumbing")); // regulator, isolation valves, lines, transducers
	FanBox = AddPart(TEXT("Fan_Box"));               // carbon suction box with stators and motors
	SeatPassenger = AddPart(TEXT("Seat_Passenger"));
	ScreenDisplay = AddPart(TEXT("Screen_Display"));
	ForwardA = AddPart(TEXT("Forward_Display_A"));
	ForwardB = AddPart(TEXT("Forward_Display_B"));

	// moving parts, positioned at their pivots (RoadsterRig.h is generated by the Blender export)
	auto Pivot = [](const char* Name, double X, double Y, double Z)
	{
		const RoadsterRig::FPivot* P = RoadsterRig::FindPivot(Name);
		return P ? B2U(P->X, P->Y, P->Z) : B2U(X, Y, Z);
	};
	Wing = AddPart(TEXT("Rear_Wing"));       WingDeployed = FTransform(Pivot("Rear_Wing", -2.10, 0, 0.93));
	WingStowed = FTransform(FRotator(-10.f, 0, 0), WingDeployed.GetLocation() + FVector(4.f, 0, -23.f));
	HoodVent = AddPart(TEXT("Hood_Vent"));   VentClosed = FTransform(Pivot("Hood_Vent", 1.773, 0, 0.627));
	BargeL = AddPart(TEXT("Barge_L"));       BargeLStowed = FTransform(Pivot("Barge_L", 0.83, 0.9506, 0.3309));
	BargeR = AddPart(TEXT("Barge_R"));       BargeRStowed = FTransform(Pivot("Barge_R", 0.83, -0.9506, 0.3309));
	// The current Blender shell has these panels removed from the static body.
	// Each mesh is local to its generated pivot; omitting one leaves a hole in the car.
	WingStruts = AddPart(TEXT("Wing_Struts"));
	for (const auto& H : RoadsterRig::Hinges)
	{
		if (FCStringAnsi::Strcmp(H.Name, "Hood_Vent") == 0 || FCStringAnsi::Strcmp(H.Name, "Barge_L") == 0 || FCStringAnsi::Strcmp(H.Name, "Barge_R") == 0) continue;
		auto* Panel = AddPart(ANSI_TO_TCHAR(H.Name));
		Panel->SetRelativeLocation(B2U(H.PX, H.PY, H.PZ));
		AdditionalHingedParts.Add(Panel);
	}
	for (const char* Name : { "Skirt_Front", "Skirt_L", "Skirt_Rear", "Skirt_R" })
	{
		auto* Panel = AddPart(ANSI_TO_TCHAR(Name));
		Panel->SetRelativeLocation(Pivot(Name, 0, 0, 0.232));
		SkirtParts.Add(Panel);
	}
	Yoke = AddPart(TEXT("Yoke"));            YokeBase = FTransform(FRotator(-24.f, 0, 0), Pivot("Yoke", 0.62, 0.38, 0.72));
	for (int32 k = 0; k < 4; ++k)
	{
		UStaticMeshComponent* F = AddPart(*FString::Printf(TEXT("Fan_%d"), k), nullptr, *FString::Printf(TEXT("Fan%d"), k));
		F->SetRelativeLocation(Pivot(TCHAR_TO_ANSI(*FString::Printf(TEXT("Fan_%d"), k)), -2.05, (k - 1.5) * 1.26 / 4, 0.284));
		Fans.Add(F);
	}
	for (int32 i = 0; i < 4; ++i)
	{
		const bool bFront = i < 2;
		Wheels.Add(AddPart(bFront ? TEXT("Wheel_Front") : TEXT("Wheel_Rear"), nullptr, *FString::Printf(TEXT("Wheel%d"), i)));
		Discs.Add(AddPart(bFront ? TEXT("Brake_Front_Disc") : TEXT("Brake_Rear_Disc"), nullptr, *FString::Printf(TEXT("Disc%d"), i)));
		Calipers.Add(AddPart(bFront ? TEXT("Brake_Front_Caliper") : TEXT("Brake_Rear_Caliper"), nullptr, *FString::Printf(TEXT("Caliper%d"), i)));
	}
	for (int32 p = 0; p < SuspensionRig::NumParts; ++p)
	{
		const SuspensionRig::FPart& Part = SuspensionRig::Parts[p];
		UStaticMeshComponent* C = AddPart(ANSI_TO_TCHAR(Part.Name));
		C->SetRelativeLocation(Part.Joint == SuspensionRig::Static ? FVector::ZeroVector : FVector(Part.Pivot[0], Part.Pivot[1], Part.Pivot[2]));
		C->SetCastShadow(Part.Joint != SuspensionRig::Static);
		SuspParts.Add(C);
	}

	// cold-gas plumes: translucent cones scaled by thrust (white condensation jets of cold nitrogen)
	UStaticMesh* Cone = LoadAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Cone.Cone"));
	auto MakePlume = [&](FName Name, const FVector& Pos, const FRotator& Rot)
	{
		UStaticMeshComponent* P = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		P->SetupAttachment(Root);
		P->SetStaticMesh(Cone);
		P->SetRelativeLocation(Pos);
		P->SetRelativeRotation(Rot);
		P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		P->SetCastShadow(false);
		P->SetVisibility(false);
		return P;
	};
	// plumes sit at the same nozzle exits the physics pushes at (Spec, body frame: x fwd, y right, z up, metres)
	ApplyRigNozzles(Sim.S);
	const Spec& NozzleSpec = Sim.S;
	for (int32 k = 0; k < 4; ++k)   // rear bank, pointing backwards (cone apex at the nozzle)
		PlumesRear.Add(MakePlume(*FString::Printf(TEXT("PlumeRear%d"), k), ToU(NozzleSpec.RearNozzlePos[k]) - FVector(5, 0, 0), FRotator(90.f, 0, 0)));
	for (int32 k = 0; k < 4; ++k)   // corner lift nozzles, pointing down
		PlumesLift.Add(MakePlume(*FString::Printf(TEXT("PlumeLift%d"), k), ToU(NozzleSpec.LiftNozzlePos[k]) - FVector(0, 0, 4), FRotator(0, 0, 0)));
	PlumesYaw.Add(MakePlume(TEXT("PlumeYawL"), ToU(NozzleSpec.YawNozzlePos[0]), FRotator(0, 0, 90.f)));    // exits to the car's left
	PlumesYaw.Add(MakePlume(TEXT("PlumeYawR"), ToU(NozzleSpec.YawNozzlePos[1]), FRotator(0, 0, -90.f)));

	Smoke = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Smoke"));
	Smoke->SetupAttachment(Root);
	Smoke->SetUsingAbsoluteLocation(true);
	Smoke->SetUsingAbsoluteRotation(true);
	Smoke->SetUsingAbsoluteScale(true);
	Smoke->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Smoke->SetCastShadow(false);
	Smoke->NumCustomDataFloats = 2;
	Smoke->SetStaticMesh(LoadAsset<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere.Sphere")));

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetUsingAbsoluteLocation(true);
	Camera->SetUsingAbsoluteRotation(true);
	Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	Camera->PostProcessSettings.MotionBlurAmount = 0.35f;
	Camera->PostProcessSettings.bOverride_SceneFringeIntensity = true;
	Camera->PostProcessSettings.SceneFringeIntensity = 0.f;
	Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	Camera->PostProcessSettings.VignetteIntensity = 0.32f;
	EarthVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EarthVisual"));
	EarthVisual->SetupAttachment(Root);
	EarthVisual->SetUsingAbsoluteLocation(true);
	EarthVisual->SetUsingAbsoluteRotation(true);
	EarthVisual->SetUsingAbsoluteScale(true);
	EarthVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EarthVisual->SetCastShadow(false);
	EarthVisual->SetVisibleInRayTracing(false);
	EarthVisual->SetVisibility(false);

	CarCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("CarCapture"));
	CarCapture->SetupAttachment(Root);
	CarCapture->bCaptureEveryFrame = false;
	CarCapture->bCaptureOnMovement = false;
	CarCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	CarCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	CarCapture->FOVAngle = 30.f;

	for (int32 s = 0; s < 2; ++s)
	{
		USpotLightComponent* L = CreateDefaultSubobject<USpotLightComponent>(s ? TEXT("HeadlightR") : TEXT("HeadlightL"));
		L->SetupAttachment(Root);
		// Candidate headlight fixtures point +X; exit lies beyond the opened cover's optical path.
		L->SetRelativeLocation(B2U(1.946303, s ? -0.696065 : 0.696065, 0.595249));
		L->SetRelativeRotation(FRotator(-2.f, 0, 0)); // EST beam aim; not a homologated photometric cutoff
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetIntensity(9000.f);
		L->SetAttenuationRadius(9000.f);
		L->SetOuterConeAngle(32.f);
		L->SetInnerConeAngle(10.f);
		L->SetLightColor(FLinearColor(1.f, 0.96f, 0.9f));
		L->SetCastShadows(true);
		Headlights.Add(L);
	}
	BrakeGlow = CreateDefaultSubobject<UPointLightComponent>(TEXT("BrakeGlow"));
	BrakeGlow->SetupAttachment(Root);
	BrakeGlow->SetRelativeLocation(B2U(-2.25, 0, 0.90));
	BrakeGlow->SetLightColor(FLinearColor(1.f, 0.02f, 0.01f));
	BrakeGlow->SetIntensityUnits(ELightUnits::Candelas);
	BrakeGlow->SetIntensity(30.f);
	BrakeGlow->SetAttenuationRadius(450.f);
	BrakeGlow->SetCastShadows(false);
	CabinGlow = CreateDefaultSubobject<UPointLightComponent>(TEXT("CabinGlow"));
	CabinGlow->SetupAttachment(Root);
	CabinGlow->SetRelativeLocation(B2U(0.2, 0, 0.62));
	CabinGlow->SetLightColor(FLinearColor(1.f, 0.05f, 0.03f));
	CabinGlow->SetIntensityUnits(ELightUnits::Candelas);
	CabinGlow->SetIntensity(1.5f);
	CabinGlow->SetAttenuationRadius(130.f);
	CabinGlow->SetCastShadows(false);

	auto MakeAudio = [&](FName Name, const TCHAR* Wave)
	{
		UAudioComponent* A = CreateDefaultSubobject<UAudioComponent>(Name);
		A->SetupAttachment(Root);
		A->bAutoActivate = false;
		A->SetSound(LoadAsset<USoundWave>(FString::Printf(TEXT("/Game/Audio/%s.%s"), Wave, Wave)));
		return A;
	};
	AudioMotor = MakeAudio(TEXT("AudioMotor"), TEXT("S_Motor"));
	AudioFan = MakeAudio(TEXT("AudioFan"), TEXT("S_Fan"));
	AudioThrust = MakeAudio(TEXT("AudioThrust"), TEXT("S_Thrust"));
	AudioWind = MakeAudio(TEXT("AudioWind"), TEXT("S_Wind"));
	AudioTire = MakeAudio(TEXT("AudioTire"), TEXT("S_Tire"));

	// [TEAM HOOKUP: constructor] overlay / VFX components are created here (Coordination/TEAM.md).
}

UStaticMeshComponent* ARoadsterPawn::AddPart(const TCHAR* MeshName, USceneComponent* Parent, FName Name)
{
	const FName CompName = Name.IsNone() ? FName(MeshName) : Name;
	UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(CompName);
	C->SetupAttachment(Parent ? Parent : (Root ? Root.Get() : nullptr));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->bReceivesDecals = false;
	if (UStaticMesh* M = LoadAsset<UStaticMesh>(FString::Printf(TEXT("/Game/Roadster/Meshes/SM_%s.SM_%s"), MeshName, MeshName)))
		C->SetStaticMesh(M);
	return C;
}

void ARoadsterPawn::BeginPlay()
{
	Super::BeginPlay();

	Sim.Init(Trim::Rocket);
	for (int32 i = 0; i < 4; ++i) Sim.Set.ColdPressureKPa[i] = Sim.S.DefaultColdPressureKPa;
	ApplyTrimVisibility();

	// in-car screens: canvas render targets painted by the UI code
	Interface = NewObject<URoadsterInterface>(this);
	Interface->Pawn = this;
	ScreenRT = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(this, UCanvasRenderTarget2D::StaticClass(), 1920, 1200);
	ScreenRT->OnCanvasRenderTargetUpdate.AddDynamic(this, &ARoadsterPawn::DrawScreen);
	ForwardRT_A = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(this, UCanvasRenderTarget2D::StaticClass(), 800, 220);
	ForwardRT_A->OnCanvasRenderTargetUpdate.AddDynamic(this, &ARoadsterPawn::DrawForwardA);
	ForwardRT_B = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(this, UCanvasRenderTarget2D::StaticClass(), 800, 220);
	ForwardRT_B->OnCanvasRenderTargetUpdate.AddDynamic(this, &ARoadsterPawn::DrawForwardB);
	CarViewRT = NewObject<UTextureRenderTarget2D>(this);
	CarViewRT->RenderTargetFormat = RTF_RGBA8;
	CarViewRT->ClearColor = FLinearColor::Black;
	CarViewRT->InitAutoFormat(1100, 620);
	CarCapture->TextureTarget = CarViewRT;
	CarCapture->ShowFlags.SetAtmosphere(false);
	CarCapture->ShowFlags.SetFog(false);
	CarCapture->ShowFlags.SetVolumetricFog(false);
	CarCapture->ShowFlags.SetCloud(false);
	CarCapture->ShowFlags.SetMotionBlur(false);
	CarCapture->ShowFlags.SetBloom(false);
	CarCapture->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
	CarCapture->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	CarCapture->PostProcessSettings.bOverride_ReflectionMethod = true;
	CarCapture->PostProcessSettings.ReflectionMethod = EReflectionMethod::None;
	CarCapture->PostProcessSettings.bOverride_AutoExposureMethod = true;
	CarCapture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	CarCapture->PostProcessSettings.bOverride_AutoExposureBias = true;
	CarCapture->PostProcessSettings.AutoExposureBias = 0.f;
	// The show-only car view uses a black background, so automatic metering would clip the
	// daylight body into a white silhouette. Use a physical daylight exposure (about EV14).
	CarCapture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	CarCapture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = true;
	CarCapture->PostProcessSettings.bOverride_CameraISO = true;
	CarCapture->PostProcessSettings.CameraISO = 100.f;
	CarCapture->PostProcessSettings.bOverride_CameraShutterSpeed = true;
	CarCapture->PostProcessSettings.CameraShutterSpeed = 250.f;
	CarCapture->PostProcessSettings.bOverride_DepthOfFieldFstop = true;
	CarCapture->PostProcessSettings.DepthOfFieldFstop = 8.f;
	CarCapture->ShowFlags.SetDepthOfField(false);
	TArray<UActorComponent*> Comps;
	GetComponents(UStaticMeshComponent::StaticClass(), Comps);
	for (UActorComponent* C : Comps)
	{
		UStaticMeshComponent* S = Cast<UStaticMeshComponent>(C);
		if (S && !PlumesRear.Contains(S) && !PlumesLift.Contains(S) && !PlumesYaw.Contains(S) && S != Smoke && S != EarthVisual && S != ScreenDisplay && S != ForwardA && S != ForwardB)
			CarCapture->ShowOnlyComponents.Add(S);
	}

	auto ScreenMat = [&](UStaticMeshComponent* Comp, UTexture* Tex)
	{
		if (!Comp || !Comp->GetStaticMesh()) return;
		if (UMaterialInterface* Base = LoadAsset<UMaterialInterface>(TEXT("/Game/Materials/M_Screen.M_Screen")))
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
			MID->SetTextureParameterValue(TEXT("ScreenTex"), Tex);
			Comp->SetMaterial(0, MID);
		}
	};
	ScreenMat(ScreenDisplay, ScreenRT);
	ScreenMat(ForwardA, ForwardRT_A);
	ScreenMat(ForwardB, ForwardRT_B);

	if (TailLights && TailLights->GetMaterial(0)) TailMID = TailLights->CreateDynamicMaterialInstance(0);
	if (FrontLights && FrontLights->GetMaterial(0)) HeadMID = FrontLights->CreateDynamicMaterialInstance(0);
	if (UMaterialInterface* PM = LoadAsset<UMaterialInterface>(TEXT("/Game/Materials/M_Plume.M_Plume")))
	{
		PlumeMID = UMaterialInstanceDynamic::Create(PM, this);
		for (UStaticMeshComponent* P : PlumesRear) P->SetMaterial(0, PlumeMID);
		for (UStaticMeshComponent* P : PlumesLift) P->SetMaterial(0, PlumeMID);
		for (UStaticMeshComponent* P : PlumesYaw) P->SetMaterial(0, PlumeMID);
	}
	if (UMaterialInterface* SM = LoadAsset<UMaterialInterface>(TEXT("/Game/Materials/M_Smoke.M_Smoke")))
		Smoke->SetMaterial(0, SM);

	for (UAudioComponent* A : { AudioMotor.Get(), AudioFan.Get(), AudioWind.Get(), AudioTire.Get(), AudioThrust.Get() })
		if (A && A->Sound) { A->SetVolumeMultiplier(0.f); A->Play(); }

	ResetToTrack(true);
	EarthVisual->SetStaticMesh(LoadAsset<UStaticMesh>(TEXT("/Game/Environment/Earth20261025/SM_EarthUnit.SM_EarthUnit")));
	EarthVisual->SetWorldLocation(FVector(0, 0, -Earth::MeanRadiusM * 100.0));
	EarthVisual->SetWorldScale3D(FVector(Earth::MeanRadiusM));
}

void ARoadsterPawn::SetTrim(Trim NewTrim)
{
	const FTransform T = BodyTransform();
	const Settings Keep = Sim.Set;
	Sim.Init(NewTrim);
	Sim.Set = Keep;
	PlaceAt(T, 0.f);
	ApplyTrimVisibility();
}

void ARoadsterPawn::StartRoadstarScenario(int32 Scenario)
{
	SetTrim(Trim::Rocket);
	Sim.Set.bRoadstarMode = true;
	Sim.Set.bHoverHold = false;
	Sim.Set.bFlightAssist = false;
	Sim.Set.bThrustersArmed = false;
	Input = Inputs();
	const double Altitude = Scenario == 1 ? 100000.0 : 400000.0;
	PlaceAt(FTransform(FQuat::Identity, FVector(0, 0, Altitude * 100.0)));
	Sim.V = Scenario == 1 ? V3(1200, 0, 300) : V3(std::sqrt(Earth::MuM3PerS2 / (Earth::MeanRadiusM + Altitude)), 0, 0);
	Sim.Orbit = Earth::OrbitalElements({Sim.X.x, Sim.X.y, Sim.X.z}, {Sim.V.x, Sim.V.y, Sim.V.z});
	CameraMode = ERoadsterCamera::ChaseFar;
	Sim.ResetRunTimer();
}

void ARoadsterPawn::ApplyTrimVisibility()
{
	ThrustersRear->SetVisibility(Sim.S.bHasRearThrusters);
	ThrustersRocket->SetVisibility(Sim.S.bHasLiftThrusters);
	COPV->SetVisibility(Sim.S.TrimLevel == Trim::Rocket);
	COPVTrunk->SetVisibility(Sim.S.TankVolumeL > 0);
	ColdGasPlumbing->SetVisibility(Sim.S.TankVolumeL > 0);
	SeatPassenger->SetVisibility(Sim.S.Seats == 2);
}

void ARoadsterPawn::SetXRay(bool bOn)
{
	if (bOn == bXRay) return;
	bXRay = bOn;
	if (!XRayMID)
		if (UMaterialInterface* XM = LoadAsset<UMaterialInterface>(TEXT("/Game/Materials/M_XRay.M_XRay")))
			XRayMID = UMaterialInstanceDynamic::Create(XM, this);
	if (!XRayMID) return;
	// the shell and its trim become the ghost; everything structural inside keeps its real materials
	TArray<UStaticMeshComponent*> Ghost = { Body, HoodVent, Wing, BargeL, BargeR, Skirts, FrontLights, TailLights };
	for (UStaticMeshComponent* C : AdditionalHingedParts) Ghost.Add(C);
	for (UStaticMeshComponent* C : SkirtParts) Ghost.Add(C);
	for (UStaticMeshComponent* C : StaticParts)
		if (C && C->GetStaticMesh() && (C->GetName().Contains(TEXT("Side_Details")) || C->GetName().Contains(TEXT("Splitter")) || C->GetName().Contains(TEXT("Plate"))))
			Ghost.Add(C);
	for (UStaticMeshComponent* C : Ghost)
	{
		if (!C) continue;
		if (bOn)
		{
			FXRaySaved& S = XRaySaved.FindOrAdd(C);
			S.Materials.Reset();
			for (int32 i = 0; i < C->GetNumMaterials(); ++i)
			{
				S.Materials.Add(C->GetMaterial(i));
				C->SetMaterial(i, XRayMID);
			}
			C->SetCastShadow(false);
		}
		else if (FXRaySaved* S = XRaySaved.Find(C))
		{
			for (int32 i = 0; i < S->Materials.Num(); ++i) C->SetMaterial(i, S->Materials[i]);
			C->SetCastShadow(true);
		}
	}
	if (!bOn) XRaySaved.Reset();
	// The white car is the brightest thing in most frames; once it turns into a ghost the histogram exposure
	// would brighten the whole scene by several stops. Hold daylight exposure while the engineering view is on.
	Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = bOn;
	Camera->PostProcessSettings.AutoExposureMinBrightness = 12.f;   // EV100 (extended luminance range)
}

FTransform ARoadsterPawn::BodyTransform() const
{
	return FTransform(ToUQ(Sim.Q), ToU(Sim.BodyOriginWorld()));
}

void ARoadsterPawn::PlaceAt(const FTransform& BodyOriginWorld, float SpeedMps)
{
	Sim.Place(ToM(BodyOriginWorld.GetLocation()), ToMQ(BodyOriginWorld.GetRotation()), SpeedMps);
	Accumulator = 0;
	SetActorTransform(BodyOriginWorld, false, nullptr, ETeleportType::TeleportPhysics);
	bCamInit = false;
}

ATrackActor* ARoadsterPawn::FindTrack() const
{
	for (TActorIterator<ATrackActor> It(GetWorld()); It; ++It) return *It;
	return nullptr;
}

void ARoadsterPawn::ResetToTrack(bool bToStart)
{
	FTransform T(FRotator::ZeroRotator, FVector(0, 0, 30.f));
	if (ATrackActor* Track = FindTrack())
	{
		FTransform Frame;
		if (bToStart ? Track->GetStartFrame(Frame) : Track->FindNearestFrame(GetActorLocation(), Frame))
			T = Frame;
	}
	else if (!bToStart)
	{
		// free roam: upright where we are, facing our current heading
		FRotator R = GetActorRotation(); R.Pitch = 0; R.Roll = 0;
		FHitResult Hit;
		const FVector P = GetActorLocation();
		if (GetWorld()->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 2000), P - FVector(0, 0, 20000), ECC_WorldStatic))
			T = FTransform(R, Hit.ImpactPoint + FVector(0, 0, 5.f));
	}
	PlaceAt(T, 0.f);
	DebugAfterReset = RoadsterLogLevel() >= 2 ? 0.0 : -1.0;
	if (RoadsterLogLevel() >= 1) UE_LOG(LogTemp, Display, TEXT("RESET at (%.1f,%.1f,%.1f) up=(%.2f,%.2f,%.2f) heave=%.0f fanrpm=%.0f"), T.GetLocation().X / 100, T.GetLocation().Y / 100, T.GetLocation().Z / 100,
		T.GetRotation().GetUpVector().X, T.GetRotation().GetUpVector().Y, T.GetRotation().GetUpVector().Z, Sim.HeavePreload, Sim.FanRPM);
	Sim.ResetRunTimer();
	LapTime = 0.f;
}

Roadster::TraceFn ARoadsterPawn::MakeTrace()
{
	UWorld* W = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RoadsterTrace), true, this);
	Params.bReturnPhysicalMaterial = false;
	FCollisionObjectQueryParams Obj;
	Obj.AddObjectTypesToQuery(ECC_WorldStatic);
	Obj.AddObjectTypesToQuery(ECC_WorldDynamic);
	const bool bPlanet = Sim.Set.bRoadstarMode;
	return [W, Params, Obj, bPlanet](const V3& s, const V3& d, double len, double r) -> Hit
	{
		Hit Out;
		const FVector Start = ToU(s);
		const FVector End = Start + FVector(d.x, d.y, d.z) * (len * M2CM);
		FHitResult H;
		bool bHit;
		if (r <= 1e-4) bHit = W->LineTraceSingleByObjectType(H, Start, End, Obj, Params);
		else bHit = W->SweepSingleByObjectType(H, Start, End, FQuat::Identity, Obj, FCollisionShape::MakeSphere(r * M2CM), Params);
		if (!bHit)
		{
			if (bPlanet)
			{
				const Earth::Vec3 P = Earth::EarthCenteredPosition({s.x, s.y, s.z});
				const Earth::Vec3 D(d.x, d.y, d.z);
				const double R = Earth::MeanRadiusM + r;
				const double B = Earth::Dot(P, D), C = Earth::Dot(P, P) - R * R;
				const double Disc = B * B - C;
				if (C >= 0 && Disc >= 0)
				{
					const double T = -B - std::sqrt(Disc);
					if (T >= 0 && T <= len)
					{
						const Earth::Vec3 N = (P + D * T) / R;
						Out.bHit = true; Out.CenterDist = T; Out.Normal = V3(N.X, N.Y, N.Z);
						Out.Point = s + d * T - Out.Normal * r; Out.Dist = (Out.Point - s).Dot(d);
					}
				}
			}
			return Out;
		}
		Out.bHit = true;
		Out.bStartPenetrating = H.bStartPenetrating;
		Out.PenetrationDepth = H.bStartPenetrating ? H.PenetrationDepth / M2CM : 0.0;
		Out.CenterDist = H.Distance / M2CM;
		Out.Point = ToM(H.ImpactPoint);
		Out.Dist = (Out.Point - s).Dot(d);
		const FVector Normal = H.bStartPenetrating ? H.Normal.GetSafeNormal() : H.ImpactNormal.GetSafeNormal();
		Out.Normal = V3(Normal.X, Normal.Y, Normal.Z);
		return Out;
	};
}

void ARoadsterPawn::StepSimulation(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RoadsterSimulation);
	CSV_SCOPED_TIMING_STAT(Roadster, Simulation);
	if (bSimPaused) return;
	const double h = 0.001;
	Accumulator += FMath::Min(DeltaSeconds, 0.05f) * TimeScale;
	const TraceFn Trace = MakeTrace();
	auto* PC = Cast<ARoadsterController>(GetController());
	int32 Steps = 0;
	while (Accumulator >= h && Steps < 60)
	{
		if (PC)
		{
			PC->ApplyPhysicsPreset(h);
			Input.bPilotStunt = PC->IsStuntActive() || PC->IsReplaying();
		}
		Sim.Step(h, Input, Trace);
		Accumulator -= h;
		if (DebugAfterReset >= 0.0)
		{
			DebugAfterReset += h;
			if (FMath::Fmod(DebugAfterReset, 0.02) < h)
			{
				const auto& W = Sim.Wheels;
				UE_LOG(LogTemp, Display, TEXT("RDBG t=%.3f up=%.2f wz=(%.0f %.0f %.0f %.0f) c=%d body=%d fan=%.0f thr=%.0f w=(%.2f,%.2f,%.2f) z=%.3f"), DebugAfterReset, Sim.Q.Up().z,
					W[0].Load, W[1].Load, W[2].Load, W[3].Load, Sim.T.WheelsInContact, Sim.T.bBodyContact ? 1 : 0, Sim.T.FanDownforceN, Sim.T.ThrustN,
					Sim.W.x, Sim.W.y, Sim.W.z, Sim.BodyOriginWorld().z);
			}
			if (DebugAfterReset > 0.6) DebugAfterReset = -1.0;
		}
		++Steps;
	}
	if (Steps == 60) Accumulator = 0;
	// fell out of the world
	if (!Sim.Set.bRoadstarMode && Sim.X.z < -200.0) ResetToTrack(false);
	SetActorTransform(BodyTransform(), false, nullptr, ETeleportType::TeleportPhysics);
}

void ARoadsterPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StepSimulation(DeltaSeconds);
	UpdateVisuals(DeltaSeconds);
	if (!(RoadsterDisableMask() & 4)) UpdateSmoke(DeltaSeconds);
	UpdateAudio(DeltaSeconds);
	EarthVisual->SetVisibility(!bPlanetXVisualActive && Sim.Set.bRoadstarMode && Sim.T.AltitudeM > 1000.0);
	UpdateLaps(DeltaSeconds);
	UpdateCamera(DeltaSeconds);

	// screens at 30 Hz, the x-ray car view at 15 Hz
	ScreenTimer += DeltaSeconds;
	if (ScreenTimer > 1.f / 30.f && !(RoadsterDisableMask() & 1))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RoadsterScreens);
		CSV_SCOPED_TIMING_STAT(Roadster, Screens);
		ScreenTimer = 0.f;
		if (ScreenRT) ScreenRT->FastUpdateResource();
		if (ForwardRT_A) ForwardRT_A->FastUpdateResource();
		if (ForwardRT_B) ForwardRT_B->FastUpdateResource();
	}
	CaptureTimer += DeltaSeconds;
	const ARoadsterController* ScreenController = Cast<ARoadsterController>(GetController());
	const bool bCarViewVisible = IsInterior() || (ScreenController && ScreenController->Mode == EUIMode::Touchscreen);
	if (CaptureTimer > 1.f / 15.f && Interface && Interface->WantsCarView() && (bCarViewVisible || !bCarPreviewCaptured) && !(RoadsterDisableMask() & 2))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RoadsterCarPreview);
		CSV_SCOPED_TIMING_STAT(Roadster, CarPreview);
		CaptureTimer = 0.f;
		const FRotator View = Interface->CarViewRotation();
		const FVector Dir = View.Vector();
		CarCapture->SetRelativeLocation(B2U(-0.1, 0, 0.5) - Dir * 1450.f);
		CarCapture->SetRelativeRotation(View);
		CarCapture->CaptureScene();
		bCarPreviewCaptured = true;
	}
}

void ARoadsterPawn::UpdateSuspension(FTransform OutUpright[4])
{
	using namespace SuspensionRig;
	auto Seg = [](const float* Xs, int32 N, float X, int32& I, float& A)
	{
		X = FMath::Clamp(X, Xs[0], Xs[N - 1]);
		I = 0;
		while (I < N - 2 && X > Xs[I + 1]) ++I;
		A = (X - Xs[I]) / FMath::Max(Xs[I + 1] - Xs[I], 1e-6f);
	};
	auto V = [](const float* P) { return FVector(P[0], P[1], P[2]); };

	FVector Pts[4][NumPoints];
	FVector TOc[4];
	float SpringS[4], RackY[4], Travel[4];
	int32 TI[4]; float TA[4];
	for (int32 c = 0; c < 4; ++c)
	{
		const FCornerTables& T = Corners[c];
		const WheelState& W = Sim.Wheels[c];
		Travel[c] = float(W.Compression - Sim.S.StaticCompression) * M2CM;   // + = bump
		Seg(T.TravelCm, NumTravel, Travel[c], TI[c], TA[c]);
		const int32 i = TI[c]; const float a = TA[c];
		for (int32 k = 0; k < NumPoints; ++k)
			Pts[c][k] = FMath::Lerp(V(T.Points[k][i]), V(T.Points[k][i + 1]), a);
		SpringS[c] = FMath::Lerp(T.SpringScale[i], T.SpringScale[i + 1], a);
		FVector WC = FMath::Lerp(V(T.WheelCentre[i]), V(T.WheelCentre[i + 1]), a);
		FQuat Up = FQuat::Slerp(FQuat(T.UprightQuat[i][0], T.UprightQuat[i][1], T.UprightQuat[i][2], T.UprightQuat[i][3]),
			FQuat(T.UprightQuat[i + 1][0], T.UprightQuat[i + 1][1], T.UprightQuat[i + 1][2], T.UprightQuat[i + 1][3]), a);
		TOc[c] = Pts[c][Pt_TO];
		RackY[c] = 0.f;
		if (T.bFront && T.SteerCount > 1)
		{
			// road-wheel angle from the sim (Unreal sign: + = right) -> table sign (+ = nose left)
			const float SteerDeg = -FMath::RadiansToDegrees(float(W.Steer));
			int32 si; float sa, si0a; int32 si0;
			Seg(T.SteerDeg, T.SteerCount, SteerDeg, si, sa);
			Seg(T.SteerDeg, T.SteerCount, 0.f, si0, si0a);
			const float Kp = FMath::Lerp(T.KingpinDeg[si], T.KingpinDeg[si + 1], sa);
			RackY[c] = FMath::Lerp(T.RackYCm[si], T.RackYCm[si + 1], sa);
			// steering swings the upright about the current kingpin axis (lower -> upper ball joint)
			const FVector LBJ = Pts[c][Pt_LBJ];
			const FVector Axis = (Pts[c][Pt_UBJ] - LBJ).GetSafeNormal();
			const FQuat Q(Axis, -FMath::DegreesToRadians(Kp));
			WC = LBJ + Q.RotateVector(WC - LBJ);
			Up = Q * Up;
			const FVector TOs = FMath::Lerp(V(T.SteerTO[si]), V(T.SteerTO[si + 1]), sa);
			const FVector TO0 = FMath::Lerp(V(T.SteerTO[si0]), V(T.SteerTO[si0 + 1]), si0a);
			TOc[c] = Pts[c][Pt_TO] + (TOs - TO0);
		}
		OutUpright[c] = FTransform(Up, WC);
	}

	FQuat DamperRot[4] = { FQuat::Identity, FQuat::Identity, FQuat::Identity, FQuat::Identity };
	FVector DamperPivot[4];
	for (int32 p = 0; p < NumParts; ++p)
	{
		const FPart& Part = Parts[p];
		const int32 c = Part.Corner;
		if (c < 0 || Part.Joint == Static) continue;
		UStaticMeshComponent* C = SuspParts[p];
		const FVector Pivot0 = V(Part.Pivot);
		const FVector SDir = V(Part.StaticDir).GetSafeNormal();
		switch (Part.Joint)
		{
		case Hinge:
		{
			const FCornerTables& T = Corners[c];
			const float Deg = FMath::Lerp(T.AngleDeg[Part.AngleTable][TI[c]], T.AngleDeg[Part.AngleTable][TI[c] + 1], TA[c]);
			C->SetRelativeLocationAndRotation(Pivot0, FQuat(V(Part.Axis).GetSafeNormal(), -FMath::DegreesToRadians(Deg)));
			break;
		}
		case Rigid:
			C->SetRelativeLocationAndRotation(OutUpright[c].GetLocation(), OutUpright[c].GetRotation());
			break;
		case Aim:
		case AimSpin:
		case AimScale:
		{
			FVector Pivot = Part.PivotPoint >= 0 ? Pts[c][Part.PivotPoint] : Pivot0;
			if (Part.bRack) Pivot.Y += RackY[c];
			if (Part.bFollowDamper) Pivot = DamperPivot[c] + DamperRot[c].RotateVector(Pivot0 - DamperPivot[c]);
			const FVector Target = Part.TargetPoint == Pt_TO ? TOc[c] : (Part.TargetPoint >= 0 ? Pts[c][Part.TargetPoint] : V(Part.TargetFixed));
			FQuat Rot = FQuat::FindBetweenNormals(SDir, (Target - Pivot).GetSafeNormal());
			if (Part.Joint == AimSpin) Rot = Rot * FQuat(SDir, float(Sim.Wheels[c].Spin) * FMath::Sign(SDir.Y));
			C->SetRelativeLocationAndRotation(Pivot, Rot);
			if (Part.Joint == AimScale) C->SetRelativeScale3D(FVector(1.f, SpringS[c], 1.f));
			if (FCStringAnsi::Strstr(Part.Name, "_Damper") && !FCStringAnsi::Strstr(Part.Name, "DamperRod"))
			{
				DamperRot[c] = Rot; DamperPivot[c] = Pivot0;
			}
			break;
		}
		default: break;
		}
	}
}

void ARoadsterPawn::UpdateVisuals(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RoadsterVisuals);
	CSV_SCOPED_TIMING_STAT(Roadster, Visuals);
	const Spec& S = Sim.S;
	// suspension first: the uprights carry the wheels and brakes (camber, toe, kingpin steering)
	FTransform Upright[4];
	UpdateSuspension(Upright);
	for (int32 i = 0; i < 4; ++i)
	{
		const WheelState& W = Sim.Wheels[i];
		const bool bRight = W.Hardpoint.y > 0;   // physics frame: +Y right
		const FVector Centre = Upright[i].GetLocation();
		const FQuat Hub = Upright[i].GetRotation();
		const FQuat Spin(FVector::RightVector, W.Spin);
		// tyre/rim: left template turned 180 deg on the right so the sidewall lettering reads correctly
		Wheels[i]->SetRelativeLocationAndRotation(Centre, Hub * Spin * (bRight ? FQuat(FVector::UpVector, PI) : FQuat::Identity));
		// discs and calipers already contain their axial offset; mirror them on the right so the calipers trail
		const FVector Mirror(1.f, bRight ? -1.f : 1.f, 1.f);
		Discs[i]->SetRelativeLocationAndRotation(Centre, Hub * Spin);   // scale is applied before rotation: same spin both sides
		Discs[i]->SetRelativeScale3D(Mirror);
		Calipers[i]->SetRelativeLocationAndRotation(Centre, Hub);
		Calipers[i]->SetRelativeScale3D(Mirror);
	}
	// fans spin about X at the simulated rpm (visual rate capped to avoid strobing)
	const float FanDeg = FMath::Min(Sim.FanRPM, 900.0) * 6.0 * DeltaSeconds;
	for (int32 i = 0; i < Fans.Num(); ++i)
	{
		const auto* Meta = RoadsterRig::FindFan(TCHAR_TO_ANSI(*FString::Printf(TEXT("Fan_%d"), i)));
		const FVector Axis = Meta ? FVector(Meta->AX, -Meta->AY, Meta->AZ).GetSafeNormal() : FVector::ForwardVector;
		Fans[i]->AddRelativeRotation(FQuat(Axis, -FMath::DegreesToRadians(FanDeg)));
	}

	// Generated Blender rest pivots and signed hinge angles are the single visual contract.
	// Reflection of Blender Y reverses the handedness of every angle.
	auto HingePose = [](const RoadsterRig::FHinge& H, float AngleDeg)
	{
		return FTransform(FQuat(FVector(H.AX, -H.AY, H.AZ).GetSafeNormal(), -FMath::DegreesToRadians(AngleDeg)), B2U(H.PX, H.PY, H.PZ));
	};
	const bool bVent = Sim.Set.bHoodVentOpen || Sim.Set.Aero == AeroPreset::HighDownforce || (Sim.Set.Aero == AeroPreset::Auto && FMath::Abs(Sim.T.ForwardSpeed) > 30.0);
	const bool bBarge = Sim.Set.Aero == AeroPreset::HighDownforce || Sim.Set.Aero == AeroPreset::Auto || (Sim.Set.Aero == AeroPreset::Custom && Sim.Set.bBargeDeployed);
	const bool bLightPower = Sim.Set.bInfiniteBattery || Sim.EnergyKWh > 1e-7;
	AutoHeadlightTimer -= DeltaSeconds;
	if (AutoHeadlightTimer <= 0.f)
	{
		AutoHeadlightTimer = 0.25f;
		bAutoHeadlights = true;
		// Auto observes actual occlusion toward the sun, so tunnels command lamps.
		for (TActorIterator<ADirectionalLight> Sun(GetWorld()); Sun; ++Sun)
		{
			const auto* Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
			if (!Light || !Light->IsVisible() || Light->Intensity < 3000.f) continue;
			const FVector Start = GetActorTransform().TransformPosition(B2U(0, 0, 1.2));
			FHitResult Hit;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(RoadsterAutomaticLamps), true, this);
			bAutoHeadlights = GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - Sun->GetActorForwardVector() * 200000.f, ECC_Visibility, Query);
			break;
		}
	}
	bHeadlightsRequested = ExteriorLights == 2 || (ExteriorLights == 3 && bAutoHeadlights);
	const float PreviousCover = HeadlightOpenFraction;
	// EST 0.4s electric actuator; a depleted battery leaves the cover at its last position.
	if (bLightPower && !bSimPaused) HeadlightOpenFraction = FMath::FInterpConstantTo(HeadlightOpenFraction, bHeadlightsRequested ? 1.f : 0.f, FMath::Min(DeltaSeconds, .05f) * TimeScale, 2.5f);
	// Keep the doors latched once moving; this is an authored presentation actuator, not a crash model.
	if (Sim.T.Speed > 1.0) bDoorsOpen = false;
	DoorOpenFraction = FMath::FInterpConstantTo(DoorOpenFraction, bDoorsOpen ? 1.f : 0.f, DeltaSeconds, 0.65f);
	VentOpenFraction = FMath::FInterpConstantTo(VentOpenFraction, bVent ? 1.f : 0.f, DeltaSeconds, 2.f);
	BargeOpenFraction = FMath::FInterpConstantTo(BargeOpenFraction, bBarge ? 1.f : 0.f, DeltaSeconds, 1.8f);
	for (UStaticMeshComponent* Panel : AdditionalHingedParts)
	{
		const FString Name = Panel->GetName();
		if (const auto* H = RoadsterRig::FindHinge(TCHAR_TO_ANSI(*Name)))
		{
			float Angle = 0.f;
			if (Name.StartsWith(TEXT("Door"))) Angle = H->Open * DoorOpenFraction;
			else if (Name.StartsWith(TEXT("FrontWing"))) Angle = H->Mid * Sim.FrontWingAngle;
			else if (Name.StartsWith(TEXT("HeadlightCover"))) Angle = H->Open * HeadlightOpenFraction;
			Panel->SetRelativeTransform(HingePose(*H, Angle));
		}
	}
	if (const auto* H = RoadsterRig::FindHinge("Hood_Vent")) HoodVent->SetRelativeTransform(HingePose(*H, H->Open * VentOpenFraction));
	if (const auto* H = RoadsterRig::FindHinge("Barge_L")) BargeL->SetRelativeTransform(HingePose(*H, H->Open * BargeOpenFraction));
	if (const auto* H = RoadsterRig::FindHinge("Barge_R")) BargeR->SetRelativeTransform(HingePose(*H, H->Open * BargeOpenFraction));
	// The wing's zero pose is its authored stowed pocket. Lift and pitch use the generated travel.
	{
		const float A = FMath::Clamp((float)Sim.WingAngle, 0.f, 1.f);
		const float Deploy = FMath::Clamp(A / 0.45f, 0.f, 1.f);
		const float Pitch = A <= 0.45f ? RoadsterRig::WingPitchLowDrag * Deploy : FMath::Lerp((float)RoadsterRig::WingPitchLowDrag, (float)RoadsterRig::WingPitchHighDownforce, (A - 0.45f) / 0.55f);
		const FVector Pivot = B2U(RoadsterRig::WingPivot[0], RoadsterRig::WingPivot[1], RoadsterRig::WingPivot[2]);
		const FVector Lift = B2U(RoadsterRig::WingLift[0], RoadsterRig::WingLift[1], RoadsterRig::WingLift[2]) * Deploy;
		Wing->SetRelativeTransform(FTransform(FQuat(FVector::RightVector, FMath::DegreesToRadians(Pitch)), Pivot + Lift));
		WingStruts->SetRelativeLocation(Pivot + Lift);
	}
	for (UStaticMeshComponent* Panel : SkirtParts)
		if (const auto* P = RoadsterRig::FindPivot(TCHAR_TO_ANSI(*Panel->GetName())))
			Panel->SetRelativeLocation(B2U(P->X, P->Y, P->Z) - FVector(0, 0, RoadsterRig::SkirtReach * 100.0 * Sim.SkirtDeploy));
	// yoke: steer-by-wire, ~150 degrees lock to lock each way
	Yoke->SetRelativeTransform(FTransform(YokeBase.GetRotation() * FQuat(FVector::ForwardVector, -Input.Steer * FMath::DegreesToRadians(150.f)), YokeBase.GetLocation()));

	// lights
	const bool bBrake = Sim.T.BrakePedal > 0.05 || Sim.T.RegenKW > 60.0;
	if (TailMID) TailMID->SetScalarParameterValue(TEXT("Intensity"), bBrake ? 220.f : 60.f);
	BrakeGlow->SetIntensity(bBrake ? 120.f : 25.f);
	const bool bHeadOn = bLightPower && bHeadlightsRequested && HeadlightOpenFraction >= .98f;
	for (USpotLightComponent* L : Headlights) L->SetVisibility(bHeadOn);
	const bool bDRL = bLightPower && ExteriorLights != 0;
	if (HeadMID) HeadMID->SetScalarParameterValue(TEXT("Intensity"), bDRL ? 80.f : 0.f);
	// EST electrical loads:15W DRLs,120W headlamps,5W while the cover actuator moves.
	Sim.LightingPowerKW = bLightPower ? (bDRL ? .015 : 0.0) + (bHeadOn ? .120 : 0.0) + (HeadlightOpenFraction != PreviousCover ? .005 : 0.0) : 0.0;
	CabinGlow->SetVisibility(IsInterior());

	// thruster plumes
	auto Plume = [&](UStaticMeshComponent* P, float Frac, float Len, float Width)
	{
		const bool bOn = Frac > 0.01f;
		P->SetVisibility(bOn);
		if (bOn)
		{
			const float Flicker = 0.9f + 0.1f * FMath::Sin(GetWorld()->TimeSeconds * 90.f + P->GetComponentLocation().X);
			P->SetRelativeScale3D(FVector(Width, Width, Len * Frac * Flicker));
		}
	};
	// per-nozzle delivered thrust from the valve-level gas model (valves open/close over 25-40 ms, thrust decays
	// as the vessels blow down)
	const ColdGas::System& G = Sim.Gas;
	for (int32 k = 0; k < 4; ++k) Plume(PlumesRear[k], G.Nozzle(k).ThrustFrac, 2.6f, 0.30f);
	for (int32 k = 0; k < 4; ++k) Plume(PlumesLift[k], G.Nozzle(ColdGas::LiftFL + k).ThrustFrac, 2.2f, 0.28f);
	Plume(PlumesYaw[0], G.Nozzle(ColdGas::YawLeftFiring).ThrustFrac, 1.2f, 0.18f);    // jet exits to the left
	Plume(PlumesYaw[1], G.Nozzle(ColdGas::YawRightFiring).ThrustFrac, 1.2f, 0.18f);
	// plume cones are 100 cm tall with the pivot at the centre: shift so the apex sits at the nozzle exit
	for (int32 k = 0; k < 4; ++k)
		PlumesRear[k]->SetRelativeLocation(ToU(Sim.S.RearNozzlePos[k]) - FVector(PlumesRear[k]->GetRelativeScale3D().Z * 50.f + 3.f, 0, 0));
	for (int32 k = 0; k < 4; ++k)
		PlumesLift[k]->SetRelativeLocation(ToU(Sim.S.LiftNozzlePos[k]) - FVector(0, 0, PlumesLift[k]->GetRelativeScale3D().Z * 50.f + 2.f));
	PlumesYaw[0]->SetRelativeLocation(ToU(Sim.S.YawNozzlePos[0]) - FVector(0, PlumesYaw[0]->GetRelativeScale3D().Z * 50.f, 0));
	PlumesYaw[1]->SetRelativeLocation(ToU(Sim.S.YawNozzlePos[1]) + FVector(0, PlumesYaw[1]->GetRelativeScale3D().Z * 50.f, 0));	if (PlumeMID) PlumeMID->SetScalarParameterValue(TEXT("Time"), GetWorld()->TimeSeconds);
}

void ARoadsterPawn::EmitPuff(const FVector& Pos, const FVector& Vel, float Size, float Life, float Growth, float Alpha)
{
	if (Puffs.Num() > 600) return;
	Puffs.Add({ Pos, Vel, 0.f, Life, Size, Growth, Alpha });
}

void ARoadsterPawn::UpdateSmoke(float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RoadsterSmoke);
	CSV_SCOPED_TIMING_STAT(Roadster, Smoke);
	const FTransform T = GetActorTransform();
	const FVector CarVel = ToU(Sim.V);
	// tire smoke from sliding rear tires, dust/vapour from the fans and thrusters
	for (int32 i = 0; i < 4; ++i)
	{
		const WheelState& W = Sim.Wheels[i];
		if (W.bContact && W.SlideSpeed > 4.0 && W.Load > 1500.0)
		{
			const float Rate = FMath::Clamp((W.SlideSpeed - 4.0) / 12.0, 0.0, 1.0);
			if (FMath::FRand() < Rate * 0.8f)
				EmitPuff(ToU(W.ContactPoint) + FMath::VRand() * 15.f, CarVel * 0.3f + FMath::VRand() * 150.f, 60.f, 2.2f, 240.f, 0.35f * Rate);
		}
	}
	// cold nitrogen leaves the nozzles at 40-100 K: the jets condense into white vapour
	for (int32 k = 0; k < 4; ++k)
	{
		const float F = Sim.Gas.Nozzle(k).ThrustFrac;
		if (F > 0.05f && FMath::FRand() < 0.9f)
			EmitPuff(T.TransformPosition(ToU(Sim.S.RearNozzlePos[k]) - FVector(15, 0, 0)), CarVel * 0.2f - T.GetUnitAxis(EAxis::X) * (3000.f * F) + FMath::VRand() * 200.f, 50.f, 1.6f, 500.f, 0.55f);
	}
	for (int32 k = 0; k < 4; ++k)
	{
		const float F = Sim.Gas.Nozzle(ColdGas::LiftFL + k).ThrustFrac;
		if (F > 0.05f && FMath::FRand() < 0.85f)
			EmitPuff(T.TransformPosition(ToU(Sim.S.LiftNozzlePos[k]) - FVector(0, 0, 10)), CarVel * 0.2f - T.GetUnitAxis(EAxis::Z) * (2500.f * F) + FMath::VRand() * 250.f, 45.f, 1.8f, 450.f, 0.5f);
	}
	for (int32 s = 0; s < 2; ++s)
	{
		const float F = Sim.Gas.Nozzle(ColdGas::YawLeftFiring + s).ThrustFrac;
		if (F > 0.05f && FMath::FRand() < 0.7f)
			EmitPuff(T.TransformPosition(ToU(Sim.S.YawNozzlePos[s])), CarVel * 0.2f + T.GetUnitAxis(EAxis::Y) * (s == 0 ? -1800.f : 1800.f) * F + FMath::VRand() * 150.f, 25.f, 1.0f, 260.f, 0.45f);
	}
	if (Sim.T.FanPowerKW > 20.0 && Sim.T.Speed < 25.0 && FMath::FRand() < 0.35f)
		EmitPuff(T.TransformPosition(B2U(-2.25, FMath::FRandRange(-0.6f, 0.6f), 0.15)), CarVel * 0.5f - T.GetUnitAxis(EAxis::X) * 900.f + FMath::VRand() * 120.f, 40.f, 1.4f, 220.f, 0.12f);

	Smoke->ClearInstances();
	for (int32 i = Puffs.Num() - 1; i >= 0; --i)
	{
		FPuff& P = Puffs[i];
		P.Age += DeltaSeconds;
		if (P.Age > P.Life) { Puffs.RemoveAtSwap(i); continue; }
		P.Vel *= FMath::Exp(-2.2f * DeltaSeconds);
		P.Vel.Z += 35.f * DeltaSeconds;
		P.Pos += P.Vel * DeltaSeconds;
		const float S = (P.Size + P.Growth * P.Age) / 100.f;
		const float t = P.Age / P.Life;
		const float A = P.Alpha * FMath::Min(1.f, t * 8.f) * (1.f - t);
		const int32 Idx = Smoke->AddInstance(FTransform(FRotator::ZeroRotator, P.Pos, FVector(S)), true);
		Smoke->SetCustomDataValue(Idx, 0, A, false);
		Smoke->SetCustomDataValue(Idx, 1, (float)(i % 16), false);
	}
	Smoke->MarkRenderStateDirty();
}

void ARoadsterPawn::UpdateAudio(float DeltaSeconds)
{
	const Telemetry& T = Sim.T;
	const float Rpm = FMath::Abs(T.MotorRPM);
	const float Load = FMath::Clamp(FMath::Abs(T.MotorPowerKW) / 900.f + FMath::Abs(T.RegenKW) / 400.f, 0.f, 1.f);
	if (AudioMotor) { AudioMotor->SetPitchMultiplier(FMath::Clamp(0.25f + Rpm / 9000.f, 0.25f, 3.5f)); AudioMotor->SetVolumeMultiplier(0.08f + 0.6f * Load + FMath::Min(Rpm / 25000.f, 0.25f)); }
	if (AudioFan) { AudioFan->SetPitchMultiplier(FMath::Clamp(0.3f + T.FanRPM / 9000.f, 0.3f, 2.2f)); AudioFan->SetVolumeMultiplier(FMath::Clamp(T.FanRPM / Sim.S.FanMaxRPM, 0.0, 1.0) * 0.8f); }
	if (AudioThrust)
	{
		const float Th = FMath::Clamp(T.ThrustN / 40000.f, 0.0, 1.0);
		AudioThrust->SetVolumeMultiplier(Th * 1.6f);
		AudioThrust->SetPitchMultiplier(0.8f + 0.4f * Th);
	}
	if (AudioWind) { AudioWind->SetVolumeMultiplier(FMath::Clamp(T.Speed * T.Speed / 6000.f * T.AirDensityKgM3 / 1.2, 0.0, 1.4) * (IsInterior() ? 0.50f : 1.f)); AudioWind->SetPitchMultiplier(FMath::Clamp(0.7f + T.Speed / 160.f, 0.7f, 3.f)); }
	float Slide = 0.f;
	for (const WheelState& W : Sim.Wheels) if (W.bContact) Slide = FMath::Max(Slide, (float)FMath::Clamp((W.SlideSpeed - 2.0) / 10.0, 0.0, 1.0) * (float)FMath::Clamp(W.Load / 5000.0, 0.0, 1.0));
	if (AudioTire) { AudioTire->SetVolumeMultiplier(Slide * 0.9f); AudioTire->SetPitchMultiplier(0.85f + Slide * 0.3f); }
}

void ARoadsterPawn::UpdateLaps(float DeltaSeconds)
{
	if (!bTrackMode) return;
	LapTime += DeltaSeconds * TimeScale;
	if (ATrackActor* Track = FindTrack())
	{
		if (Track->CrossedStartLine(GetActorLocation()))
		{
			if (LapTime > 5.f)
			{
				LastLap = LapTime;
				BestLap = BestLap < 0 ? LapTime : FMath::Min(BestLap, LapTime);
				LapHistory.Insert(LapTime, 0);
				if (LapHistory.Num() > 10) LapHistory.SetNum(10);
				++LapCount;
			}
			LapTime = 0.f;
		}
	}
}

void ARoadsterPawn::SetCameraMode(ERoadsterCamera NewMode)
{
	if (CameraMode == NewMode) return;
	CameraMode = NewMode;
	LookOffset = FRotator::ZeroRotator;
	bCamInit = false;
}

void ARoadsterPawn::CycleCamera()
{
	SetCameraMode((ERoadsterCamera)(((uint8)CameraMode + 1) % ((uint8)ERoadsterCamera::Cinematic + 1)));
}

void ARoadsterPawn::UpdateCamera(float DeltaSeconds)
{
	using namespace RoadsterCameraMotion;
	const FTransform T = GetActorTransform();
	const FVector Fwd = T.GetUnitAxis(EAxis::X), Up = T.GetUnitAxis(EAxis::Z);
	const FVector CarPos = T.GetLocation();
	const float Speed = Sim.T.Speed;
	const float Feel = FMath::Clamp(SpeedFeelIntensity, 0.f, 1.5f);
	const float Rush = FMath::Clamp(Speed / 85.f, 0.f, 1.f);
	const bool bResetView = ResetView(bCamInit, int(CameraPreviousMode), int(CameraMode), bPreviousExternalCam, bExternalCam);
	const bool bWasFallback = bCameraFallbackActive;
	const bool bWasObstructed = bCameraWasObstructed;
	if (bResetView)
	{
		CamUp = Up;
		CameraPreviousAnchor = CarPos;
		CameraFollowPos = CarPos;
		CameraClearDistance = 0.f;
		bCameraFallbackActive = false;
	}
	const float AccelTarget = float(Sim.T.AccelG.x);
	CameraAccelG = bResetView ? AccelTarget : FMath::Lerp(CameraAccelG, AccelTarget, float(Alpha(DeltaSeconds, 8.0)));
	float TargetFov = 70.f;
	bool bBoomObstructed = false;
	float RequestedBoomDistance = 0.f;
	auto LookRot = [](const FVector& Dir, const FVector& UpHint) { return FRotationMatrix::MakeFromXZ(Dir, UpHint).Rotator(); };
	auto CockpitPose = [&]()
	{
		const float LongLag = FMath::Clamp(CameraAccelG * 0.022f, -0.035f, 0.065f) * Feel;
		const float LatLag = FMath::Clamp(float(Sim.T.AccelG.y) * 0.012f, -0.025f, 0.025f) * Feel;
		CamPos = T.TransformPosition(B2U(-0.08 - LongLag, 0.38 + LatLag, 0.98));
		CamRot = (T.GetRotation() * FQuat(FRotator(-6.f + LookOffset.Pitch - CameraAccelG * 0.4f * Feel, LookOffset.Yaw, FMath::Clamp(float(Sim.T.AccelG.y), -2.f, 2.f) * 0.5f * Feel))).Rotator();
		CamUp = Up;
		TargetFov = 80.f + (Rush * 7.f + FMath::Clamp(CameraAccelG, 0.f, 3.f)) * Feel;
	};

	if (bExternalCam)
	{
		// The builder/photo free camera intentionally remains unrestricted.
		CamPos = ExtCamPos;
		CamRot = ExtCamRot;
		TargetFov = 70.f;
		bCameraFallbackActive = false;
	}
	else switch (CameraMode)
	{
	case ERoadsterCamera::Chase:
	case ERoadsterCamera::ChaseFar:
	{
		const bool bFar = CameraMode == ERoadsterCamera::ChaseFar;
		CamUp = FMath::VInterpNormalRotationTo(CamUp, Up, DeltaSeconds, 220.f);
		FVector Head = Fwd;
		if (Speed > 3.0) Head = FMath::Lerp(Fwd, ToU(Sim.V).GetSafeNormal(), 0.35f).GetSafeNormal();
		const FVector Back = FQuat(CamUp, FMath::DegreesToRadians(LookOffset.Yaw)).RotateVector(-Head);
		const float Dist = (bFar ? 980.f : 640.f) + FMath::Min(Speed, 160.f) * 1.1f + FMath::Clamp(CameraAccelG, -2.f, 4.f) * 12.f * Feel;
		const float Height = (bFar ? 300.f : 170.f) - Rush * 22.f * Feel + FMath::Clamp(LookOffset.Pitch, -30.f, 60.f) * 4.f;
		const FVector DesiredOffset = Back * Dist + CamUp * Height;
		// Carry all car translation immediately; smooth only the changing boom offset.
		// Keep this unobstructed state separate from the collision-resolved eye position.
		CameraFollowPos = bResetView ? CarPos + DesiredOffset : FollowRelative(CameraFollowPos, CameraPreviousAnchor, CarPos, DesiredOffset, DeltaSeconds);
		CamPos = CameraFollowPos;
		CamRot = LookRot((CarPos + CamUp * 80.f + Fwd * 120.f - CamPos).GetSafeNormal(), CamUp);
		TargetFov = 72.f + (Rush * 16.f + FMath::Clamp(CameraAccelG, 0.f, 3.f) * 1.4f) * Feel;
		break;
	}
	case ERoadsterCamera::Cockpit:
		CockpitPose();
		break;
	case ERoadsterCamera::Hood:
		CamPos = T.TransformPosition(B2U(1.75, 0, 0.95));
		CamRot = (T.GetRotation() * FQuat(FRotator(-3.f + LookOffset.Pitch, LookOffset.Yaw, 0))).Rotator();
		CamUp = Up;
		TargetFov = 85.f;
		break;
	case ERoadsterCamera::Wheel:
		CamPos = T.TransformPosition(B2U(-1.0, 1.25, 0.32));
		CamRot = (T.GetRotation() * FQuat(FRotator(2.f, -8.f + LookOffset.Yaw, 0))).Rotator();
		CamUp = Up;
		TargetFov = 90.f;
		break;
	case ERoadsterCamera::Orbit:
	{
		OrbitYaw += LookOffset.Yaw;
		OrbitPitch = FMath::Clamp(OrbitPitch + LookOffset.Pitch, -80.f, 10.f);
		LookOffset = FRotator::ZeroRotator;
		const FQuat Q = T.GetRotation() * FQuat(FRotator(OrbitPitch, OrbitYaw, 0));
		const FVector Centre = T.TransformPosition(B2U(0, 0, 0.6));
		CamPos = Centre - Q.GetForwardVector() * 720.f;
		CamRot = LookRot((Centre - CamPos).GetSafeNormal(), Up);
		CamUp = Up;
		TargetFov = 60.f;
		break;
	}
	case ERoadsterCamera::Cinematic:
	{
		CinematicTimer -= DeltaSeconds;
		if (bResetView || CinematicTimer <= 0.f || FVector::Dist(CinematicPos, CarPos) > 9000.f)
		{
			const FVector Vel = ToU(Sim.V);
			CinematicPos = CarPos + Vel * 2.2f + FVector::CrossProduct(Up, Fwd).GetSafeNormal() * FMath::FRandRange(600.f, 1100.f) * (FMath::RandBool() ? 1.f : -1.f) + Up * FMath::FRandRange(80.f, 400.f);
			CinematicTimer = 4.5f;
		}
		CamPos = CinematicPos;
		CamUp = FMath::Abs(Up.Z) > 0.3f ? FVector(0, 0, FMath::Sign(Up.Z)) : Up;
		CamRot = LookRot((CarPos + Up * 60.f - CamPos).GetSafeNormal(), CamUp);
		TargetFov = FMath::Clamp(3000.f / FMath::Max(300.f, float(FVector::Dist(CamPos, CarPos))) * 30.f, 12.f, 70.f);
		break;
	}
	}
	if (!bExternalCam && (CameraMode == ERoadsterCamera::Cockpit || CameraMode == ERoadsterCamera::Hood || CameraMode == ERoadsterCamera::Chase))
	{
		const float Contact = Sim.T.WheelsInContact > 0 ? 1.f : 0.15f;
		const float Micro = Rush * Rush * Contact * Feel;
		CamPos += Up * (FMath::Sin(float(Sim.SimTime) * 43.f) * 0.55f + FMath::Sin(float(Sim.SimTime) * 71.f) * 0.18f) * Micro;
	}

	const bool bPlayerBoom = !bExternalCam && (CameraMode == ERoadsterCamera::Chase || CameraMode == ERoadsterCamera::ChaseFar || CameraMode == ERoadsterCamera::Orbit || CameraMode == ERoadsterCamera::Cinematic);
	bCameraFallbackActive = false;
	if (bPlayerBoom)
	{
		FVector BoundsMin(-225.f, -105.f, 0.f), BoundsMax(225.f, 105.f, 120.f);
		if (Body && Body->GetStaticMesh()) Body->GetLocalBounds(BoundsMin, BoundsMax);
		const FVector Anchor = T.TransformPosition((BoundsMin + BoundsMax) * 0.5);
		// Enclose the car, plus probe/skin and a small hardware margin. Never pull the eye into the car.
		const float CarClearance = float((BoundsMax - BoundsMin).Size() * 0.5) + 25.f;
		const FVector Direction = (CamPos - Anchor).GetSafeNormal();
		RequestedBoomDistance = float(FVector::Dist(Anchor, CamPos));
		const FCameraBoomQuery Query = QueryCameraBoom(GetWorld(), this, Anchor, CamPos);
		bBoomObstructed = Query.bObstructed;
		bCameraFallbackActive = NeedsCockpit(Query.bStartPenetrating, Query.AllowedDistance, CarClearance);
		if (bCameraFallbackActive)
		{
			CameraClearDistance = Query.AllowedDistance;
			CockpitPose();
		}
		else
		{
			// Obstructions retract immediately; an opening boom releases smoothly. A cockpit
			// fallback exits with a cut, avoiding interpolation through seats/body panels.
			CameraClearDistance = float(ReleaseDistance(CameraClearDistance, Query.AllowedDistance, DeltaSeconds, bResetView, bWasFallback));
			CamPos = Anchor + Direction * CameraClearDistance;
			const FVector Aim = CameraMode == ERoadsterCamera::Chase || CameraMode == ERoadsterCamera::ChaseFar ? CarPos + CamUp * 80.f + Fwd * 120.f : CarPos + Up * 60.f;
			CamRot = LookRot((Aim - CamPos).GetSafeNormal(), CamUp);
		}
	}

	// Apply comfort consistently after any view change, rather than inheriting another mode's blur.
	Camera->PostProcessSettings.MotionBlurAmount = bExternalCam ? 0.f : FMath::Lerp(0.18f, 0.38f, Rush) * FMath::Min(Feel, 1.f);
	CamFov = bResetView || bWasFallback != bCameraFallbackActive ? TargetFov : FMath::Lerp(CamFov, TargetFov, float(Alpha(DeltaSeconds, 3.0)));
	if (bResetView || bWasFallback != bCameraFallbackActive)
	{
		Camera->NotifyCameraCut();
		if (auto* PC = Cast<APlayerController>(GetController()); PC && PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame();
	}
	Camera->SetWorldLocationAndRotation(CamPos, CamRot);
	Camera->SetFieldOfView(CamFov);
	const bool bCabin = !bExternalCam && (IsInterior() || bCameraFallbackActive);
	Camera->PostProcessSettings.bOverride_AutoExposureBias = bCabin;
	Camera->PostProcessSettings.AutoExposureBias = -0.9f;
	const int32 CameraLog = CVarRoadsterCameraDebug.GetValueOnGameThread();
	if (CameraLog > 1 || (CameraLog > 0 && (bResetView || bWasFallback != bCameraFallbackActive || bWasObstructed != bBoomObstructed)))
		UE_LOG(LogTemp, Display, TEXT("CAMERA mode=%d free=%d blocked=%d fallback=%d boom=%.2fm clear=%.2fm fov=%.2f eye=(%.2f,%.2f,%.2f)"), int(CameraMode), bExternalCam, bBoomObstructed, bCameraFallbackActive, RequestedBoomDistance / 100.f, CameraClearDistance / 100.f, CamFov, CamPos.X / 100.f, CamPos.Y / 100.f, CamPos.Z / 100.f);
	CameraPreviousAnchor = CarPos;
	CameraPreviousMode = CameraMode;
	bPreviousExternalCam = bExternalCam;
	bCameraWasObstructed = bBoomObstructed;
	bCamInit = true;
}

void ARoadsterPawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	Camera->GetCameraView(DeltaTime, OutResult);
	// Cockpit details sit within the engine's default 10cm near plane. A 1cm perspective
	// near plane retains nearby trim and the yoke when the driver looks around.
	OutResult.PerspectiveNearClipPlane = 1.0f;
}

void ARoadsterPawn::DrawScreen(UCanvas* Canvas, int32 Width, int32 Height)
{
	if (Interface) Interface->DrawCenterScreen(Canvas, FVector2D(0, 0), FVector2D(Width, Height), FVector2D(-1, -1), false, false, false);
}

void ARoadsterPawn::DrawForwardA(UCanvas* Canvas, int32 Width, int32 Height)
{
	if (Interface) Interface->DrawForwardDisplay(Canvas, Width, Height, 0);
}

void ARoadsterPawn::DrawForwardB(UCanvas* Canvas, int32 Width, int32 Height)
{
	if (Interface) Interface->DrawForwardDisplay(Canvas, Width, Height, 1);
}
