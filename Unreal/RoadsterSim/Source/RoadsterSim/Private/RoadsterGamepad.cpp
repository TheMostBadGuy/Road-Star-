#include "RoadsterGamepad.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

THIRD_PARTY_INCLUDES_START
#include "RoadsterSonyGamepadSDL.h"
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogRoadsterGamepad, Log, All);

class FRoadsterGamepad::FImpl
{
public:
    RoadsterInput::SonyGamepadSDL Pad;
    void* DLL = nullptr;
    bool bAttempted = false;
    unsigned LastDevice = 0;
    FString LastName;
    FString LastStatus = TEXT("PlayStation input has not initialized");
};

namespace
{
void* ResolveSDL(void* Library, const char* Name)
{
    return FPlatformProcess::GetDllExport(Library, UTF8_TO_TCHAR(Name));
}

int32 ButtonForKey(const FKey& Key)
{
    if (Key == EKeys::Gamepad_FaceButton_Bottom) return SDL_GAMEPAD_BUTTON_SOUTH; // Cross
    if (Key == EKeys::Gamepad_FaceButton_Right) return SDL_GAMEPAD_BUTTON_EAST; // Circle
    if (Key == EKeys::Gamepad_FaceButton_Left) return SDL_GAMEPAD_BUTTON_WEST; // Square
    if (Key == EKeys::Gamepad_FaceButton_Top) return SDL_GAMEPAD_BUTTON_NORTH; // Triangle
    if (Key == EKeys::Gamepad_Special_Left) return SDL_GAMEPAD_BUTTON_BACK; // Share / Create / Select
    if (Key == EKeys::Gamepad_Special_Right) return SDL_GAMEPAD_BUTTON_START; // Options / Start
    if (Key == EKeys::Gamepad_LeftShoulder) return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    if (Key == EKeys::Gamepad_RightShoulder) return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    if (Key == EKeys::Gamepad_LeftThumbstick) return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    if (Key == EKeys::Gamepad_RightThumbstick) return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    if (Key == EKeys::Gamepad_DPad_Up) return SDL_GAMEPAD_BUTTON_DPAD_UP;
    if (Key == EKeys::Gamepad_DPad_Down) return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    if (Key == EKeys::Gamepad_DPad_Left) return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    if (Key == EKeys::Gamepad_DPad_Right) return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    if (Key == EKeys::Gamepad_LeftTrigger) return RoadsterInput::SonyGamepadSDL::LeftTriggerButton;
    if (Key == EKeys::Gamepad_RightTrigger) return RoadsterInput::SonyGamepadSDL::RightTriggerButton;
    return -1;
}
}

FRoadsterGamepad::FRoadsterGamepad() : Impl(MakeUnique<FImpl>()) {}
FRoadsterGamepad::~FRoadsterGamepad() { Shutdown(); }

bool FRoadsterGamepad::Initialize()
{
    if (Impl->bAttempted) return Impl->Pad.Available();
    Impl->bAttempted = true;
#if PLATFORM_WINDOWS
    const FString PackagedDLL = FPaths::Combine(FPlatformProcess::BaseDir(), TEXT("SDL3.dll"));
    const FString DevelopmentDLL = FPaths::ProjectDir() / TEXT("ThirdParty/SDL3/lib/x64/SDL3.dll");
    const FString DLLPath = FPaths::FileExists(PackagedDLL) ? PackagedDLL : DevelopmentDLL;
    Impl->DLL = FPlatformProcess::GetDllHandle(*DLLPath);
    if (Impl->DLL && Impl->Pad.Load(&ResolveSDL, Impl->DLL))
    {
        Impl->LastStatus = FString::Printf(TEXT("SDL %d ready; connect a PlayStation controller"), Impl->Pad.Version());
        UE_LOG(LogRoadsterGamepad, Display, TEXT("%s"), *Impl->LastStatus);
        return true;
    }
    Impl->LastStatus = Impl->DLL
        ? FString::Printf(TEXT("PlayStation input unavailable: %s"), UTF8_TO_TCHAR(Impl->Pad.Error))
        : TEXT("SDL3.dll unavailable; standard Unreal gamepad input remains active");
#else
    Impl->LastStatus = TEXT("Native PlayStation supplement is built for Windows; standard input remains active");
#endif
    UE_LOG(LogRoadsterGamepad, Warning, TEXT("%s"), *Impl->LastStatus);
    Shutdown();
    Impl->bAttempted = true; // Failure should not retry and log on every frame.
    return false;
}

