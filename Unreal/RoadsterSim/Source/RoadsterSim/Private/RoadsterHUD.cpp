#include "RoadsterHUD.h"
#include "RoadsterController.h"
#include "RoadsterPawn.h"
#include "RoadsterInterface.h"
#include "TrackActor.h"
#include "Engine/Canvas.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/GameUserSettings.h"

using namespace RoadsterUI;
using namespace Roadster;

namespace { FLinearColor A(const FLinearColor& C, float Alpha) { FLinearColor O = C; O.A = Alpha; return O; } }

extern int32 RoadsterDisableMask();

void ARoadsterHUD::DrawHUD()
{
	Super::DrawHUD();
	if (RoadsterDisableMask() & 8) return;
	ARoadsterController* PC = Cast<ARoadsterController>(GetOwningPlayerController());
	ARoadsterPawn* P = PC ? PC->Car() : nullptr;
	if (!PC || !P || !Canvas) return;

	const float S = Canvas->ClipY / FContext::VH;
	VW = Canvas->ClipX / S;
	const bool bInteract = PC->Mode != EUIMode::Driving;
	UI.Begin(Canvas, FVector2D::ZeroVector, FVector2D(FContext::VW * S, FContext::VH * S), PC->MousePos, PC->bMouseDown, PC->bMouseClicked, bInteract);

	switch (PC->Mode)
	{
	case EUIMode::Title: DrawTitle(PC, P); break;
	case EUIMode::Driving: DrawDriving(PC, P); break;
	case EUIMode::Touchscreen: DrawTouchscreen(PC, P); break;
	case EUIMode::Menu: DrawMenu(PC, P); break;
	case EUIMode::Builder: if (!FString(FCommandLine::Get()).Contains(TEXT("camat="))) DrawBuilder(PC, P); break;
	case EUIMode::Help: DrawDriving(PC, P); break;
	}
	// [TEAM HOOKUP: HUD overlays] overlay panels (Physics Lab, legends) draw here, above the mode UI, below toasts.
	if (PC->ToastTime > 0.f)
	{
		const float a = FMath::Clamp(PC->ToastTime * 2.f, 0.f, 1.f);
		const float W = UI.TextWidth(PC->Toast, 30, true) + 80;
		UI.Rect(VW * 0.5f - W * 0.5f, 120, W, 64, A(Black, 0.75f * a));
		UI.Outline(VW * 0.5f - W * 0.5f, 120, W, 64, A(Red, a), 1.5f);
		UI.Text(PC->Toast, VW * 0.5f, 152, 30, A(Red, a), EAlign::Center, true);
	}
	UI.End();
}

void ARoadsterHUD::DrawWordmark(float CX, float CY, float H, const FLinearColor& C)
{
	// widely tracked "ROADSTER" wordmark
	const FString Word = TEXT("R O A D S T E R");
	UI.Text(Word, CX, CY, H, C, EAlign::Center, true);
}

// ============================================================================ title
void ARoadsterHUD::DrawTitle(ARoadsterController* PC, ARoadsterPawn* P)
{
	UI.Gradient(0, 0, VW, 600, A(Black, 0.35f), A(Black, 0.25f));
	UI.Gradient(0, 600, VW, 600, A(Black, 0.25f), A(Black, 0.85f));
	DrawWordmark(VW * 0.5f, 380, 110, White);
	UI.Text(TEXT("2ND GENERATION  ·  2026"), VW * 0.5f, 470, 28, Grey, EAlign::Center, true);
	UI.Text(FString::Printf(TEXT("%s augment  ·  4 suction fans  ·  cold-gas thrusters"), *FString(TrimName(P->Sim.S.TrimLevel))), VW * 0.5f, 520, 26, Red, EAlign::Center);
	const float Pulse = 0.55f + 0.45f * FMath::Sin(GetWorld()->TimeSeconds * 3.f);
	UI.Text(TEXT("Press ENTER  or  (A)  to drive"), VW * 0.5f, 680, 36, A(White, Pulse), EAlign::Center, true);
	if (UI.Button(VW * 0.5f - 230, 740, 460, 84, TEXT("DRIVE"), true, 32)) PC->SetMode(EUIMode::Driving);
	if (UI.Button(VW * 0.5f - 230, 840, 460, 70, TEXT("Track Builder"), false, 26)) PC->SetMode(EUIMode::Builder);
	DrawControls(VW * 0.5f - 700, 950);
	UI.Text(TEXT("Specs from Tesla's 15 Oct 2026 reveal and engineering interviews. Simulation estimates where Tesla has not published figures."), VW * 0.5f, 1170, 18, Grey, EAlign::Center);
}

