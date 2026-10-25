#include "RoadsterPawn.h"
#include "RoadsterInterface.h"
#include "TrackActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SpotLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/AudioComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundWave.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

using namespace Roadster;

// Feature switches for profiling: 1 screens, 2 car-view capture, 4 smoke, 8 HUD (bit set = disabled)
static TAutoConsoleVariable<int32> CVarRoadsterDisable(TEXT("roadster.Disable"), 0, TEXT("Bitmask: 1 screens, 2 car capture, 4 smoke, 8 hud"));
int32 RoadsterDisableMask() { return CVarRoadsterDisable.GetValueOnGameThread(); }
// Diagnostic logging: 0 off, 1 telemetry + resets (default with -autodrive), 2 verbose (autopilot, reset settle, respawn search)
static TAutoConsoleVariable<int32> CVarRoadsterLog(TEXT("roadster.Log"), 0, TEXT("0 off, 1 telemetry, 2 verbose"));
int32 RoadsterLogLevel() { return CVarRoadsterLog.GetValueOnGameThread(); }
void SetRoadsterLogLevel(int32 Level) { CVarRoadsterLog->Set(Level, ECVF_SetByCommandline); }

namespace
{
	constexpr float M2CM = 100.f;
	// Blender (x fwd, y LEFT, z up, metres) -> Unreal (x fwd, y RIGHT, z up, cm)
	FVector B2U(double x, double y, double z) { return FVector(x * M2CM, -y * M2CM, z * M2CM); }
	FVector ToU(const V3& v) { return FVector(v.x, v.y, v.z) * M2CM; }
	V3 ToM(const FVector& v) { return V3(v.X / M2CM, v.Y / M2CM, v.Z / M2CM); }
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
		TEXT("Interior"), TEXT("Seat_Driver"), TEXT("Screen_Bezel") })
		StaticParts.Add(AddPart(N));
	FrontLights = AddPart(TEXT("Front_Lights"));
	TailLights = AddPart(TEXT("Tail_Lights"));
	ThrustersRear = AddPart(TEXT("Thrusters_Rear"));
	ThrustersRocket = AddPart(TEXT("Thrusters_Rocket"));
	COPV = AddPart(TEXT("COPV_Rocket"));
	SeatPassenger = AddPart(TEXT("Seat_Passenger"));
	ScreenDisplay = AddPart(TEXT("Screen_Display"));
	ForwardA = AddPart(TEXT("Forward_Display_A"));
	ForwardB = AddPart(TEXT("Forward_Display_B"));

	// moving parts, positioned at their pivots (manifest.json)
	Wing = AddPart(TEXT("Rear_Wing"));       WingDeployed = FTransform(B2U(-2.10, 0, 0.93));
	WingStowed = FTransform(FRotator(-10.f, 0, 0), B2U(-2.06, 0, 0.70));
	HoodVent = AddPart(TEXT("Hood_Vent"));   VentClosed = FTransform(B2U(1.91, 0, 0.5797));
	BargeL = AddPart(TEXT("Barge_L"));       BargeLStowed = FTransform(B2U(0.83, 0.9506, 0.3309));
	BargeR = AddPart(TEXT("Barge_R"));       BargeRStowed = FTransform(B2U(0.83, -0.9506, 0.3309));
	Skirts = AddPart(TEXT("Skirts"));        SkirtsUp = FTransform(B2U(0, 0, 0.112));
	Yoke = AddPart(TEXT("Yoke"));            YokeBase = FTransform(FRotator(-24.f, 0, 0), B2U(0.62, 0.38, 0.72));
	for (int32 k = 0; k < 4; ++k)
	{
		UStaticMeshComponent* F = AddPart(*FString::Printf(TEXT("Fan_%d"), k), nullptr, *FString::Printf(TEXT("Fan%d"), k));
		F->SetRelativeLocation(B2U(-2.05, (k - 1.5) * 1.26 / 4, 0.284));
		Fans.Add(F);
	}
	for (int32 i = 0; i < 4; ++i)
	{
		const bool bFront = i < 2;
		Wheels.Add(AddPart(bFront ? TEXT("Wheel_Front") : TEXT("Wheel_Rear"), nullptr, *FString::Printf(TEXT("Wheel%d"), i)));
		Discs.Add(AddPart(bFront ? TEXT("Brake_Front_Disc") : TEXT("Brake_Rear_Disc"), nullptr, *FString::Printf(TEXT("Disc%d"), i)));
		Calipers.Add(AddPart(bFront ? TEXT("Brake_Front_Caliper") : TEXT("Brake_Rear_Caliper"), nullptr, *FString::Printf(TEXT("Caliper%d"), i)));
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
	for (int32 k = 0; k < 4; ++k)   // rear bank, pointing backwards (cone apex at the nozzle)
		PlumesRear.Add(MakePlume(*FString::Printf(TEXT("PlumeRear%d"), k), B2U(-2.25, (k - 1.5) * 0.25, 0.545), FRotator(90.f, 0, 0)));
	const FVector LiftPos[4] = { B2U(0.9325, 0.80, 0.06), B2U(0.9325, -0.80, 0.06), B2U(-0.9125, 0.82, 0.06), B2U(-0.9125, -0.82, 0.06) };
	for (int32 k = 0; k < 4; ++k)
		PlumesLift.Add(MakePlume(*FString::Printf(TEXT("PlumeLift%d"), k), LiftPos[k], FRotator(0, 0, 0)));
	PlumesYaw.Add(MakePlume(TEXT("PlumeYawL"), B2U(2.13, 0.46, 0.17), FRotator(0, 0, 90.f)));
	PlumesYaw.Add(MakePlume(TEXT("PlumeYawR"), B2U(2.13, -0.46, 0.17), FRotator(0, 0, -90.f)));

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
	Camera->PostProcessSettings.SceneFringeIntensity = 0.25f;
	Camera->PostProcessSettings.bOverride_VignetteIntensity = true;
	Camera->PostProcessSettings.VignetteIntensity = 0.32f;

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
		L->SetRelativeLocation(B2U(2.05, s ? -0.68 : 0.68, 0.56));
		L->SetRelativeRotation(FRotator(-3.f, s ? 3.f : -3.f, 0));
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
	CarCapture->PostProcessSettings.AutoExposureBias = 11.f;
	TArray<UActorComponent*> Comps;
	GetComponents(UStaticMeshComponent::StaticClass(), Comps);
	for (UActorComponent* C : Comps)
	{
		UStaticMeshComponent* S = Cast<UStaticMeshComponent>(C);
		if (S && !PlumesRear.Contains(S) && !PlumesLift.Contains(S) && !PlumesYaw.Contains(S) && S != Smoke)
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

void ARoadsterPawn::ApplyTrimVisibility()
{
	ThrustersRear->SetVisibility(Sim.S.bHasRearThrusters);
	ThrustersRocket->SetVisibility(Sim.S.bHasLiftThrusters);
	COPV->SetVisibility(Sim.S.TrimLevel == Trim::Rocket);
	SeatPassenger->SetVisibility(Sim.S.Seats == 2);
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
	return [W, Params, Obj](const V3& s, const V3& d, double len, double r) -> Hit
	{
		Hit Out;
		const FVector Start = ToU(s);
		const FVector End = Start + FVector(d.x, d.y, d.z) * (len * M2CM);
		FHitResult H;
		bool bHit;
		if (r <= 1e-4) bHit = W->LineTraceSingleByObjectType(H, Start, End, Obj, Params);
		else bHit = W->SweepSingleByObjectType(H, Start, End, FQuat::Identity, Obj, FCollisionShape::MakeSphere(r * M2CM), Params);
		if (!bHit || H.bStartPenetrating) return Out;
		Out.bHit = true;
		Out.CenterDist = H.Distance / M2CM;
		Out.Point = ToM(H.ImpactPoint);
		Out.Dist = (Out.Point - s).Dot(d);
		Out.Normal = V3(H.ImpactNormal.X, H.ImpactNormal.Y, H.ImpactNormal.Z);
		return Out;
	};
}

void ARoadsterPawn::StepSimulation(float DeltaSeconds)
{
	if (bSimPaused) return;
	const double h = 0.001;
	Accumulator += FMath::Min(DeltaSeconds, 0.05f) * TimeScale;
	const TraceFn Trace = MakeTrace();
	int32 Steps = 0;
	while (Accumulator >= h && Steps < 60)
	{
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
	if (Sim.X.z < -200.0) ResetToTrack(false);
	SetActorTransform(BodyTransform(), false, nullptr, ETeleportType::TeleportPhysics);
}

void ARoadsterPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StepSimulation(DeltaSeconds);
	UpdateVisuals(DeltaSeconds);
	if (!(RoadsterDisableMask() & 4)) UpdateSmoke(DeltaSeconds);
	UpdateAudio(DeltaSeconds);
	UpdateLaps(DeltaSeconds);
	UpdateCamera(DeltaSeconds);

	// screens at 30 Hz, the x-ray car view at 15 Hz
	ScreenTimer += DeltaSeconds;
	if (ScreenTimer > 1.f / 30.f && !(RoadsterDisableMask() & 1))
	{
		ScreenTimer = 0.f;
		if (ScreenRT) ScreenRT->UpdateResource();
		if (ForwardRT_A) ForwardRT_A->UpdateResource();
		if (ForwardRT_B) ForwardRT_B->UpdateResource();
	}
	CaptureTimer += DeltaSeconds;
	if (CaptureTimer > 1.f / 15.f && Interface && Interface->WantsCarView() && !(RoadsterDisableMask() & 2))
	{
		CaptureTimer = 0.f;
		const FRotator View = Interface->CarViewRotation();
		const FVector Dir = View.Vector();
		CarCapture->SetRelativeLocation(B2U(-0.1, 0, 0.5) - Dir * 1450.f);
		CarCapture->SetRelativeRotation(View);
		CarCapture->CaptureScene();
	}
}

void ARoadsterPawn::UpdateVisuals(float DeltaSeconds)
{
	const Spec& S = Sim.S;
	// wheels / brakes: hardpoint - up * suspension length, steer about Z, spin about Y
	for (int32 i = 0; i < 4; ++i)
	{
		const WheelState& W = Sim.Wheels[i];
		const FVector Centre = FVector(W.Hardpoint.x, W.Hardpoint.y, W.Hardpoint.z - W.Length) * M2CM;
		const bool bRight = W.Hardpoint.y > 0;   // physics frame: +Y right
		const FQuat Flip = bRight ? FQuat(FVector::UpVector, PI) : FQuat::Identity;
		const FQuat Steer(FVector::UpVector, W.Steer);
		const FQuat Spin(FVector::RightVector, W.Spin);
		Wheels[i]->SetRelativeLocationAndRotation(Centre, Steer * Spin * Flip);
		const FVector In = Steer.RotateVector(FVector(0, bRight ? -7.f : 7.f, 0));
		Discs[i]->SetRelativeLocationAndRotation(Centre + In, Steer * Spin * Flip);
		Calipers[i]->SetRelativeLocationAndRotation(Centre + In, Steer * Flip);
	}
	// fans spin about X at the simulated rpm (visual rate capped to avoid strobing)
	const float FanDeg = FMath::Min(Sim.FanRPM, 900.0) * 6.0 * DeltaSeconds;
	for (UStaticMeshComponent* F : Fans) F->AddRelativeRotation(FRotator(0, 0, FanDeg));

	// wing: blend stowed -> deployed by WingAngle
	{
		const float a = Sim.WingAngle;
		FTransform T;
		T.Blend(WingStowed, WingDeployed, FMath::Clamp(a, 0.f, 1.f));
		// low-drag sits at a shallower angle of attack
		T.ConcatenateRotation(FQuat(FRotator(FMath::Lerp(-6.f, 0.f, a), 0, 0)));
		Wing->SetRelativeTransform(T);
	}
	// hood vent hinges at its front edge
	{
		const float Open = Sim.T.Speed > 0 ? (Sim.Set.bHoodVentOpen || (Sim.Set.Aero == AeroPreset::Auto && FMath::Abs(Sim.T.ForwardSpeed) > 30.f) || Sim.Set.Aero == AeroPreset::HighDownforce ? 1.f : 0.f) : (Sim.Set.bHoodVentOpen ? 1.f : 0.f);
		static float VentAngle = 0.f;
		VentAngle = FMath::FInterpTo(VentAngle, Open * 9.f, DeltaSeconds, 4.f);
		HoodVent->SetRelativeTransform(FTransform(FRotator(-VentAngle, 0, 0), VentClosed.GetLocation()));
	}
	// barge boards swing out
	{
		const bool bDeploy = Sim.Set.Aero == AeroPreset::LowDrag ? false : (Sim.Set.Aero == AeroPreset::Custom ? Sim.Set.bBargeDeployed : true);
		static float B = 1.f;
		B = FMath::FInterpTo(B, bDeploy ? 1.f : 0.f, DeltaSeconds, 5.f);
		BargeL->SetRelativeTransform(FTransform(FRotator(0, -25.f * B, 0), BargeLStowed.GetLocation() + FVector(10.f * B, -4.f * B, 0)));
		BargeR->SetRelativeTransform(FTransform(FRotator(0, 25.f * B, 0), BargeRStowed.GetLocation() + FVector(10.f * B, 4.f * B, 0)));
	}
	// skirts drop with deployment
	Skirts->SetRelativeLocation(SkirtsUp.GetLocation() - FVector(0, 0, 6.5f * Sim.SkirtDeploy));
	// yoke: steer-by-wire, ~150 degrees lock to lock each way
	Yoke->SetRelativeTransform(FTransform(YokeBase.GetRotation() * FQuat(FVector::ForwardVector, -Input.Steer * FMath::DegreesToRadians(150.f)), YokeBase.GetLocation()));

	// lights
	const bool bBrake = Sim.T.BrakePedal > 0.05 || Sim.T.RegenKW > 60.0;
	if (TailMID) TailMID->SetScalarParameterValue(TEXT("Intensity"), bBrake ? 220.f : 60.f);
	BrakeGlow->SetIntensity(bBrake ? 120.f : 25.f);
	const bool bHeadOn = ExteriorLights == 2 || ExteriorLights == 3;
	for (USpotLightComponent* L : Headlights) L->SetVisibility(bHeadOn);
	if (HeadMID) HeadMID->SetScalarParameterValue(TEXT("Intensity"), ExteriorLights == 0 ? 0.f : 80.f);
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
	for (int32 k = 0; k < 4; ++k) Plume(PlumesRear[k], Sim.T.ThrustRear, 2.6f, 0.30f);
	for (int32 k = 0; k < 4; ++k) Plume(PlumesLift[k], Sim.T.ThrustCorner[k], 2.2f, 0.28f);
	Plume(PlumesYaw[0], Sim.T.ThrustYawR, 1.2f, 0.18f);   // nose pushed right -> jet exits left
	Plume(PlumesYaw[1], Sim.T.ThrustYawL, 1.2f, 0.18f);
	// plume cones are 100 cm tall with the pivot at the centre: shift so the apex sits at the nozzle
	for (int32 k = 0; k < 4; ++k)
		PlumesRear[k]->SetRelativeLocation(B2U(-2.15, (k - 1.5) * 0.25, 0.545) - FVector(PlumesRear[k]->GetRelativeScale3D().Z * 50.f, 0, 0));
	const FVector LiftBase[4] = { B2U(0.9325, 0.80, 0.06), B2U(0.9325, -0.80, 0.06), B2U(-0.9125, 0.82, 0.06), B2U(-0.9125, -0.82, 0.06) };
	for (int32 k = 0; k < 4; ++k)
		PlumesLift[k]->SetRelativeLocation(LiftBase[k] - FVector(0, 0, PlumesLift[k]->GetRelativeScale3D().Z * 50.f));
	if (PlumeMID) PlumeMID->SetScalarParameterValue(TEXT("Time"), GetWorld()->TimeSeconds);
}

void ARoadsterPawn::EmitPuff(const FVector& Pos, const FVector& Vel, float Size, float Life, float Growth, float Alpha)
{
	if (Puffs.Num() > 600) return;
	Puffs.Add({ Pos, Vel, 0.f, Life, Size, Growth, Alpha });
}

void ARoadsterPawn::UpdateSmoke(float DeltaSeconds)
{
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
	if (Sim.T.ThrustRear > 0.05)
		for (int32 k = 0; k < 4; ++k)
			if (FMath::FRand() < 0.9f)
			{
				const FVector Nozzle = T.TransformPosition(B2U(-2.3, (k - 1.5) * 0.25, 0.545));
				EmitPuff(Nozzle, CarVel * 0.2f - T.GetUnitAxis(EAxis::X) * (3000.f * Sim.T.ThrustRear) + FMath::VRand() * 200.f, 50.f, 1.6f, 500.f, 0.55f);
			}
	for (int32 k = 0; k < 4; ++k)
		if (Sim.T.ThrustCorner[k] > 0.05 && FMath::FRand() < 0.85f)
		{
			const FVector Lift[4] = { B2U(0.9325, 0.80, 0.0), B2U(0.9325, -0.80, 0.0), B2U(-0.9125, 0.82, 0.0), B2U(-0.9125, -0.82, 0.0) };
			EmitPuff(T.TransformPosition(Lift[k]), CarVel * 0.2f - T.GetUnitAxis(EAxis::Z) * (2500.f * Sim.T.ThrustCorner[k]) + FMath::VRand() * 250.f, 45.f, 1.8f, 450.f, 0.5f);
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
	if (AudioWind) { AudioWind->SetVolumeMultiplier(FMath::Clamp(T.Speed * T.Speed / 6000.f, 0.0, 1.2) * (IsInterior() ? 0.45f : 1.f)); AudioWind->SetPitchMultiplier(0.7f + T.Speed / 160.f); }
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

void ARoadsterPawn::CycleCamera()
{
	CameraMode = (ERoadsterCamera)(((uint8)CameraMode + 1) % ((uint8)ERoadsterCamera::Cinematic + 1));
	LookOffset = FRotator::ZeroRotator;
	bCamInit = false;
}

void ARoadsterPawn::UpdateCamera(float DeltaSeconds)
{
	const FTransform T = GetActorTransform();
	const FVector Fwd = T.GetUnitAxis(EAxis::X), Up = T.GetUnitAxis(EAxis::Z);
	const FVector CarPos = T.GetLocation();
	const float Speed = Sim.T.Speed;
	float TargetFov = 70.f;

	auto LookRot = [](const FVector& Dir, const FVector& UpHint) { return FRotationMatrix::MakeFromXZ(Dir, UpHint).Rotator(); };

	switch (CameraMode)
	{
	case ERoadsterCamera::Chase:
	case ERoadsterCamera::ChaseFar:
	{
		const bool bFar = CameraMode == ERoadsterCamera::ChaseFar;
		// up follows the car (so loops and ceilings stay readable), smoothed
		CamUp = FMath::VInterpNormalRotationTo(CamUp, Up, DeltaSeconds, 220.f);
		// heading follows velocity when moving, the body otherwise
		FVector Head = Fwd;
		if (Speed > 3.0) Head = FMath::Lerp(Fwd, ToU(Sim.V).GetSafeNormal(), 0.35f).GetSafeNormal();
		const FRotator Look = LookOffset;
		const FVector Back = FQuat(CamUp, FMath::DegreesToRadians(Look.Yaw)).RotateVector(-Head);
		const float Dist = (bFar ? 980.f : 640.f) + Speed * 1.2f;
		const float Height = (bFar ? 300.f : 185.f) + FMath::Clamp(Look.Pitch, -30.f, 60.f) * 4.f;
		const FVector Desired = CarPos + Back * Dist + CamUp * Height;
		if (!bCamInit) { CamPos = Desired; CamVel = FVector::ZeroVector; bCamInit = true; }
		// exponential follow in the car frame: exact for any frame time (stable at low fps)
		const float Follow = 1.f - FMath::Exp(-7.f * FMath::Min(DeltaSeconds, 0.25f));
		const FVector Rel = CamPos - CarPos;
		const FVector DesiredRel = Desired - CarPos;
		CamPos = CarPos + FMath::Lerp(Rel, DesiredRel, Follow);
		if (FVector::DistSquared(CamPos, Desired) > FMath::Square(3000.f)) CamPos = Desired;
		const FVector Target = CarPos + CamUp * 80.f + Fwd * 120.f;
		CamRot = LookRot((Target - CamPos).GetSafeNormal(), CamUp);
		TargetFov = 72.f + FMath::Clamp(Speed * 0.12f, 0.f, 22.f);
		break;
	}
	case ERoadsterCamera::Cockpit:
	{
		// driver's eye, left-hand drive
		CamPos = T.TransformPosition(B2U(-0.18, 0.38, 1.0));
		CamRot = (T.GetRotation() * FQuat(FRotator(-6.f + LookOffset.Pitch, LookOffset.Yaw, 0))).Rotator();
		CamUp = Up;
		TargetFov = 80.f;
		break;
	}
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
		OrbitYaw += LookOffset.Yaw; OrbitPitch = FMath::Clamp(OrbitPitch + LookOffset.Pitch, -80.f, 10.f);
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
		// trackside "TV" cameras: drop a camera ahead of the car and let it pass
		CinematicTimer -= DeltaSeconds;
		if (!bCamInit || CinematicTimer <= 0.f || FVector::Dist(CinematicPos, CarPos) > 9000.f)
		{
			const FVector Vel = ToU(Sim.V);
			const FVector Ahead = CarPos + Vel * 2.2f + FVector::CrossProduct(Up, Fwd).GetSafeNormal() * FMath::FRandRange(600.f, 1100.f) * (FMath::RandBool() ? 1.f : -1.f) + Up * FMath::FRandRange(80.f, 400.f);
			CinematicPos = Ahead; CinematicTimer = 4.5f; bCamInit = true;
		}
		CamPos = CinematicPos;
		CamRot = LookRot((CarPos + Up * 60.f - CamPos).GetSafeNormal(), FMath::Abs(Up.Z) > 0.3f ? FVector(0, 0, FMath::Sign(Up.Z)) : Up);
		TargetFov = FMath::Clamp(3000.f / FMath::Max(300.f, (float)FVector::Dist(CamPos, CarPos)) * 30.f, 12.f, 70.f);
		break;
	}
	}
	if (bExternalCam) { CamPos = ExtCamPos; CamRot = ExtCamRot; TargetFov = 70.f; bCamInit = false; }
	CamFov = FMath::FInterpTo(CamFov, TargetFov, DeltaSeconds, 3.f);
	Camera->SetWorldLocationAndRotation(CamPos, CamRot);
	Camera->SetFieldOfView(CamFov);
}

void ARoadsterPawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	Camera->GetCameraView(DeltaTime, OutResult);
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
