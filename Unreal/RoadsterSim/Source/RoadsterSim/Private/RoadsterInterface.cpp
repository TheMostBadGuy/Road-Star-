#include "RoadsterInterface.h"
#include "RoadsterPawn.h"
#include "TrackActor.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"

using namespace RoadsterUI;
using namespace Roadster;

namespace
{
	const TArray<FString> kTabs = { TEXT("Home"), TEXT("Vehicle"), TEXT("Listen"), TEXT("Climate"), TEXT("Navigate") };
	FLinearColor A(const FLinearColor& C, float Alpha) { FLinearColor O = C; O.A = Alpha; return O; }
}

bool URoadsterInterface::WantsCarView() const
{
	if (!Pawn) return false;
	if (Pawn->bTrackMode) return false;
	return Tab == ECarTab::Home || (Tab == ECarTab::Vehicle && Page == EVehiclePage::Dynamics) || (Tab == ECarTab::Vehicle && Page == EVehiclePage::Charging);
}

FString URoadsterInterface::Speed(double Mps, bool bUnit) const
{
	const double V = bMetric ? Mps * 3.6 : Mps / kMph;
	return FString::Printf(TEXT("%d%s"), FMath::RoundToInt(FMath::Abs(V)), bUnit ? (bMetric ? TEXT(" km/h") : TEXT(" mph")) : TEXT(""));
}

FString URoadsterInterface::Temp(double C) const
{
	return bCelsius ? FString::Printf(TEXT("%d°"), FMath::RoundToInt(C)) : FString::Printf(TEXT("%d°"), FMath::RoundToInt(C * 9.0 / 5.0 + 32.0));
}

FString URoadsterInterface::Pressure(double KPa, bool bUnit) const
{
	switch (PressureUnit)
	{
	case 1: return FString::Printf(TEXT("%.2f%s"), KPa / 100.0, bUnit ? TEXT(" bar") : TEXT(""));
	case 2: return FString::Printf(TEXT("%d%s"), FMath::RoundToInt(KPa), bUnit ? TEXT(" kPa") : TEXT(""));
	default: return FString::Printf(TEXT("%d%s"), FMath::RoundToInt(KPa * kPsiPerKPa), bUnit ? TEXT(" psi") : TEXT(""));
	}
}

FString URoadsterInterface::LapStr(float T) const
{
	if (T < 0) return TEXT("--:--.---");
	const int32 M = FMath::FloorToInt(T / 60.f);
	const float S = T - M * 60.f;
	return FString::Printf(TEXT("%02d:%06.3f"), M, S);
}

// ============================================================================ frame
void URoadsterInterface::DrawCenterScreen(UCanvas* Canvas, FVector2D Origin, FVector2D Size, FVector2D Mouse, bool bDown, bool bClick, bool bInteractive)
{
	if (!Pawn || !Canvas) return;
	const double Now = FPlatformTime::Seconds();
	const float Dt = LastTime > 0 ? FMath::Clamp(float(Now - LastTime), 0.f, 0.1f) : 0.016f;
	LastTime = Now;
	FloorPhase += Dt * FMath::Clamp((float)Pawn->Sim.T.Speed / 20.f, 0.f, 3.f);
	MusicT = FMath::Fmod(MusicT + Dt, 236.f);
	CarYaw = FMath::FInterpTo(CarYaw, CarYawTarget, Dt, 3.f);
	CarPitch = FMath::FInterpTo(CarPitch, CarPitchTarget, Dt, 3.f);

	UI.Begin(Canvas, Origin, Size, Mouse, bDown, bClick, bInteractive);
	UI.Rect(0, 0, FContext::VW, FContext::VH, Black);

	TopBar();
	if (Pawn->bTrackMode)
	{
		if (bTrackSetup) TrackSetupPage(); else TrackDashboard();
	}
	else
	{
		DriveStrip();
		switch (Tab)
		{
		case ECarTab::Home: HomeTab(); break;
		case ECarTab::Vehicle: VehicleTab(); break;
		case ECarTab::Listen: ListenTab(); break;
		case ECarTab::Climate: ClimateTab(); break;
		case ECarTab::Navigate: NavigateTab(); break;
		}
		BottomBar();
	}
	// display brightness
	if (Brightness < 0.99f) UI.Rect(0, 0, FContext::VW, FContext::VH, A(Black, 1.f - Brightness));
	UI.End();
}

void URoadsterInterface::TopBar()
{
	const Telemetry& T = Pawn->Sim.T;
	// P R N D
	const TCHAR* Gears[] = { TEXT("P"), TEXT("R"), TEXT("N"), TEXT("D") };
	const int32 Active = T.Speed < 0.2 && Pawn->Input.Brake > 0.5 && !T.bReverse ? 0 : (T.bReverse ? 1 : 3);
	float X = 40;
	for (int32 i = 0; i < 4; ++i)
	{
		UI.Text(Gears[i], X, 42, 30, i == Active ? Red : RedDim, EAlign::Left, i == Active);
		X += 34;
	}
	X += 20;
	UI.Text(TEXT("·"), X - 14, 42, 30, RedDim);
	// segmented battery icon
	const float Soc = (float)T.SOC;
	for (int32 i = 0; i < 6; ++i) UI.Rect(X + i * 7, 32, 4.5f, 20, (i + 0.5f) / 6.f <= Soc ? Red : RedFaint);
	UI.Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Soc * 100.f)), X + 52, 42, 30, Red);

	if (Pawn->bTrackMode)
	{
		const FString Mid = FString::Printf(TEXT("%d / %d   ·   %s   ·   POS 1"), Pawn->LapCount + 1, 60, *LapStr(Pawn->LapTime).RightChop(1));
		UI.Text(Mid, FContext::VW * 0.5f, 42, 30, Red, EAlign::Center, true);
	}
	const FDateTime Now = FDateTime::Now();
	const TCHAR* Days[] = { TEXT("Mon"), TEXT("Tue"), TEXT("Wed"), TEXT("Thu"), TEXT("Fri"), TEXT("Sat"), TEXT("Sun") };
	const TCHAR* Months[] = { TEXT("Jan"), TEXT("Feb"), TEXT("Mar"), TEXT("Apr"), TEXT("May"), TEXT("Jun"), TEXT("Jul"), TEXT("Aug"), TEXT("Sep"), TEXT("Oct"), TEXT("Nov"), TEXT("Dec") };
	const int32 H12 = Now.GetHour12();
	const FString Date = FString::Printf(TEXT("%s %s %d   ·"), Days[(int32)Now.GetDayOfWeek()], Months[Now.GetMonth() - 1], Now.GetDay());
	const FString Time = FString::Printf(TEXT("%d:%02d %s"), H12, Now.GetMinute(), Now.IsAfternoon() ? TEXT("PM") : TEXT("AM"));
	const float TW = UI.TextWidth(Time, 30, true);
	UI.Text(Time, FContext::VW - 40, 42, 30, Red, EAlign::Right, true);
	UI.Text(Date, FContext::VW - 52 - TW, 42, 30, RedDim, EAlign::Right);
}

void URoadsterInterface::DriveStrip()
{
	// slide-to-shift strip on the left edge: tap the arrows for Drive / Reverse
	const float X = 30, W = 60;
	UI.Line(X + W * 0.5f, 140, X + W * 0.5f, 170, RedDim, 3);
	UI.Line(X + W * 0.5f - 12, 155, X + W * 0.5f, 140, RedDim, 3);
	UI.Line(X + W * 0.5f + 12, 155, X + W * 0.5f, 140, RedDim, 3);
	// top-view car icon
	UI.Outline(X + 16, 320, W - 32, 100, Red, 2);
	UI.Rect(X + 22, 345, W - 44, 30, RedFaint);
	UI.Line(X + W * 0.5f, 590, X + W * 0.5f, 620, RedDim, 3);
	UI.Line(X + W * 0.5f - 12, 605, X + W * 0.5f, 620, RedDim, 3);
	UI.Line(X + W * 0.5f + 12, 605, X + W * 0.5f, 620, RedDim, 3);
	if (UI.Clicked(X, 120, W, 80)) { Pawn->Input.bWantReverse = false; UI.bConsumedClick = true; }
	if (UI.Clicked(X, 570, W, 80) && Pawn->Sim.T.Speed < 1.0) { Pawn->Input.bWantReverse = true; UI.bConsumedClick = true; }
}

