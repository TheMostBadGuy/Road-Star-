#include "RoadsterInterface.h"
#include "RoadsterController.h"
#include "RoadsterPawn.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include <limits>

using namespace RoadsterUI;
namespace ColdGas = Roadster::ColdGas;

namespace
{
	FString GasName(const ColdGas::System& Gas) { return UTF8_TO_TCHAR(Gas.PropellantProperties().Name); }

	void FitText(FContext& UI, const FString& Text, float X, float Y, float W, float Size,
		const FLinearColor& Color = RedDim, bool bBold = false)
	{
		while (Size > 16.f && UI.TextWidth(Text, Size, bBold) > W) Size -= 1.f;
		UI.Text(Text, X, Y, Size, Color, EAlign::Left, bBold);
	}

	FString AltitudeText(double Metres)
	{
		return FMath::Abs(Metres) >= 10000.0 ? FString::Printf(TEXT("%.2f km"), Metres / 1000.0)
			: FString::Printf(TEXT("%.1f m"), Metres);
	}
}

void URoadsterInterface::PrewarmSimulatorLab()
{
	const double Start = FPlatformTime::Seconds();
	(void)ColdGas::IsentropeTable::Get(ColdGas::GasSpecies::Nitrogen);
	(void)ColdGas::IsentropeTable::Get(ColdGas::GasSpecies::Helium);
	(void)ColdGas::IsentropeTable::Get(ColdGas::GasSpecies::Argon);
	UE_LOG(LogTemp, Display, TEXT("ROADSTER_LAB_PREWARM N2 He Ar ready; elapsed=%.3f seconds"), FPlatformTime::Seconds() - Start);
}

void URoadsterInterface::SyncSimulatorLabDraft()
{
	if (!Pawn) return;
	const auto& Sim = Pawn->Sim;
	LabGasSpecies = static_cast<int32>(Sim.Gas.Propellant());
	LabFillPressureBar = FMath::Clamp(static_cast<float>(Sim.Gas.Tel().DisplayBar),
		static_cast<float>(ColdGas::kPatm / ColdGas::kBarPa), static_cast<float>(Sim.Gas.MaxFillPressureBar()));
	LabFillTemperatureK = FMath::Clamp(static_cast<float>(Sim.Gas.Tel().Tank[0].TempK), 200.f, 350.f);
	LabFillMassKg = FMath::Clamp(static_cast<float>(Sim.Gas.GasMassKg()), .01f, 500.f);
	LabDraftTrim = static_cast<int32>(Sim.S.TrimLevel);
	bLabDraftInitialized = true;
	bLabFillRejected = false;
	LabFillStatus = TEXT("Draft copied from live vessels. Apply to refill.");
	LabFillDetail = TEXT("Sliders and gas selection leave the running vehicle unchanged.");
}

