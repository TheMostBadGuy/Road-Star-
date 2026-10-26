#include <cstdio>
#include <cmath>
#include <limits>
#include "../Unreal/RoadsterSim/Source/RoadsterSim/Public/RoadsterControlMath.h"

static unsigned Checks = 0;
#define CHECK(E) do { ++Checks; if (!(E)) { std::printf("FAIL line %d: %s\n", __LINE__, #E); return 1; } } while(0)
int main()
{
    using namespace RoadsterControls;
    CHECK(Curve(0.f, .1f, 1.5f) == 0.f);
    CHECK(Curve(.1f, .1f, 1.5f) == 0.f);
    CHECK(Curve(-.1f, .1f, 1.5f) == 0.f);
    CHECK(Curve(1.f, .1f, 1.5f) == 1.f);
    CHECK(Curve(-1.f, .1f, 1.5f) == -1.f);
    float Previous = -1.f;
    for (int N = -1000; N <= 1000; ++N)
    {
        float Value = Curve(float(N) / 1000.f, .1f, 1.5f);
        CHECK(Value >= Previous && Value >= -1.f && Value <= 1.f);
        CHECK(std::fabs(Value + Curve(-float(N) / 1000.f, .1f, 1.5f)) < .00001f);
        Previous = Value;
    }
    CHECK(Curve(std::numeric_limits<float>::quiet_NaN(), .1f, 1.5f) == 0.f);
    CHECK(std::isfinite(Curve(.5f, 1.f, 100.f)));
    CHECK(Bounded(std::numeric_limits<float>::infinity(), .1f, 0.f, .4f) == .1f);
    CHECK(Trigger(0.f) == 0.f && Trigger(1.f) == 1.f && Trigger(.01f) == 0.f);
    CHECK(Trigger(.5f) > .45f && Trigger(.5f) < .5f);
    CHECK(SteeringAuthority(0.f) == 1.f);
    CHECK(SteeringAuthority(100.f) >= .58f && SteeringAuthority(100.f) < SteeringAuthority(20.f));
    CHECK(SteeringAuthority(-100.f) == SteeringAuthority(100.f));
    CHECK(SteeringRate(100.f, true, false) < SteeringRate(0.f, true, false));
    CHECK(SteeringRate(100.f, true, true) > SteeringRate(100.f, true, false));
    const float OneStep = Smooth(0.f, 1.f, 8.f, .1f);
    const float TwoSteps = Smooth(Smooth(0.f, 1.f, 8.f, .05f), 1.f, 8.f, .05f);
    CHECK(std::fabs(OneStep - TwoSteps) < .00001f);
    CHECK(Smooth(0.f, 1.f, 8.f, -1.f) == 0.f);
    for (int Preset = 0; Preset < 4; ++Preset)
    {
        FValveRequest R;
        CHECK(SamplePreset(Preset, 0.f, R) && R.Lift == 1.f);
        CHECK(!SamplePreset(Preset, -1.f, R) && R.Lift == 0.f);
        CHECK(!SamplePreset(Preset, PresetDuration(Preset), R) && R.Lift == 0.f);
        CHECK(!SamplePreset(Preset, std::numeric_limits<float>::quiet_NaN(), R));
        for (float T = 0.f; T < 4.f; T += .01f)
        {
            const bool Active = SamplePreset(Preset, T, R);
            CHECK(R.Lift >= 0.f && R.Lift <= 1.f && R.Roll >= -1.f && R.Roll <= 1.f && R.Pitch >= -1.f && R.Pitch <= 1.f);
            CHECK(R.Boost == 0.f && R.Yaw == 0.f);
            if (!Active) CHECK(R.Lift == 0.f && R.Roll == 0.f && R.Pitch == 0.f && !R.AttitudeBrake);
        }
    }
    FValveRequest R;
    CHECK(SamplePreset(0, PresetRotationStart(0) + .1f, R) && R.Roll > 0.f && R.Pitch > 0.f);
    CHECK(SamplePreset(1, PresetRotationStart(1) + .1f, R) && R.Roll == 1.f && R.Pitch == 0.f);
    CHECK(SamplePreset(2, PresetRotationStart(2) + .1f, R) && R.Pitch == 1.f && R.Roll == 0.f);
    CHECK(SamplePreset(3, PresetRotationStart(3) + .1f, R) && R.AttitudeBrake && R.Pitch == 0.f && R.Roll == 0.f);
    std::printf("PASS %u checks: bounded curves, monotonic/symmetric deadzone, triggers, speed shaping, frame-independent smoothing, finite valve presets.\n", Checks);
    return 0;
}