void ARoadsterHUD::DrawControls(float X, float Y)
{
	const TCHAR* Lines[][2] = {
		{ TEXT("W / S"), TEXT("Accelerator / brake (hold S at rest = reverse)") }, { TEXT("A / D"), TEXT("Steer") },
		{ TEXT("SHIFT"), TEXT("BOOST - rear thrusters") }, { TEXT("CTRL"), TEXT("Lift thrusters (Rocket)") },
		{ TEXT("Q / E"), TEXT("Yaw thrusters") }, { TEXT("F  [  ]"), TEXT("Fan mode / underbody pressure") },
		{ TEXT("TAB"), TEXT("Touchscreen (mouse)") }, { TEXT("T"), TEXT("Track Mode") },
		{ TEXT("C / V"), TEXT("Camera / cockpit") }, { TEXT("R"), TEXT("Reset to track") },
		{ TEXT("1-6"), TEXT("Load track") }, { TEXT("F2"), TEXT("Track builder") },
	};
	for (int32 i = 0; i < 12; ++i)
	{
		const float CX = X + (i % 3) * 470, CY = Y + (i / 3) * 34;
		UI.Text(Lines[i][0], CX, CY, 20, Red, EAlign::Left, true);
		UI.Text(Lines[i][1], CX + 100, CY, 18, Grey);
	}
}