bool URoadsterInterface::ApplySimulatorLabFill()
{
	auto Reject = [&](const FString& Reason, const FString& Detail)
	{
		bLabFillRejected = true;
		LabFillStatus = TEXT("REJECTED: ") + Reason;
		LabFillDetail = Detail;
		return false;
	};
	if (!Pawn || Pawn->Sim.S.TankVolumeL <= 0.0)
		return Reject(TEXT("no cold-gas vessels fitted."), TEXT("Select Thrust or Rocket on the Thrusters page."));
	if (LabGasSpecies < 0 || LabGasSpecies > 2 || LabFillMode < 0 || LabFillMode > 1
		|| !FMath::IsFinite(LabFillTemperatureK) || !FMath::IsFinite(LabFillPressureBar) || !FMath::IsFinite(LabFillMassKg))
		return Reject(TEXT("invalid fill request."), TEXT("Select a supported gas and finite pressure, mass and temperature."));

	auto& Sim = Pawn->Sim;
	const double Temperature = FMath::Clamp(static_cast<double>(LabFillTemperatureK), 200.0, 350.0);
	const double Pressure = FMath::Clamp(static_cast<double>(LabFillPressureBar), ColdGas::kPatm / ColdGas::kBarPa, Sim.Gas.MaxFillPressureBar());
	const double Mass = FMath::Clamp(static_cast<double>(LabFillMassKg), .01, 500.0);
	const bool bClamped = Temperature != LabFillTemperatureK || Pressure != LabFillPressureBar || Mass != LabFillMassKg;
	const auto Species = static_cast<ColdGas::GasSpecies>(LabGasSpecies);

	// Configure only a copy. In mass mode the new gas's EOS derives the vessel pressure at fixed volume.
	// A rejected mass fill must never partially switch the actual propellant or reset its burst counters.
	ColdGas::System Candidate = Sim.Gas;
	Candidate.SetArmed(Sim.Set.bThrustersArmed);
	if (!Candidate.ConfigureExperiment(Species, LabFillMode == 1 ? ColdGas::kPatm / ColdGas::kBarPa : Pressure, Temperature))
		return Reject(TEXT("unsupported fill state."), TEXT("The live gas, vehicle mass and pilot inputs were preserved."));
	if (LabFillMode == 1 && !Candidate.FillByMassKg(Mass, Temperature))
	{
		const ColdGas::Thermodynamics Thermo(Species);
		const double RequestedBar = Thermo.Pressure(Temperature, Sim.S.TankVolumeL * .001 / Mass) / ColdGas::kBarPa;
		return Reject(TEXT("mass is outside the vessel pressure range."),
			FString::Printf(TEXT("%.2f kg needs %.1f bar at %.0f K; allowed %.2f-%.0f bar. Live fill unchanged."),
				Mass, RequestedBar, Temperature, ColdGas::kPatm / ColdGas::kBarPa, Sim.Gas.MaxFillPressureBar()));
	}
	const auto& GT = Candidate.Tel();
	const double TotalGas = Candidate.GasMassKg() + Candidate.ManifoldMassKg();
	if (!std::isfinite(TotalGas) || TotalGas <= 0.0 || !std::isfinite(GT.DisplayBar)
		|| GT.DisplayBar < ColdGas::kPatm / ColdGas::kBarPa - 1e-4 || GT.DisplayBar > Candidate.MaxFillPressureBar() + 1e-3)
		return Reject(TEXT("nonphysical fill result."), TEXT("The live gas, vehicle mass and pilot inputs were preserved."));

	Sim.Gas = Candidate;
	Sim.TankKg = TotalGas;
	Sim.T.TankKg = TotalGas;
	Sim.T.TankBar = Sim.Gas.Tel().DisplayBar;
	Sim.T.TankPercent = Sim.Gas.Tel().GasFraction;
	Sim.T.ThrustN = Sim.T.ThrustRear = Sim.T.ThrustYawL = Sim.T.ThrustYawR = 0.0;
	for (double& Thrust : Sim.T.ThrustCorner) Thrust = 0.0;
	// The existing pit refill now repeats this selected gas's applied pressure and temperature.
	Sim.S.TankFullBar = Sim.T.TankBar;
	Sim.S.GasTempK = Temperature;
	Pawn->Input = Roadster::Inputs();
	LabFillPressureBar = static_cast<float>(Sim.T.TankBar);
	LabFillTemperatureK = static_cast<float>(Temperature);
	LabFillMassKg = static_cast<float>(Sim.Gas.GasMassKg());
	LabDraftTrim = static_cast<int32>(Sim.S.TrimLevel);
	bLabDraftInitialized = true;
	bLabFillRejected = false;
	LabFillStatus = FString::Printf(TEXT("APPLIED: %s, %.1f bar, %.0f K%s"), *GasName(Sim.Gas), Sim.T.TankBar,
		Temperature, bClamped ? TEXT(" (request bounded)") : TEXT(""));
	LabFillDetail = FString::Printf(TEXT("%.2f kg vessels + %.3f kg feed manifold. Pilot inputs released; vehicle %.1f kg."),
		Sim.Gas.GasMassKg(), Sim.Gas.ManifoldMassKg(), Sim.TotalMass());
	UE_LOG(LogTemp, Display, TEXT("ROADSTER_LAB_FILL %s %s"), *LabFillStatus, *LabFillDetail);
	return true;
}