void FRoadsterGamepad::Shutdown()
{
    Impl->Pad.Shutdown();
    if (Impl->DLL) { FPlatformProcess::FreeDllHandle(Impl->DLL); Impl->DLL = nullptr; }
    Impl->LastDevice = 0;
    Impl->LastName.Empty();
    Impl->bAttempted = false;
}

void FRoadsterGamepad::Tick(float DeltaSeconds, bool bAcceptInput)
{
    if (!Impl->bAttempted) Initialize();
    Impl->Pad.Tick(FMath::Max(DeltaSeconds, 0.f), bAcceptInput);
    if (Impl->LastDevice == Impl->Pad.DeviceID) return;
    Impl->LastDevice = Impl->Pad.DeviceID;
    Impl->LastName = Impl->Pad.Name() ? UTF8_TO_TCHAR(Impl->Pad.Name()) : TEXT("");
    Impl->LastStatus = Impl->Pad.Connected()
        ? FString::Printf(TEXT("PlayStation connected: %s"), *Impl->LastName)
        : TEXT("PlayStation disconnected; standard Unreal input active");
    UE_LOG(LogRoadsterGamepad, Display, TEXT("%s"), *Impl->LastStatus);
}

bool FRoadsterGamepad::IsAvailable() const { return Impl->Pad.Available(); }
bool FRoadsterGamepad::IsConnected() const { return Impl->Pad.Connected(); }
FString FRoadsterGamepad::DeviceName() const { return Impl->LastName; }
FString FRoadsterGamepad::Status() const { return Impl->LastStatus; }
bool FRoadsterGamepad::IsPlayStation5() const { return Impl->Pad.Type == SDL_GAMEPAD_TYPE_PS5; }
bool FRoadsterGamepad::IsPlayStation3() const { return Impl->Pad.Type == SDL_GAMEPAD_TYPE_PS3; }

float FRoadsterGamepad::Axis(const FKey& Key) const
{
    if (Key == EKeys::Gamepad_LeftX) return Impl->Pad.Axes[SDL_GAMEPAD_AXIS_LEFTX];
    if (Key == EKeys::Gamepad_LeftY) return Impl->Pad.Axes[SDL_GAMEPAD_AXIS_LEFTY];
    if (Key == EKeys::Gamepad_RightX) return Impl->Pad.Axes[SDL_GAMEPAD_AXIS_RIGHTX];
    if (Key == EKeys::Gamepad_RightY) return Impl->Pad.Axes[SDL_GAMEPAD_AXIS_RIGHTY];
    if (Key == EKeys::Gamepad_LeftTriggerAxis) return Impl->Pad.Axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER];
    if (Key == EKeys::Gamepad_RightTriggerAxis) return Impl->Pad.Axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER];
    return 0.f;
}

bool FRoadsterGamepad::Down(const FKey& Key) const
{
    return Impl->Pad.Down(ButtonForKey(Key)) || (Key == EKeys::Gamepad_Special_Left && Impl->Pad.Down(SDL_GAMEPAD_BUTTON_TOUCHPAD));
}
bool FRoadsterGamepad::Pressed(const FKey& Key) const
{
    return Impl->Pad.Pressed(ButtonForKey(Key)) || (Key == EKeys::Gamepad_Special_Left && Impl->Pad.Pressed(SDL_GAMEPAD_BUTTON_TOUCHPAD));
}
bool FRoadsterGamepad::Released(const FKey& Key) const
{
    return Impl->Pad.Released(ButtonForKey(Key)) || (Key == EKeys::Gamepad_Special_Left && Impl->Pad.Released(SDL_GAMEPAD_BUTTON_TOUCHPAD));
}
bool FRoadsterGamepad::Rumble(float Low, float High, uint32 DurationMS) { return Impl->Pad.Rumble(Low, High, DurationMS); }
