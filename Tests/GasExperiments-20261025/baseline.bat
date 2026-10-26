@echo off
setlocal
set TEMP=E:\TeslaRoadsterSim\Tests\GasExperiments-20261025
set TMP=E:\TeslaRoadsterSim\Tests\GasExperiments-20261025
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d E:\TeslaRoadsterSim\Tests\GasExperiments-20261025
cl /nologo /O2 /std:c++17 /EHsc /W3 ..\ColdGasHarness.cpp /Fe:NitrogenBaseline.exe /Fo:NitrogenBaseline.obj >baseline_build.log 2>&1
if errorlevel 1 exit /b 1
NitrogenBaseline.exe >baseline_output.log 2>&1
exit /b %errorlevel%
