# Cook and package the game to E:\TeslaRoadsterSim\Build (Windows, Shipping by default).
#   powershell -ExecutionPolicy Bypass -File package.ps1 [-Config Development]
param([string]$Config = "Shipping")
$UE = "E:\Epic Games\UE_5.8"
$Root = $PSScriptRoot
# keep the derived-data cache on E: (C: has little free space)
${env:UE-LocalDataCachePath} = "$Root\DDC"
New-Item -ItemType Directory -Path "$Root\Temp" -Force | Out-Null
$env:TEMP = "$Root\Temp"
$env:TMP = $env:TEMP
& "$UE\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
    "-project=$Root\Unreal\RoadsterSim\RoadsterSim.uproject" `
    -platform=Win64 "-clientconfig=$Config" -build -cook -stage -pak -archive `
    "-archivedirectory=$Root\Build" -noP4 -utf8output -unattended -NoUBA
exit $LASTEXITCODE
