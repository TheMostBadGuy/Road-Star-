@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
cl /nologo /O2 /std:c++17 /EHsc /W4 /I"..\Unreal\RoadsterSim\ThirdParty\SDL3\include" GamepadBackendHarness.cpp /Fe:GamepadBackendHarness.exe /Fo:GamepadBackendHarness.obj || exit /b 1
GamepadBackendHarness.exe "%~dp0..\Unreal\RoadsterSim\ThirdParty\SDL3\lib\x64\SDL3.dll"
exit /b %errorlevel%
