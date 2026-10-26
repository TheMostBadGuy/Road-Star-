#include "RoadsterPlanetXWorldActor.h"

#include "RoadsterPawn.h"
#include "TrackActor.h"
#include "Camera/CameraComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "PlanetX/Assets/PlanetXPlanetAsset.h"
#include "PlanetX/Components/PlanetXCoordinateComponent.h"
#include "PlanetX/Components/PlanetXPlanetComponent.h"
#include "PlanetX/Components/PlanetXPlanetProxyComponent.h"
#include "PlanetX/Components/PlanetXViewpointComponent.h"
#include "PlanetX/Coordinates/PlanetXCoordinateUtils.h"
#include "PlanetX/Subsystems/PlanetXSubsystem.h"
#include "ProceduralMeshComponent.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "ProceduralMeshConversion.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#endif

namespace
{
	constexpr double EarthRadiusCm = Earth::MeanRadiusM * 100.0;
	const FVector EarthCenterCm(0, 0, -EarthRadiusCm);
	const FName EarthPlanetId(TEXT("RoadsterEarth"));
	const TCHAR* CandidateRoot = TEXT("/Game/Environment/PlanetX20261026/");

	UPlanetXSubsystem* GetFacade(const AActor* Actor)
	{
		const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
		UGameInstance* Game = World ? World->GetGameInstance() : nullptr;
		return Game ? Game->GetSubsystem<UPlanetXSubsystem>() : nullptr;
	}
}

ARoadsterPlanetXWorldActor::ARoadsterPlanetXWorldActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	PrimaryActorTick.TickInterval = .2f; // status and stale-source checks; PlanetX's own observer still runs normally
	// A planet gravity query remains disabled even if a future caller adds a Movement component.
	PlanetComponent->GravitySettings.bEnabled = false;
	PlanetComponent->PlanetBindingId = TEXT("RoadsterEarthHost");
	IntegrationStatus = TEXT("Awaiting saved Earth asset and genuine Proxy Bake / Visual Build");
}

void ARoadsterPlanetXWorldActor::BeginPlay()
{
	Super::BeginPlay();
	RuntimeStartTime = GetWorld()->GetTimeSeconds();
	bCheckRequested = FParse::Param(FCommandLine::Get(), TEXT("planetxcheck"));
	for (AActor* Source : VisualBakeSources)
	{
		// Never accept a live Track actor as a disposable visual source.
		if (IsValid(Source) && Source->ActorHasTag(TEXT("RoadsterPlanetXVisualBakeSource")) && !Source->IsA<ATrackActor>())
		{
			Source->SetActorHiddenInGame(true);
			Source->SetActorEnableCollision(false);
		}
	}
	UpdateParticipant();
	UpdateStatus();
}

void ARoadsterPlanetXWorldActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateParticipant();
	UpdateStatus();
	if (bCheckRequested && !bCheckWritten && GetWorld()->GetTimeSeconds() - RuntimeStartTime >= 5.0
		&& ((bGeneratedVisualReady && bHasTransitionObservation) || GetWorld()->GetTimeSeconds() - RuntimeStartTime >= 10.0))
	{
		FString Report;
		RunIntegrationCheck(Report);
		bCheckWritten = FFileHelper::SaveStringToFile(Report, *(FPaths::ProjectSavedDir() / TEXT("PlanetXIntegrationCheck.txt")));
	}
}

void ARoadsterPlanetXWorldActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UpdateLegacyEarthSuppression(false);
	ReleaseParticipant();
	Super::EndPlay(EndPlayReason);
}

bool ARoadsterPlanetXWorldActor::ValidateEarthFrame(FString& OutReason) const
{
	const UPlanetXPlanetAsset* Asset = GetPlanetAsset();
	if (!Asset || Asset->GetPlanetId() != EarthPlanetId)
	{
		OutReason = TEXT("Assign the RoadsterEarth PlanetX asset");
		return false;
	}
	if (!FMath::IsNearlyEqual(Asset->GetRadiusCm(), EarthRadiusCm, .01)
		|| !GetActorLocation().Equals(EarthCenterCm, .01)
		|| !GetActorQuat().Equals(FQuat::Identity, 1.e-8)
		|| !GetActorScale3D().Equals(FVector::OneVector, 1.e-8))
	{
		OutReason = TEXT("Earth frame mismatch: radius 637100000 cm, center (0,0,-637100000), identity rotation/scale required");
		return false;
	}
	const FPlanetXCoordinateConvention& C = Asset->GetCoordinateConvention();
	if (!C.NorthPoleAxis.Equals(FVector::UpVector, 1.e-8)
		|| !C.LonZeroAxis.Equals(FVector::ForwardVector, 1.e-8)
		|| !C.Lon90Axis.Equals(FVector::RightVector, 1.e-8)
		|| !C.bLongitudeCounterClockwise || !C.bSourceXMapsToEast || !C.bSourceYMapsToNorth || !C.bSourceZMapsToUp
		|| !FMath::IsNearlyEqual(C.UnrealUnitToCm, 1.0, 1.e-8))
	{
		OutReason = TEXT("PlanetX convention does not match the translated inertial Earth frame");
		return false;
	}
	OutReason.Reset();
	return true;
}

