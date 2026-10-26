#pragma once
// Physics visualisation overlays (Overlays agent, Coordination/TEAM.md):
//   F3  heat map: false-colour temperature of every car part, the road and the cold-gas jets (post-process,
//       Custom Depth stencil IDs per component); Shift+F3 cycles the temperature scale.
//   F4  airflow: translucent car-frame slices of the Flow solver (RoadsterFlow.h) with wind-tunnel smoke;
//       Shift+F4 cycles the plotted quantity (|U|, Cp, T, vorticity).
//   K   Physics Lab: every physical quantity the simulation computes, live, plus the editable environment.
// Every number drawn comes from the simulation state (Roadster::Vehicle, ColdGas::System, Thermal C3, Flow C4,
// JetVFX C5). Values the model does not compute are labelled as such instead of being invented.

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RoadsterOverlays.generated.h"

class ARoadsterPawn;
class ARoadsterController;
class APlayerController;
class UCanvas;
class UTexture2D;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
namespace RoadsterUI { class FContext; }

/** Where a displayed temperature comes from (shown in the Physics Lab). */
enum class ERoadsterTempSource : uint8 { None, Physics, Thermal, ColdGas, JetVFX, Ambient, Derived };

/** Temperatures gathered from the simulation each frame (Kelvin). Shared by the heat map and the Physics Lab. */
struct FRoadsterTemps
{
	float AmbientK = 297.15f, RoadK = 313.15f, SkyK = 280.f, BodyK = 297.15f, CabinK = 297.15f, UnderfloorK = 297.15f;
	float TreadK[4] = {}, CarcassK[4] = {}, GasK[4] = {}, RimK[4] = {}, DiscK[4] = {}, CaliperK[4] = {}, HubK[4] = {};
	float MotorK[2] = {}, InverterK[2] = {}, BatteryK = 0, CoolantK = 0, FanMotorK[4] = {};
	float TankWallK[2] = {}, TankLinerK[2] = {}, TankGasK[2] = {}, ManifoldK = 0, RegOutletK = 0;
	float NozzleWallK[10] = {};
	bool bTank[2] = { false, false };
	bool bColdGas = false;
	ERoadsterTempSource BodySrc = ERoadsterTempSource::Ambient, RimSrc = ERoadsterTempSource::None, CaliperSrc = ERoadsterTempSource::None,
		HubSrc = ERoadsterTempSource::None, GasSrc = ERoadsterTempSource::None, NozzleSrc = ERoadsterTempSource::Ambient,
		FanSrc = ERoadsterTempSource::None, SkySrc = ERoadsterTempSource::Derived, InverterSrc = ERoadsterTempSource::None,
		CoolantSrc = ERoadsterTempSource::None, CabinSrc = ERoadsterTempSource::Ambient, UnderSrc = ERoadsterTempSource::Ambient;
};

/** Temperature -> palette coordinate (0..1). The same mapping runs in the post-process shader (M_HeatMapPP). */
struct ROADSTERSIM_API FRoadsterHeatScale
{
	int32 Mode = 0;            // 0 Full range (asinh), 1 Auto (linear, car span), 2 Tyre, 3 Brake
	float LowK = 73.15f, HighK = 1273.15f, CentreK = 293.15f, SoftK = 25.f, CentreU = 0.40f;
	bool bLinear = false;
	float ToU(float K) const;
	float FromU(float U) const;
	const TCHAR* Name() const;
	static constexpr int32 NumModes = 4;
};

namespace RoadsterOverlays
{
	/** Ironbow-style thermal palette, sRGB 0..1 (shared by the shader table and the legend). */
	ROADSTERSIM_API void Ironbow(float U, float& R, float& G, float& B);
	/** Turbo (Google 2019 polynomial fit), sRGB 0..1. */
	ROADSTERSIM_API void Turbo(float U, float& R, float& G, float& B);
	/** Diverging blue-white-red (Moreland "cool to warm"), sRGB 0..1. */
	ROADSTERSIM_API void CoolWarm(float U, float& R, float& G, float& B);
	/** Canvas colour from sRGB 0..1. */
	ROADSTERSIM_API FLinearColor SRGB(float R, float G, float B, float A = 1.f);
	ROADSTERSIM_API FString TempC(float K, int32 Decimals = 0);
	/** Effective clear-sky radiative temperature (Berdahl & Martin 1984 emissivity from the dew point). */
	ROADSTERSIM_API float SkyTempK(float AirK, float DewPointK);
	/** HUD entry point (RoadsterHUD.cpp, [TEAM HOOKUP: HUD overlays]): legends, callouts and the Physics Lab. */
	ROADSTERSIM_API void DrawHUD(UCanvas* Canvas, ARoadsterController* PC, ARoadsterPawn* Pawn);
	/** Command-line options shared by the overlay components (-overlay=heat,flow,lab ...). */
	ROADSTERSIM_API bool CommandLineHas(const TCHAR* Overlay);
}

// ====================================================================================== heat map
UCLASS(ClassGroup = (Roadster))
class ROADSTERSIM_API URoadsterThermalOverlay : public UActorComponent
{
	GENERATED_BODY()

public:
	URoadsterThermalOverlay();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void SetEnabled(bool bOn);
	bool IsEnabled() const { return bEnabled; }
	void CycleScale();
	void SetScaleMode(int32 Mode);

