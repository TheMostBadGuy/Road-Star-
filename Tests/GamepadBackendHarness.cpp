// Runs the production backend against SDL's in-process virtual joysticks.
// Synthetic input is not evidence of USB/Bluetooth hardware compatibility.
#include <windows.h>
#include <cstdio>
#include <cmath>
#include "../Unreal/RoadsterSim/Source/RoadsterSim/Private/RoadsterSonyGamepadSDL.h"

static unsigned Checks = 0;
#define CHECK(Expression) do { ++Checks; if (!(Expression)) { std::printf("FAIL line %d: %s\n", __LINE__, #Expression); return 1; } } while (0)
static void* Resolve(void* Context, const char* Name) { return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(Context), Name)); }
template<typename T> static T Export(HMODULE DLL, const char* Name) { return reinterpret_cast<T>(GetProcAddress(DLL, Name)); }
static Uint16 LastLow = 0, LastHigh = 0;
static bool SDLCALL RumbleCallback(void*, Uint16 Low, Uint16 High) { LastLow = Low; LastHigh = High; return true; }

int main(int Count, char** Args)
{
    CHECK(Count == 2);
    HMODULE DLL = LoadLibraryA(Args[1]);
    CHECK(DLL != nullptr);
    RoadsterInput::SonyGamepadSDL Pad;
    CHECK(Pad.Load(&Resolve, DLL));
    CHECK(Pad.Available());
    CHECK(Pad.Version() == SDL_VERSION);
    std::printf("Loaded official SDL %d from %s\n", Pad.Version(), Args[1]);
    std::printf("Physical devices present before synthetic tests: ");
    int PhysicalCount = 0;
    SDL_JoystickID* Physical = Pad.SDL_GetGamepadsFn(&PhysicalCount);
    std::printf("%d (friendly names only)\n", PhysicalCount);
    auto GetNameForID = Export<decltype(&SDL_GetGamepadNameForID)>(DLL, "SDL_GetGamepadNameForID");
    for (int Index = 0; Index < PhysicalCount; ++Index) std::printf("  %s\n", GetNameForID(Physical[Index]));
    Pad.SDL_freeFn(Physical);

    auto Attach = Export<decltype(&SDL_AttachVirtualJoystick)>(DLL, "SDL_AttachVirtualJoystick");
    auto Detach = Export<decltype(&SDL_DetachVirtualJoystick)>(DLL, "SDL_DetachVirtualJoystick");
    auto Open = Export<decltype(&SDL_OpenJoystick)>(DLL, "SDL_OpenJoystick");
    auto Close = Export<decltype(&SDL_CloseJoystick)>(DLL, "SDL_CloseJoystick");
    auto SetAxis = Export<decltype(&SDL_SetJoystickVirtualAxis)>(DLL, "SDL_SetJoystickVirtualAxis");
    auto SetButton = Export<decltype(&SDL_SetJoystickVirtualButton)>(DLL, "SDL_SetJoystickVirtualButton");
    auto SetMapping = Export<decltype(&SDL_SetGamepadMapping)>(DLL, "SDL_SetGamepadMapping");
    CHECK(Attach && Detach && Open && Close && SetAxis && SetButton && SetMapping);

    SDL_VirtualJoystickDesc Desc = {};
    SDL_INIT_INTERFACE(&Desc);
    Desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    Desc.naxes = 6; Desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    Desc.axis_mask = (1u << 6) - 1;
    Desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    Desc.name = "Roadster synthetic Xbox exclusion";
    Desc.Rumble = &RumbleCallback;
    SDL_JoystickID XboxID = Attach(&Desc);
    CHECK(XboxID != 0);
    CHECK(SetMapping(XboxID, "*,Roadster synthetic Xbox,type:xboxone,a:b0,b:b1,x:b2,y:b3,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,"));
    Pad.Tick(1.f);
    CHECK(!Pad.Connected()); // supplement must not duplicate UE's XInput path
    CHECK(Detach(XboxID));

    Desc.name = "Roadster synthetic DualSense";
    SDL_JoystickID ID = Attach(&Desc);
    CHECK(ID != 0);
    CHECK(SetMapping(ID, "*,Roadster synthetic DualSense,type:ps5,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,touchpad:b20,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,"));
    CHECK(Pad.SDL_GetGamepadTypeForIDFn(ID) == SDL_GAMEPAD_TYPE_PS5);
    SDL_Joystick* Joystick = Open(ID);
    CHECK(Joystick != nullptr);
    CHECK(SetAxis(Joystick, 4, -32768)); // virtual joystick raw triggers rest at minimum
    CHECK(SetAxis(Joystick, 5, -32768));
    CHECK(SetButton(Joystick, SDL_GAMEPAD_BUTTON_SOUTH, true));
    Pad.Tick(1.f);
    CHECK(Pad.Connected());
    CHECK(Pad.Type == SDL_GAMEPAD_TYPE_PS5);
    CHECK(Pad.Down(SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK(!Pad.Pressed(SDL_GAMEPAD_BUTTON_SOUTH)); // connection does not start game
    CHECK(Pad.Axes[4] == 0.f && Pad.Axes[5] == 0.f);
    CHECK(SetButton(Joystick, SDL_GAMEPAD_BUTTON_SOUTH, false));
    Pad.Tick(0.016f);
    CHECK(Pad.Released(SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK(SetButton(Joystick, SDL_GAMEPAD_BUTTON_SOUTH, true));
    CHECK(SetButton(Joystick, SDL_GAMEPAD_BUTTON_START, true));
    CHECK(SetButton(Joystick, SDL_GAMEPAD_BUTTON_TOUCHPAD, true));
    CHECK(SetAxis(Joystick, 0, -32768));
    CHECK(SetAxis(Joystick, 1, -32768));
    CHECK(SetAxis(Joystick, 2, 32767));
    CHECK(SetAxis(Joystick, 3, 32767));
    CHECK(SetAxis(Joystick, 4, 32767));
    CHECK(SetAxis(Joystick, 5, 0));
    Pad.Tick(0.016f);
    CHECK(Pad.Pressed(SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK(Pad.Pressed(SDL_GAMEPAD_BUTTON_SOUTH)); // non-consuming edge read
    CHECK(Pad.Pressed(SDL_GAMEPAD_BUTTON_START));
    CHECK(Pad.Pressed(SDL_GAMEPAD_BUTTON_TOUCHPAD));
    CHECK(Pad.Axes[0] == -1.f && Pad.Axes[1] == 1.f);
    CHECK(Pad.Axes[2] == 1.f && Pad.Axes[3] == -1.f);
    CHECK(Pad.Axes[4] == 1.f);
    CHECK(std::fabs(Pad.Axes[5] - 0.5f) < 0.001f);
    CHECK(Pad.Down(RoadsterInput::SonyGamepadSDL::LeftTriggerButton));
    Pad.Tick(0.016f);
    CHECK(Pad.Down(SDL_GAMEPAD_BUTTON_SOUTH) && !Pad.Pressed(SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK(Pad.Rumble(2.f, -1.f, 100));
    CHECK(LastLow == 65535 && LastHigh == 0);
    CHECK(Pad.Rumble(0.5f, 0.25f, 100));
    CHECK(LastLow > 32000 && LastLow < 33000 && LastHigh > 16000 && LastHigh < 17000);

    Pad.Tick(0.016f, false);
    CHECK(!Pad.Down(SDL_GAMEPAD_BUTTON_SOUTH));
    for (float Axis : Pad.Axes) CHECK(Axis == 0.f);
    Pad.Tick(0.016f, true);
    CHECK(Pad.Down(SDL_GAMEPAD_BUTTON_SOUTH) && !Pad.Pressed(SDL_GAMEPAD_BUTTON_SOUTH));

    CHECK(SetAxis(Joystick, 0, 1000));
    Pad.Tick(0.016f);
    CHECK(Pad.Axes[0] > 0.f && Pad.Axes[0] < 0.04f); // raw; parent applies deadzone once
    Close(Joystick);
    CHECK(Detach(ID));
    Pad.Tick(0.016f);
    CHECK(!Pad.Connected());
    CHECK(!Pad.Down(SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK(Pad.Released(SDL_GAMEPAD_BUTTON_SOUTH));
    for (float Axis : Pad.Axes) CHECK(Axis == 0.f);
    CHECK(!Pad.Rumble(1.f, 1.f, 100));

    // Reconnect as another Sony generation while a face button is held.
    Desc.name = "Roadster synthetic DualShock 4 reconnect";
    ID = Attach(&Desc);
    CHECK(ID != 0);
    CHECK(SetMapping(ID, "*,Roadster synthetic DualShock 4,type:ps4,a:b0,b:b1,x:b2,y:b3,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,"));
    Joystick = Open(ID);
    CHECK(Joystick != nullptr);
    CHECK(SetButton(Joystick, SDL_GAMEPAD_BUTTON_SOUTH, true));
    Pad.Tick(1.f);
    CHECK(Pad.Connected() && Pad.Type == SDL_GAMEPAD_TYPE_PS4);
    CHECK(Pad.Down(SDL_GAMEPAD_BUTTON_SOUTH) && !Pad.Pressed(SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK(SetButton(Joystick, SDL_GAMEPAD_BUTTON_SOUTH, false));
    Pad.Tick(0.016f);
    CHECK(SetButton(Joystick, SDL_GAMEPAD_BUTTON_SOUTH, true));
    Pad.Tick(0.016f);
    CHECK(Pad.Pressed(SDL_GAMEPAD_BUTTON_SOUTH));
    Close(Joystick);
    CHECK(Detach(ID));
    Pad.Tick(0.016f);
    CHECK(!Pad.Connected());

    // The recognizer accepts SDL's PS3/PS4 types without promising their drivers.
    CHECK(RoadsterInput::SonyGamepadSDL::IsSonyType(SDL_GAMEPAD_TYPE_PS3));
    CHECK(RoadsterInput::SonyGamepadSDL::IsSonyType(SDL_GAMEPAD_TYPE_PS4));
    CHECK(!RoadsterInput::SonyGamepadSDL::IsSonyType(SDL_GAMEPAD_TYPE_XBOXONE));
    Pad.Shutdown();
    Pad.Shutdown(); // idempotent shutdown
    CHECK(!Pad.Available() && !Pad.Connected());
    CHECK(Pad.Load(&Resolve, DLL));
    Pad.Tick(1.f);
    CHECK(!Pad.Connected());
    Pad.Shutdown();
    FreeLibrary(DLL);
    std::printf("PASS %u checks: actual SDL load, synthetic Sony mapping, axes/triggers, cached edges, focus, hotplug, XInput exclusion, rumble, shutdown.\n", Checks);
    return 0;
}
