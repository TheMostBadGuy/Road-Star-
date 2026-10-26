[System.AppDomain]::CurrentDomain.add_AssemblyResolve({
    param($sender, $args)
    $dir = 'e:\Tesla Sim PC\Tesla Sim PC\Tesla-simulator-PC_Data\Managed'
    $name = ($args.Name.Split(','))[0] + '.dll'
    $path = Join-Path $dir $name
    if (Test-Path $path) {
        return [System.Reflection.Assembly]::Load([System.IO.File]::ReadAllBytes($path))
    }
    return $null
})

$bytes = [System.IO.File]::ReadAllBytes('e:\Tesla Sim PC\Tesla Sim PC\Tesla-simulator-PC_Data\Managed\Assembly-CSharp.dll')
$asm = [System.Reflection.Assembly]::Load($bytes)
$types = $asm.GetTypes()
foreach ($t in $types) {
    if ($t.IsPublic) {
        Write-Output $t.FullName
    }
}
