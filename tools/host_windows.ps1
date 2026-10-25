# Launch gt4_host on the logged-in Windows desktop from WSL or session 0 (where no window can present).
# Task registration is temporary and removed in finally. No game data is touched.
#   -Session DIR       working directory for the run: native.log, the exit code and the launch script
#   (default)          records a scripted session in DIR (--session-dir DIR)
#   -SmokeTest         runs --smoke-test instead
#   -Arguments "..."   runs gt4_host with exactly these arguments instead (e.g. "--frames 60 --gpu 1")
#   -Extra "..."       appends these arguments to the session command line (e.g. "--set render.aa=smaa")
param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Session,
    [switch]$NoAudio,
    [switch]$SmokeTest,
    [string]$Arguments = '',
    [string]$Extra = '',
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
    Remove-Item -LiteralPath $exitFile -ErrorAction SilentlyContinue
    # Values are quoted command arguments, never interpolated as executable command text.
    # Reject characters that cmd expands even inside quoted paths.
    foreach ($path in @($Executable, $Session, $commandFile)) {
        if ($path -match '["%\r\n]') { throw 'Launch paths cannot contain quotes, percent signs or newlines.' }
    }
    if (($Arguments + $Extra) -match '["%&|<>^\r\n]') { throw 'Arguments cannot contain quotes, percent signs, redirections or newlines.' }
    $commandLine = '--session-dir "' + $Session + '"'
    if ($NoAudio) { $commandLine += ' --no-audio' }
    if ($Extra) { $commandLine += ' ' + $Extra }
    if ($SmokeTest) { $commandLine = '--smoke-test' }
    if ($Arguments) { $commandLine = $Arguments }
    $lines = @('@echo off', ('"' + $Executable + '" ' + $commandLine + ' > "' + $output + '" 2>&1'),
               ('echo %errorlevel% > "' + $exitFile + '"'))
    Set-Content -LiteralPath $commandFile -Value $lines -Encoding ASCII
    $action = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument ('/d /c ""' + $commandFile + '""') -WorkingDirectory (Get-Location).Path
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
    Start-Sleep -Milliseconds 100
    if (Test-Path -LiteralPath $output) { Get-Content -LiteralPath $output }
    $result = [int](Get-Content -LiteralPath $exitFile)
} finally {
    if ($registered) {
        Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    }
}
exit $result
