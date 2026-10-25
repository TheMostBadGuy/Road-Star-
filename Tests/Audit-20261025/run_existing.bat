@echo off
setlocal
set TEMP=E:\TeslaRoadsterSim\Tests\Audit-20261025
set TMP=E:\TeslaRoadsterSim\Tests\Audit-20261025
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d E:\TeslaRoadsterSim\Tests\Audit-20261025
cl /nologo /O2 /std:c++17 /EHsc /W3 ..\PhysicsHarness.cpp /Fe:PhysicsHarness.exe /Fo:PhysicsHarness.obj >physics_build.log 2>&1
if errorlevel 1 exit /b 1
PhysicsHarness.exe >physics_output.log 2>&1
if errorlevel 1 exit /b 2
cl /nologo /O2 /std:c++17 /EHsc /W3 ..\ColdGasHarness.cpp /Fe:ColdGasHarness.exe /Fo:ColdGasHarness.obj >coldgas_build.log 2>&1
if errorlevel 1 exit /b 3
ColdGasHarness.exe >coldgas_output.log 2>&1
if errorlevel 1 exit /b 4
exit /b 0
