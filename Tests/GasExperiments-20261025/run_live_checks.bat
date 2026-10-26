@echo off
setlocal
set TEMP=E:\TeslaRoadsterSim\Tests\GasExperiments-20261025
set TMP=E:\TeslaRoadsterSim\Tests\GasExperiments-20261025
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d E:\TeslaRoadsterSim\Tests\GasExperiments-20261025
cl /nologo /O2 /std:c++17 /EHsc /W4 /DROADSTER_LIVE_KERNEL GasChecks.cpp /Fe:GasChecksLive.exe /Fo:GasChecksLive.obj >live_build.log 2>&1
if errorlevel 1 exit /b 1
GasChecksLive.exe >live_output.log 2>&1
if errorlevel 1 exit /b 2
cl /nologo /O2 /std:c++17 /EHsc /W3 ..\ColdGasHarness.cpp /Fe:NitrogenAfter.exe /Fo:NitrogenAfter.obj >nitrogen_after_build.log 2>&1
if errorlevel 1 exit /b 3
NitrogenAfter.exe >nitrogen_after_output.log 2>&1
exit /b %errorlevel%