void URoadsterInterface::BottomBar()
{
	// mini music strip
	const float Y = 1000;
	UI.Rect(560, Y - 30, 52, 52, RedDim);
	for (int32 i = 0; i < 5; ++i) UI.Rect(566 + i * 9, Y - 24 + (i % 2) * 18, 6, 22, Red);
	UI.Text(TEXT("Glass Weather"), 630, Y - 14, 24, Red, EAlign::Left, true);
	UI.Text(TEXT("Lumen Vale"), 630, Y + 14, 22, RedDim);
	const float Cx = 960;
	UI.Text(TEXT("⇄"), Cx - 210, Y, 30, RedDim, EAlign::Center);
	UI.Text(TEXT("⏮"), Cx - 105, Y, 30, Red, EAlign::Center);
	UI.Rect(Cx - 12, Y - 16, 8, 32, Red); UI.Rect(Cx + 4, Y - 16, 8, 32, Red);
	UI.Text(TEXT("⏭"), Cx + 105, Y, 30, Red, EAlign::Center);
	UI.Text(TEXT("↻"), Cx + 210, Y, 30, RedDim, EAlign::Center);
	UI.Text(FString::Printf(TEXT("%02d:%02d"), (int32)MusicT / 60, (int32)MusicT % 60), 1360, Y - 14, 24, Red, EAlign::Right, true);
	UI.Text(TEXT("03:56"), 1360, Y + 14, 22, RedDim, EAlign::Right);
	UI.Rect(0, Y + 52, FContext::VW, 2, RedFaint);
	UI.Rect(0, Y + 52, FContext::VW * MusicT / 236.f, 2, Red);
	UI.Disc(FContext::VW * MusicT / 236.f, Y + 53, 6, Red);

	// climate, tabs, volume
	const float BY = 1135;
	UI.Text(TEXT("‹"), 70, BY, 40, RedDim, EAlign::Center);
	UI.Digits(FString::Printf(TEXT("%d"), FMath::RoundToInt(bCelsius ? (ClimateF - 32) * 5 / 9 : ClimateF)), 150, BY, 44, Red, EAlign::Center);
	UI.Text(TEXT("›"), 230, BY, 40, RedDim, EAlign::Center);
	if (UI.Clicked(40, BY - 40, 60, 80)) { ClimateF -= 1; UI.bConsumedClick = true; }
	if (UI.Clicked(200, BY - 40, 60, 80)) { ClimateF += 1; UI.bConsumedClick = true; }
	const float TabX0 = 560, TabW = 160;
	for (int32 i = 0; i < kTabs.Num(); ++i)
	{
		const bool bOn = (int32)Tab == i;
		UI.Text(kTabs[i], TabX0 + TabW * i + TabW * 0.5f, BY, 28, bOn ? Red : RedDim, EAlign::Center, bOn);
		if (UI.Clicked(TabX0 + TabW * i, BY - 40, TabW, 80)) { Tab = (ECarTab)i; Hotspot = -1; UI.bConsumedClick = true; }
	}
	UI.Text(TEXT("‹"), 1700, BY, 40, RedDim, EAlign::Center);
	UI.Text(TEXT("♪"), 1770, BY, 34, Red, EAlign::Center);
	for (int32 i = 0; i < 3; ++i) UI.Rect(1792 + i * 9, BY - 6 - i * 5, 5, 12 + i * 10, i < Volume / 4 + 1 ? Red : RedFaint);
	UI.Text(TEXT("›"), 1860, BY, 40, RedDim, EAlign::Center);
	if (UI.Clicked(1670, BY - 40, 60, 80)) { Volume = FMath::Max(0, Volume - 1); UI.bConsumedClick = true; }
	if (UI.Clicked(1830, BY - 40, 60, 80)) { Volume = FMath::Min(11, Volume + 1); UI.bConsumedClick = true; }
}

// ============================================================================ car image + hotspots
FVector2D URoadsterInterface::ProjectCarPoint(const FVector& BodyPointCm, float X, float Y, float W, float H) const
{
	const FRotator View = CarViewRotation();
	const FVector Dir = View.Vector();
	const FVector Cam = FVector(-10.f, 0.f, 50.f) - Dir * 1450.f;
	const FVector L = View.UnrotateVector(BodyPointCm - Cam);
	if (L.X < 1.f) return FVector2D(-1000, -1000);
	const float TanHalf = FMath::Tan(FMath::DegreesToRadians(30.f * 0.5f));
	const float Aspect = 1100.f / 620.f;
	const float sx = 0.5f + (L.Y / L.X) / (2.f * TanHalf);
	const float sy = 0.5f - (L.Z / L.X) * Aspect / (2.f * TanHalf);
	return FVector2D(X + sx * W, Y + sy * H);
}

void URoadsterInterface::CarImage(float X, float Y, float W, float H, bool bFloor)
{
	if (bFloor) UI.DotFloor(X - 200, Y + H * 0.45f, W + 400, H * 0.62f, FloorPhase);
	if (Pawn->CarViewRT) UI.Texture(Pawn->CarViewRT, X, Y, W, H, FLinearColor(1.f, 0.93f, 0.93f, 1.f));
	// drag to rotate
	if (UI.bInteract)
	{
		if (UI.bClick && UI.Hover(X, Y, W, H) && Hotspot < 0) { bDragCar = true; DragStartX = UI.Mouse.X; DragStartYaw = CarYawTarget; }
		if (!UI.bDown) bDragCar = false;
		if (bDragCar) CarYawTarget = DragStartYaw - (UI.Mouse.X - DragStartX) * 0.25f;
	}
}

void URoadsterInterface::HomeTab()
{
	const float X = 160, Y = 110, W = 1600, H = 902;
	CarImage(X, Y, W, H);
	Settings& Set = Pawn->Sim.Set;

	struct FSpot { FString Name; FVector P; float Yaw, Pitch; TArray<FString> Opts; int32 Cur; };
	const int32 WingCur = Set.Aero == AeroPreset::Custom ? (int32)Set.RWing : (Pawn->Sim.WingAngle > 0.7 ? 2 : (Pawn->Sim.WingAngle > 0.2 ? 1 : 0));
	TArray<FSpot> Spots = {
		{ TEXT("Rear Wing"), FVector(-200, 0, 117), 215, -14, { TEXT("Stowed"), TEXT("Low Drag"), TEXT("High Downforce") }, WingCur },
		{ TEXT("Barge Board"), FVector(95, -98, 35), 270, -6, { TEXT("Retract"), TEXT("Deploy") }, Set.bBargeDeployed ? 1 : 0 },
		{ TEXT("Hood Vent"), FVector(170, 0, 60), 150, -18, { TEXT("Open"), TEXT("Close") }, Set.bHoodVentOpen ? 0 : 1 },
		{ TEXT("Exterior Lights"), FVector(222, -60, 46), 165, -8, { TEXT("Off"), TEXT("Daytime Running Light"), TEXT("On"), TEXT("Auto") }, Pawn->ExteriorLights },
	};
	for (int32 i = 0; i < Spots.Num(); ++i)
	{
		const FVector2D S = ProjectCarPoint(Spots[i].P, X, Y, W, H);
		if (S.X < X || S.X > X + W) continue;
		const bool bSel = Hotspot == i;
		UI.Diamond(S.X, S.Y, bSel ? 13 : 10, bSel ? Red : A(Red, 0.85f));
		if (UI.Clicked(S.X - 26, S.Y - 26, 52, 52))
		{
			UI.bConsumedClick = true;
			Hotspot = bSel ? -1 : i;
			if (Hotspot >= 0) { CarYawTarget = Spots[i].Yaw; CarPitchTarget = Spots[i].Pitch; }
		}
		if (!bSel) continue;
		// callout: red header + stacked options joined by a thin leader line
		const float CX = FMath::Clamp((float)S.X + 120.f, 300.f, 1500.f), CY = FMath::Clamp((float)S.Y - 140.f, 140.f, 650.f);
		UI.Line(S.X, S.Y, CX, CY + 30, Red, 1.5f);
		const float BW = 380, BH = 62;
		UI.Rect(CX, CY, BW, BH, HeaderFill);
		UI.Text(Spots[i].Name, CX + 22, CY + BH * 0.5f, 28, FLinearColor::FromSRGBColor(FColor(60, 0, 0)), EAlign::Left, true);
		for (int32 k = 0; k < Spots[i].Opts.Num(); ++k)
		{
			const float OY = CY + BH + 8 + k * (BH + 6);
			UI.Line(CX + 10, OY - 6, CX + 10, OY + BH * 0.5f, RedDim, 1.f);
			if (UI.Button(CX + 20, OY, BW - 20, BH, Spots[i].Opts[k], k == Spots[i].Cur, 26))
			{
				switch (i)
				{
				case 0: Set.Aero = AeroPreset::Custom; Set.RWing = (RearWing)k; break;
				case 1: Set.Aero = AeroPreset::Custom; Set.bBargeDeployed = k == 1; break;
				case 2: Set.Aero = AeroPreset::Custom; Set.bHoodVentOpen = k == 0; break;
				case 3: Pawn->ExteriorLights = k; break;
				}
			}
		}
	}
	// status chips
	const Telemetry& T = Pawn->Sim.T;
	UI.Text(FString::Printf(TEXT("%s   ·   %s"), *FString(TrimName(Pawn->Sim.S.TrimLevel)).ToUpper(), *Speed(T.Speed, true)), 960, 140, 26, RedDim, EAlign::Center);
	if (T.FanDownforceN > 500)
		UI.Text(FString::Printf(TEXT("Suction %.1f kPa  ·  %.1f kN"), T.FanPressureKPa, T.FanDownforceN / 1000.0), 960, 176, 24, Red, EAlign::Center);
}

