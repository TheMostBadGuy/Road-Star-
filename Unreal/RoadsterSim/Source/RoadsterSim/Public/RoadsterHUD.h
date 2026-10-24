#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RoadsterUI.h"
#include "RoadsterHUD.generated.h"

class ARoadsterController;
class ARoadsterPawn;
class ATrackActor;

UCLASS()
class ROADSTERSIM_API ARoadsterHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	RoadsterUI::FContext UI;
	float VW = 1920.f;   // virtual width of the HUD space (height is always 1200)
	float BuilderScroll = 0.f;
	float RebuildTimer = -1.f;
	int32 SaveCounter = 1;
	int32 MenuPage = 0;   // 0 main, 1 tracks, 2 graphics, 3 controls
	int32 Quality = 3;

	void DrawTitle(ARoadsterController* PC, ARoadsterPawn* P);
	void DrawDriving(ARoadsterController* PC, ARoadsterPawn* P);
	void DrawTouchscreen(ARoadsterController* PC, ARoadsterPawn* P);
	void DrawMenu(ARoadsterController* PC, ARoadsterPawn* P);
	void DrawBuilder(ARoadsterController* PC, ARoadsterPawn* P);
	void DrawControls(float X, float Y);
	void DrawWordmark(float CX, float CY, float H, const FLinearColor& C);
};
