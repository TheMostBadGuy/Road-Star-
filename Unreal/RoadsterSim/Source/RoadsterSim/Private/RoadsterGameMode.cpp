#include "RoadsterGameMode.h"
#include "RoadsterPawn.h"
#include "RoadsterController.h"
#include "RoadsterHUD.h"
#include "TrackActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"

ARoadsterGameMode::ARoadsterGameMode()
{
	DefaultPawnClass = ARoadsterPawn::StaticClass();
	PlayerControllerClass = ARoadsterController::StaticClass();
	HUDClass = ARoadsterHUD::StaticClass();
}

void ARoadsterGameMode::StartPlay()
{
	// a track is always present; the map may already contain one placed in the editor
	bool bHasTrack = false;
	for (TActorIterator<ATrackActor> It(GetWorld()); It; ++It) { bHasTrack = true; break; }
	if (!bHasTrack)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<ATrackActor>(ATrackActor::StaticClass(), FTransform(FVector(0, 0, 4.f)), P);
	}
	Super::StartPlay();
}