// ============================================================================ Vehicle tab
void URoadsterInterface::VehicleTab()
{
	const TArray<FString> Pages = { TEXT("Dynamics"), TEXT("Suction"), TEXT("Tires"), TEXT("Thrusters"), TEXT("Lights"), TEXT("Display"), TEXT("Charging") };
	float X = 160;
	for (int32 i = 0; i < Pages.Num(); ++i)
	{
		const bool bOn = (int32)Page == i;
		const float TW = UI.TextWidth(Pages[i], 30, bOn) + 60;
		UI.Text(Pages[i], X + TW * 0.5f, 120, 30, bOn ? Red : RedDim, EAlign::Center, bOn);
		if (bOn) UI.Rect(X + 20, 148, TW - 40, 3, Red);
		if (UI.Clicked(X, 90, TW, 60)) { Page = (EVehiclePage)i; UI.bConsumedClick = true; }
		X += TW;
	}
	const float PX = 140, PY = 180, PW = 1720, PH = 780;
	switch (Page)
	{
	case EVehiclePage::Dynamics: PageDynamics(PX, PY, PW, PH); break;
	case EVehiclePage::Suction: PageSuction(PX, PY, PW, PH); break;
	case EVehiclePage::Tires: PageTires(PX, PY, PW, PH); break;
	case EVehiclePage::Thrusters: PageThrusters(PX, PY, PW, PH); break;
	case EVehiclePage::Lights: PageLights(PX, PY, PW, PH); break;
	case EVehiclePage::Display: PageDisplay(PX, PY, PW, PH); break;
	case EVehiclePage::Charging: PageCharging(PX, PY, PW, PH); break;
	}
}

void URoadsterInterface::PageDynamics(float X, float Y, float W, float H)
{
	Settings& Set = Pawn->Sim.Set;
	const TArray<FString> Names = { TEXT("Comfort"), TEXT("Balance"), TEXT("Sport"), TEXT("Custom") };
	for (int32 i = 0; i < 4; ++i)
	{
		const bool bOn = (int32)Set.Dynamics == i && !Pawn->bTrackMode;
		if (UI.Card(X, Y + i * 150, 380, 136, Names[i], bOn)) Set.Dynamics = (DynamicsPreset)i;
	}
	// Track Mode card with a flag icon
	if (UI.Card(X, Y + 600, 380, 136, TEXT("Track Mode"), false))
	{
		Pawn->bTrackMode = true; Set.Dynamics = DynamicsPreset::Track; bTrackSetup = false;
		Pawn->LapTime = 0; Set.bESC = false; Set.bTCS = false;
	}
	for (int32 r = 0; r < 3; ++r) for (int32 c = 0; c < 4; ++c) UI.Rect(X + 320 + c * 9, Y + 640 + r * 9, 8, 8, ((r + c) % 2) ? Red : RedFaint);

	const float OX = X + 420, OW = 760;
	if (Set.Dynamics != DynamicsPreset::Custom)
	{
		const FString Rows[3][2] = {
			{ TEXT("Acceleration"), AccelName(Set.Acceleration) },
			{ TEXT("Ride & Handling"), HandlingName(Set.RideHandling) },
			{ TEXT("Ride Height"), Set.bLowSuspension ? TEXT("Low") : TEXT("Standard") } };
		for (int32 i = 0; i < 3; ++i)
		{
			UI.Panel(OX, Y + i * 96, OW, 84);
			UI.Text(Rows[i][0], OX + 24, Y + i * 96 + 42, 27, Red);
			UI.Text(Rows[i][1], OX + OW - 24, Y + i * 96 + 42, 27, Red, EAlign::Right, true);
		}
	}
	else
	{
		float RY = Y; const float RH = 84, Gap = 92;
		Set.Acceleration = (Accel)UI.Segmented(OX, RY, OW, RH, TEXT("Acceleration"), { TEXT("Chill"), TEXT("Standard"), TEXT("Sport") }, (int32)Set.Acceleration); RY += Gap;
		Set.RideHandling = (Handling)UI.Segmented(OX, RY, OW, RH, TEXT("Ride & Handling"), { TEXT("Comfort"), TEXT("Balance"), TEXT("Sport") }, (int32)Set.RideHandling); RY += Gap;
		const int32 FW = UI.Segmented(OX, RY, OW, RH, TEXT("Front Wing"), { TEXT("Low Drag"), TEXT("High Downforce") }, (int32)Set.FWing); RY += Gap;
		const int32 RW = UI.Segmented(OX, RY, OW, RH, TEXT("Rear Wing"), { TEXT("Stowed"), TEXT("Low Drag"), TEXT("High Downforce") }, (int32)Set.RWing); RY += Gap;
		Set.bLowSuspension = UI.Segmented(OX, RY, OW, RH, TEXT("Suspension"), { TEXT("Standard"), TEXT("Low") }, Set.bLowSuspension ? 1 : 0) == 1; RY += Gap;
		const int32 V = UI.Segmented(OX, RY, OW, RH, TEXT("Vent Hood"), { TEXT("Close"), TEXT("Open") }, Set.bHoodVentOpen ? 1 : 0); RY += Gap;
		const int32 B = UI.Segmented(OX, RY, OW, RH, TEXT("Barge Board"), { TEXT("Retract"), TEXT("Deploy") }, Set.bBargeDeployed ? 1 : 0); RY += Gap;
		if (FW != (int32)Set.FWing || RW != (int32)Set.RWing || V != (Set.bHoodVentOpen ? 1 : 0) || B != (Set.bBargeDeployed ? 1 : 0))
		{
			Set.Aero = AeroPreset::Custom; Set.FWing = (FrontWing)FW; Set.RWing = (RearWing)RW; Set.bHoodVentOpen = V == 1; Set.bBargeDeployed = B == 1;
		}
	}
	CarYawTarget = 205.f; CarPitchTarget = -12.f;
	CarImage(X + 1200, Y + 160, 560, 316, false);
}

