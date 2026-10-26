#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

// Local-player supplement for native PlayStation devices on Windows. When this
// reports Connected(), use its gamepad values instead of merging with UE's pad
// values (translation software can expose the same pad a second time as XInput).
class ROADSTERSIM_API FRoadsterGamepad
{
public:
    FRoadsterGamepad();
    ~FRoadsterGamepad();
    FRoadsterGamepad(const FRoadsterGamepad&) = delete;
    FRoadsterGamepad& operator=(const FRoadsterGamepad&) = delete;

    bool Initialize();
    void Shutdown();
    // Call once before reading any controls that frame. Pass viewport focus as
    // bAcceptInput to neutralize cached inputs when the game loses focus.
    void Tick(float DeltaSeconds, bool bAcceptInput = true);
    bool IsAvailable() const;
    bool IsConnected() const;
    FString DeviceName() const;
    FString Status() const;
    // Raw normalized values: sticks [-1,1], triggers [0,1], Y positive upward.
    // Apply the user's deadzones/curves once in the player controller.
    float Axis(const FKey& Key) const;
    bool Down(const FKey& Key) const;
    bool Pressed(const FKey& Key) const;
    bool Released(const FKey& Key) const;
    // Low/high intensity [0,1]; false means absent or unsupported on that pad.
    bool Rumble(float Low, float High, uint32 DurationMS);
    bool IsPlayStation5() const;
    bool IsPlayStation3() const;

private:
    class FImpl;
    TUniquePtr<FImpl> Impl;
};
