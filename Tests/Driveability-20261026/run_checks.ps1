$ErrorActionPreference='Stop'
$projectPath='E:\TeslaRoadsterSim'
$outputPath='E:\TeslaRoadsterSim\Tests\Driveability-20261026'
$env:TEMP='E:\TeslaRoadsterSim\Temp'
$env:TMP=$env:TEMP
$vcvars='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
Set-Location -LiteralPath $outputPath
$studySource=Join-Path $outputPath 'DriveabilityStudy.cpp'
$studyExe=Join-Path $outputPath 'DriveabilityStudy-live.exe'
$studyObj=Join-Path $outputPath 'DriveabilityStudy-live.obj'
$studyCompile='call "'+$vcvars+'" >nul && cl /nologo /O2 /std:c++17 /EHsc /W4 "'+$studySource+'" /Fe:"'+$studyExe+'" /Fo:"'+$studyObj+'"'
& cmd.exe /d /c $studyCompile 2>&1|Set-Content -LiteralPath 'patched-build.log' -Encoding UTF8
if($LASTEXITCODE-ne 0){throw 'Driveability study build failed'}
foreach($mode in @('patched','raw')){& $studyExe $mode 2>&1|Set-Content -LiteralPath ($mode+'-run.log') -Encoding UTF8;if($LASTEXITCODE-ne 0){throw 'Driveability study run failed'}}
$jobs=@(
 @{Name='DriveabilityChecks';Source='Tests\Driveability-20261026\DriveabilityChecks.cpp';Flags=''},
 @{Name='AuditChecks';Source='Tests\Audit-20261025\AuditChecks.cpp';Flags=''},
 @{Name='LandingChecks';Source='Tests\LandingAssist-20261025\LandingChecks.cpp';Flags=''},
 @{Name='ControlMathHarness';Source='Tests\ControlMathHarness.cpp';Flags=''},
 @{Name='TimerAcceptance';Source='Verification-20261026\Physics\TimerAcceptance.cpp';Flags=''},
 @{Name='LightingEnergyChecks';Source='Verification-20261026\Physics\LightingEnergyChecks.cpp';Flags=''},
 @{Name='TouchdownChecks';Source='Verification-20261026\Physics\TouchdownChecks.cpp';Flags=''},
 @{Name='GasChecksLive';Source='Tests\GasExperiments-20261025\GasChecks.cpp';Flags='/DROADSTER_LIVE_KERNEL'}
)
Copy-Item -LiteralPath @((Get-ChildItem -LiteralPath (Join-Path $projectPath 'Tests\GasExperiments-20261025') -Filter '*-NIST.tsv' -File).FullName) -Destination $outputPath
$results=@()
foreach($job in $jobs){
 $name=$job.Name;$source=Join-Path $projectPath $job.Source;$exe=Join-Path $outputPath ($name+'.exe');$obj=Join-Path $outputPath ($name+'.obj')
 $command='call "'+$vcvars+'" >nul && cl /nologo /O2 /std:c++17 /EHsc /W4 '+$job.Flags+' "'+$source+'" /Fe:"'+$exe+'" /Fo:"'+$obj+'"'
 & cmd.exe /d /c $command 2>&1|Set-Content -LiteralPath ($name+'-build.log') -Encoding UTF8
 $build=$LASTEXITCODE;$run=$null
 if($build-eq 0){& $exe 2>&1|Set-Content -LiteralPath ($name+'-run.log') -Encoding UTF8;$run=$LASTEXITCODE}
 $lines=if(Test-Path -LiteralPath ($name+'-run.log')){@(Get-Content -LiteralPath ($name+'-run.log'))}else{@()}
 $result=[pscustomobject]@{Name=$name;BuildExit=$build;RunExit=$run;Summary=@($lines|Where-Object{$_-match '^RESULT|^PASS \d+ checks|^FAIL'})}
 $results+=$result;$results|ConvertTo-Json -Depth 6|Set-Content -LiteralPath 'regressions.json' -Encoding UTF8
 $result|ConvertTo-Json -Depth 4 -Compress|Write-Output
}
$script:MetricChecks=0;$script:MetricFailures=0;$log=@()
function CheckMetric([string]$name,[bool]$pass,[double]$value=0){$script:MetricChecks++;if(-not $pass){$script:MetricFailures++};$line=('{0} | {1} | {2:G12}'-f $(if($pass){'PASS'}else{'FAIL'}),$name,$value);$script:log+=$line;Write-Output $line}
$rows=@(Import-Csv -LiteralPath 'patched-metrics.csv')
foreach($row in $rows){CheckMetric ($row.case+' finite') ($row.finite-eq '1');CheckMetric ($row.case+' all wheels retain contact') ([int]$row.min_wheels-eq 4) ([double]$row.min_wheels);CheckMetric ($row.case+' physical tire force envelope') ([double]$row.peak_grip_usage-le 1.05) ([double]$row.peak_grip_usage)}
foreach($name in @('keys-pulse20','keys-pulse35','keys-pulse60','keys-slalom20','keys-slalom20-fps20','keys-slalom20-fps120','analog-full-pulse35','boost-steer20','brake-steer35')){$row=$rows|Where-Object{$_.case-eq $name};CheckMetric ($name+' controlled body sideslip') ([double]$row.max_side_deg-lt 8) ([double]$row.max_side_deg);CheckMetric ($name+' no sustained deep rear-tire slide') ([double]$row.high_rear_slip_seconds-eq 0) ([double]$row.max_rear_alpha_deg);CheckMetric ($name+' yaw settles after steering release') ([math]::Abs([double]$row.final_yaw_radps)-lt .02) ([double]$row.final_yaw_radps)}
foreach($name in @('keys-launch','analog-launch')){$row=$rows|Where-Object{$_.case-eq $name};CheckMetric ($name+' symmetric straight path') ([math]::Abs([double]$row.lateral_final_m)-lt .01) ([double]$row.lateral_final_m);CheckMetric ($name+' bounded motor slip under TCS') ([double]$row.max_rear_kappa-lt .2) ([double]$row.max_rear_kappa)}
$keys=@($rows|Where-Object{$_.case-like 'keys-slalom20*'});$min=($keys|ForEach-Object{[double]$_.max_side_deg}|Measure-Object -Minimum).Minimum;$max=($keys|ForEach-Object{[double]$_.max_side_deg}|Measure-Object -Maximum).Maximum;CheckMetric 'keyboard slalom20-120fps sideslip spread below1degree' (($max-$min)-lt 1) ($max-$min)
$stop=$rows|Where-Object{$_.case-eq 'brake35'};CheckMetric 'brake and Hold stop without residual crawl' ([double]$stop.final_speed_mps-lt .05) ([double]$stop.final_speed_mps)
foreach($name in @('reverse-right','reverse-left')){$row=$rows|Where-Object{$_.case-eq $name};CheckMetric ($name+' low-speed CG angle is not mistaken for tire sliding') ([double]$row.max_rear_alpha_deg-lt 3) ([double]$row.max_rear_alpha_deg)}
$raw=@(Import-Csv -LiteralPath 'raw-metrics.csv');$rawBoost=$raw|Where-Object{$_.case-eq 'boost-steer20'};$assistBoost=$rows|Where-Object{$_.case-eq 'boost-steer20'};CheckMetric 'Raw mode is a genuine driver opt-out' ([double]$rawBoost.max_side_deg-gt 3*[double]$assistBoost.max_side_deg) ([double]$rawBoost.max_side_deg)
$log|Set-Content -LiteralPath 'metrics-checks.log' -Encoding UTF8
[pscustomobject]@{Checks=$MetricChecks;Passed=$MetricChecks-$MetricFailures;Failed=$MetricFailures;CaseCount=$rows.Count}|ConvertTo-Json|Set-Content -LiteralPath 'metrics-checks.json' -Encoding UTF8
Write-Output ('METRICS RESULT '+($MetricChecks-$MetricFailures)+'/'+$MetricChecks)
if($MetricFailures-or @($results|Where-Object{$_.BuildExit-ne 0-or $_.RunExit-ne 0}).Count){throw 'Acceptance failed; inspect source-bound logs.'}

