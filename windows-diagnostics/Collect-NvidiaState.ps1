param(
    [ValidateSet('Auto','Manual','Test')][string]$Mode='Manual',
    [string]$OutputRoot='',
    [int]$Samples=1
)
$ErrorActionPreference='Stop'
$revision='native-cap01-v4-collector-1'
$taskName='Codex-UEFI-NVIDIA-OneBoot'
function Save-Json($Path,$Value) {
    $parent=Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 8))
    $temporary=$Path+'.tmp'
    $file=[IO.File]::Open($temporary,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $file.Write($bytes,0,$bytes.Length); $file.Flush($true) } finally { $file.Dispose() }
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}
function Invoke-BoundedProcess([string]$Executable,[string]$Arguments,[int]$TimeoutMs=8000) {
    $process=[Diagnostics.Process]::new()
    $process.StartInfo=[Diagnostics.ProcessStartInfo]::new()
    $process.StartInfo.FileName=$Executable
    $process.StartInfo.Arguments=$Arguments
    $process.StartInfo.UseShellExecute=$false
    $process.StartInfo.CreateNoWindow=$true
    $process.StartInfo.RedirectStandardOutput=$true
    $process.StartInfo.RedirectStandardError=$true
    $result=[ordered]@{executable=$Executable;started=$false;timed_out=$false;exit_code=$null;stdout='';stderr='';error=$null}
    try {
        $result.started=$process.Start()
        $stdout=$process.StandardOutput.ReadToEndAsync()
        $stderr=$process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutMs)) {
            $result.timed_out=$true
            try { $process.Kill() } catch {}
            $null=$process.WaitForExit(3000)
        }
        if ($process.HasExited) { $result.exit_code=$process.ExitCode }
        if ($stdout.Wait(1000)) { $result.stdout=$stdout.Result }
        if ($stderr.Wait(1000)) { $result.stderr=$stderr.Result }
    } catch { $result.error=$_.Exception.Message }
    finally { $process.Dispose() }
    return [pscustomobject]$result
}
function Get-Observation([string]$Directory,[string]$Label) {
    $record=[ordered]@{revision=$revision;time=(Get-Date).ToString('o');label=$Label;identity=[Security.Principal.WindowsIdentity]::GetCurrent().Name;gpu_reset_attempted=$false;driver_changes=$false}
    Save-Json (Join-Path $Directory ($Label+'-started.json')) $record
    $smi=Join-Path $env:SystemRoot 'System32\nvidia-smi.exe'
    $record.nvidia_smi=Invoke-BoundedProcess $smi '--query-gpu=name,driver_version,pci.bus_id,pstate,display_active,temperature.gpu,utilization.gpu,memory.used --format=csv,noheader' 8000
    Save-Json (Join-Path $Directory ($Label+'-nvml.json')) $record
    try {
        $record.adapters=@(Get-CimInstance Win32_PnPEntity -Filter "PNPDeviceID LIKE 'PCI%VEN_10DE%DEV_2489%'" -OperationTimeoutSec 8 | Select-Object Name,Status,ConfigManagerErrorCode)
    } catch { $record.adapters_error=$_.Exception.Message }
    try {
        $record.video=@(Get-CimInstance Win32_VideoController -OperationTimeoutSec 8 | Select-Object Name,Status,DriverVersion,ConfigManagerErrorCode,CurrentHorizontalResolution,CurrentVerticalResolution)
    } catch { $record.video_error=$_.Exception.Message }
    try {
        $record.connections=@(Get-CimInstance -Namespace root\wmi -ClassName WmiMonitorConnectionParams -OperationTimeoutSec 8 | Select-Object Active,VideoOutputTechnology)
        $record.monitors=@(Get-CimInstance -Namespace root\wmi -ClassName WmiMonitorID -OperationTimeoutSec 8 | ForEach-Object {
            [pscustomobject]@{Active=$_.Active;Name=(-join ($_.UserFriendlyName | Where-Object {$_ -ne 0} | ForEach-Object {[char]$_}))}
        })
    } catch { $record.monitors_error=$_.Exception.Message }
    Save-Json (Join-Path $Directory ($Label+'.json')) $record
    return [pscustomobject]$record
}
function Save-BootEvents([string]$Directory,[datetime]$Since) {
    $events=[ordered]@{time=(Get-Date).ToString('o');since=$Since.ToString('o')}
    try {
        $events.system=@(Get-WinEvent -FilterHashtable @{LogName='System';StartTime=$Since} -MaxEvents 800 -ErrorAction Stop |
            Where-Object { $_.ProviderName -match 'nvlddmkm|^Display$|Kernel-General|Kernel-Boot|Kernel-Power|Wininit|^EventLog$|WHEA' } |
            Select-Object TimeCreated,Id,ProviderName,LevelDisplayName,Message)
    } catch { $events.system_error=$_.Exception.Message }
    try {
        $events.resume=@(Get-WinEvent -FilterHashtable @{LogName='Microsoft-Windows-Kernel-Boot/Operational';StartTime=$Since;Id=158} -MaxEvents 8 -ErrorAction Stop |
            Select-Object TimeCreated,Id,ProviderName,Message)
    } catch { $events.resume_note=$_.Exception.Message }
    Save-Json (Join-Path $Directory 'events.json') $events
}

