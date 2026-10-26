# Native PlayStation controller supplement

`FRoadsterGamepad` supplements Unreal's Windows XInput input path with SDL3's
PS4/PS5 HIDAPI support. It opens only SDL-recognized PS3, PS4, or PS5 gamepad
types. Xbox and other controllers remain on Unreal's existing path.

`WinDualShock` in this installed Unreal 5.7 distribution was verified to compile
with `DUALSHOCK4_SUPPORT=0` and `DUALSENSE_SUPPORT=0`: its build requires the
separately available licensed LibScePad. Merely enabling that plugin would not
give this installation native Sony input. RawInput and GameInput would require
device/report mappings; SDL provides maintained transport-specific Sony parsers.

## Player-controller integration

Own one `FRoadsterGamepad` per local game controller, initialize in BeginPlay,
call `Tick(DeltaSeconds, bViewportFocused)` once before all control reads, and
call `Shutdown()` during EndPlay. The destructor also shuts it down safely.

When `IsConnected()` is true, use this backend for every gamepad key. Otherwise
use Unreal's standard gamepad reads. Keep keyboard and mouse available. Do not
add values from both gamepad paths: tools such as Steam Input can expose a native
Sony controller and a translated XInput device at the same time.

`Axis(FKey)` returns raw normalized sticks in [-1,1], triggers in [0,1], and Y
positive up to match Unreal. It does not apply a deadzone or response curve.
The player controller should apply the user's tuning exactly once.

`Down`, `Pressed`, and `Released` are cached for a whole tick and reads do not
consume edges. On device connection or foreground regain, held buttons do not
produce a fresh press. On disconnect or focus loss, inputs become neutral. The
core polls hotplug every 0.5 seconds while disconnected and never opens Xbox
devices. Rumble returns false when a controller is absent or lacks support;
it clamps intensities to [0,1] and uses SDL's low/high frequency rumble API.

| Unreal key | Sony control |
| --- | --- |
| FaceButton_Bottom / Right / Left / Top | Cross / Circle / Square / Triangle |
| Special_Right | Options / Start |
| Special_Left | Touchpad click OR Share / Create / Select |
| LeftShoulder / RightShoulder | L1 / R1 |
| LeftTriggerAxis / RightTriggerAxis | L2 / R2 analog |
| LeftTrigger / RightTrigger | L2 / R2 above 0.5 |
| LeftThumbstick / RightThumbstick | L3 / R3 clicks |
| DPad directions | D-pad directions |

## Verification and limits

Run `Tests/build_gamepad_harness.bat`. It compiles the same portable core used
by the Unreal adapter and loads the actual shipped x64 SDL3 DLL. SDL virtual
joysticks exercise Sony type mapping, exclusion of Xbox types, stick endpoints,
Y inversion, analog triggers, small raw values, cached edges, held-button connect,
unplug/reconnect, focus gating, rumble callbacks, and shutdown/reinitialization.
These devices exist only inside the test process and require no system driver.

On October 25, 2026, read-only PnP friendly-name inspection and live SDL
enumeration found no connected physical gamepads. No USB or Bluetooth controller
has therefore been physically verified in this environment. Synthetic tests and
a successful build do not establish physical transport behavior or rumble feel.

Expected native scope is DualShock 4 and DualSense devices recognized by SDL's
Windows HIDAPI drivers, over their supported USB/Bluetooth paths. DualSense Edge
is recognized as the PS5 family; additional paddles/adaptive triggers/gyro and
touchpad position are outside this binding set. Standard rumble capability varies
by device and transport. PS3 support is conditional on SDL recognizing a device
through a compatible driver already installed: SDL documents Windows driver
requirements, and this work installs none. PS1/PS2 controllers need an adapter
that Windows/Unreal recognizes; universal support across those generations,
clones, adapters, and every transport is not claimed.