void URoadsterInterface::PageSimulatorLab(float X, float Y, float W, float H)
{
	if (!bLabDraftInitialized || LabDraftTrim != static_cast<int32>(Pawn->Sim.S.TrimLevel)) SyncSimulatorLabDraft();
	UI.Text(TEXT("Simulator Lab"), X, Y + 18, 32, Red, EAlign::Left, true);
	UI.Text(TEXT("EXPERIMENT / ESTIMATED VEHICLE"), X + W, Y + 18, 21, Amber, EAlign::Right, true);
	FitText(UI, TEXT("Configure the simulation: estimated hardware, finite propellant and an idealized spherical Earth model."),
		X, Y + 54, W, 22);
	SimulatorWorkspace = UI.Segmented(X, Y + 82, W, 64, TEXT("Workspace"),
		{ TEXT("Gas Fill"), TEXT("Flight / Orbit"), TEXT("Controls") }, FMath::Clamp(SimulatorWorkspace, 0, 2), 250);
	const float CY = Y + 164, CH = H - 164;
	switch (SimulatorWorkspace)
	{
	case 1: LabFlightPage(X, CY, W, CH); break;
	case 2: LabControlsPage(X, CY, W, CH); break;
	default: LabGasPage(X, CY, W, CH); break;
	}
}

