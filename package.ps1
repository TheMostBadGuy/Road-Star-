# Cook and package the game to E:\TeslaRoadsterSim\Build (Windows, Shipping by default).
#   powershell -ExecutionPolicy Bypass -File package.ps1 [-Config Development]
param([string]$Config = "Shipping")
$UE = "C:\Program Files\Epic Games\UE_5.7"
$Root = $PSScriptRoot
# keep the derived-data cache on E: (C: has little free space)
${env:UE-LocalDataCachePath} = "$Root\DDC"
& "$UE\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
    "-project=$Root\Unreal\RoadsterSim\RoadsterSim.uproject" `
    -platform=Win64 "-clientconfig=$Config" -build -cook -stage -pak -archive `
    "-archivedirectory=$Root\Build" -noP4 -utf8output -unattended
exit $LASTEXITCODE
