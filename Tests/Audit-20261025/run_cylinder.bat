@echo off
setlocal
set TEMP=E:\TeslaRoadsterSim\Tests\Audit-20261025
set TMP=E:\TeslaRoadsterSim\Tests\Audit-20261025
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d E:\TeslaRoadsterSim\Tests\Audit-20261025
cl /nologo /O2 /std:c++17 /EHsc /W3 CylinderStudy.cpp /Fe:CylinderStudy.exe /Fo:CylinderStudy.obj >cylinder_build.log 2>&1
if errorlevel 1 exit /b 2
CylinderStudy.exe >cylinder_output.log 2>&1
exit /b %errorlevel%
