#pragma once

// This small core also builds outside Unreal for real SDL virtual-device tests.
// It owns only SDL's gamepad subsystem; Unreal continues to own the window,
// keyboard, mouse, audio, and XInput devices.
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_version.h>

namespace RoadsterInput
{
class SonyGamepadSDL
{
public:
    using Resolver = void* (*)(void*, const char*);
    static constexpr unsigned LeftTriggerButton = 30;
    static constexpr unsigned RightTriggerButton = 31;

    // No static SDL imports: a missing DLL leaves Unreal's normal input usable.
#define ROADSTER_SDL_FUNCTIONS(X) \
    X(SDL_InitSubSystem) X(SDL_QuitSubSystem) X(SDL_SetHint) \
    X(SDL_SetGamepadEventsEnabled) X(SDL_SetJoystickEventsEnabled) \
    X(SDL_UpdateGamepads) X(SDL_GetGamepads) X(SDL_free) \
    X(SDL_GetGamepadTypeForID) X(SDL_OpenGamepad) X(SDL_CloseGamepad) \
    X(SDL_GamepadConnected) X(SDL_GetGamepadName) X(SDL_GetGamepadAxis) \
    X(SDL_GetGamepadButton) X(SDL_RumbleGamepad) X(SDL_GetError) X(SDL_GetVersion)
#define ROADSTER_SDL_DECLARE(Name) decltype(&Name) Name##Fn = nullptr;
    ROADSTER_SDL_FUNCTIONS(ROADSTER_SDL_DECLARE)
#undef ROADSTER_SDL_DECLARE