void URoadsterInterface::PageSuction(float X, float Y, float W, float H)
{
	Settings& Set = Pawn->Sim.Set;
	const Telemetry& T = Pawn->Sim.T;
	const Spec& S = Pawn->Sim.S;
	const float OW = 980;
	float RY = Y;
	const int32 FM = UI.Segmented(X, RY, OW, 84, TEXT("Fan"), { TEXT("Off"), TEXT("Braking"), TEXT("Low Drag"), TEXT("Max Grip"), TEXT("Custom") }, (int32)Set.Fan, 200);
	if (FM != (int32)Set.Fan) { Set.Fan = (FanMode)FM; if (Set.Aero != AeroPreset::Custom) Set.Aero = AeroPreset::Custom; }
	RY += 96;
	float Kpa = Set.FanCustomKPa;
	if (UI.Slider(X, RY, OW, 84, TEXT("Underbody pressure"), Kpa, 0.f, (float)S.FanMaxPressureKPa, FString::Printf(TEXT("-%.2f kPa"), Kpa), 300))
	{
		Set.FanCustomKPa = Kpa; Set.Fan = FanMode::Custom; Set.Aero = AeroPreset::Custom;
	}
	RY += 96;
	Set.Load = (AeroLoad)UI.Segmented(X, RY, OW, 84, TEXT("Aero Load"), { TEXT("Low Drag"), TEXT("Balance"), TEXT("Max Grip") }, (int32)Set.Load, 200); RY += 96;
	const int32 Pre = UI.Segmented(X, RY, OW, 84, TEXT("Aero Mode"), { TEXT("Auto"), TEXT("Low Drag"), TEXT("High Downforce"), TEXT("Custom") }, (int32)Set.Aero, 200); RY += 96;
	Set.Aero = (AeroPreset)Pre;
	// balance tiles
	for (int32 b = 0; b < 2; ++b)
	{
		const float BX = X + b * 495, BY = RY, BW = 485, BH = 230;
		int32& Val = b == 0 ? Set.AeroBalance : Set.BrakeBalance;
		UI.Panel(BX, BY, BW, BH);
		UI.Text(b == 0 ? TEXT("Aero Balance") : TEXT("Brake Balance"), BX + 22, BY + 32, 26, Red);
		const float Front = b == 0 ? Set.AeroFrontShare() : Set.BrakeFrontShare();
		UI.Text(TEXT("FRONT"), BX + 22, BY + 90, 15, Red, EAlign::Left, true);
		UI.Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Front * 100)), BX + 22, BY + 116, 24, Red);
		UI.Text(TEXT("REAR"), BX + BW - 22, BY + 90, 15, Red, EAlign::Right, true);
		UI.Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt((1 - Front) * 100)), BX + BW - 22, BY + 116, 24, Red, EAlign::Right);
		UI.Digits(FString::Printf(TEXT("%s%d"), Val > 0 ? TEXT("+") : (Val < 0 ? TEXT("-") : TEXT("")), FMath::Abs(Val)), BX + BW * 0.5f, BY + 112, 78, Red, EAlign::Center);
		for (int32 d = -3; d <= 3; ++d) UI.Disc(BX + BW * 0.5f + d * 22, BY + 196, d == Val ? 5 : 3, d == Val ? Red : RedDim);
		if (UI.Clicked(BX, BY + 50, BW * 0.35f, BH - 50)) { Val = FMath::Max(-3, Val - 1); UI.bConsumedClick = true; }
		if (UI.Clicked(BX + BW * 0.65f, BY + 50, BW * 0.35f, BH - 50)) { Val = FMath::Min(3, Val + 1); UI.bConsumedClick = true; }
	}

	// live readout column
	const float LX = X + 1030, LW = 690;
	UI.Panel(LX, Y, LW, 720);
	UI.Text(TEXT("Suction System"), LX + 24, Y + 40, 30, Red, EAlign::Left, true);
	UI.Text(TEXT("4 axial fans · sealed underfloor · deployable skirts"), LX + 24, Y + 78, 20, RedDim);
	const float Gx = LX + 24, Gw = LW - 48;
	auto Row = [&](float RY2, const FString& L, const FString& V, float Frac)
	{
		UI.Text(L, Gx, RY2, 24, Red);
		UI.Text(V, Gx + Gw, RY2, 26, Red, EAlign::Right, true);
		UI.Bar(Gx, RY2 + 22, Gw, 10, Frac, Red, false);
	};
	Row(Y + 140, TEXT("Underbody pressure"), FString::Printf(TEXT("-%.2f kPa"), T.FanPressureKPa), T.FanPressureKPa / S.FanMaxPressureKPa);
	Row(Y + 220, TEXT("Fan speed"), FString::Printf(TEXT("%d rpm"), FMath::RoundToInt(T.FanRPM)), T.FanRPM / S.FanMaxRPM);
	Row(Y + 300, TEXT("Suction downforce"), FString::Printf(TEXT("%.1f kN"), T.FanDownforceN / 1000.0), T.FanDownforceN / (S.FanMaxPressureKPa * 1000 * S.FanEffectiveArea));
	Row(Y + 380, TEXT("Wing downforce"), FString::Printf(TEXT("%.1f kN"), T.WingDownforceN / 1000.0), T.WingDownforceN / 15000.0);
	Row(Y + 460, TEXT("Skirt seal"), FString::Printf(TEXT("%d%%"), FMath::RoundToInt(T.FanSeal * 100)), T.FanSeal);
	Row(Y + 540, TEXT("Fan power"), FString::Printf(TEXT("%.0f kW"), T.FanPowerKW), T.FanPowerKW / S.FanMaxPowerKW);
	const double Weight = Pawn->Sim.TotalMass() * kG;
	const double Ratio = T.FanDownforceN / Weight;
	UI.Text(FString::Printf(TEXT("Suction / weight  %.2f"), Ratio), Gx, Y + 640, 26, Ratio >= 1.0 ? Green : Red, EAlign::Left, true);
	UI.Text(Ratio >= 1.0 ? TEXT("CAN HOLD INVERTED") : TEXT("needs speed to stay inverted"), Gx + Gw, Y + 640, 20, Ratio >= 1.0 ? Green : RedDim, EAlign::Right);
}

void URoadsterInterface::PageTires(float X, float Y, float W, float H)
{
	Settings& Set = Pawn->Sim.Set;
	const TCHAR* Names[] = { TEXT("Front Left"), TEXT("Front Right"), TEXT("Rear Left"), TEXT("Rear Right") };
	for (int32 i = 0; i < 4; ++i)
	{
		const float TX = X + (i % 2) * 870, TY = Y + (i / 2) * 330, TW = 850, TH = 310;
		const WheelState& Wh = Pawn->Sim.Wheels[i];
		UI.Panel(TX, TY, TW, TH);
		UI.Text(Names[i], TX + 24, TY + 36, 28, Red, EAlign::Left, true);
		UI.Text(i < 2 ? TEXT("295/30 ZR20  P ZERO TROFEO RS") : TEXT("335/30 ZR21  P ZERO TROFEO RS"), TX + TW - 24, TY + 36, 18, RedDim, EAlign::Right);
		UI.Digits(Pressure(Wh.PressureKPa, false), TX + 24, TY + 120, 72, Red);
		UI.Text(TEXT("HOT"), TX + 24 + UI.DigitsWidth(Pressure(Wh.PressureKPa, false), 72) + 14, TY + 104, 16, RedDim, EAlign::Left, true);
		UI.Text(Pressure(Wh.PressureKPa, true).RightChop(Pressure(Wh.PressureKPa, false).Len()), TX + 24 + UI.DigitsWidth(Pressure(Wh.PressureKPa, false), 72) + 14, TY + 132, 22, Red);
		UI.Text(FString::Printf(TEXT("Surface %s   Carcass %s   Grip %d%%"), *Temp(Wh.SurfaceTempC), *Temp(Wh.CarcassTempC), FMath::RoundToInt(Wh.GripFactor * 100)), TX + TW - 24, TY + 120, 22, Red, EAlign::Right);
		float Cold = Set.ColdPressureKPa[i] * kPsiPerKPa;
		if (UI.Slider(TX + 10, TY + 190, TW - 20, 80, TEXT("Cold pressure"), Cold, 14.f, 45.f, FString::Printf(TEXT("%.1f psi"), Cold), 220))
			Set.ColdPressureKPa[i] = Cold / kPsiPerKPa;
	}
	if (UI.Button(X, Y + 680, 420, 76, TEXT("Reset all to 23 psi cold"), false))
		for (double& P : Set.ColdPressureKPa) P = Pawn->Sim.S.DefaultColdPressureKPa;
	if (UI.Button(X + 440, Y + 680, 420, 76, TEXT("Fronts -1 psi"), false))
		for (int32 i = 0; i < 2; ++i) Set.ColdPressureKPa[i] -= 1.0 / kPsiPerKPa;
	if (UI.Button(X + 880, Y + 680, 420, 76, TEXT("Rears -1 psi"), false))
		for (int32 i = 2; i < 4; ++i) Set.ColdPressureKPa[i] -= 1.0 / kPsiPerKPa;
	UI.Text(TEXT("Optimum: 28 psi hot · grip falls away either side; pressure rises with carcass heat"), X + 1720, Y + 718, 20, RedDim, EAlign::Right);
}

