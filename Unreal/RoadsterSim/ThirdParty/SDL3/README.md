# SDL3 gamepad dependency

The Roadster simulator's native PlayStation supplement uses official SDL
3.4.18, released October 2, 2026. It does not install a service, driver,
controller translation tool, or system-wide configuration.

Upstream release: https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18

Both downloaded archives were checked against the release asset digests returned
by GitHub's official API on October 25, 2026:

| Archive | SHA-256 |
| --- | --- |
| `Archives/SDL3-devel-3.4.18-VC.zip` | `78d84602ae616cfe26b33a73b7d3b9b6b56e8a6650e53d92c9c6ca938f747acd` |
| `Archives/SDL3-3.4.18.zip` (full source) | `9cd42377704398796071b8597cd7e21da254a43bad98c4199739647adc13fa6f` |

The include tree, x64 DLL/import library, and `LICENSE.txt` were extracted
unchanged from the VC archive. The runtime uses explicit DLL export resolution;
it does not statically import SDL or its entry point. The build stages `SDL3.dll`
and the SDL license beside the packaged executable. Editor/development runs can
also load the DLL from this directory's `lib/x64/SDL3.dll`.

SDL is distributed under the zlib license in `LICENSE.txt`. The complete upstream
source is retained in the source archive for inspection and rebuilding.

Official API references used:

- https://wiki.libsdl.org/SDL3/CategoryGamepad
- https://wiki.libsdl.org/SDL3/SDL_UpdateGamepads
- https://wiki.libsdl.org/SDL3/SDL_SetGamepadEventsEnabled
- https://wiki.libsdl.org/SDL3/SDL_GamepadAxis
- https://wiki.libsdl.org/SDL3/SDL_GamepadType
- https://wiki.libsdl.org/SDL3/SDL_HINT_JOYSTICK_HIDAPI_PS4
- https://wiki.libsdl.org/SDL3/SDL_HINT_JOYSTICK_HIDAPI_PS5
- https://wiki.libsdl.org/SDL3/SDL_HINT_JOYSTICK_HIDAPI_PS3
- https://wiki.libsdl.org/SDL3/SDL_RumbleGamepad
- https://wiki.libsdl.org/SDL3/SDL_VirtualJoystickDesc
- https://wiki.libsdl.org/SDL3/SDL_SetGamepadMapping