if ($Mode -eq 'Test') {
    if (-not $OutputRoot) { throw 'Test OutputRoot is required' }
    $powershell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $timer=[Diagnostics.Stopwatch]::StartNew()
    $slow=Invoke-BoundedProcess $powershell '-NoProfile -NonInteractive -Command "Start-Sleep -Seconds 30"' 500
    $timer.Stop()
    $missing=Invoke-BoundedProcess (Join-Path $OutputRoot 'missing-program.exe') '' 500
    if (-not $slow.timed_out -or $timer.ElapsedMilliseconds -gt 6500 -or $missing.started -or -not $missing.error) { throw 'Bounded process failure tests failed' }
    Save-Json (Join-Path $OutputRoot 'tests.json') @{revision=$revision;timeout_test=$slow;timeout_elapsed_ms=$timer.ElapsedMilliseconds;missing_executable=$missing;passed=$true}
    Write-Output 'PASS: hung process terminated within bound; missing executable recorded; no GPU mutation'
    exit 0
}

if ($Mode -eq 'Auto') {
    $armFile=Join-Path $PSScriptRoot 'armed.json'
    if (-not (Test-Path -LiteralPath $armFile)) { exit 0 }
    $arm=Get-Content -LiteralPath $armFile -Raw | ConvertFrom-Json
    if ([datetime]::UtcNow -gt [datetime]::Parse($arm.expires_utc).ToUniversalTime()) { exit 0 }
    $startupStamp=(Get-Date).ToString('yyyyMMdd-HHmmss')
    Save-Json (Join-Path $PSScriptRoot ('startup-'+$startupStamp+'.json')) @{revision=$revision;time=(Get-Date).ToString('o');identity=[Security.Principal.WindowsIdentity]::GetCurrent().Name;phase='before_boot_time_query'}
    try { $boot=(Get-CimInstance Win32_OperatingSystem -OperationTimeoutSec 8).LastBootUpTime }
    catch {
        Save-Json (Join-Path $PSScriptRoot ('startup-'+$startupStamp+'-error.json')) @{revision=$revision;time=(Get-Date).ToString('o');error=$_.Exception.Message}
        exit 1
    }
    $bootUtc=$boot.ToUniversalTime().ToString('o')
    if ($bootUtc -eq $arm.origin_boot_utc) {
        $healthDir=Join-Path $PSScriptRoot 'health-current-boot'
        $observation=Get-Observation $healthDir 'health'
        Save-Json (Join-Path $PSScriptRoot 'health-current-boot.json') @{revision=$revision;identity=[Security.Principal.WindowsIdentity]::GetCurrent().Name;time=(Get-Date).ToString('o');boot_utc=$bootUtc;smi_ok=($observation.nvidia_smi.exit_code -eq 0 -and -not $observation.nvidia_smi.timed_out);next_boot_still_armed=$true}
        exit 0
    }
    $OutputRoot=Join-Path $PSScriptRoot ('logs\boot-'+$boot.ToString('yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
    Move-Item -LiteralPath $armFile -Destination (Join-Path $OutputRoot 'claimed-arm.json')
    Save-Json (Join-Path $OutputRoot 'bootstrap.json') @{revision=$revision;boot_utc=$bootUtc;start=(Get-Date).ToString('o');identity=[Security.Principal.WindowsIdentity]::GetCurrent().Name;one_boot_claimed=$true}
    $delays=@(0,20,60,120,240)
    $timer=[Diagnostics.Stopwatch]::StartNew()
    try {
        for ($i=0;$i -lt $delays.Count;$i++) {
            $remaining=[int]($delays[$i]*1000-$timer.ElapsedMilliseconds)
            if ($remaining -gt 0) { Start-Sleep -Milliseconds $remaining }
            try { $null=Get-Observation $OutputRoot ('sample-'+$i) }
            catch { Save-Json (Join-Path $OutputRoot ('sample-'+$i+'-error.json')) @{time=(Get-Date).ToString('o');error=$_.Exception.Message} }
            Save-BootEvents $OutputRoot $boot
        }
        Save-Json (Join-Path $OutputRoot 'complete.json') @{revision=$revision;time=(Get-Date).ToString('o');samples=$delays.Count;completed=$true}
    } finally {
        try { Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction Stop }
        catch { Save-Json (Join-Path $OutputRoot 'task-cleanup-note.json') @{error=$_.Exception.Message;armed_file_consumed=$true} }
    }
    exit 0
}

if (-not $OutputRoot) { throw 'Manual OutputRoot is required' }
$boot=(Get-CimInstance Win32_OperatingSystem -OperationTimeoutSec 8).LastBootUpTime
for($i=0;$i -lt [Math]::Min(3,[Math]::Max(1,$Samples));$i++) { $null=Get-Observation $OutputRoot ('sample-'+$i) }
Save-BootEvents $OutputRoot $boot
Write-Output ('MANUAL_OBSERVATION_SAVED='+$OutputRoot)