void URoadsterInterface::PageThrusters(float X, float Y, float W, float H)
{
	Settings& Set = Pawn->Sim.Set;
	const Telemetry& T = Pawn->Sim.T;
	const Spec& S = Pawn->Sim.S;
	float RY = Y;
	const int32 TrimIdx = UI.Segmented(X, RY, 980, 84, TEXT("Augment"), { TEXT("Performance"), TEXT("Thrust"), TEXT("Rocket") }, (int32)S.TrimLevel, 200);
	if (TrimIdx != (int32)S.TrimLevel) Pawn->SetTrim((Trim)TrimIdx);
	RY += 96;
	Set.bThrustersArmed = UI.Segmented(X, RY, 980, 84, TEXT("Thrusters"), { TEXT("Safe"), TEXT("Armed") }, Set.bThrustersArmed ? 1 : 0, 200) == 1; RY += 96;
	float Lvl = Set.BoostLevel;
	if (UI.Slider(X, RY, 980, 84, TEXT("Valve authority"), Lvl, 0.25f, 1.f, FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Lvl * 100)), 300)) Set.BoostLevel = Lvl;
	RY += 96;
	Set.bFlightAssist = UI.Segmented(X, RY, 980, 84, TEXT("Flight assist"), { TEXT("Off"), TEXT("On") }, Set.bFlightAssist ? 1 : 0, 200) == 1; RY += 96;
	Set.bInfiniteGas = UI.Segmented(X, RY, 980, 84, TEXT("Gas supply"), { TEXT("Tank (real)"), TEXT("Unlimited (demo)") }, Set.bInfiniteGas ? 1 : 0, 200) == 1; RY += 96;
	if (UI.Button(X, RY + 10, 480, 80, TEXT("Refill nitrogen (pit)"), false)) Pawn->Sim.RefillTank();
	if (UI.Button(X + 500, RY + 10, 480, 80, TEXT("Recharge battery (pit)"), false)) Pawn->Sim.Recharge();

	const float LX = X + 1030, LW = 690;
	UI.Panel(LX, Y, LW, 720);
	UI.Text(TEXT("Cold-Gas Thrusters"), LX + 24, Y + 40, 30, Red, EAlign::Left, true);
	UI.Text(S.TankVolumeL > 0 ? FString::Printf(TEXT("%d L carbon COPV · nitrogen · %s"), FMath::RoundToInt(S.TankVolumeL), S.bHasLiftThrusters ? TEXT("4 rear + 4 lift + 2 yaw") : TEXT("4 rear nozzles")) : FString(TEXT("Not fitted on Performance")), LX + 24, Y + 78, 20, RedDim);
	if (S.TankVolumeL > 0)
	{
		UI.Digits(FString::Printf(TEXT("%d"), FMath::RoundToInt(T.TankBar * kPsiPerBar)), LX + LW * 0.5f, Y + 230, 120, Red, EAlign::Center);
		UI.Text(TEXT("PSI"), LX + LW * 0.5f, Y + 320, 24, RedDim, EAlign::Center, true);
		UI.Bar(LX + 24, Y + 380, LW - 48, 22, T.TankPercent, Red, true);
		UI.Text(FString::Printf(TEXT("%d%%  ·  %.1f kg N₂"), FMath::RoundToInt(T.TankPercent * 100), T.TankKg), LX + 24, Y + 440, 26, Red);
		const double Impulse = T.TankKg * S.ExhaustVelocity / 1000.0;
		UI.Text(FString::Printf(TEXT("Impulse left %.0f kN·s"), Impulse), LX + LW - 24, Y + 440, 24, Red, EAlign::Right);
		UI.Text(FString::Printf(TEXT("Rear thrust at this pressure  %.0f kN"), S.RearThrustFullN * FMath::Clamp(T.TankBar / S.TankFullBar, 0.0, 1.0) * Set.BoostLevel / 1000.0), LX + 24, Y + 520, 24, Red);
		if (S.bHasLiftThrusters)
			UI.Text(FString::Printf(TEXT("Lift (4 corners)  %.0f kN vs weight %.0f kN"), 4 * S.LiftThrustFullN * FMath::Clamp(T.TankBar / S.TankFullBar, 0.0, 1.0) * Set.BoostLevel / 1000.0, Pawn->Sim.TotalMass() * kG / 1000.0), LX + 24, Y + 570, 24, Red);
		UI.Text(T.TankPercent > 0.98 ? TEXT("BOOST READY") : (T.TankPercent > 0.05 ? TEXT("BOOST AVAILABLE") : TEXT("REFILL REQUIRED")), LX + LW * 0.5f, Y + 660, 30, T.TankPercent > 0.05 ? Red : Amber, EAlign::Center, true);
	}
}

void URoadsterInterface::PageLights(float X, float Y, float W, float H)
{
	Pawn->ExteriorLights = UI.Segmented(X, Y, 1100, 84, TEXT("Exterior Lights"), { TEXT("Off"), TEXT("DRL"), TEXT("On"), TEXT("Auto") }, Pawn->ExteriorLights, 260);
}

void URoadsterInterface::PageDisplay(float X, float Y, float W, float H)
{
	UI.Slider(X, Y, 1100, 84, TEXT("Brightness"), Brightness, 0.3f, 1.f, FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Brightness * 100)));
	bMetric = UI.Segmented(X, Y + 96, 1100, 84, TEXT("Distance"), { TEXT("Miles"), TEXT("Kilometers") }, bMetric ? 1 : 0, 260) == 1;
	bCelsius = UI.Segmented(X, Y + 192, 1100, 84, TEXT("Temperature"), { TEXT("°F"), TEXT("°C") }, bCelsius ? 1 : 0, 260) == 1;
	PressureUnit = UI.Segmented(X, Y + 288, 1100, 84, TEXT("Tire Pressure"), { TEXT("PSI"), TEXT("BAR"), TEXT("kPa") }, PressureUnit, 260);
}

void URoadsterInterface::PageCharging(float X, float Y, float W, float H)
{
	const Telemetry& T = Pawn->Sim.T;
	CarYawTarget = 270.f; CarPitchTarget = -10.f;
	CarImage(X + 260, Y, 1200, 520, true);
	const float BX = X + 260, BY = Y + 560, BW = 1200;
	UI.Rect(BX, BY, BW, 26, RedFaint);
	UI.Rect(BX, BY, BW * T.SOC, 26, Red);
	UI.Rect(BX + BW * ChargeLimit - 2, BY - 12, 5, 50, Green);
	UI.Digits(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(T.SOC * 100)), BX, BY + 110, 72, Red);
	UI.Text(FString::Printf(TEXT("%s  estimated range"), bMetric ? *FString::Printf(TEXT("%d km"), FMath::RoundToInt(T.RangeKm)) : *FString::Printf(TEXT("%d mi"), FMath::RoundToInt(T.RangeKm / 1.609))), BX + BW, BY + 110, 28, Red, EAlign::Right);
	UI.Slider(BX, BY + 170, BW, 76, TEXT("Charge limit"), ChargeLimit, 0.5f, 1.f, FString::Printf(TEXT("%d%%"), FMath::RoundToInt(ChargeLimit * 100)));
}

// ============================================================================ Track Mode
void URoadsterInterface::ThrusterBar(float X, float Y, float W, float H)
{
	const Telemetry& T = Pawn->Sim.T;
	const bool bHas = Pawn->Sim.S.TankVolumeL > 0;
	for (int32 i = 0; i < 16; ++i)
	{
		const float a = i / 16.f;
		UI.Rect(X, Y + H * a, W * (bHas ? T.TankPercent : 0.f), H / 16.f + 1, A(Red, 0.05f + 0.35f * a));
	}
	UI.Rect(X, Y + H - 3, W, 3, Red);
	UI.Text(bHas ? FString::Printf(TEXT("Thruster  ·  %s PSI"), *FString::FormatAsNumber(FMath::RoundToInt(T.TankBar * kPsiPerBar))) : FString(TEXT("Thruster  ·  not fitted")), X + 24, Y + H * 0.5f, 26, Red, EAlign::Left, true);
	const FString R = bHas ? FString::Printf(TEXT("%d%%  ·  %s"), FMath::RoundToInt(T.TankPercent * 100), T.ThrustRear > 0.02 ? TEXT("BOOST") : (T.TankPercent > 0.98 ? TEXT("Boost Ready") : TEXT("Boost Available"))) : FString();
	UI.Text(R, X + W - 24, Y + H * 0.5f, 26, Red, EAlign::Right, true);
}

void URoadsterInterface::DrawTrackMap(float X, float Y, float W, float H, bool bPerspective)
{
	ATrackActor* Track = Pawn->FindTrack();
	if (!Track) return;
	const TArray<FVector>& Pts = Track->GetCenterline();
	if (Pts.Num() < 2) return;
	FBox Box(Pts);
	const FVector C = Box.GetCenter();
	const float Ext = FMath::Max(Box.GetExtent().X, Box.GetExtent().Y) * 1.1f + 1.f;
	auto Map = [&](const FVector& P)
	{
		float u = (P.X - C.X) / Ext, v = (P.Y - C.Y) / Ext;   // -1..1
		if (bPerspective)
		{
			// tilt the map away from the viewer (Tesla's lap map sits on a perspective floor)
			const float d = 1.f / (1.6f - v * 0.55f);
			u *= d * 1.3f; v = v * d;
		}
		return FVector2D(X + W * 0.5f + v * W * 0.45f, Y + H * 0.5f - u * H * 0.45f);
	};
	FVector2D Prev = Map(Pts[0]);
	for (int32 i = 1; i < Pts.Num(); ++i)
	{
		const FVector2D P = Map(Pts[i]);
		UI.Line(Prev.X, Prev.Y, P.X, P.Y, A(Red, 0.25f), 9.f);
		UI.Line(Prev.X, Prev.Y, P.X, P.Y, Red, 3.5f);
		Prev = P;
	}
	const FVector2D Car = Map(Pawn->GetActorLocation());
	const FVector F = Pawn->GetActorForwardVector();
	const FVector2D Dir = (Map(Pawn->GetActorLocation() + F * 2000.f) - Car).GetSafeNormal();
	const FVector2D N(-Dir.Y, Dir.X);
	UI.Line(Car.X + Dir.X * 18, Car.Y + Dir.Y * 18, Car.X - Dir.X * 10 + N.X * 11, Car.Y - Dir.Y * 10 + N.Y * 11, White, 4);
	UI.Line(Car.X + Dir.X * 18, Car.Y + Dir.Y * 18, Car.X - Dir.X * 10 - N.X * 11, Car.Y - Dir.Y * 10 - N.Y * 11, White, 4);
}