void URoadsterInterface::LabGasPage(float X, float Y, float W, float H)
{
	auto& Sim = Pawn->Sim;
	if (Sim.S.TankVolumeL <= 0.0)
	{
		UI.Panel(X, Y, W, H);
		UI.Text(TEXT("No cold-gas vessels fitted on Performance"), X + 40, Y + 70, 32, Amber, EAlign::Left, true);
		UI.Text(TEXT("The experiment uses the installed Thrust or Rocket hardware volume and pressure rating."), X + 40, Y + 130, 25, RedDim);
		if (UI.Button(X + 40, Y + 190, 500, 76, TEXT("Open Thrusters / choose augment"), false, 24)) Page = EVehiclePage::Thrusters;
		return;
	}
	const float LW = 930, RX = X + LW + 26, RW = W - LW - 26;
	LabGasSpecies = UI.Segmented(X, Y, LW, 68, TEXT("Propellant"),
		{ TEXT("Nitrogen"), TEXT("Helium"), TEXT("Argon") }, FMath::Clamp(LabGasSpecies, 0, 2), 230);
	LabFillMode = UI.Segmented(X, Y + 82, LW, 68, TEXT("Fill input"),
		{ TEXT("Pressure"), TEXT("Vessel mass") }, FMath::Clamp(LabFillMode, 0, 1), 230);
	const ColdGas::Thermodynamics Thermo(static_cast<ColdGas::GasSpecies>(FMath::Clamp(LabGasSpecies, 0, 2)));
	const double VolumeM3 = Sim.S.TankVolumeL * .001;
	const double Temperature = FMath::Clamp(static_cast<double>(LabFillTemperatureK), 200.0, 350.0);
	const double MaxKg = VolumeM3 / Thermo.VFromPT(Sim.Gas.MaxFillPressureBar() * ColdGas::kBarPa, Temperature);
	if (LabFillMode == 0)
		UI.Slider(X, Y + 164, LW, 80, TEXT("Fill pressure"), LabFillPressureBar,
			static_cast<float>(ColdGas::kPatm / ColdGas::kBarPa), static_cast<float>(Sim.Gas.MaxFillPressureBar()),
			FString::Printf(TEXT("%.1f bar"), LabFillPressureBar), 260);
	else
		UI.Slider(X, Y + 164, LW, 80, TEXT("Vessel gas mass"), LabFillMassKg, .01f,
			static_cast<float>(FMath::Clamp(MaxKg * 1.15, 1.0, 500.0)),
			FString::Printf(TEXT("%.2f kg"), LabFillMassKg), 280);
	UI.Slider(X, Y + 258, LW, 80, TEXT("Fill temperature"), LabFillTemperatureK, 200.f, 350.f,
		FString::Printf(TEXT("%.0f K"), LabFillTemperatureK), 280);

	const double PreviewBar = LabFillMode == 0 ? LabFillPressureBar
		: Thermo.Pressure(Temperature, VolumeM3 / FMath::Max(.01, static_cast<double>(LabFillMassKg))) / ColdGas::kBarPa;
	const double PreviewKg = LabFillMode == 1 ? LabFillMassKg
		: VolumeM3 / Thermo.VFromPT(FMath::Max(ColdGas::kPatm, PreviewBar * ColdGas::kBarPa), Temperature);
	const bool bWithinRating = std::isfinite(PreviewBar) && PreviewBar >= ColdGas::kPatm / ColdGas::kBarPa
		&& PreviewBar <= Sim.Gas.MaxFillPressureBar() + .001;
	FitText(UI, FString::Printf(TEXT("Pending: %.2f kg vessels at %.1f bar / %.0f K"), PreviewKg, PreviewBar, Temperature),
		X + 14, Y + 370, LW - 28, 24, bWithinRating ? Red : Amber, true);
	FitText(UI, FString::Printf(TEXT("Fixed %.0f L; maximum at this gas / temperature: %.2f kg (%.0f bar)."),
		Sim.S.TankVolumeL, MaxKg, Sim.Gas.MaxFillPressureBar()), X + 14, Y + 407, LW - 28, 22);
	if (UI.Button(X, Y + 448, 454, 72, TEXT("Apply pit fill"), false, 27)) ApplySimulatorLabFill();
	if (UI.Button(X + 476, Y + 448, 454, 72, TEXT("Read live values"), false, 25)) SyncSimulatorLabDraft();
	FitText(UI, LabFillStatus, X + 12, Y + 554, LW - 24, 23, bLabFillRejected ? Amber : Red, true);
	FitText(UI, LabFillDetail, X + 12, Y + 589, LW - 24, 20);

	UI.Panel(RX, Y, RW, H);
	UI.Text(TEXT("Live gas / vehicle"), RX + 24, Y + 38, 29, Red, EAlign::Left, true);
	auto Row = [&](float DY, const FString& Label, const FString& Value)
	{
		UI.Text(Label, RX + 24, Y + DY, 23, RedDim);
		UI.Text(Value, RX + RW - 24, Y + DY, 25, Red, EAlign::Right, true);
	};
	Row(98, TEXT("Propellant"), GasName(Sim.Gas));
	Row(148, TEXT("Vessel pressure"), FString::Printf(TEXT("%.1f bar"), Sim.Gas.Tel().DisplayBar));
	Row(198, TEXT("Vessel gas"), FString::Printf(TEXT("%.2f kg"), Sim.Gas.GasMassKg()));
	Row(248, TEXT("Feed manifold"), FString::Printf(TEXT("%.3f kg"), Sim.Gas.ManifoldMassKg()));
	Row(298, TEXT("Total vehicle"), FString::Printf(TEXT("%.1f kg"), Sim.TotalMass()));
	Row(348, TEXT("Remaining impulse"), FString::Printf(TEXT("%.1f kN s"), Sim.Gas.RemainingImpulseNs() / 1000.0));
	Sim.Set.bInfiniteGas = UI.Segmented(RX + 16, Y + 390, RW - 32, 68, TEXT("Supply"),
		{ TEXT("Finite"), TEXT("Unlimited demo") }, Sim.Set.bInfiniteGas ? 1 : 0, 170) == 1;
	FitText(UI, Sim.Set.bInfiniteGas ? TEXT("Demo restores mass and energy; it is not a finite tank run.")
		: TEXT("Propellant consumption changes vehicle mass and thrust."), RX + 24, Y + 490, RW - 48, 21,
		Sim.Set.bInfiniteGas ? Amber : RedDim);
	FitText(UI, TEXT("Apply refills every installed vessel and resets the burst."), RX + 24, Y + 532, RW - 48, 21);
	FitText(UI, TEXT("Gas mass input excludes the separately modeled manifold."), RX + 24, Y + 566, RW - 48, 21);
}

