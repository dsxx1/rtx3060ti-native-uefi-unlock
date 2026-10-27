$ErrorActionPreference='Stop'
$taskName='Codex-UEFI-NVIDIA-OneBoot'
$root=Join-Path $env:ProgramData 'CodexUefiCapture\native-cap01-v4'
$expected=Join-Path $root 'Collect-NvidiaState.ps1'
$task=Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
if($task){
    if($task.Actions.Arguments -notlike ('*'+$expected+'*')){throw 'Task action differs from expected collector; removal refused'}
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
}
$arm=Join-Path $root 'armed.json'
if(Test-Path -LiteralPath $arm){Remove-Item -LiteralPath $arm}
Write-Output 'Diagnostic task removed/disarmed. Saved logs and scripts preserved.'