void URoadsterInterface::TrackDashboard()
{
	const Telemetry& T = Pawn->Sim.T;
	const Settings& Set = Pawn->Sim.Set;
	const float G = 14, Top = 90, ColW[4] = { 420, 420, 860, 400 };
	float ColX[4]; ColX[0] = 16; for (int32 i = 1; i < 4; ++i) ColX[i] = ColX[i - 1] + ColW[i - 1] + G;

	auto BarTile = [&](float X, float Y, float W, const FString& L, const FString& V, float F)
	{
		UI.Panel(X, Y, W, 120);
		UI.Text(L, X + 20, Y + 34, 24, Red);
		UI.Text(V, X + W - 20, Y + 34, 24, Red, EAlign::Right, true);
		UI.Bar(X + 20, Y + 70, W - 40, 12, F, Red);
	};
	// column 1
	float Y = Top;
	BarTile(ColX[0], Y, ColW[0], TEXT("Brake"), FString::Printf(TEXT("%d%%"), FMath::RoundToInt(T.BrakePedal * 100)), T.BrakePedal); Y += 134;
	BarTile(ColX[0], Y, ColW[0], TEXT("Regen"), FString::Printf(TEXT("%d kW"), FMath::RoundToInt(T.RegenKW)), T.RegenKW / Pawn->Sim.S.MaxRegenKW); Y += 134;
	BarTile(ColX[0], Y, ColW[0], TEXT("Battery Temp"), Temp(T.BatteryTempC), FMath::Clamp((T.BatteryTempC - 20) / 40.0, 0.0, 1.0)); Y += 134;
	BarTile(ColX[0], Y, ColW[0], TEXT("Motor Temp"), Temp(T.MotorTempC), FMath::Clamp((T.MotorTempC - 30) / 120.0, 0.0, 1.0)); Y += 134;
	// brake temp: top-view car with a value at each corner
	auto CornerTile = [&](float X, float Y0, float W, float H, const FString& L, const FString& Unit, TFunctionRef<FString(int32)> Val)
	{
		UI.Panel(X, Y0, W, H);
		UI.Text(L, X + 20, Y0 + 34, 24, Red);
		UI.Text(Unit, X + W - 20, Y0 + 34, 22, Red, EAlign::Right);
		const float CX = X + W * 0.5f, CY = Y0 + H * 0.58f;
		UI.Outline(CX - 34, CY - 92, 68, 184, Red, 1.5f);
		UI.Outline(CX - 26, CY - 50, 52, 70, RedDim, 1.f);
		for (int32 i = 0; i < 4; ++i)
		{
			const float WX = CX + ((i % 2) ? 40 : -52), WY = CY + (i < 2 ? -78 : 46);
			UI.Rect(WX, WY, 12, 34, Red);
			UI.Text(Val(i), CX + ((i % 2) ? 64 : -64), WY + 16, 23, Red, (i % 2) ? EAlign::Left : EAlign::Right, true);
		}
	};
	CornerTile(ColX[0], Y, ColW[0], 1000 - Y, TEXT("Brake Temp"), bCelsius ? TEXT("°C") : TEXT("°F"), [&](int32 i) { return Temp(Pawn->Sim.Wheels[i].BrakeTempC); });

	// column 2
	Y = Top;
	BarTile(ColX[1], Y, ColW[1], TEXT("Accel"), FString::Printf(TEXT("%d%%"), FMath::RoundToInt(T.AccelPedal * 100)), T.AccelPedal); Y += 134;
	{
		const float MX = ColX[1], MY = Y, MW = ColW[1], MH = 400;
		UI.Panel(MX, MY, MW, MH);
		UI.Text(TEXT("Meter"), MX + 20, MY + 34, 24, Red);
		const float CX = MX + MW * 0.5f, CY = MY + MH * 0.55f, R = 140;
		for (int32 r = -4; r <= 4; ++r) for (int32 c = -4; c <= 4; ++c)
			if (r * r + c * c <= 17) UI.Rect(CX + c * R / 4 - 1, CY + r * R / 4 - 1, 2.5f, 2.5f, RedDim);
		UI.Text(TEXT("3.5"), CX, CY - R - 18, 20, Red, EAlign::Center, true);
		UI.Text(TEXT("3.5"), CX, CY + R + 18, 20, Red, EAlign::Center, true);
		UI.Text(TEXT("3.5"), CX - R - 26, CY, 20, Red, EAlign::Center, true);
		UI.Text(TEXT("3.5"), CX + R + 26, CY, 20, Red, EAlign::Center, true);
		const float gx = FMath::Clamp((float)T.AccelG.y / 3.5f, -1.f, 1.f), gy = FMath::Clamp(-(float)T.AccelG.x / 3.5f, -1.f, 1.f);
		UI.Circle(CX + gx * R, CY + gy * R, 14, Red, 2);
		UI.Disc(CX + gx * R, CY + gy * R, 7, Red);
		UI.Text(FString::Printf(TEXT("%.2f g"), FMath::Sqrt(T.AccelG.x * T.AccelG.x + T.AccelG.y * T.AccelG.y)), MX + MW - 20, MY + 34, 22, Red, EAlign::Right, true);
		Y += MH + 14;
	}
	CornerTile(ColX[1], Y, ColW[1], 1000 - Y, TEXT("Tire"), PressureUnit == 0 ? (bCelsius ? TEXT("PSI · °C") : TEXT("PSI · °F")) : TEXT("P · T"),
		[&](int32 i) { return FString::Printf(TEXT("%s\n%s"), *Pressure(Pawn->Sim.Wheels[i].PressureKPa, false), *Temp(Pawn->Sim.Wheels[i].SurfaceTempC)); });

	// column 3: lap map + session
	{
		const float MX = ColX[2], MY = Top, MW = ColW[2], MH = 500;
		UI.Panel(MX, MY, MW, MH);
		UI.Text(TEXT("Lap"), MX + 20, MY + 34, 24, Red);
		UI.Text(TEXT("Elevation"), MX + MW - 20, MY + 34, 22, Red, EAlign::Right);
		UI.Text(FString::Printf(TEXT("%d ft"), FMath::RoundToInt(Pawn->GetActorLocation().Z / 30.48f)), MX + MW - 20, MY + 62, 24, Red, EAlign::Right, true);
		UI.DotFloor(MX + 10, MY + 60, MW - 20, MH - 70, 0.f);
		DrawTrackMap(MX + 40, MY + 60, MW - 200, MH - 80, true);
		UI.Text(TEXT("S1"), MX + MW - 130, MY + MH - 120, 22, Red); UI.Text(TEXT("S2"), MX + MW - 130, MY + MH - 80, 22, Red); UI.Text(TEXT("S3"), MX + MW - 130, MY + MH - 40, 22, Red);
		UI.Text(Pawn->LastLap > 0 ? FString::Printf(TEXT("%.1f"), Pawn->LastLap / 3) : TEXT("---"), MX + MW - 24, MY + MH - 120, 22, Red, EAlign::Right, true);
		UI.Text(TEXT("---"), MX + MW - 24, MY + MH - 80, 22, Red, EAlign::Right);
		UI.Text(TEXT("---"), MX + MW - 24, MY + MH - 40, 22, Red, EAlign::Right);

		const float SY = MY + MH + 14, SH = 1000 - SY;
		UI.Panel(MX, SY, MW, SH);
		UI.Text(TEXT("Session"), MX + 20, SY + 34, 24, Red);
		UI.Text(TEXT("Last"), MX + 30, SY + 100, 24, Red);
		UI.Text(LapStr(Pawn->LastLap), MX + 30, SY + 150, 46, Red, EAlign::Left, true);
		UI.Text(TEXT("Best"), MX + 30, SY + 240, 24, Red);
		UI.Text(LapStr(Pawn->BestLap), MX + 30, SY + 290, 46, Red, EAlign::Left, true);
		for (int32 i = 0; i < Pawn->LapHistory.Num() && i < 10; ++i)
		{
			const float LX = MX + 360 + (i / 5) * 250, LY = SY + 90 + (i % 5) * 52;
			UI.Text(FString::Printf(TEXT("%d"), Pawn->LapCount - i), LX, LY, 22, Red, EAlign::Left, true);
			UI.Text(LapStr(Pawn->LapHistory[i]), LX + 60, LY, 22, Red);
		}
		// drag-strip results also live here
		if (T.T60 > 0)
			UI.Text(FString::Printf(TEXT("0-60 %.2fs (%.2fs 1-ft)   1/4 mi %s"), T.T60, T.T60Rollout, T.TQuarter > 0 ? *FString::Printf(TEXT("%.2fs @ %.0f mph"), T.TQuarter, T.QuarterTrapMph) : TEXT("--")), MX + MW - 20, SY + 34, 20, Red, EAlign::Right);
	}

	// column 4: dynamics / aero status
	{
		const float X = ColX[3], W = ColW[3];
		float Y4 = Top;
		const float H1 = 330;
		UI.Panel(X, Y4, W, H1);
		const FString Rows[6][2] = {
			{ TEXT("Dynamics"), AeroPresetName(Set.Aero) },
			{ TEXT("Front Wing"), FrontWingName(Pawn->Sim.FrontWingAngle > 0.5 ? FrontWing::HighDownforce : FrontWing::LowDrag) },
			{ TEXT("Rear Wing"), Pawn->Sim.WingAngle > 0.7 ? TEXT("High Downforce") : (Pawn->Sim.WingAngle > 0.2 ? TEXT("Low Drag") : TEXT("Stowed")) },
			{ TEXT("Hood Vent"), Set.bHoodVentOpen ? TEXT("Open") : TEXT("Closed") },
			{ TEXT("Barge Boards"), Set.bBargeDeployed ? TEXT("Deploy") : TEXT("Retract") },
			{ TEXT("Aero Load"), AeroLoadName(Set.Load) } };
		for (int32 i = 0; i < 6; ++i)
		{
			UI.Text(Rows[i][0], X + 20, Y4 + 36 + i * 50 + (i > 0 ? 20 : 0), 22, Red);
			UI.Text(Rows[i][1], X + W - 20, Y4 + 36 + i * 50 + (i > 0 ? 20 : 0), 22, Red, EAlign::Right, true);
		}
		Y4 += H1 + 14;
		UI.Panel(X, Y4, W, 130);
		UI.Text(TEXT("Fan"), X + 20, Y4 + 36, 24, Red);
		UI.Text(T.FanRPM > 100 ? TEXT("ON") : TEXT("OFF"), X + W - 20, Y4 + 36, 24, Red, EAlign::Right, true);
		UI.Text(FanModeName(Set.Fan), X + 20, Y4 + 88, 24, Red, EAlign::Left, true);
		UI.Text(FString::Printf(TEXT("-%.1f kPa"), T.FanPressureKPa), X + W - 20, Y4 + 88, 22, Red, EAlign::Right);
		Y4 += 144;
		for (int32 b = 0; b < 2; ++b)
		{
			UI.Panel(X, Y4, W, 120);
			const float F = b == 0 ? Set.BrakeFrontShare() : Set.AeroFrontShare();
			const int32 V = b == 0 ? Set.BrakeBalance : Set.AeroBalance;
			UI.Text(b == 0 ? TEXT("Brake Balance") : TEXT("Aero Balance"), X + 20, Y4 + 34, 24, Red);
			UI.Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(F * 100)), X + 20, Y4 + 84, 24, Red, EAlign::Left, true);
			UI.Text(FString::Printf(TEXT("%s%d"), V > 0 ? TEXT("+") : TEXT(""), V), X + W * 0.5f, Y4 + 84, 24, Red, EAlign::Center, true);
			UI.Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt((1 - F) * 100)), X + W - 20, Y4 + 84, 24, Red, EAlign::Right, true);
			Y4 += 134;
		}
		UI.Panel(X, Y4, W, 1000 - Y4);
		UI.Text(TEXT("ESC"), X + 20, Y4 + 40, 24, Red); UI.Text(Set.bESC ? TEXT("ON") : TEXT("OFF"), X + 20, Y4 + 86, 24, Red, EAlign::Left, true);
		UI.Text(TEXT("TCS"), X + W * 0.55f, Y4 + 40, 24, Red); UI.Text(Set.bTCS ? TEXT("ON") : TEXT("OFF"), X + W * 0.55f, Y4 + 86, 24, Red, EAlign::Left, true);
	}

	// bottom bar: Setup | thruster | Exit Track Mode
	const float BY = 1040, BH = 110;
	if (UI.Button(16, BY, 420, BH, TEXT("Setup"), false, 28)) bTrackSetup = true;
	ThrusterBar(450, BY, 1040, BH);
	if (UI.Button(1504, BY, 400, BH, TEXT("⎗  Exit Track Mode"), false, 28))
	{
		Pawn->bTrackMode = false;
		Pawn->Sim.Set.Dynamics = DynamicsPreset::Sport;
		Pawn->Sim.Set.bESC = Pawn->Sim.Set.bTCS = true;
	}
}

