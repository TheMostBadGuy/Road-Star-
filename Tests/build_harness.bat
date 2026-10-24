@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
cl /nologo /O2 /std:c++17 /EHsc /W3 PhysicsHarness.cpp /Fe:PhysicsHarness.exe /Fo:PhysicsHarness.obj || exit /b 1
PhysicsHarness.exe
