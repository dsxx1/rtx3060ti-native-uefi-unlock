#Requires -Version 5.1
[CmdletBinding()]
param([string]$DriveLetter='',[string]$FolderPath='',[string]$AssetsRoot='',[switch]$ProbeOnly)
$ErrorActionPreference='Stop'
$repo=$PSScriptRoot
. (Join-Path $repo 'HardwareDiscovery.ps1')
if([bool]$DriveLetter -eq [bool]$FolderPath){throw 'Specify exactly one of -DriveLetter or -FolderPath'}
if($DriveLetter){
    if($DriveLetter -notmatch '^[A-Za-z]:?$'){throw 'Invalid drive letter'}
    $letter=$DriveLetter.TrimEnd(':')
    $partition=Get-Partition -DriveLetter $letter
    $disk=Get-Disk -Number $partition.DiskNumber
    $volume=Get-Volume -DriveLetter $letter
    if($disk.BusType -ne 'USB' -or $disk.IsBoot -or $disk.IsSystem -or (Get-VolumeFileSystemName $volume) -ne 'FAT32'){throw 'Target must be a non-system FAT32 USB disk'}
    $target=$letter+':\'
}else{
    $target=[IO.Path]::GetFullPath($FolderPath)
    $root=[IO.Path]::GetFullPath($repo).TrimEnd('\')+'\'
    $t=$target.TrimEnd('\')+'\'
    if($target.TrimEnd('\') -eq [IO.Path]::GetPathRoot($target).TrimEnd('\') -or
       $root.StartsWith($t,[StringComparison]::OrdinalIgnoreCase) -or
       ($t.StartsWith($root,[StringComparison]::OrdinalIgnoreCase) -and -not $t.StartsWith($root+'build\',[StringComparison]::OrdinalIgnoreCase))){throw 'Unsafe staging folder'}
}
$build=Join-Path $repo 'build\native-cap01'
$manifest=Get-Content -LiteralPath (Join-Path $build 'build-result.json') -Raw | ConvertFrom-Json
$unlock=Join-Path $build 'UNLOCK-MANUAL.EFI';$probe=Join-Path $build 'PROBE-CONSOLE.EFI'
foreach($item in @(@{path=$unlock;hash=$manifest.unlock.sha256},@{path=$probe;hash=$manifest.console_probe.sha256})){
    if((Get-FileHash -LiteralPath $item.path).Hash.ToLowerInvariant() -ne $item.hash -or -not (Test-EfiApplicationBytes ([IO.File]::ReadAllBytes($item.path)))){throw 'EFI integrity verification failed'}
}
if(-not $AssetsRoot){$AssetsRoot=Join-Path $repo 'assets'}
$pins=Get-Content -LiteralPath (Join-Path $repo 'firmware-manifest.json') -Raw | ConvertFrom-Json
$assets=@($pins.PSObject.Properties | Where-Object {$_.Value.arch -in 'ampere','shared'})
if(-not $ProbeOnly){foreach($pin in $assets){$p=Join-Path $AssetsRoot $pin.Name;if((Get-Item -LiteralPath $p).Length -ne $pin.Value.bytes -or (Get-FileHash -LiteralPath $p).Hash.ToLowerInvariant() -ne $pin.Value.sha256){throw ('Firmware mismatch: '+$pin.Name)}}}
New-Item -ItemType Directory -Path $target -Force|Out-Null
$backup=Join-Path $target ('NATIVE-CAP01-BACKUP\'+[datetime]::UtcNow.ToString('yyyyMMdd-HHmmssfff'))
function Stage-File($source,$relative){
    $dest=Join-Path $target $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force|Out-Null
    if(Test-Path -LiteralPath $dest){$saved=Join-Path $backup $relative;New-Item -ItemType Directory -Path (Split-Path -Parent $saved) -Force|Out-Null;Copy-Item -LiteralPath $dest -Destination $saved}
    $temporary=$dest+'.new';$bytes=[IO.File]::ReadAllBytes($source)
    $file=[IO.File]::Open($temporary,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$file.Write($bytes,0,$bytes.Length);$file.Flush($true)}finally{$file.Dispose()}
    if((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $temporary).Hash){throw 'Staged hash mismatch'}
    Move-Item -LiteralPath $temporary -Destination $dest -Force
}
Stage-File $probe 'EFI\BOOT\PROBE-CONSOLE.EFI'
if($ProbeOnly){Stage-File $probe 'EFI\BOOT\BOOTX64.EFI'}else{
    Stage-File $unlock 'EFI\BOOT\UNLOCK-MANUAL.EFI'
    Stage-File $unlock 'EFI\BOOT\BOOTX64.EFI'
    Stage-File (Join-Path $repo 'profiles\NATIVE-CAP01.INI') 'RTX4090-PREP\CONFIG.INI'
    foreach($pin in $assets){Stage-File (Join-Path $AssetsRoot $pin.Name) ('RTX4090-PREP\ASSETS\'+$pin.Name)}
}
New-Item -ItemType Directory -Path (Join-Path $target 'GPU-PROBE') -Force|Out-Null
[IO.File]::WriteAllText((Join-Path $target 'GPU-PROBE\ALLOW-READONLY.TAG'),"RTX4090-READONLY-CAPTURE-V1`n",[Text.UTF8Encoding]::new($false))
Stage-File (Join-Path $repo 'docs\NATIVE-CAP01.md') 'NATIVE-CAP01-INSTRUCTIONS.md'
Write-Output ('Staged '+$(if($ProbeOnly){'read-only console probe'}else{'manual native CAP01 unlock'})+' to '+$target)
Write-Output 'No firmware settings, Windows boot files, partitions or existing capture logs were changed.'