void URoadsterInterface::TrackSetupPage()
{
	Settings& Set = Pawn->Sim.Set;
	const TArray<FString> Modes = { TEXT("Auto"), TEXT("Low Drag"), TEXT("High Downforce"), TEXT("Custom") };
	for (int32 i = 0; i < 4; ++i)
		if (UI.Card(150 + i * 420, 110, 400, 210, Modes[i], (int32)Set.Aero == i)) Set.Aero = (AeroPreset)i;
	UI.Text(TEXT("‹"), 70, 600, 60, RedDim, EAlign::Center);
	if (UI.Clicked(30, 520, 80, 160)) { bTrackSetup = false; UI.bConsumedClick = true; }

	float RY = 340; const float LX = 150, LW = 820, RH = 92, Gap = 104;
	const bool bCustom = Set.Aero == AeroPreset::Custom;
	auto Seg = [&](const FString& L, const TArray<FString>& O, int32 Cur) { const int32 V = UI.Segmented(LX, RY, LW, RH, L, O, Cur, 240); RY += Gap; return V; };
	const int32 FW = Seg(TEXT("Front Wing"), { TEXT("Low Drag"), TEXT("High Downforce") }, (int32)Set.FWing);
	const int32 RW = Seg(TEXT("Rear Wing"), { TEXT("Stowed"), TEXT("Low Drag"), TEXT("High Downforce") }, (int32)Set.RWing);
	const int32 AL = Seg(TEXT("Aero Load"), { TEXT("Low Drag"), TEXT("Balance"), TEXT("Max Grip") }, (int32)Set.Load);
	const int32 FN = Seg(TEXT("Fan"), { TEXT("Off"), TEXT("Braking"), TEXT("Low Drag"), TEXT("Max Grip") }, FMath::Min((int32)Set.Fan, 3));
	if (FW != (int32)Set.FWing || RW != (int32)Set.RWing || FN != FMath::Min((int32)Set.Fan, 3))
	{
		Set.Aero = AeroPreset::Custom; Set.FWing = (FrontWing)FW; Set.RWing = (RearWing)RW; Set.Fan = (FanMode)FN;
	}
	Set.Load = (AeroLoad)AL;
	// hood vent & barge boards as single toggles
	if (UI.Button(LX, RY, 400, RH, FString::Printf(TEXT("Hood Vent      %s"), Set.bHoodVentOpen ? TEXT("Open") : TEXT("Closed")), Set.bHoodVentOpen, 25)) { Set.bHoodVentOpen = !Set.bHoodVentOpen; Set.Aero = AeroPreset::Custom; }
	if (UI.Button(LX + 420, RY, 400, RH, FString::Printf(TEXT("Barge Boards   %s"), Set.bBargeDeployed ? TEXT("Deploy") : TEXT("Retract")), Set.bBargeDeployed, 25)) { Set.bBargeDeployed = !Set.bBargeDeployed; Set.Aero = AeroPreset::Custom; }

	// balance tiles + ESC / TCS
	for (int32 b = 0; b < 2; ++b)
	{
		const float BX = 1000, BY = 340 + b * 250, BW = 770, BH = 236;
		int32& Val = b == 0 ? Set.AeroBalance : Set.BrakeBalance;
		const float Front = b == 0 ? Set.AeroFrontShare() : Set.BrakeFrontShare();
		UI.Panel(BX, BY, BW, BH);
		UI.Text(b == 0 ? TEXT("Aero Balance") : TEXT("Brake Balance"), BX + 24, BY + 36, 26, Red);
		UI.Text(TEXT("FRONT"), BX + 24, BY + 100, 16, Red, EAlign::Left, true);
		UI.Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Front * 100)), BX + 24, BY + 128, 26, Red);
		UI.Text(TEXT("REAR"), BX + BW - 24, BY + 100, 16, Red, EAlign::Right, true);
		UI.Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt((1 - Front) * 100)), BX + BW - 24, BY + 128, 26, Red, EAlign::Right);
		UI.Digits(FString::Printf(TEXT("%s%d"), Val > 0 ? TEXT("+") : (Val < 0 ? TEXT("-") : TEXT("")), FMath::Abs(Val)), BX + BW * 0.5f, BY + 118, 96, Red, EAlign::Center);
		for (int32 d = -3; d <= 3; ++d) UI.Disc(BX + BW * 0.5f + d * 24, BY + 206, d == Val ? 5 : 3, d == Val ? Red : RedDim);
		if (UI.Clicked(BX, BY + 50, BW * 0.35f, BH - 50)) { Val = FMath::Max(-3, Val - 1); UI.bConsumedClick = true; }
		if (UI.Clicked(BX + BW * 0.65f, BY + 50, BW * 0.35f, BH - 50)) { Val = FMath::Min(3, Val + 1); UI.bConsumedClick = true; }
	}
	if (UI.Button(1000, 840, 380, 104, FString::Printf(TEXT("ESC      %s"), Set.bESC ? TEXT("ON") : TEXT("OFF")), !Set.bESC, 28)) Set.bESC = !Set.bESC;
	if (UI.Button(1390, 840, 380, 104, FString::Printf(TEXT("TCS      %s"), Set.bTCS ? TEXT("ON") : TEXT("OFF")), !Set.bTCS, 28)) Set.bTCS = !Set.bTCS;

	const float BY = 1040, BH = 110;
	if (UI.Button(16, BY, 420, BH, TEXT("Dashboard"), false, 28)) bTrackSetup = false;
	ThrusterBar(450, BY, 1040, BH);
	if (UI.Button(1504, BY, 400, BH, TEXT("⎗  Exit Track Mode"), false, 28)) { Pawn->bTrackMode = false; bTrackSetup = false; Set.Dynamics = DynamicsPreset::Sport; Set.bESC = Set.bTCS = true; }
}

