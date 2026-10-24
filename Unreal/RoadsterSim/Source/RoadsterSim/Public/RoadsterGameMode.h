#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RoadsterGameMode.generated.h"

UCLASS()
class ROADSTERSIM_API ARoadsterGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARoadsterGameMode();
	virtual void StartPlay() override;
};
