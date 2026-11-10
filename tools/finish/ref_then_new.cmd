@echo off
rem %1: the branch to build after main's reference host
cd /d C:\Users\julie\gt4-wt-land
git checkout -q --detach origin/main || exit /b 1
if exist build\host-windows-Release rmdir /s /q build\host-windows-Release
call tools\build_windows.cmd Release > build\ref_build.log 2>&1 || exit /b 1
copy /y build\host-windows-Release\gt4_host.exe build\ref\gt4_host_main.exe > nul || exit /b 1
git rev-parse --short HEAD > build\ref\HEAD.txt
git checkout -q %1 || exit /b 1
rmdir /s /q build\host-windows-Release
call tools\build_windows.cmd Release > build\new_build.log 2>&1 || exit /b 1
findstr /c:"tests passed" build\ref_build.log build\new_build.log