// ============================================================================ other tabs
void URoadsterInterface::ListenTab()
{
	UI.Rect(160, 120, 420, 420, RedFaint);
	for (int32 i = 0; i < 12; ++i) UI.Rect(190 + i * 31, 300 - (i % 4) * 30, 18, 60 + (i % 4) * 60, A(Red, 0.6f + 0.03f * i));
	UI.Text(TEXT("Last Listened"), 620, 150, 24, RedDim);
	UI.Text(TEXT("Glass Weather"), 620, 220, 56, Red, EAlign::Left, true);
	UI.Text(TEXT("Lumen Vale"), 620, 290, 34, RedDim);
	UI.Button(620, 360, 260, 80, TEXT("Play Album"), true, 28);
	UI.Text(TEXT("Recently Played"), 160, 600, 28, Red, EAlign::Left, true);
	for (int32 i = 0; i < 6; ++i) { UI.Rect(160 + i * 290, 640, 270, 270, i % 2 ? RedFaint : Tile); UI.Outline(160 + i * 290, 640, 270, 270, TileBorder, 1); }
}

void URoadsterInterface::ClimateTab()
{
	UI.Digits(FString::Printf(TEXT("%d"), FMath::RoundToInt(bCelsius ? (ClimateF - 32) * 5 / 9 : ClimateF)), 960, 420, 260, Red, EAlign::Center);
	if (UI.Button(560, 360, 140, 140, TEXT("−"), false, 60)) ClimateF -= 1;
	if (UI.Button(1220, 360, 140, 140, TEXT("+"), false, 60)) ClimateF += 1;
	UI.Text(TEXT("AUTO"), 960, 640, 40, Red, EAlign::Center, true);
	UI.Text(FString::Printf(TEXT("Cabin %s   ·   Outside %s"), *Temp((ClimateF - 32) * 5 / 9), *Temp(Pawn->Sim.S.AmbientC)), 960, 720, 28, RedDim, EAlign::Center);
}

void URoadsterInterface::NavigateTab()
{
	UI.DotFloor(100, 140, 1720, 820, 0);
	DrawTrackMap(160, 120, 1600, 860, false);
	if (ATrackActor* Track = Pawn->FindTrack())
		UI.Text(Track->TrackName, 160, 150, 34, Red, EAlign::Left, true);
}

// ============================================================================ forward displays
void URoadsterInterface::DrawForwardDisplay(UCanvas* Canvas, int32 W, int32 H, int32 Index)
{
	if (!Pawn || !Canvas) return;
	FContext F;
	// map a 1920x528 virtual strip onto the 800x220 target (keeps 1920-wide coordinates)
	F.Begin(Canvas, FVector2D(0, 0), FVector2D(W, H * (FContext::VH / 528.f)), FVector2D(-1, -1), false, false, false);
	F.Rect(0, 0, FContext::VW, 528, Black);
	const Telemetry& T = Pawn->Sim.T;
	if (Index == 0)
	{
		F.Digits(Speed(T.Speed), 760, 250, 300, Red, EAlign::Right, 0.15f);
		F.Text(bMetric ? TEXT("KM/H") : TEXT("MPH"), 800, 360, 44, RedDim, EAlign::Left, true);
		const TCHAR* Gear = T.bReverse ? TEXT("R") : (T.Speed < 0.1 && Pawn->Input.Brake > 0.5 ? TEXT("P") : TEXT("D"));
		F.Text(Gear, 800, 160, 90, Red, EAlign::Left, true);
		// power (right) / regen (left) bar
		const float P = FMath::Clamp((float)T.PowerKW / 1000.f, -0.35f, 1.f);
		F.Rect(1000, 230, 860, 22, RedFaint);
		if (P >= 0) F.Rect(1000 + 240, 230, 620 * P, 22, Red); else F.Rect(1000 + 240 + 240 * P / 0.35f * 1.f, 230, -240 * P / 0.35f, 22, Green);
		F.Rect(1238, 210, 4, 62, White);
		F.Text(FString::Printf(TEXT("%d kW"), FMath::RoundToInt(T.PowerKW)), 1860, 320, 50, Red, EAlign::Right, true);
		F.Text(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(T.SOC * 100)), 1000, 320, 44, RedDim);
		if (T.bTC) F.Text(TEXT("TC"), 1000, 120, 46, Amber, EAlign::Left, true);
		if (T.bABS) F.Text(TEXT("ABS"), 1100, 120, 46, Amber, EAlign::Left, true);
		if (T.bLaunch) F.Text(TEXT("LAUNCH"), 1860, 120, 46, Red, EAlign::Right, true);
		if (T.bAirborne) F.Text(FString::Printf(TEXT("AIR %.1fs"), T.AirTime), 1860, 120, 46, White, EAlign::Right, true);
	}
	else
	{
		if (Pawn->bTrackMode)
		{
			F.Text(TEXT("LAP"), 60, 120, 44, RedDim, EAlign::Left, true);
			F.Text(LapStr(Pawn->LapTime), 60, 260, 150, Red, EAlign::Left, true);
			F.Text(FString::Printf(TEXT("BEST %s"), *LapStr(Pawn->BestLap)), 60, 420, 50, RedDim);
		}
		else
		{
			F.Text(TEXT("0-60"), 60, 120, 44, RedDim, EAlign::Left, true);
			F.Digits(T.T60 > 0 ? FString::Printf(TEXT("%.2f"), T.T60) : FString(TEXT("-.--")), 60, 270, 190, Red);
			F.Text(T.TQuarter > 0 ? FString::Printf(TEXT("1/4 %.2fs @ %.0f mph"), T.TQuarter, T.QuarterTrapMph) : FString(TEXT("1/4 MILE --")), 60, 430, 46, RedDim);
		}
		const float g = FMath::Sqrt(T.AccelG.x * T.AccelG.x + T.AccelG.y * T.AccelG.y);
		F.Digits(FString::Printf(TEXT("%.1f"), g), 1500, 230, 170, Red, EAlign::Right);
		F.Text(TEXT("G"), 1520, 270, 60, RedDim, EAlign::Left, true);
		if (Pawn->Sim.S.TankVolumeL > 0)
		{
			F.Rect(1180, 400, 640, 20, RedFaint);
			F.Rect(1180, 400, 640 * T.TankPercent, 20, Red);
			F.Text(TEXT("N₂"), 1170, 410, 40, RedDim, EAlign::Right, true);
		}
		if (T.FanDownforceN > 500) F.Text(FString::Printf(TEXT("SUCTION %.1f kN"), T.FanDownforceN / 1000.0), 1860, 90, 42, Red, EAlign::Right, true);
	}
	F.End();
}
