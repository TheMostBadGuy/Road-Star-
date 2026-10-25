#include "RoadsterGameMode.h"
#include "RoadsterPawn.h"
#include "RoadsterController.h"
#include "RoadsterHUD.h"
#include "TrackActor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <processthreadsapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

/** Opt the game out of Windows EcoQoS power throttling: otherwise Windows 11 slows the process to a
 *  fraction of a core whenever its window is not the foreground one, and the 1 kHz sim falls behind. */
static void DisablePowerThrottling()
{
#if PLATFORM_WINDOWS
	PROCESS_POWER_THROTTLING_STATE S = {};
	S.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
	S.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
	S.StateMask = 0;
	::SetProcessInformation(::GetCurrentProcess(), ProcessPowerThrottling, &S, sizeof(S));
#endif
}

ARoadsterGameMode::ARoadsterGameMode()
{
	DefaultPawnClass = ARoadsterPawn::StaticClass();
	PlayerControllerClass = ARoadsterController::StaticClass();
	HUDClass = ARoadsterHUD::StaticClass();
}

void ARoadsterGameMode::StartPlay()
{
	DisablePowerThrottling();
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