void URoadsterInterface::LabFlightPage(float X, float Y, float W, float H)
{
	auto& Sim = Pawn->Sim;
	auto& Set = Sim.Set;
	const auto& T = Sim.T;
	const float LW = 930, RX = X + LW + 26, RW = W - LW - 26;
	const int32 EarthMode = UI.Segmented(X, Y, LW, 64, TEXT("Roadstar Earth"),
		{ TEXT("Off"), TEXT("On") }, Set.bRoadstarMode ? 1 : 0, 300);
	if (EarthMode != static_cast<int32>(Set.bRoadstarMode))
	{
		Set.bRoadstarMode = EarthMode == 1;
		LabScenarioStatus = TEXT("Earth model switched; position and velocity were retained.");
	}
	Set.bThrustersArmed = UI.Segmented(X, Y + 76, LW, 64, TEXT("Thrusters"),
		{ TEXT("Safe"), TEXT("Armed") }, Set.bThrustersArmed ? 1 : 0, 300) == 1;
	Set.bLandingAssist = UI.Segmented(X, Y + 152, LW, 64, TEXT("Landing assist"),
		{ TEXT("Off"), TEXT("On") }, Set.bLandingAssist ? 1 : 0, 300) == 1;
	Set.bFlightAssist = UI.Segmented(X, Y + 228, LW, 64, TEXT("Flight assist"),
		{ TEXT("Off"), TEXT("On") }, Set.bFlightAssist ? 1 : 0, 300) == 1;
	const int32 Hover = UI.Segmented(X, Y + 304, LW, 64, TEXT("Hover hold"),
		{ TEXT("Off"), TEXT("On") }, Set.bHoverHold ? 1 : 0, 300);
	if (Hover != static_cast<int32>(Set.bHoverHold))
	{
		if (Hover == 1 && !Sim.S.bHasLiftThrusters) LabScenarioStatus = TEXT("Hover requires the Rocket augment's lift nozzles.");
		else
		{
			Set.bHoverHold = Hover == 1;
			if (Set.bHoverHold) Set.HoverAltitudeM = T.AltitudeM;
			LabScenarioStatus = Set.bHoverHold ? TEXT("Hover target captured. Lift remains limited by gas and attitude.") : TEXT("Hover hold released.");
		}
	}
	// Logarithmic travel gives useful control both around a 2 m hover and at orbital initial altitudes.
	float LogAltitude = static_cast<float>(std::log10(FMath::Clamp(Set.HoverAltitudeM, 0.0, 1000000.0) + 1.0));
	if (UI.Slider(X, Y + 380, LW, 76, TEXT("Target altitude"), LogAltitude, 0.f, 6.f,
		AltitudeText(Set.HoverAltitudeM), 280)) Set.HoverAltitudeM = std::pow(10.0, LogAltitude) - 1.0;
	if (UI.Button(X, Y + 470, 454, 70, TEXT("100 km coast (external)"), false, 25))
	{
		Pawn->StartRoadstarScenario(1);
		LabScenarioStatus = TEXT("External setup: 100 km altitude, 1200 m/s horizontal, +300 m/s vertical.");
	}
	if (UI.Button(X + 476, Y + 470, 454, 70, TEXT("400 km orbit (external)"), false, 25))
	{
		Pawn->StartRoadstarScenario(0);
		LabScenarioStatus = TEXT("External setup: 400 km altitude with circular orbital velocity imposed.");
	}
	FitText(UI, LabScenarioStatus.IsEmpty() ? TEXT("Hover target is elevation, not radar height above terrain.") : LabScenarioStatus,
		X + 12, Y + 571, LW - 24, 22, Amber);
	FitText(UI, TEXT("Presets supply initial conditions; the car did not launch itself into orbit."), X + 12, Y + 603, LW - 24, 20);

	UI.Panel(RX, Y, RW, H);
	UI.Text(TEXT("Live flight telemetry"), RX + 24, Y + 36, 29, Red, EAlign::Left, true);
	auto Row = [&](float DY, const FString& Label, const FString& Value)
	{
		UI.Text(Label, RX + 24, Y + DY, 22, RedDim);
		UI.Text(Value, RX + RW - 24, Y + DY, 24, Red, EAlign::Right, true);
	};
	Row(90, Set.bRoadstarMode ? TEXT("Radial altitude") : TEXT("Datum elevation"), AltitudeText(T.AltitudeM));
	Row(132, TEXT("Vertical speed"), FString::Printf(TEXT("%+.2f m/s"), T.VerticalSpeedMps));
	Row(174, TEXT("Speed / Mach"), FString::Printf(TEXT("%.1f m/s / %.3f"), Sim.V.Len(), T.Mach));
	Row(216, TEXT("Dynamic pressure q"), FString::Printf(TEXT("%.3f kPa"), T.DynamicPressurePa / 1000.0));
	Row(258, TEXT("Gravity"), FString::Printf(TEXT("%.4f m/s2"), T.GravityMps2));
	Row(300, TEXT("Air density"), FString::Printf(TEXT("%.3g kg/m3"), T.AirDensityKgM3));
	if (Set.bRoadstarMode)
	{
		const Earth::OrbitSample Orbit = Earth::OrbitalElements({Sim.X.x, Sim.X.y, Sim.X.z}, {Sim.V.x, Sim.V.y, Sim.V.z});
		const FString Condition = !Orbit.Valid ? TEXT("INVALID STATE") : Orbit.Impacting
			? (Orbit.Bound ? TEXT("BOUND / EARTH-INTERSECTING") : TEXT("ESCAPE ENERGY / EARTH-INTERSECTING"))
			: (Orbit.Bound ? TEXT("BOUND / NON-IMPACTING") : TEXT("ESCAPE / UNBOUND"));
		FitText(UI, Condition, RX + 24, Y + 359, RW - 48, 25, Orbit.Valid && !Orbit.Impacting ? Green : Amber, true);
		Row(407, TEXT("Periapsis altitude"), AltitudeText(Orbit.PeriapsisAltitudeM));
		Row(449, TEXT("Apoapsis altitude"), Orbit.Bound ? AltitudeText(Orbit.ApoapsisAltitudeM) : TEXT("unbound"));
		Row(491, TEXT("Eccentricity"), FString::Printf(TEXT("%.6f"), Orbit.Eccentricity));
		FitText(UI, TEXT("Two-body classification; atmospheric drag can change it."), RX + 24, Y + 544, RW - 48, 21);
	}
	else FitText(UI, TEXT("Enable Roadstar Earth for orbital classification."), RX + 24, Y + 365, RW - 48, 24, Amber);
	FitText(UI, TEXT("No Earth rotation, terrain or other gravitating bodies."), RX + 24, Y + 582, RW - 48, 20);
}