	/** False-colour blend over the normal image (0..1). */
	float Strength = 0.55f;
	/** Facing-ratio shading kept in the false colour so shapes stay readable (0..1). */
	float Detail = 0.6f;
	/** Opacity of the cold-gas jets in the heat map (0..1). */
	float JetOpacity = 0.85f;
	bool bCallouts = true;
	FRoadsterHeatScale Scale;

	/** Temperatures of the last frame (Kelvin). */
	const FRoadsterTemps& Temps() const { return LastTemps; }
	void GatherTemps(FRoadsterTemps& Out) const;
	/** Coldest / hottest temperature currently drawn on the car (for the legend markers). */
	float CarMinK = 0, CarMaxK = 0;
	bool bMaterialOK = false;

	void DrawHUD(RoadsterUI::FContext& UI, UCanvas* Canvas, float VW, APlayerController* PC);

private:
	bool bEnabled = false;
	bool bStencilsApplied = false;
	float AutoLowK = 280.f, AutoHighK = 330.f;
	float RoadFieldTimer = 0.f;
	FRoadsterTemps LastTemps;

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MID;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> DataTex;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> FieldTex;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Mapped;
	TArray<uint8> MappedIds;
	int32 FieldW = 0, FieldH = 0;

	struct FCallout { FVector World; FString Text; float K; };
	TArray<FCallout> Callouts;

	void ApplyStencils(bool bRender);
	uint8 IdFor(const UStaticMeshComponent* C) const;
	void UploadData(const FRoadsterTemps& T);
	void UploadRoadField();
	void PollInput();
};

// ====================================================================================== airflow
UCLASS(ClassGroup = (Roadster))
class ROADSTERSIM_API URoadsterFlowOverlay : public UActorComponent
{
	GENERATED_BODY()

public:
	URoadsterFlowOverlay();
	virtual ~URoadsterFlowOverlay() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void BeginDestroy() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 0 off, 1 side slice, 2 top slice, 3 both slices + smoke. */
	int32 Mode = 0;
	/** 0 |U| m/s, 1 Cp, 2 static temperature, 3 vorticity. */
	int32 Quantity = 0;
	static constexpr int32 NumQuantities = 4;
	float Opacity = 0.45f;
	float SmokeOpacity = 0.8f;
	bool bSmokeAlways = false;
	bool bForceInCockpit = false;
	void SetMode(int32 NewMode);
	void CycleQuantity() { Quantity = (Quantity + 1) % NumQuantities; }
	bool IsVisibleNow() const { return bVisibleNow; }
	bool bMaterialOK = false;

	void DrawHUD(RoadsterUI::FContext& UI, UCanvas* Canvas, float VW, APlayerController* PC);
	/** Legend range of the current quantity. */
	void Range(float& Lo, float& Hi, FString& Unit, FString& Title) const;
	void QuantityColor(float U, float& R, float& G, float& B) const;

	struct FImpl;

private:
	TUniquePtr<FImpl> Impl;
	bool bVisibleNow = false;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MID;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> DataTex;
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> FieldTex;   // side, top
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> SmokeTex;   // side, top

	void PollInput();
	void FeedSolver(float DeltaTime);
	void UpdateSlices(float DeltaTime);
	void UploadData();
};

// ====================================================================================== Physics Lab
UCLASS(ClassGroup = (Roadster))
class ROADSTERSIM_API URoadsterPhysicsLab : public UActorComponent
{
	GENERATED_BODY()

public:
	URoadsterPhysicsLab();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	bool IsOpen() const { return bOpen; }
	void SetOpen(bool bInOpen);
	int32 Tab = 0;
	static constexpr int32 NumTabs = 8;
	void DrawHUD(RoadsterUI::FContext& UI, UCanvas* Canvas, float VW, ARoadsterController* PC);

	/** Environment the user asked for in the ENVIRONMENT tab (applied through Thermal's settings, contract C1/C3). */
	struct FEnvRequest
	{
		float AirTempC = 24.f, RelHumidityPct = 45.f, AltitudeM = 0.f, WindMps = 0.f, WindFromDeg = 270.f, GustPct = 0.f, SolarWm2 = 800.f;
		float TrackTempC = 40.f; bool bTrackOverride = false;
	};
	FEnvRequest Env;

private:
	bool bOpen = false;
	bool bCursorOwned = false;
	bool bEnvInit = false;
	float Scroll = 0.f;
	void ApplyEnvironment();
	void ReadEnvironment();
	// tab painters (RoadsterPhysicsLab.cpp)
	void DrawAir(RoadsterUI::FContext& UI, float X, float Y, float W);
	void DrawTyres(RoadsterUI::FContext& UI, float X, float Y, float W);
	void DrawPower(RoadsterUI::FContext& UI, float X, float Y, float W);
	void DrawAero(RoadsterUI::FContext& UI, float X, float Y, float W);
	void DrawColdGas(RoadsterUI::FContext& UI, float X, float Y, float W);
	void DrawBody(RoadsterUI::FContext& UI, float X, float Y, float W);
	void DrawFlight(RoadsterUI::FContext& UI, float X, float Y, float W);
	void DrawEnvironment(RoadsterUI::FContext& UI, float X, float Y, float W, ARoadsterController* PC);
};