void ARoadsterPlanetXWorldActor::ReleaseParticipant()
{
	if (IsValid(OwnedViewpoint)) OwnedViewpoint->DestroyComponent();
	if (IsValid(OwnedCoordinate)) OwnedCoordinate->DestroyComponent();
	OwnedViewpoint = nullptr;
	OwnedCoordinate = nullptr;
	Participant.Reset();
	bParticipantReady = false;
}

void ARoadsterPlanetXWorldActor::UpdateParticipant()
{
	FString Reason;
	bEarthFrameCompatible = ValidateEarthFrame(Reason);
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	AActor* Target = PC ? PC->GetViewTarget() : nullptr;
	if (!bEarthFrameCompatible || !IsValid(Target))
	{
		if (Participant.IsValid()) ReleaseParticipant();
		return;
	}
	if (Participant.Get() == Target && IsValid(OwnedCoordinate) && IsValid(OwnedViewpoint))
	{
		bParticipantReady = OwnedCoordinate->RefreshRuntimeContext();
		return;
	}
	ReleaseParticipant();
	const UPlanetXPlanetAsset* Asset = GetPlanetAsset();
	if (!Asset->HasSuccessfulVisualBuild() || Asset->IsProxyBakeStale() || Asset->IsVisualBuildStale()
		|| GroundSectionId.IsNone() || GetWorld()->GetTimeSeconds() < NextParticipantRetryTime) return;
	NextParticipantRetryTime = GetWorld()->GetTimeSeconds() + 1.0;
	// Do not take ownership of components authored by another integration.
	if (Target->FindComponentByClass<UPlanetXCoordinateComponent>()
		|| Target->FindComponentByClass<UPlanetXViewpointComponent>()) return;
	TInlineComponentArray<UCameraComponent*> Cameras(Target);
	bool bActiveCamera = false;
	for (const UCameraComponent* Camera : Cameras) bActiveCamera |= Camera && Camera->IsActive();
	if (!bActiveCamera || !RegisterToPlanetXRuntime()) return;

	OwnedCoordinate = NewObject<UPlanetXCoordinateComponent>(Target, TEXT("RoadsterPlanetXObserverCoordinates"));
	OwnedCoordinate->ReferencePlanetActor = this;
	OwnedCoordinate->ReferenceSectionId = GroundSectionId;
	OwnedCoordinate->bAutoResolveSectionFromWorld = false;
	OwnedCoordinate->RepresentationDomain = EPlanetXRepresentationDomain::Orbit;
	OwnedCoordinate->PresentationScope = EPlanetXPresentationScope::Global;
	OwnedCoordinate->ActorSpatialLoadingPolicy = EPlanetXActorSpatialLoadingPolicy::ActorManaged;
	OwnedCoordinate->TransformSource = EPlanetXTransformSource::WorldTransform;
	OwnedCoordinate->SetAutomaticSameWorldEntryEnabled(false);
	OwnedCoordinate->SetAutomaticSameWorldReturnEnabled(false);
	OwnedCoordinate->SpatialEntryPolicy.MovementContinuityPolicy = EPlanetXMovementContinuityPolicy::DoNotApply;
	Target->AddInstanceComponent(OwnedCoordinate);
	OwnedCoordinate->RegisterComponent();
	const bool bResolved = OwnedCoordinate->RefreshRuntimeContext();
	OwnedCoordinate->PullFromWorld();

	OwnedViewpoint = NewObject<UPlanetXViewpointComponent>(Target, TEXT("RoadsterPlanetXObserverViewpoint"));
	OwnedViewpoint->PresentationCompensationMode = EPlanetXTransitionPresentationCompensationMode::Disabled;
	Target->AddInstanceComponent(OwnedViewpoint);
	OwnedViewpoint->RegisterComponent();
	Participant = Target;
	bParticipantReady = bResolved;
}