void URoadsterInterface::LabControlsPage(float X, float Y, float W, float H)
{
	ARoadsterController* Controller = Cast<ARoadsterController>(Pawn->GetController());
	if (!Controller)
	{
		UI.Text(TEXT("No player controller is attached."), X + 24, Y + 50, 28, Amber);
		return;
	}
	const float LW = 930, RX = X + LW + 26, RW = W - LW - 26;
	FitText(UI, TEXT("Input: ") + Controller->GetControlDeviceLabel(), X + 12, Y + 22, LW - 24, 25, Red, true);
	UI.Slider(X, Y + 56, LW, 78, TEXT("Steering deadzone"), Controller->SteeringDeadzone, 0.f, .4f,
		FString::Printf(TEXT("%.2f"), Controller->SteeringDeadzone), 310);
	UI.Slider(X, Y + 146, LW, 78, TEXT("Stick exponent"), Controller->StickExponent, .5f, 3.f,
		FString::Printf(TEXT("%.2f"), Controller->StickExponent), 310);
	UI.Slider(X, Y + 236, LW, 78, TEXT("Camera sensitivity"), Controller->CameraSensitivity, .2f, 3.f,
		FString::Printf(TEXT("%.2fx"), Controller->CameraSensitivity), 310);
	Controller->bHapticsEnabled = UI.Segmented(X, Y + 326, LW, 68, TEXT("Controller vibration"),
		{ TEXT("Off"), TEXT("On") }, Controller->bHapticsEnabled ? 1 : 0, 330) == 1;
	UI.Slider(X, Y + 406, LW, 78, TEXT("Speed feel / comfort"), Pawn->SpeedFeelIntensity, 0.f, 1.5f,
		FString::Printf(TEXT("%.2fx"), Pawn->SpeedFeelIntensity), 330);
	if (UI.Button(X, Y + 500, 454, 70, TEXT("Save controller tuning"), false, 26))
	{
		Controller->SaveControlSettings();
		FConfigFile Saved;
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Config/Windows/RoadsterControls.ini");
		float Deadzone = -1, Exponent = -1, Sensitivity = -1;
		bool bHaptics = !Controller->bHapticsEnabled;
		Saved.Read(Path);
		const bool bSaved = Saved.GetFloat(TEXT("Controls"), TEXT("SteeringDeadzone"), Deadzone)
			&& Saved.GetFloat(TEXT("Controls"), TEXT("StickExponent"), Exponent)
			&& Saved.GetFloat(TEXT("Controls"), TEXT("CameraSensitivity"), Sensitivity)
			&& Saved.GetBool(TEXT("Controls"), TEXT("Haptics"), bHaptics)
			&& FMath::IsNearlyEqual(Deadzone, Controller->SteeringDeadzone, .0001f)
			&& FMath::IsNearlyEqual(Exponent, Controller->StickExponent, .0001f)
			&& FMath::IsNearlyEqual(Sensitivity, Controller->CameraSensitivity, .0001f)
			&& bHaptics == Controller->bHapticsEnabled;
		LabControlStatus = bSaved ? TEXT("Saved and read back controller tuning. Speed feel is session-only.")
			: TEXT("Could not verify the saved tuning file. Current settings remain live.");
	}
	FitText(UI, TEXT("Live tuning; 0x speed feel removes the added camera motion."), X + 12, Y + 601, LW - 24, 21);
	UI.Panel(RX, Y, RW, H);
	UI.Text(TEXT("Stunts / pilot inputs"), RX + 24, Y + 38, 29, Red, EAlign::Left, true);
	FitText(UI, Controller->GetStuntStatus(), RX + 24, Y + 96, RW - 48, 25, Red, true);
	const FString Instructions[] = {
		TEXT("F6: choose a stunt preset     J: play its valve sequence"),
		TEXT("F7: start recording / save     F8: replay saved inputs"),
		TEXT("Move the flight controls to cancel a preset or replay."),
		TEXT("Input recordings last up to 180 seconds."),
		TEXT("H: hover hold     Page Up / Down: target elevation"),
		TEXT("Rocket flight: W/S pitch, A/D roll, Q/E yaw."),
		TEXT("Shift: rear boost   Space/Ctrl: lift   Alt: rate brake"),
		TEXT("Gamepad: left stick steers or rolls / pitches in flight."),
		TEXT("Right stick looks; screen cursor uses left stick + A.")
	};
	for (int32 i = 0; i < UE_ARRAY_COUNT(Instructions); ++i)
		FitText(UI, Instructions[i], RX + 24, Y + 160 + i * 40, RW - 48, 22);
	FitText(UI, TEXT("Sequences command finite jets; they do not force a trajectory."), RX + 24, Y + 545, RW - 48, 20);
	// Status is drawn after the panel so its save result stays visible.
	FitText(UI, LabControlStatus, RX + 24, Y + 586, RW - 48, 20, Amber);
}