// ============================================================================ driving overlay
void ARoadsterHUD::DrawDriving(ARoadsterController* PC, ARoadsterPawn* P)
{
	const Telemetry& T = P->Sim.T;
	const bool bCockpit = P->IsInterior();
	URoadsterInterface* I = P->Interface;
	const bool bMetric = I && I->bMetric;

	if (!bCockpit)
	{
		// bottom-centre: speed (squared numerals), gear, power / regen bar
		const float CX = VW * 0.5f;
		const FString Spd = FString::Printf(TEXT("%d"), FMath::RoundToInt(bMetric ? T.Speed * 3.6 : T.Speed / kMph));
		UI.Digits(Spd, CX, 1080, 96, White, EAlign::Center, 0.15f);
		UI.Text(bMetric ? TEXT("KM/H") : TEXT("MPH"), CX, 1160, 20, Grey, EAlign::Center, true);
		const TCHAR* Gear = T.bReverse ? TEXT("R") : TEXT("D");
		UI.Text(Gear, CX - 150, 1080, 44, Red, EAlign::Center, true);
		const float BW = 520, BX = CX - BW * 0.5f, BY = 1188;
		UI.Rect(BX, BY, BW, 6, A(White, 0.15f));
		const float Pw = FMath::Clamp((float)T.PowerKW / 1100.f, -0.3f, 1.f);
		if (Pw >= 0) UI.Rect(CX - 70, BY, (BW * 0.5f + 70) * Pw, 6, Red);
		else UI.Rect(CX - 70 + (BW * 0.5f - 190) * Pw / 0.3f, BY, -(BW * 0.5f - 190) * Pw / 0.3f, 6, Green);
		UI.Rect(CX - 71, BY - 6, 2, 18, White);

		// left: suction + aero, right: thrusters + g
		const float LX = 50, LY = 960;
		UI.Text(FString::Printf(TEXT("FAN  %s"), *FString(FanModeName(P->Sim.Set.Fan)).ToUpper()), LX, LY, 22, Red, EAlign::Left, true);
		UI.Text(FString::Printf(TEXT("-%.1f kPa   %.1f kN   seal %d%%"), T.FanPressureKPa, T.FanDownforceN / 1000.0, FMath::RoundToInt(T.FanSeal * 100)), LX, LY + 34, 22, White);
		UI.Bar(LX, LY + 58, 360, 8, T.FanPressureKPa / P->Sim.S.FanMaxPressureKPa, Red, false);
		UI.Text(FString::Printf(TEXT("AERO %s   WING %s"), *FString(AeroPresetName(P->Sim.Set.Aero)).ToUpper(), P->Sim.WingAngle > 0.7 ? TEXT("HIGH") : (P->Sim.WingAngle > 0.2 ? TEXT("LOW DRAG") : TEXT("STOWED"))), LX, LY + 100, 20, Grey, EAlign::Left, true);
		UI.Text(FString::Printf(TEXT("%s  ·  %d%%  ·  %d kW"), *FString(TrimName(P->Sim.S.TrimLevel)), FMath::RoundToInt(T.SOC * 100), FMath::RoundToInt(T.PowerKW)), LX, LY + 134, 20, Grey);

		const float RX = VW - 50;
		if (P->Sim.S.TankVolumeL > 0)
		{
			UI.Text(FString::Printf(TEXT("THRUSTERS  %s PSI"), *FString::FormatAsNumber(FMath::RoundToInt(T.TankBar * kPsiPerBar))), RX, LY, 22, Red, EAlign::Right, true);
			UI.Bar(RX - 360, LY + 26, 360, 8, T.TankPercent, Red, false);
			UI.Text(T.ThrustN > 100 ? FString::Printf(TEXT("%.0f kN"), T.ThrustN / 1000.0) : (T.TankPercent > 0.98 ? FString(TEXT("BOOST READY")) : FString::Printf(TEXT("%d%%"), FMath::RoundToInt(T.TankPercent * 100))), RX, LY + 62, 22, White, EAlign::Right, true);
		}
		// g-ball
		const float GX = RX - 70, GY = LY + 150, GR = 56;
		UI.Circle(GX, GY, GR, A(Red, 0.6f), 1.5f);
		UI.Circle(GX, GY, GR * 0.5f, A(Red, 0.3f), 1.f);
		const float gx = FMath::Clamp((float)T.AccelG.y / 3.5f, -1.f, 1.f), gy = FMath::Clamp(-(float)T.AccelG.x / 3.5f, -1.f, 1.f);
		UI.Disc(GX + gx * GR, GY + gy * GR, 7, Red);
		UI.Text(FString::Printf(TEXT("%.2f g"), FMath::Sqrt(T.AccelG.x * T.AccelG.x + T.AccelG.y * T.AccelG.y)), GX - GR - 16, GY, 22, White, EAlign::Right, true);
	}

	// top-left: track, lap, timers
	if (ATrackActor* Tr = PC->Track())
		UI.Text(Tr->TrackName.ToUpper(), 50, 50, 24, Red, EAlign::Left, true);
	if (P->bTrackMode)
	{
		UI.Text(FString::Printf(TEXT("LAP %d   %02d:%06.3f"), P->LapCount + 1, (int32)(P->LapTime / 60), FMath::Fmod(P->LapTime, 60.f)), 50, 88, 30, White, EAlign::Left, true);
		if (P->BestLap > 0) UI.Text(FString::Printf(TEXT("BEST %02d:%06.3f"), (int32)(P->BestLap / 60), FMath::Fmod(P->BestLap, 60.f)), 50, 124, 22, Grey);
	}
	if (T.bRunActive || T.T60 > 0)
	{
		const float X = 50, Y = 180;
		UI.Text(TEXT("RUN"), X, Y, 18, Grey, EAlign::Left, true);
		UI.Text(FString::Printf(TEXT("0-60  %s"), T.T60 > 0 ? *FString::Printf(TEXT("%.2f s  (%.2f s 1-ft)"), T.T60, T.T60Rollout) : TEXT("--")), X, Y + 30, 22, White);
		UI.Text(FString::Printf(TEXT("0-100 mph  %s"), T.T100mph > 0 ? *FString::Printf(TEXT("%.2f s"), T.T100mph) : TEXT("--")), X, Y + 58, 22, White);
		UI.Text(FString::Printf(TEXT("1/4 mile  %s"), T.TQuarter > 0 ? *FString::Printf(TEXT("%.2f s @ %.1f mph"), T.TQuarter, T.QuarterTrapMph) : TEXT("--")), X, Y + 86, 22, White);
		UI.Text(FString::Printf(TEXT("top %d mph   peak %.2f g"), FMath::RoundToInt(T.TopSpeed / kMph), T.PeakG), X, Y + 114, 20, Grey);
	}
	// state banners
	FString Banner;
	if (T.bLaunch) Banner = TEXT("LAUNCH CONTROL  ·  release brake");
	else if (T.UpDotWorld < -0.3 && T.WheelsInContact > 0) Banner = FString::Printf(TEXT("INVERTED  ·  suction %.1f kN vs %.1f kN weight"), T.FanDownforceN / 1000.0, P->Sim.TotalMass() * kG / 1000.0);
	else if (T.bAirborne && T.AirTime > 0.4) Banner = FString::Printf(TEXT("AIRBORNE  %.1f s"), T.AirTime);
	if (!Banner.IsEmpty()) UI.Text(Banner, VW * 0.5f, 230, 30, Red, EAlign::Center, true);
	if (P->bSimPaused) UI.Text(TEXT("PAUSED  (P)"), VW * 0.5f, 600, 48, White, EAlign::Center, true);

	if (PC->bShowHints && !bCockpit)
		UI.Text(TEXT("F1 hide  ·  TAB touchscreen  ·  SHIFT boost  ·  F fan  ·  T track mode  ·  C camera  ·  ESC menu"), VW - 50, 50, 18, Grey, EAlign::Right);
}

