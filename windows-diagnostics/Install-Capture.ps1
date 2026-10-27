$ErrorActionPreference='Stop'
$taskName='Codex-UEFI-NVIDIA-OneBoot'
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run this installer as Administrator. It installs a one-boot read-only diagnostic task.' }
$destination=Join-Path $env:ProgramData 'CodexUefiCapture\native-cap01-v4'
$statusFile=Join-Path $PSScriptRoot 'installer-status.json'
$created=$false
try {
    $existing=Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
    if ($existing) { throw 'A task with this name already exists. Refusing to replace it.' }
    $manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'collector-manifest.json') -Raw | ConvertFrom-Json
    $source=Join-Path $PSScriptRoot 'Collect-NvidiaState.ps1'
    if ((Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() -ne $manifest.collector_sha256) { throw 'Collector SHA256 mismatch' }
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
    $installed=Join-Path $destination 'Collect-NvidiaState.ps1'
    if ((Get-FileHash -LiteralPath $installed).Hash.ToLowerInvariant() -ne $manifest.collector_sha256) { throw 'Installed collector SHA256 mismatch' }
    $acl=[Security.AccessControl.DirectorySecurity]::new()
    $acl.SetAccessRuleProtection($true,$false)
    $acl.SetOwner([Security.Principal.SecurityIdentifier]::new('S-1-5-32-544'))
    foreach ($sid in 'S-1-5-18','S-1-5-32-544') {
        $rule=[Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($sid),'FullControl','ContainerInherit,ObjectInherit','None','Allow')
        $acl.AddAccessRule($rule)
    }
    $acl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new('S-1-5-32-545'),'ReadAndExecute','ContainerInherit,ObjectInherit','None','Allow'))
    Set-Acl -LiteralPath $destination -AclObject $acl
    # Apply protected permissions to the script as well; no user-writable SYSTEM action.
    $fileAcl=[Security.AccessControl.FileSecurity]::new();$fileAcl.SetAccessRuleProtection($true,$false)
    $fileAcl.SetOwner([Security.Principal.SecurityIdentifier]::new('S-1-5-32-544'))
    foreach ($sid in 'S-1-5-18','S-1-5-32-544') { $fileAcl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new($sid),'FullControl','Allow')) }
    $fileAcl.AddAccessRule([Security.AccessControl.FileSystemAccessRule]::new([Security.Principal.SecurityIdentifier]::new('S-1-5-32-545'),'ReadAndExecute','Allow'))
    Set-Acl -LiteralPath $installed -AclObject $fileAcl
    $boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime
    @{origin_boot_utc=$boot.ToUniversalTime().ToString('o');expires_utc=[datetime]::UtcNow.AddHours(48).ToString('o');one_boot=$true;revision='native-cap01-v4'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'armed.json') -Encoding utf8
    $healthFile=Join-Path $destination 'health-current-boot.json'
    if(Test-Path -LiteralPath $healthFile){Remove-Item -LiteralPath $healthFile}
    $powershell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $action=New-ScheduledTaskAction -Execute $powershell -Argument ('-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -File "'+$installed+'" -Mode Auto')
    $trigger=New-ScheduledTaskTrigger -AtStartup
    $settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 8) -MultipleInstances IgnoreNew -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -Hidden
    $taskPrincipal=New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
    $null=Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Settings $settings -Principal $taskPrincipal -Description 'One Windows boot: read-only NVIDIA/monitor state after a manual UEFI test; local files only; no GPU reset.'
    $created=$true
    Start-ScheduledTask -TaskName $taskName
    $deadline=[datetime]::UtcNow.AddSeconds(60)
    while(-not (Test-Path -LiteralPath $healthFile) -and [datetime]::UtcNow -lt $deadline){Start-Sleep -Milliseconds 500}
    if(-not (Test-Path -LiteralPath $healthFile)){throw 'SYSTEM diagnostic health check did not complete within 60 seconds'}
    $health=Get-Content -LiteralPath $healthFile -Encoding UTF8 -Raw | ConvertFrom-Json
    $identitySid=[Security.Principal.NTAccount]::new($health.identity).Translate([Security.Principal.SecurityIdentifier]).Value
    if(-not $health.smi_ok -or $identitySid -ne 'S-1-5-18'){throw 'SYSTEM task health check failed or NVIDIA query failed'}
    $task=Get-ScheduledTask -TaskName $taskName
    $result=[ordered]@{success=$true;time=(Get-Date).ToString('o');task=$taskName;installed_script=$installed;collector_sha256=$manifest.collector_sha256;script_owner=(Get-Acl -LiteralPath $installed).Owner;identity=$health.identity;identity_sid=$identitySid;system_smi_ok=$health.smi_ok;next_windows_boot_armed=(Test-Path -LiteralPath (Join-Path $destination 'armed.json'));origin_boot_utc=$boot.ToUniversalTime().ToString('o');trigger=$task.Triggers.CimClass.CimClassName;action=$task.Actions.Arguments;logs=Join-Path $destination 'logs';no_gpu_reset=$true;no_driver_changes=$true}
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $statusFile -Encoding utf8
} catch {
    if($created){Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue}
    @{success=$false;time=(Get-Date).ToString('o');error=$_.Exception.Message;task_removed=$created} | ConvertTo-Json | Set-Content -LiteralPath $statusFile -Encoding utf8
    exit 1
}