bool URoadsterInterface::RunSimulatorLabVerification(FString& OutReport)
{
	if (!Pawn) { OutReport = TEXT("FAIL: no pawn"); return false; }
	const Roadster::Vehicle SavedSim = Pawn->Sim;
	const Roadster::Inputs SavedInputs = Pawn->Input;
	const int32 SavedSpecies = LabGasSpecies, SavedMode = LabFillMode, SavedTrim = LabDraftTrim;
	const float SavedPressure = LabFillPressureBar, SavedTemperature = LabFillTemperatureK, SavedMass = LabFillMassKg;
	const FString SavedStatus = LabFillStatus, SavedDetail = LabFillDetail;
	const bool bSavedRejected = bLabFillRejected, bSavedInitialized = bLabDraftInitialized;
	int32 Passed = 0, Failed = 0;
	TArray<FString> Failures;
	auto Check = [&](bool bPass, const TCHAR* Name)
	{
		if (bPass) ++Passed;
		else { ++Failed; Failures.Add(Name); }
	};
	Pawn->Sim.Init(Roadster::Trim::Rocket);
	Pawn->Sim.Set.bThrustersArmed = false;
	LabFillMode = 0;
	LabFillPressureBar = 379.f;
	LabFillTemperatureK = 293.f;
	LabFillMassKg = 1.f;
	for (int32 Species = 0; Species < 3; ++Species)
	{
		LabGasSpecies = Species;
		Pawn->Input.Throttle = .7;
		Check(ApplySimulatorLabFill(), TEXT("species pressure fill"));
		Check(static_cast<int32>(Pawn->Sim.Gas.Propellant()) == Species, TEXT("applied species"));
		Check(std::isfinite(Pawn->Sim.Gas.GasMassKg()) && Pawn->Sim.Gas.GasMassKg() > 0.0, TEXT("finite gas mass"));
		Check(FMath::Abs(Pawn->Sim.TankKg - Pawn->Sim.Gas.GasMassKg() - Pawn->Sim.Gas.ManifoldMassKg()) < 1e-8,
			TEXT("immediate tank mass sync"));
		Check(FMath::Abs(Pawn->Sim.TotalMass() - Pawn->Sim.S.DryMassKg - Pawn->Sim.S.TrimExtraMassKg - Pawn->Sim.T.TankKg) < 1e-8,
			TEXT("immediate total mass sync"));
		Check(FMath::Abs(Pawn->Sim.T.TankBar - 379.0) < .001 && Pawn->Input.Throttle == 0.0, TEXT("pressure and explicit input release"));
	}
	LabGasSpecies = 1;
	LabFillMode = 1;
	LabFillMassKg = 500.f;
	const auto BeforeReject = Pawn->Sim.Gas;
	const double BeforeMass = Pawn->Sim.TankKg;
	Pawn->Input.Throttle = .4;
	Check(!ApplySimulatorLabFill() && bLabFillRejected, TEXT("over-rating mass rejected"));
	Check(Pawn->Sim.Gas.Propellant() == BeforeReject.Propellant()
		&& Pawn->Sim.Gas.Tel().DisplayBar == BeforeReject.Tel().DisplayBar
		&& Pawn->Sim.Gas.GasMassKg() == BeforeReject.GasMassKg()
		&& Pawn->Sim.TankKg == BeforeMass && Pawn->Input.Throttle == .4, TEXT("rejection preserves live state and input"));
	LabGasSpecies = static_cast<int32>(Pawn->Sim.Gas.Propellant());
	LabFillMassKg = static_cast<float>(Pawn->Sim.Gas.GasMassKg() * .75);
	const double RequestedMass = LabFillMassKg;
	Check(ApplySimulatorLabFill(), TEXT("valid mass fill"));
	Check(FMath::Abs(Pawn->Sim.Gas.GasMassKg() - RequestedMass) < 1e-4 * FMath::Max(1.0, RequestedMass), TEXT("EOS mass fill is coherent"));
	LabFillTemperatureK = std::numeric_limits<float>::quiet_NaN();
	Check(!ApplySimulatorLabFill(), TEXT("nonfinite draft rejected"));
	Pawn->Sim = SavedSim;
	Pawn->Input = SavedInputs;
	LabGasSpecies = SavedSpecies; LabFillMode = SavedMode; LabDraftTrim = SavedTrim;
	LabFillPressureBar = SavedPressure; LabFillTemperatureK = SavedTemperature; LabFillMassKg = SavedMass;
	LabFillStatus = SavedStatus; LabFillDetail = SavedDetail;
	bLabFillRejected = bSavedRejected; bLabDraftInitialized = bSavedInitialized;
	OutReport = FString::Printf(TEXT("%s passed=%d failed=%d; vehicle, inputs and UI draft restored%s%s"),
		Failed == 0 ? TEXT("PASS") : TEXT("FAIL"), Passed, Failed,
		Failures.IsEmpty() ? TEXT("") : TEXT("; failures="), *FString::Join(Failures, TEXT(", ")));
	UE_LOG(LogTemp, Display, TEXT("ROADSTER_LAB_CHECK %s"), *OutReport);
	return Failed == 0;
}