// ============================================================================ touchscreen overlay
void ARoadsterHUD::DrawTouchscreen(ARoadsterController* PC, ARoadsterPawn* P)
{
	UI.Rect(0, 0, VW, 1200, A(Black, 0.55f));
	if (!P->Interface) return;
	const float S = Canvas->ClipY / FContext::VH;
	const float H = Canvas->ClipY * 0.86f, W = H * 1.6f;
	const FVector2D Org((Canvas->ClipX - W) * 0.5f, (Canvas->ClipY - H) * 0.5f);
	UI.Outline((Org.X / S) - 6, (Org.Y / S) - 6, W / S + 12, H / S + 12, FLinearColor::FromSRGBColor(FColor(160, 160, 165)), 5);
	P->Interface->DrawCenterScreen(Canvas, Org, FVector2D(W, H), PC->MousePos, PC->bMouseDown, PC->bMouseClicked, true);
	UI.Text(TEXT("TAB to close  ·  this is the car's centre screen: every control is live"), VW * 0.5f, 1188, 18, Grey, EAlign::Center);
}

// ============================================================================ menu
void ARoadsterHUD::DrawMenu(ARoadsterController* PC, ARoadsterPawn* P)
{
	UI.Rect(0, 0, VW, 1200, A(Black, 0.7f));
	DrawWordmark(VW * 0.5f, 170, 70, White);
	const float X = VW * 0.5f - 300, W = 600, H = 76;
	float Y = 280;
	if (MenuPage == 0)
	{
		if (UI.Button(X, Y, W, H, TEXT("Resume"), true, 30)) PC->SetMode(EUIMode::Driving); Y += 90;
		if (UI.Button(X, Y, W, H, TEXT("Tracks"), false, 30)) MenuPage = 1; Y += 90;
		if (UI.Button(X, Y, W, H, TEXT("Track Builder"), false, 30)) PC->SetMode(EUIMode::Builder); Y += 90;
		if (UI.Button(X, Y, W, H, TEXT("Vehicle Settings (touchscreen)"), false, 30)) { if (P->Interface) { P->Interface->Tab = ECarTab::Vehicle; } PC->SetMode(EUIMode::Touchscreen); } Y += 90;
		const int32 TrimIdx = UI.Segmented(X - 100, Y, W + 200, H, TEXT("Augment"), { TEXT("Performance"), TEXT("Thrust"), TEXT("Rocket") }, (int32)P->Sim.S.TrimLevel, 220);
		if (TrimIdx != (int32)P->Sim.S.TrimLevel) P->SetTrim((Trim)TrimIdx);
		Y += 90;
		if (UI.Button(X, Y, W, H, TEXT("Graphics"), false, 30)) MenuPage = 2; Y += 90;
		if (UI.Button(X, Y, W, H, TEXT("Controls"), false, 30)) MenuPage = 3; Y += 90;
		if (UI.Button(X, Y, W, H, TEXT("Quit"), false, 30)) UKismetSystemLibrary::QuitGame(GetWorld(), PC, EQuitPreference::Quit, false);
	}
	else if (MenuPage == 1)
	{
		const TArray<FString> Names = ATrackActor::PresetNames();
		for (int32 i = 0; i < Names.Num(); ++i)
		{
			if (UI.Button(X, Y, W, H - 10, FString::Printf(TEXT("%d   %s"), i + 1, *Names[i]), false, 28))
				if (ATrackActor* T = PC->Track()) { T->LoadPreset(i); T->Rebuild(); P->ResetToTrack(true); PC->SetMode(EUIMode::Driving); }
			Y += 76;
		}
		for (const FString& N : ATrackActor::SavedTracks())
		{
			if (Y > 1040) break;
			if (UI.Button(X, Y, W, H - 10, FString::Printf(TEXT("Saved · %s"), *N), false, 26))
				if (ATrackActor* T = PC->Track()) { if (T->LoadFromFile(N)) { P->ResetToTrack(true); PC->SetMode(EUIMode::Driving); } }
			Y += 76;
		}
		if (UI.Button(X, 1080, W, 70, TEXT("Back"), false, 26)) MenuPage = 0;
	}
	else if (MenuPage == 2)
	{
		Quality = UI.Segmented(X - 200, Y, W + 400, H, TEXT("Quality"), { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic") }, Quality, 200);
		Y += 90;
		if (UI.Button(X, Y, W, H, TEXT("Apply"), false, 28))
		{
			if (UGameUserSettings* GS = UGameUserSettings::GetGameUserSettings())
			{
				GS->SetOverallScalabilityLevel(Quality);
				GS->ApplySettings(false);
			}
		}
		Y += 90;
		if (UI.Button(X, Y, W, H, TEXT("Toggle fullscreen"), false, 28))
			if (UGameUserSettings* GS = UGameUserSettings::GetGameUserSettings())
			{
				GS->SetFullscreenMode(GS->GetFullscreenMode() == EWindowMode::Windowed ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
				GS->ApplySettings(false);
			}
		if (UI.Button(X, 1080, W, 70, TEXT("Back"), false, 26)) MenuPage = 0;
	}
	else
	{
		DrawControls(VW * 0.5f - 700, 330);
		const TCHAR* More[] = {
			TEXT("SPACE handbrake  ·  X hold: DRS (stow wing)  ·  Z aero preset  ·  H lights  ·  M augment  ·  N unlimited nitrogen"),
			TEXT("B pit refill  ·  G slow motion  ·  P pause  ·  BACKSPACE start line  ·  Right mouse: look / orbit"),
			TEXT("In the air (Rocket): W/S pitch, A/D roll with the corner nozzles  ·  flight assist levels the car automatically"),
			TEXT("Gamepad: RT/LT pedals, LS steer, A boost, X lift, B handbrake, Y camera, RB fan, LB track mode, D-pad pressure / yaw"),
		};
		for (int32 i = 0; i < 4; ++i) UI.Text(More[i], VW * 0.5f, 520 + i * 44, 22, White, EAlign::Center);
		if (UI.Button(X, 1080, W, 70, TEXT("Back"), false, 26)) MenuPage = 0;
	}
}

// ============================================================================ track builder
void ARoadsterHUD::DrawBuilder(ARoadsterController* PC, ARoadsterPawn* P)
{
	ATrackActor* Tr = PC->Track();
	if (!Tr) return;
	TArray<FTrackPiece>& Pieces = Tr->Pieces;
	int32& Sel = Tr->SelectedPiece;
	bool bChanged = false;

	// highlight the selected piece in the world
	if (Sel >= 0)
	{
		const FTransform T = Tr->GetActorTransform();
		FVector2D Prev = FVector2D::ZeroVector; bool bHave = false;
		const float S = Canvas->ClipY / FContext::VH;
		for (const FTrackSample& Sm : Tr->GetSamples())
		{
			if (Sm.Piece != Sel) { bHave = false; continue; }
			const FVector Sp = Project(T.TransformPosition(Sm.P + Sm.Q.GetUpVector() * 60.f));
			if (Sp.Z <= 0) { bHave = false; continue; }
			const FVector2D Cur(Sp.X / S, Sp.Y / S);
			if (bHave) UI.Line(Prev.X, Prev.Y, Cur.X, Cur.Y, Red, 4);
			Prev = Cur; bHave = true;
		}
	}

	// ---- left panel: presets, pieces
	const float LX = 20, LW = 560;
	UI.Rect(LX, 20, LW, 1160, A(Black, 0.82f));
	UI.Outline(LX, 20, LW, 1160, Red, 1);
	UI.Text(TEXT("TRACK BUILDER"), LX + 24, 56, 30, Red, EAlign::Left, true);
	UI.Text(FString::Printf(TEXT("%s  ·  %.2f km  ·  %d pieces"), *Tr->TrackName, Tr->GetLengthM() / 1000.f, Pieces.Num()), LX + 24, 92, 18, Grey);
	const TArray<FString> Presets = ATrackActor::PresetNames();
	for (int32 i = 0; i < Presets.Num(); ++i)
		if (UI.Button(LX + 20 + (i % 3) * 176, 120 + (i / 3) * 52, 168, 44, Presets[i], false, 17)) { Tr->LoadPreset(i); bChanged = true; Sel = -1; }

	// add-piece palette (inserted after the selection)
	struct FAdd { const TCHAR* Label; ETrackPiece Type; float L, Ang, R, Bank, Off; };
	const FAdd Adds[] = {
		{ TEXT("Straight"), ETrackPiece::Straight, 80, 0, 0, 0, 0 }, { TEXT("Turn L"), ETrackPiece::Turn, 0, -90, 60, -12, 0 },
		{ TEXT("Turn R"), ETrackPiece::Turn, 0, 90, 60, 12, 0 }, { TEXT("Loop"), ETrackPiece::Loop, 0, 0, 15, 0, 15 },
		{ TEXT("Corkscrew"), ETrackPiece::Corkscrew, 300, 360, 5, 0, 0 }, { TEXT("Invert"), ETrackPiece::Corkscrew, 160, 180, 4.5f, 0, 0 },
		{ TEXT("Climb"), ETrackPiece::Pitch, 40, 15, 0, 0, 0 }, { TEXT("Descend"), ETrackPiece::Pitch, 40, -15, 0, 0, 0 },
		{ TEXT("Jump gap"), ETrackPiece::Gap, 30, 0, 0, 0, 2 }, { TEXT("Tube"), ETrackPiece::Tube, 80, 0, 7.5f, 0, 0 },
		{ TEXT("Roll to roof"), ETrackPiece::BarrelRoll, 60, 180, 7.5f, 0, 0 }, { TEXT("Wall ride"), ETrackPiece::Turn, 0, 90, 45, 75, 0 },
	};
	float Y = 240;
	UI.Text(TEXT("ADD"), LX + 24, Y, 16, Grey, EAlign::Left, true);
	Y += 20;
	for (int32 i = 0; i < 12; ++i)
	{
		if (UI.Button(LX + 20 + (i % 4) * 132, Y + (i / 4) * 50, 126, 44, Adds[i].Label, false, 17))
		{
			FTrackPiece N; N.Type = Adds[i].Type; N.Length = Adds[i].L; N.Angle = Adds[i].Ang; N.Radius = Adds[i].R; N.Bank = Adds[i].Bank; N.Offset = Adds[i].Off;
			const int32 At = Sel >= 0 ? Sel + 1 : Pieces.Num();
			Pieces.Insert(N, At); Sel = At; bChanged = true;
		}
	}
	Y += 160;

	// piece list
	const TCHAR* TypeNames[] = { TEXT("Straight"), TEXT("Turn"), TEXT("Loop"), TEXT("Corkscrew"), TEXT("Pitch"), TEXT("Gap"), TEXT("Tube"), TEXT("Barrel Roll") };
	const float ListY = Y, RowH = 40, ListH = 1160 - ListY - 120;
	const int32 Rows = FMath::FloorToInt(ListH / RowH);
	if (UI.Hover(LX, ListY, LW, ListH)) BuilderScroll = FMath::Clamp(BuilderScroll - PC->MouseWheel * 3.f, 0.f, FMath::Max(0.f, float(Pieces.Num() - Rows)));
	const int32 First = FMath::FloorToInt(BuilderScroll);
	for (int32 r = 0; r < Rows && First + r < Pieces.Num(); ++r)
	{
		const int32 i = First + r;
		const FTrackPiece& Pc = Pieces[i];
		FString Desc;
		switch (Pc.Type)
		{
		case ETrackPiece::Turn: Desc = FString::Printf(TEXT("%+.0f°  R%.0f  bank %.0f°"), Pc.Angle, Pc.Radius, Pc.Bank); break;
		case ETrackPiece::Loop: Desc = FString::Printf(TEXT("R%.0f  offset %.0f"), Pc.Radius, Pc.Offset); break;
		case ETrackPiece::Corkscrew: case ETrackPiece::BarrelRoll: Desc = FString::Printf(TEXT("%+.0f° over %.0f m"), Pc.Angle, Pc.Length); break;
		case ETrackPiece::Pitch: Desc = FString::Printf(TEXT("%+.0f° over %.0f m"), Pc.Angle, Pc.Length); break;
		default: Desc = FString::Printf(TEXT("%.0f m"), Pc.Length); break;
		}
		const float RY = ListY + r * RowH;
		if (UI.Button(LX + 14, RY, LW - 28, RowH - 4, TEXT(""), i == Sel, 18)) Sel = i;
		UI.Text(FString::Printf(TEXT("%02d"), i + 1), LX + 30, RY + RowH * 0.45f, 18, Grey);
		UI.Text(TypeNames[(int32)Pc.Type], LX + 80, RY + RowH * 0.45f, 20, i == Sel ? Red : White, EAlign::Left, true);
		UI.Text(Desc, LX + LW - 30, RY + RowH * 0.45f, 18, Grey, EAlign::Right);
	}
	// list actions
	const float AY = 1160 - 100;
	if (UI.Button(LX + 20, AY, 126, 44, TEXT("Up"), false, 18) && Sel > 0) { Pieces.Swap(Sel, Sel - 1); --Sel; bChanged = true; }
	if (UI.Button(LX + 152, AY, 126, 44, TEXT("Down"), false, 18) && Sel >= 0 && Sel + 1 < Pieces.Num()) { Pieces.Swap(Sel, Sel + 1); ++Sel; bChanged = true; }
	if (UI.Button(LX + 284, AY, 126, 44, TEXT("Duplicate"), false, 18) && Sel >= 0) { Pieces.Insert(Pieces[Sel], Sel + 1); ++Sel; bChanged = true; }
	if (UI.Button(LX + 416, AY, 126, 44, TEXT("Delete"), false, 18) && Sel >= 0 && Pieces.Num() > 1) { Pieces.RemoveAt(Sel); Sel = FMath::Min(Sel, Pieces.Num() - 1); bChanged = true; }
	if (UI.Button(LX + 20, AY + 50, 258, 44, Tr->bCloseLoop ? TEXT("Close loop: ON") : TEXT("Close loop: OFF"), Tr->bCloseLoop, 18)) { Tr->bCloseLoop = !Tr->bCloseLoop; bChanged = true; }
	if (UI.Button(LX + 284, AY + 50, 258, 44, TEXT("Save track"), false, 18))
	{
		const FString Name = FString::Printf(TEXT("MyTrack_%02d"), SaveCounter++);
		Tr->TrackName = Name;
		PC->ShowToast(Tr->SaveToFile(Name) ? FString::Printf(TEXT("Saved %s"), *Name) : FString(TEXT("Save failed")));
	}

	// ---- right panel: selected piece parameters
	const float RX = VW - 600, RW = 580;
	UI.Rect(RX, 20, RW, 760, A(Black, 0.82f));
	UI.Outline(RX, 20, RW, 760, Red, 1);
	if (Sel >= 0 && Sel < Pieces.Num())
	{
		FTrackPiece& Pc = Pieces[Sel];
		UI.Text(FString::Printf(TEXT("PIECE %02d  ·  %s"), Sel + 1, TypeNames[(int32)Pc.Type]), RX + 24, 56, 26, Red, EAlign::Left, true);
		float PY = 90;
		auto Param = [&](const TCHAR* L, float& V, float Mn, float Mx, const TCHAR* Unit)
		{
			float X = V;
			if (UI.Slider(RX + 10, PY, RW - 20, 70, L, X, Mn, Mx, FString::Printf(TEXT("%.0f %s"), X, Unit), 170)) { V = FMath::RoundToFloat(X); bChanged = true; }
			PY += 78;
		};
		const bool bLen = Pc.Type != ETrackPiece::Turn && Pc.Type != ETrackPiece::Loop;
		if (bLen) Param(TEXT("Length"), Pc.Length, 5, 800, TEXT("m"));
		if (Pc.Type == ETrackPiece::Turn || Pc.Type == ETrackPiece::Pitch || Pc.Type == ETrackPiece::Corkscrew || Pc.Type == ETrackPiece::BarrelRoll)
			Param(TEXT("Angle"), Pc.Angle, Pc.Type == ETrackPiece::Pitch ? -45.f : -360.f, Pc.Type == ETrackPiece::Pitch ? 45.f : 360.f, TEXT("deg"));
		if (Pc.Type == ETrackPiece::Turn || Pc.Type == ETrackPiece::Loop || Pc.Type == ETrackPiece::Corkscrew || Pc.Type == ETrackPiece::Tube || Pc.Type == ETrackPiece::BarrelRoll)
			Param(TEXT("Radius"), Pc.Radius, 3, 250, TEXT("m"));
		if (Pc.Type == ETrackPiece::Straight || Pc.Type == ETrackPiece::Turn || Pc.Type == ETrackPiece::Pitch || Pc.Type == ETrackPiece::Tube)
			Param(TEXT("Bank"), Pc.Bank, -90, 90, TEXT("deg"));
		if (Pc.Type == ETrackPiece::Loop || Pc.Type == ETrackPiece::Gap)
			Param(Pc.Type == ETrackPiece::Loop ? TEXT("Side offset") : TEXT("Drop"), Pc.Offset, -40, 40, TEXT("m"));
		Param(TEXT("Width"), Pc.Width, 8, 30, TEXT("m"));
		if (UI.Button(RX + 20, PY, 260, 54, Pc.bWalls ? TEXT("Barriers: ON") : TEXT("Barriers: OFF"), Pc.bWalls, 20)) { Pc.bWalls = !Pc.bWalls; bChanged = true; }
		PY += 70;
		if (UI.Button(RX + 20, PY, RW - 40, 64, TEXT("Drive from this piece"), true, 24))
		{
			FTransform F;
			if (Tr->GetFrameAtPiece(Sel, F)) { P->PlaceAt(F, 0.f); PC->SetMode(EUIMode::Driving); }
		}
		PY += 76;
		if (UI.Button(RX + 20, PY, RW - 40, 56, TEXT("Fly camera to piece"), false, 20))
		{
			FTransform F;
			if (Tr->GetFrameAtPiece(Sel, F)) { PC->BuilderPos = F.GetLocation() + FVector(-2500, -2500, 1800); PC->BuilderRot = (F.GetLocation() - PC->BuilderPos).Rotation(); }
		}
	}
	else
	{
		UI.Text(TEXT("Select a piece, or add one"), RX + 24, 60, 24, Grey);
		if (UI.Button(RX + 20, 120, RW - 40, 64, TEXT("Drive from the start"), true, 24)) { P->ResetToTrack(true); PC->SetMode(EUIMode::Driving); }
	}
	UI.Text(TEXT("Right mouse: look   WASD / QE: fly   SHIFT: fast   F2: back to driving"), VW - 40, 1170, 18, Grey, EAlign::Right);

	if (bChanged) RebuildTimer = 0.15f;
	if (RebuildTimer > 0.f)
	{
		RebuildTimer -= GetWorld()->GetDeltaSeconds();
		if (RebuildTimer <= 0.f && !PC->bMouseDown) { Tr->Rebuild(); RebuildTimer = -1.f; }
		else if (RebuildTimer <= 0.f) RebuildTimer = 0.05f;
	}
}
