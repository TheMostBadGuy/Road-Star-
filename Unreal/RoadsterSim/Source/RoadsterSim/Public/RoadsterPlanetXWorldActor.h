#pragma once

#include "CoreMinimal.h"
#include "PlanetX/Actors/PlanetXPlanetActor.h"
#include "PlanetX/Transition/PlanetXTransitionTypes.h"
#include "RoadsterPlanetXWorldActor.generated.h"

class ATrackActor;
class ARoadsterPawn;
class UPlanetXCoordinateComponent;
class UPlanetXViewpointComponent;
class UProceduralMeshComponent;
class UStaticMesh;

/** PlanetX presentation host. Roadster::Vehicle retains the inertial SI frame and all forces. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Roadster PlanetX Earth"))
class ROADSTERSIM_API ARoadsterPlanetXWorldActor : public APlanetXPlanetActor
{
	GENERATED_BODY()

public:
	ARoadsterPlanetXWorldActor();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Filled from the genuine Proxy Bake result, never a fabricated Section. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roadster|PlanetX")
	FName GroundSectionId;

	/** Hash of the track authoring inputs represented by the last reviewed static snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roadster|PlanetX")
	FString SnapshotTrackFingerprint;

	/** Visual-only bake inputs. Hidden at runtime; original ATrackActor remains the collision source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roadster|PlanetX")
	TArray<TObjectPtr<AActor>> VisualBakeSources;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Roadster|PlanetX|Status")
	FString IntegrationStatus;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Roadster|PlanetX|Status")
	bool bEarthFrameCompatible = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Roadster|PlanetX|Status")
	bool bParticipantReady = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Roadster|PlanetX|Status")
	bool bGeneratedVisualReady = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Roadster|PlanetX|Status")
	bool bProxyRefreshPending = true;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Roadster|PlanetX|Status")
	bool bHasTransitionObservation = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Roadster|PlanetX|Status")
	double AltitudeM = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Roadster|PlanetX|Status")
	double AltitudeErrorM = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Roadster|PlanetX|Status")
	double RoundTripErrorCm = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Roadster|PlanetX|Status")
	FPlanetXTransitionRuntimeResult TransitionObservation;

	/** Observes the actual facade and verifies coordinate conversions without moving the player. */
	UFUNCTION(BlueprintCallable, Category="Roadster|PlanetX|Diagnostics")
	bool RunIntegrationCheck(FString& OutReport);

	/** Fresh DataAssetFactory products only. Uses PlanetX's immutable creation API. Editor only. */
	UFUNCTION(BlueprintCallable, Category="Roadster|PlanetX|Authoring")
	static bool InitializeFreshEarthAsset(UPlanetXPlanetAsset* Asset, FString& OutReason);

	/** Creates a new immutable visual snapshot asset; never changes Source or its collision. Editor only. */
	UFUNCTION(BlueprintCallable, Category="Roadster|PlanetX|Authoring")
	static UStaticMesh* CreateTrackVisualSnapshot(UProceduralMeshComponent* Source,
		const FString& NewAssetPackage, FString& OutReason);

	/** Used by candidate setup/finalization and runtime stale-proxy reporting. */
	UFUNCTION(BlueprintPure, Category="Roadster|PlanetX|Authoring")
	static FString ComputeTrackFingerprint(ATrackActor* Track);

private:
	void UpdateParticipant();
	void ReleaseParticipant();
	void UpdateStatus();
	void UpdateLegacyEarthSuppression(bool bSuppress);
	bool ValidateEarthFrame(FString& OutReason) const;

	UPROPERTY(Transient) TObjectPtr<UPlanetXCoordinateComponent> OwnedCoordinate;
	UPROPERTY(Transient) TObjectPtr<UPlanetXViewpointComponent> OwnedViewpoint;
	TWeakObjectPtr<AActor> Participant;
	TWeakObjectPtr<ARoadsterPawn> SuppressedPawn;
	bool bPreviousLegacySuppression = false;
	bool bCheckRequested = false;
	bool bCheckWritten = false;
	double RuntimeStartTime = 0;
	double NextParticipantRetryTime = 0;
	double NextTrackCheckTime = 0;
	double NextStatusLogTime = 0;
	FString LastLoggedStatus;
};
