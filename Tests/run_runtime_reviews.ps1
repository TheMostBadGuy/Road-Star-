param(
    [string]$Executable = 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe',
    [switch]$Packaged,
    [string]$Label = 'EditorReview'
)
$ErrorActionPreference = 'Stop'
$root = 'E:\TeslaRoadsterSim'
$env:TEMP = "$root\Temp"
$env:TMP = $env:TEMP
${env:UE-LocalDataCachePath} = "$root\DDC"
$prefix = if ($Packaged) { '' } else { '"E:\TeslaRoadsterSim\Unreal\RoadsterSim\RoadsterSim.uproject" -game ' }
$cases = @(
    @{Name='Exterior'; Args='-review=exterior -shots=9 -exitafter=12'},
    @{Name='Cockpit'; Args='-review=cockpit -shots=9 -exitafter=12'},
    @{Name='Touchscreen'; Args='-review=screen -shots=9 -exitafter=12'},
    @{Name='Builder'; Args='-review=builder -shots=9 -exitafter=12'},
    @{Name='TunnelDrive'; Args='-autodrive -track=1 -cam=0 -verifytracks -shots=12,16,24 -exitafter=30'}
)
foreach ($case in $cases) {
    $out = "$root\Verification-20261025\$Label\$($case.Name)"
    New-Item -ItemType Directory -Path $out -Force | Out-Null
    $arguments = $prefix + '-windowed -ResX=1920 -ResY=1080 -NoSplash -unattended -reviewquality=3 ' + $case.Args +
        ' -shotdir="' + $out + '" -abslog="' + $out + '\runtime.log"'
    $process = Start-Process -FilePath $Executable -ArgumentList $arguments -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Runtime $($case.Name) exited with $($process.ExitCode)" }
    if (!(Test-Path -LiteralPath "$out\shot_00.png")) { throw "Missing capture for $($case.Name)" }
    Get-Content -LiteralPath "$out\performance.txt"
}