void ARoadsterPlanetXWorldActor::UpdateLegacyEarthSuppression(bool bSuppress)
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	ARoadsterPawn* Pawn = PC ? Cast<ARoadsterPawn>(PC->GetPawn()) : nullptr;
	if (SuppressedPawn.IsValid() && (!bSuppress || SuppressedPawn.Get() != Pawn))
	{
		SuppressedPawn->bPlanetXVisualActive = bPreviousLegacySuppression;
		SuppressedPawn.Reset();
	}
	if (bSuppress && IsValid(Pawn) && !SuppressedPawn.IsValid())
	{
		SuppressedPawn = Pawn;
		bPreviousLegacySuppression = Pawn->bPlanetXVisualActive;
		Pawn->bPlanetXVisualActive = true;
	}
}

void ARoadsterPlanetXWorldActor::UpdateStatus()
{
	FString FrameReason;
	bEarthFrameCompatible = ValidateEarthFrame(FrameReason);
	bGeneratedVisualReady = false;
	bHasTransitionObservation = false;
	const UPlanetXPlanetAsset* Asset = GetPlanetAsset();
	UPlanetXSubsystem* Facade = GetFacade(this);
	if (!bEarthFrameCompatible)
	{
		IntegrationStatus = FrameReason;
	}
	else
	{
		const bool bCurrentBuild = !Asset->IsProxyBakeStale()
			&& Asset->HasSuccessfulVisualBuild() && !Asset->IsVisualBuildStale();
		int32 RenderablePlanetMeshes = 0;
		TInlineComponentArray<UMeshComponent*> Meshes(this);
		for (const UMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh->IsRegistered() && Mesh->IsRenderStateCreated()
				&& Mesh->IsVisible() && !Mesh->bHiddenInGame
				&& Mesh->Bounds.SphereRadius > EarthRadiusCm * .1) ++RenderablePlanetMeshes;
		}
		bGeneratedVisualReady = bCurrentBuild && RenderablePlanetMeshes > 0 && Facade;
		if (GetWorld()->GetTimeSeconds() >= NextTrackCheckTime)
		{
			NextTrackCheckTime = GetWorld()->GetTimeSeconds() + 1.0;
			int32 TrackCount = 0;
			FString Current;
			for (TActorIterator<ATrackActor> It(GetWorld()); It; ++It) { ++TrackCount; Current = ComputeTrackFingerprint(*It); }
			bProxyRefreshPending = TrackCount != 1 || SnapshotTrackFingerprint.IsEmpty()
				|| SnapshotTrackFingerprint != Current;
		}
		if (bParticipantReady && Facade && Participant.IsValid())
		{
			const FVector P = Participant->GetActorLocation();
			const FPlanetXGeoCoordinate Geo = FPlanetXCoordinateUtils::PlanetLocalToGeo(
				Asset->GetCoordinateConvention(), EarthRadiusCm, GetActorTransform().InverseTransformPosition(P));
			AltitudeM = Geo.AltitudeCm / 100.0;
			const double Expected = Earth::Altitude({P.X / 100.0, P.Y / 100.0, P.Z / 100.0});
			AltitudeErrorM = FMath::Abs(AltitudeM - Expected);
			FPlanetXTransform Pose;
			FPlanetXTransformResolveResult CaptureResult, ResolveResult;
			FTransform Resolved;
			if (Facade->CaptureActorPlanetXTransform(this, GetPlanetId(), PlanetComponent->GetPlanetBindingId(),
				Participant.Get(), Pose, CaptureResult)
				&& Facade->ResolvePlanetXTransform(this, Pose, Resolved, ResolveResult))
			{
				RoundTripErrorCm = FVector::Distance(P, Resolved.GetLocation());
			}
			else RoundTripErrorCm = -1;
			TArray<FPlanetXTransitionRuntimeResult> Results;
			Facade->GetTransitionRuntimeResults(this, Results);
			for (const auto& Result : Results)
			{
				if (Result.PlanetId == GetPlanetId() && Result.PlanetBindingId == PlanetComponent->GetPlanetBindingId()
					&& Result.ViewpointActor.Get() == Participant.Get() && Result.bOwnsPlanetPresentation)
				{
					TransitionObservation = Result;
					bHasTransitionObservation = true;
					break;
				}
			}
		}
		if (!bCurrentBuild) IntegrationStatus = TEXT("Earth asset connected; genuine Proxy Bake and Apply & Build required");
		else if (!bGeneratedVisualReady) IntegrationStatus = TEXT("Waiting for PlanetX generated planet render resources");
		else if (!bParticipantReady) IntegrationStatus = TEXT("PlanetX visual ready; waiting for actual View Target camera / unowned observer components");
		else if (!bHasTransitionObservation) IntegrationStatus = TEXT("PlanetX coordinates ready; waiting for genuine Transition Endpoint observation");
		else if (bProxyRefreshPending) IntegrationStatus = TEXT("PlanetX active; track edited: refresh VISUAL snapshot and Proxy Bake; live collision remains current");
		else IntegrationStatus = FString::Printf(TEXT("PlanetX active; altitude %.2f m; transition %s alpha %.3f; vehicle physics authoritative"),
			AltitudeM, *UEnum::GetValueAsString(TransitionObservation.State), TransitionObservation.Alpha);
	}
	UpdateLegacyEarthSuppression(bEarthFrameCompatible && bGeneratedVisualReady && bParticipantReady);
	if (IntegrationStatus != LastLoggedStatus && GetWorld()->GetTimeSeconds() >= NextStatusLogTime)
	{
		UE_LOG(LogTemp, Display, TEXT("[RoadsterPlanetX] %s"), *IntegrationStatus);
		LastLoggedStatus = IntegrationStatus;
		NextStatusLogTime = GetWorld()->GetTimeSeconds() + 1.0;
	}
}