    bool Load(Resolver Resolve, void* Context)
    {
#define ROADSTER_SDL_RESOLVE(Name) \
        Name##Fn = reinterpret_cast<decltype(Name##Fn)>(Resolve(Context, #Name)); \
        if (!Name##Fn) { Error = "SDL3.dll is missing a required export: " #Name; return false; }
        ROADSTER_SDL_FUNCTIONS(ROADSTER_SDL_RESOLVE)
#undef ROADSTER_SDL_RESOLVE
        // PS4/5 HIDAPI works without adding a controller translation driver.
        SDL_SetHintFn(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1");
        SDL_SetHintFn(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1");
        // PS3 remains conditional on the device/driver already exposing it.
        // Preserve SDL's platform default rather than changing system drivers.
        SDL_SetHintFn(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        if (!SDL_InitSubSystemFn(SDL_INIT_GAMEPAD))
        {
            Error = SDL_GetErrorFn();
            return false;
        }
        Initialized = true;
        // Polling prevents an unconsumed SDL event queue growing beside UE's loop.
        SDL_SetGamepadEventsEnabledFn(false);
        SDL_SetJoystickEventsEnabledFn(false);
        return true;
    }

    void Shutdown()
    {
        Close();
        if (Initialized) SDL_QuitSubSystemFn(SDL_INIT_GAMEPAD);
        Initialized = false;
        Buttons = PressedButtons = ReleasedButtons = 0;
        for (float& Axis : Axes) Axis = 0.f;
    }

    static bool IsSonyType(SDL_GamepadType Type)
    {
        return Type == SDL_GAMEPAD_TYPE_PS3 || Type == SDL_GAMEPAD_TYPE_PS4 || Type == SDL_GAMEPAD_TYPE_PS5;
    }

    // Call exactly once each game frame. Cached edges remain stable on repeated
    // reads in menus, driving, flight, and UI during that frame.
    void Tick(float DeltaSeconds, bool AcceptInput = true)
    {
        PressedButtons = ReleasedButtons = 0;
        if (!Initialized) return;
        SDL_UpdateGamepadsFn();
        if (Pad && !SDL_GamepadConnectedFn(Pad)) Close();
        ScanCountdown -= DeltaSeconds;
        bool NewDevice = false;
        if (!Pad && ScanCountdown <= 0.f)
        {
            ScanCountdown = 0.5f;
            int Count = 0;
            SDL_JoystickID* IDs = SDL_GetGamepadsFn(&Count);
            for (int Index = 0; IDs && Index < Count && !Pad; ++Index)
            {
                const SDL_GamepadType CandidateType = SDL_GetGamepadTypeForIDFn(IDs[Index]);
                if (!IsSonyType(CandidateType)) continue; // UE handles XInput.
                Pad = SDL_OpenGamepadFn(IDs[Index]);
                if (Pad) { Type = CandidateType; DeviceID = IDs[Index]; NewDevice = true; }
            }
            SDL_freeFn(IDs);
        }
        const unsigned Previous = Buttons;
        Buttons = 0;
        for (float& Axis : Axes) Axis = 0.f;
        if (Pad && AcceptInput)
        {
            for (int Index = 0; Index < 6; ++Index)
            {
                const Sint16 Raw = SDL_GetGamepadAxisFn(Pad, static_cast<SDL_GamepadAxis>(Index));
                Axes[Index] = Index < 4 ? NormalizeStick(Raw) : NormalizeTrigger(Raw);
            }
            // SDL Y is down-positive; Unreal's Gamepad_*Y is up-positive.
            Axes[SDL_GAMEPAD_AXIS_LEFTY] *= -1.f;
            Axes[SDL_GAMEPAD_AXIS_RIGHTY] *= -1.f;
            for (int Index = 0; Index < SDL_GAMEPAD_BUTTON_COUNT; ++Index)
                if (SDL_GetGamepadButtonFn(Pad, static_cast<SDL_GamepadButton>(Index))) Buttons |= 1u << Index;
            if (Axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] > 0.5f) Buttons |= 1u << LeftTriggerButton;
            if (Axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] > 0.5f) Buttons |= 1u << RightTriggerButton;
        }
        // Connecting while holding Cross/Options must not start or pause a game.
        // Likewise re-focusing with a held button must not create a new press.
        const bool SuppressEdges = NewDevice || (!WasAcceptingInput && AcceptInput);
        PressedButtons = SuppressEdges ? 0 : Buttons & ~Previous;
        ReleasedButtons = Previous & ~Buttons;
        WasAcceptingInput = AcceptInput;
    }

    bool Rumble(float Low, float High, unsigned DurationMS)
    {
        if (!Pad || !Initialized || !SDL_GamepadConnectedFn(Pad)) return false;
        return SDL_RumbleGamepadFn(Pad, ToRumble(Low), ToRumble(High), DurationMS);
    }

    static float NormalizeStick(Sint16 Value) { return float(Value) / (Value < 0 ? 32768.f : 32767.f); }
    static float NormalizeTrigger(Sint16 Value) { return Value <= 0 ? 0.f : float(Value) / 32767.f; }
    static Uint16 ToRumble(float Value) { return Value > 0.f ? (Value >= 1.f ? 65535 : static_cast<Uint16>(Value * 65535.f)) : 0; }
    bool Connected() const { return Pad != nullptr; }
    const char* Name() const { return Pad ? SDL_GetGamepadNameFn(Pad) : nullptr; }
    int Version() const { return Initialized ? SDL_GetVersionFn() : 0; }
    bool Available() const { return Initialized; }
    bool Down(unsigned Button) const { return Button < 32 && (Buttons & (1u << Button)) != 0; }
    bool Pressed(unsigned Button) const { return Button < 32 && (PressedButtons & (1u << Button)) != 0; }
    bool Released(unsigned Button) const { return Button < 32 && (ReleasedButtons & (1u << Button)) != 0; }
    float Axes[6] = {};
    unsigned DeviceID = 0;
    SDL_GamepadType Type = SDL_GAMEPAD_TYPE_UNKNOWN;
    const char* Error = "";

private:
    void Close()
    {
        if (Pad)
        {
            SDL_RumbleGamepadFn(Pad, 0, 0, 0);
            SDL_CloseGamepadFn(Pad);
            Pad = nullptr;
        }
        DeviceID = 0;
        Type = SDL_GAMEPAD_TYPE_UNKNOWN;
        ScanCountdown = 0.f;
    }
    SDL_Gamepad* Pad = nullptr;
    bool Initialized = false;
    bool WasAcceptingInput = false;
    float ScanCountdown = 0.f;
    unsigned Buttons = 0, PressedButtons = 0, ReleasedButtons = 0;
};
#undef ROADSTER_SDL_FUNCTIONS
}
