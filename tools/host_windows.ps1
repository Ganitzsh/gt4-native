# Launch a native playtest on the logged-in Windows desktop from WSL/session 0.
# Task registration is temporary and removed in finally. No game data is touched.
param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Session,
    [switch]$NoAudio,
    [switch]$SmokeTest,
    [int]$TimeoutSeconds = 60
)
$ErrorActionPreference = 'Stop'
$taskName = 'GT4-native-playtest-' + [guid]::NewGuid().ToString('N')
$registered = $false
try {
    $desktopUser = (Get-CimInstance Win32_ComputerSystem).UserName
    if (-not $desktopUser) { throw 'No logged-in Windows desktop user. Log in and retry.' }
    New-Item -ItemType Directory -Force -Path $Session | Out-Null
    $output = Join-Path $Session 'native.log'
    $exitFile = Join-Path $Session 'native-exit.txt'
    $commandFile = Join-Path $Session 'launch.cmd'
    # Values are quoted command arguments, never interpolated as executable command text.
    # Reject characters that cmd expands even inside quoted paths.
    foreach ($path in @($Executable, $Session, $commandFile)) {
        if ($path -match '["%\r\n]') { throw 'Launch paths cannot contain quotes, percent signs or newlines.' }
    }
    $arguments = '--session-dir "' + $Session + '"'
    if ($NoAudio) { $arguments += ' --no-audio' }
    if ($SmokeTest) { $arguments = '--smoke-test' }
    $lines = @('@echo off', ('"' + $Executable + '" ' + $arguments + ' > "' + $output + '" 2>&1'),
               ('echo %errorlevel% > "' + $exitFile + '"'))
    Set-Content -LiteralPath $commandFile -Value $lines -Encoding ASCII
    $action = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument ('/d /c ""' + $commandFile + '""')
    $principal = New-ScheduledTaskPrincipal -UserId $desktopUser -LogonType Interactive -RunLevel Limited
    $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Seconds ($TimeoutSeconds + 5)) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
    Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings | Out-Null
    $registered = $true
    Start-ScheduledTask -TaskName $taskName
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not (Test-Path -LiteralPath $exitFile)) {
        if ((Get-Date) -gt $deadline) { throw 'Interactive native playtest timed out.' }
        Start-Sleep -Milliseconds 50
    }
    if (Test-Path -LiteralPath $output) { Get-Content -LiteralPath $output }
    $result = [int](Get-Content -LiteralPath $exitFile)
} finally {
    if ($registered) {
        Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    }
}
exit $result