bool ARoadsterPlanetXWorldActor::RunIntegrationCheck(FString& OutReport)
{
	FString Reason;
	if (!ValidateEarthFrame(Reason) || !GetFacade(this)) { OutReport = Reason.IsEmpty() ? TEXT("Runtime facade unavailable") : Reason; return false; }
	const UPlanetXPlanetAsset* Asset = GetPlanetAsset();
	const AActor* Target = Participant.Get();
	const FTransform Before = Target ? Target->GetActorTransform() : FTransform::Identity;
	const ARoadsterPawn* Pawn = Cast<ARoadsterPawn>(Target);
	const Roadster::V3 BeforeP = Pawn ? Pawn->Sim.X : Roadster::V3();
	const Roadster::V3 BeforeV = Pawn ? Pawn->Sim.V : Roadster::V3();
	const Roadster::V3 BeforeW = Pawn ? Pawn->Sim.W : Roadster::V3();
	const double BeforeGasKg = Pawn ? Pawn->Sim.TankKg : 0;
	const double BeforeBatteryKWh = Pawn ? Pawn->Sim.EnergyKWh : 0;
	const FVector Samples[] = {FVector::ZeroVector, FVector(0,0,40000000), FVector(1234567,-7654321,12000000), FVector(EarthRadiusCm,0,-EarthRadiusCm)};
	double MaxPositionError = 0, MaxAltitudeError = 0;
	for (const FVector& P : Samples)
	{
		const FVector Local = GetActorTransform().InverseTransformPosition(P);
		const auto Geo = FPlanetXCoordinateUtils::PlanetLocalToGeo(Asset->GetCoordinateConvention(), EarthRadiusCm, Local);
		const FVector Restored = GetActorTransform().TransformPosition(FPlanetXCoordinateUtils::GeoToPlanetLocal(Asset->GetCoordinateConvention(), EarthRadiusCm, Geo));
		MaxPositionError = FMath::Max(MaxPositionError, FVector::Distance(P, Restored));
		MaxAltitudeError = FMath::Max(MaxAltitudeError, FMath::Abs(Geo.AltitudeCm / 100.0 - Earth::Altitude({P.X/100,P.Y/100,P.Z/100})));
	}
	UpdateStatus();
	const bool bLiveStateUnchanged = Target && Before.Equals(Target->GetActorTransform(), 0)
		&& (!Pawn || ((Pawn->Sim.X - BeforeP).Len() == 0 && (Pawn->Sim.V - BeforeV).Len() == 0
			&& (Pawn->Sim.W - BeforeW).Len() == 0 && Pawn->Sim.TankKg == BeforeGasKg && Pawn->Sim.EnergyKWh == BeforeBatteryKWh));
	const bool bReadOnlyPolicy = IsValid(OwnedCoordinate)
		&& !OwnedCoordinate->GetSpatialEntryPolicy().bAutomaticSameWorldEntryEnabled
		&& !OwnedCoordinate->GetSpatialEntryPolicy().bAutomaticSameWorldReturnEnabled
		&& OwnedCoordinate->SpatialEntryPolicy.MovementContinuityPolicy == EPlanetXMovementContinuityPolicy::DoNotApply
		&& IsValid(OwnedViewpoint) && OwnedViewpoint->PresentationCompensationMode == EPlanetXTransitionPresentationCompensationMode::Disabled;
	const bool bPass = MaxPositionError < .001 && MaxAltitudeError < 1.e-5
		&& bParticipantReady && bReadOnlyPolicy && RoundTripErrorCm >= 0 && RoundTripErrorCm < .001
		&& bGeneratedVisualReady && bHasTransitionObservation && !bProxyRefreshPending && bLiveStateUnchanged;
	OutReport = FString::Printf(TEXT("Roadster PlanetX %s\nFrame: center=(0,0,-637100000) cm, radius=637100000 cm; SI physics translated inertial frame\nActual plugin conversion: max round trip %.9g cm, radial altitude error %.9g m\nFacade actor round trip %.9g cm; participant=%d read-only-policy=%d generated-render-ready=%d endpoint-observed=%d proxy-refresh-pending=%d live-pose-momentum-resources-unchanged=%d\n%s\nThis check does not claim baked terrain collision, world-streaming completeness, or that finite car propellant reaches orbit.\n"),
		bPass ? TEXT("PASS") : TEXT("INCOMPLETE/FAIL"), MaxPositionError, MaxAltitudeError, RoundTripErrorCm,
		bParticipantReady, bReadOnlyPolicy, bGeneratedVisualReady, bHasTransitionObservation, bProxyRefreshPending, bLiveStateUnchanged, *IntegrationStatus);
	UE_LOG(LogTemp, Display, TEXT("[RoadsterPlanetXCheck] %s"), *OutReport);
	return bPass;
}

