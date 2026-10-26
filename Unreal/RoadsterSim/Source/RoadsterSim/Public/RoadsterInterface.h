#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RoadsterUI.h"
#include "RoadsterInterface.generated.h"

class ARoadsterPawn;
class UCanvas;

UENUM()
enum class ECarTab : uint8 { Home, Vehicle, Listen, Climate, Navigate };
UENUM()
enum class EVehiclePage : uint8 { Dynamics, Suction, Tires, Thrusters, Lights, Display, Charging, ColdGas, SimulatorLab };

/**
 * The Roadster's centre touchscreen and the two forward displays, recreated from Tesla's official
 * UI video (23 Oct 2026). One implementation paints the in-car render targets and the interactive
 * full-screen overlay; every control is wired to the live simulation.
 */
UCLASS()
class ROADSTERSIM_API URoadsterInterface : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY() TObjectPtr<ARoadsterPawn> Pawn;

	void DrawCenterScreen(UCanvas* Canvas, FVector2D Origin, FVector2D Size, FVector2D Mouse, bool bDown, bool bClick, bool bInteractive);
	void DrawForwardDisplay(UCanvas* Canvas, int32 W, int32 H, int32 Index);

	bool WantsCarView() const;
	FRotator CarViewRotation() const { return FRotator(CarPitch, CarYaw, 0); }

	ECarTab Tab = ECarTab::Home;
	EVehiclePage Page = EVehiclePage::Dynamics;
	bool bTrackSetup = false;
	int32 Hotspot = -1;
	bool bMetric = false;
	bool bCelsius = false;
	int32 PressureUnit = 0;   // 0 psi, 1 bar, 2 kPa
	float Brightness = 1.f;
	float ClimateF = 68.f;
	int32 Volume = 6;
	float MusicT = 147.f;
	float ChargeLimit = 0.9f;

	// Simulator-only workspaces: 0 gas fill, 1 flight/orbit, 2 controls. Ordinary launches stay on Home.
	int32 SimulatorWorkspace = 0;
	int32 LabGasSpecies = 0;   // ColdGas::GasSpecies: nitrogen, helium, argon
	int32 LabFillMode = 0;     // 0 pressure, 1 installed-vessel gas mass (manifold excluded)
	float LabFillPressureBar = 379.f, LabFillTemperatureK = 293.f, LabFillMassKg = 1.f;
	FString LabFillStatus, LabFillDetail;
	bool bLabFillRejected = false;
	/** Call before an interactive lab review to pay the shared EOS-table startup cost once. */
	static void PrewarmSimulatorLab();
	void SyncSimulatorLabDraft();
	/** Explicit pit operation. Rejected fills preserve the live gas system, vehicle mass and pilot inputs. */
	bool ApplySimulatorLabFill();
	/** Exercises the same fill operation used by the buttons, then restores the vehicle and draft. */
	bool RunSimulatorLabVerification(FString& OutReport);

private:
	RoadsterUI::FContext UI;
	float CarYaw = 215.f, CarPitch = -14.f, CarYawTarget = 215.f, CarPitchTarget = -14.f;
	float FloorPhase = 0.f;
	double LastTime = 0.0;
	bool bDragCar = false;
	float DragStartX = 0.f, DragStartYaw = 0.f;
	bool bLabDraftInitialized = false;
	int32 LabDraftTrim = -1;
	FString LabScenarioStatus, LabControlStatus;

	void TopBar();
	void DriveStrip();
	void BottomBar();
	void HomeTab();
	void VehicleTab();
	void TrackDashboard();
	void TrackSetupPage();
	void ListenTab();
	void ClimateTab();
	void NavigateTab();
	void ThrusterBar(float X, float Y, float W, float H);
	void CarImage(float X, float Y, float W, float H, bool bFloor = true);
	FVector2D ProjectCarPoint(const FVector& BodyPointCm, float X, float Y, float W, float H) const;
	void DrawTrackMap(float X, float Y, float W, float H, bool bPerspective);

	// page helpers
	void PageDynamics(float X, float Y, float W, float H);
	void PageSuction(float X, float Y, float W, float H);
	void PageTires(float X, float Y, float W, float H);
	void PageThrusters(float X, float Y, float W, float H);
	void PageColdGas(float X, float Y, float W, float H);
	void PageLights(float X, float Y, float W, float H);
	void PageDisplay(float X, float Y, float W, float H);
	void PageCharging(float X, float Y, float W, float H);
	void PageSimulatorLab(float X, float Y, float W, float H);
	void LabGasPage(float X, float Y, float W, float H);
	void LabFlightPage(float X, float Y, float W, float H);
	void LabControlsPage(float X, float Y, float W, float H);

	FString Speed(double Mps, bool bUnit = false) const;
	FString Temp(double C) const;
	FString Pressure(double KPa, bool bUnit = true) const;
	FString LapStr(float T) const;
};
