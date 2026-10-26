#pragma once

// Pure, engine-independent control shaping and finite valve-request presets.
#include <cmath>

namespace RoadsterControls
{
inline float Clamp(float V, float Low, float High) { return V < Low ? Low : (V > High ? High : V); }
inline float Bounded(float V, float Fallback, float Low, float High) { return std::isfinite(V) ? Clamp(V, Low, High) : Fallback; }
inline float Curve(float Value, float Deadzone, float Exponent)
{
    Value = Bounded(Value, 0.f, -1.f, 1.f);
    Deadzone = Bounded(Deadzone, .10f, 0.f, .40f);
    Exponent = Bounded(Exponent, 1.5f, .5f, 3.f);
    const float Magnitude = std::fabs(Value);
    return Magnitude <= Deadzone ? 0.f : std::copysign(std::pow((Magnitude - Deadzone) / (1.f - Deadzone), Exponent), Value);
}
inline float Trigger(float Value, float Deadzone = .025f)
{
    return Clamp((Bounded(Value, 0.f, 0.f, 1.f) - Deadzone) / (1.f - Deadzone), 0.f, 1.f);
}
inline float Smooth(float Current, float Target, float Rate, float Dt)
{
    return Current + (Target - Current) * (1.f - std::exp(-Rate * Clamp(Dt, 0.f, .1f)));
}
inline float SteeringAuthority(float SpeedMps)
{
    // The physics core also changes steering lock with speed; this final input
    // reduction remains modest and never removes the ability to countersteer.
    return Clamp(1.f - std::fabs(SpeedMps) / 220.f, .58f, 1.f);
}
inline float SteeringRate(float SpeedMps, bool Keyboard, bool Returning)
{
    const float SpeedFraction = Clamp(std::fabs(SpeedMps) / 100.f, 0.f, 1.f);
    return Returning ? 11.f : (Keyboard ? 5.5f - SpeedFraction * 2.f : 18.f - SpeedFraction * 8.f);
}
struct FValveRequest
{
    float Lift = 0, Pitch = 0, Roll = 0, Yaw = 0, Boost = 0;
    bool AttitudeBrake = false;
};
inline const char* PresetName(int Preset)
{
    const char* Names[] = { "Kickflip", "Barrel roll", "Backflip", "Hop" };
    return Names[(Preset % 4 + 4) % 4];
}
inline float PresetRotationStart(int Preset) { return Preset == 0 ? 1.7f : (Preset == 1 ? 1.5f : (Preset == 2 ? 1.4f : .5f)); }
inline float PresetRotationEnd(int Preset) { return PresetRotationStart(Preset) + (Preset < 2 ? .6f : 1.f); }
inline float PresetDuration(int Preset) { return Preset == 3 ? 1.6f : PresetRotationEnd(Preset) + 1.f; }
inline bool SamplePreset(int Preset, float Time, FValveRequest& Out)
{
    Out = {};
    if (!std::isfinite(Time) || Time < 0.f || Time >= PresetDuration(Preset)) return false;
    // EST valve timing, checked on the finite-N2 Rocket from rest. The 1.4-1.7 s
    // flip launch supplies ballistic recovery time before requesting rotation;
    // the old .5 s launch made the landing supervisor abort the named flips.
    // Requests operate real valves. Gas, attitude and initial motion determine
    // the result; no pose or velocity is scripted, and the landing guard stays on.
    if (Time < PresetRotationStart(Preset)) Out.Lift = 1.f;
    else if (Preset == 3)
    {
        Out.Lift = Time < .85f ? .42f : 0.f;
        Out.AttitudeBrake = true;
    }
    else if (Time < PresetRotationEnd(Preset))
    {
        Out.Lift = .26f;
        if (Preset == 0) { Out.Roll = .9f; Out.Pitch = .1f; }
        if (Preset == 1) Out.Roll = 1.f;
        if (Preset == 2) Out.Pitch = 1.f;
    }
    else Out.AttitudeBrake = true;
    return true;
}
}