FString ARoadsterPlanetXWorldActor::ComputeTrackFingerprint(ATrackActor* Track)
{
	if (!IsValid(Track)) return FString();
	FString Inputs(TEXT("RoadsterTrackSnapshot-v1|"));
	Inputs += Track->TrackName + TEXT("|") + (Track->bCloseLoop ? TEXT("1|") : TEXT("0|"));
	const FTransform T = Track->GetActorTransform();
	const FVector P = T.GetLocation(), S = T.GetScale3D(); const FQuat Q = T.GetRotation();
	Inputs += FString::Printf(TEXT("%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g|"), P.X,P.Y,P.Z,Q.X,Q.Y,Q.Z,Q.W,S.X,S.Y,S.Z);
	for (const auto& V : Track->Pieces)
		Inputs += FString::Printf(TEXT("%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d|"), static_cast<int32>(V.Type),V.Length,V.Angle,V.Radius,V.Bank,V.Offset,V.Width,V.bWalls);
	if (auto* Mesh = Track->FindComponentByClass<UProceduralMeshComponent>())
		for (int32 I = 0; I < Mesh->GetNumSections(); ++I)
		{
			const FProcMeshSection* Section = Mesh->GetProcMeshSection(I);
			Inputs += FString::Printf(TEXT("%d,%d,%d,%s|"), I, Section ? Section->ProcVertexBuffer.Num() : -1,
				Section ? Section->ProcIndexBuffer.Num() : -1, *GetPathNameSafe(Mesh->GetMaterial(I)));
		}
	FTCHARToUTF8 Bytes(*Inputs);
	FSHAHash Hash; FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash.Hash);
	return Hash.ToString();
}

bool ARoadsterPlanetXWorldActor::InitializeFreshEarthAsset(UPlanetXPlanetAsset* Asset, FString& OutReason)
{
#if WITH_EDITOR
	if (!IsValid(Asset) || !Asset->GetPathName().StartsWith(CandidateRoot)
		|| !Asset->GetPlanetId().IsNone() || Asset->GetSections().Num() || Asset->GetLevelPairs().Num())
	{
		OutReason = TEXT("Only a fresh, uninitialized asset under /Game/Environment/PlanetX20261026 is accepted"); return false;
	}
	UMaterialInterface* EarthMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/PlanetX/PlanetX/Materials/Samples/PlanetSurface/MI_PlanetX_Earth.MI_PlanetX_Earth"));
	if (!EarthMaterial) { OutReason = TEXT("Included PlanetX Earth completion material missing"); return false; }
	if (!Asset->InitializeNewPlanetAsset(EarthPlanetId, EarthRadiusCm, FPlanetXCoordinateConvention()))
	{
		OutReason = TEXT("PlanetX rejected immutable initialization"); return false;
	}
	FPlanetXSurfaceCompletionSettings Completion = Asset->GetSurfaceCompletionSettings();
	Completion.CompletionNoiseStrengthPercent = 0; // visual Earth shares the spherical physics datum; no invented 637 km mountains
	for (auto& Region : Completion.TerrainRegionProfiles) Region.Strength = 0;
	Completion.CompletionMaterial = EarthMaterial;
	Asset->SetSurfaceCompletionSettings(Completion);
	FPlanetXEnvironmentAuthoringSettings Environment = Asset->GetEnvironmentSettings();
	Environment.PostProcess.bEnabled = false; // keep existing project exposure until the planet lighting is reviewed
	Asset->SetEnvironmentSettings(Environment);
	OutReason = TEXT("Fresh RoadsterEarth asset initialized; Section/Bake/Visual Build still required"); return true;
#else
	OutReason = TEXT("Earth asset authoring is editor only"); return false;
#endif
}

