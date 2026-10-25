@echo off
rem Build gt4-native on Windows with MSVC + Ninja (VS 2022 Build Tools): the SDL3 + Vulkan host and the tests.
rem   tools\build_windows.cmd [Release|Debug] [extra CMake arguments]
setlocal
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
if not defined VSPATH (echo Visual Studio C++ tools not found & exit /b 1)
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
cd /d "%~dp0.."
set BUILD=build\host-windows-%CONFIG%
cmake -S . -B %BUILD% -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DGT4_BUILD_HOST=ON -DBUILD_TESTING=ON %2 %3 %4 %5 || exit /b 1
cmake --build %BUILD% --parallel || exit /b 1
ctest --test-dir %BUILD% --output-on-failure || exit /b 1
echo Built %BUILD%\gt4_host.exe
