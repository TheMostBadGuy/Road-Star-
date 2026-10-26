@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
cl /nologo /O2 /std:c++17 /EHsc /W4 ControlMathHarness.cpp /Fe:ControlMathHarness.exe /Fo:ControlMathHarness.obj || exit /b 1
ControlMathHarness.exe
exit /b %errorlevel%