UStaticMesh* ARoadsterPlanetXWorldActor::CreateTrackVisualSnapshot(UProceduralMeshComponent* Source,
	const FString& NewAssetPackage, FString& OutReason)
{
#if WITH_EDITOR
	if (!IsValid(Source) || !Source->GetOwner() || !Source->GetOwner()->IsA<ATrackActor>()
		|| !NewAssetPackage.StartsWith(CandidateRoot) || !FPackageName::IsValidLongPackageName(NewAssetPackage)
		|| FPackageName::DoesPackageExist(NewAssetPackage) || FindPackage(nullptr, *NewAssetPackage))
	{
		OutReason = TEXT("Expected an actual track component and unused candidate asset package; overwrite refused"); return nullptr;
	}
	int32 Triangles = 0;
	for (int32 I=0; I<Source->GetNumSections(); ++I)
	{
		const FProcMeshSection* Section = Source->GetProcMeshSection(I);
		if (!Section) { OutReason = TEXT("Sparse procedural sections are unsupported by the engine converter"); return nullptr; }
		for (const auto& Vertex : Section->ProcVertexBuffer)
			if (Vertex.Position.ContainsNaN() || Vertex.Normal.ContainsNaN()) { OutReason = TEXT("Nonfinite track vertex rejected"); return nullptr; }
		for (const uint32 Index : Section->ProcIndexBuffer)
			if (Index >= static_cast<uint32>(Section->ProcVertexBuffer.Num())) { OutReason = TEXT("Invalid track triangle index rejected"); return nullptr; }
		Triangles += Section->ProcIndexBuffer.Num()/3;
	}
	if (Triangles <= 0) { OutReason = TEXT("Track has no generated triangles"); return nullptr; }
	FMeshDescription Description = BuildMeshDescription(Source); // engine conversion preserves all four UV channels
	if (Description.Triangles().Num() != Triangles) { OutReason = TEXT("Track conversion triangle count changed"); return nullptr; }
	UPackage* Package = CreatePackage(*NewAssetPackage);
	UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, *FPackageName::GetLongPackageAssetName(NewAssetPackage), RF_Public|RF_Standalone);
	FStaticMeshAttributes Attributes(Description);
	const auto Names = Attributes.GetPolygonGroupMaterialSlotNames();
	for (const FPolygonGroupID Group : Description.PolygonGroups().GetElementIDs())
	{
		UMaterialInterface* Match = nullptr;
		for (int32 I=0; I<Source->GetNumSections(); ++I)
		{
			UMaterialInterface* M = Source->GetMaterial(I);
			if (!M) M = UMaterial::GetDefaultMaterial(MD_Surface);
			if (M->GetFName() == Names[Group]) { Match=M; break; }
		}
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Match, Names[Group], Names[Group]));
	}
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bCommitMeshDescription = true;
	Params.bFastBuild = false;
	if (!Mesh->BuildFromMeshDescriptions({&Description}, Params))
	{
		OutReason = TEXT("Static snapshot render build failed; no success declared"); return nullptr;
	}
	FAssetRegistryModule::AssetCreated(Mesh);
	Mesh->MarkPackageDirty();
	OutReason = FString::Printf(TEXT("Visual snapshot created: %d exact procedural triangles, %d UV channels; actor must use NoCollision; save explicitly"),
		Triangles, Mesh->GetNumUVChannels(0));
	return Mesh;
#else
	OutReason = TEXT("Static snapshot authoring is editor only"); return nullptr;
#endif
}
