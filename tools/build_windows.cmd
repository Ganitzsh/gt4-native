@echo off
rem Build gt4-native on Windows with MSVC + Ninja (VS 2022 Build Tools): the SDL3 + Vulkan host and the tests.
rem   tools\build_windows.cmd [Release|Debug] [extra CMake arguments]
rem GT4_ARCH=x64 (default) or arm64. arm64 is a cross build from an x64 machine (PLAN.md C4): it needs the
rem "MSVC v143 ARM64/ARM64EC build tools" component, compiles the shaders with the Vulkan SDK's glslang (it runs
rem here; the arm64 one would not, cmake/HostDependencies.cmake), builds into build\host-windows-arm64-CONFIG and
rem runs no tests (the binaries do not run on x64). Windows ARM64 packages are built by CI.
setlocal
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Release
if "%GT4_ARCH%"=="" set GT4_ARCH=x64
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if /i "%GT4_ARCH%"=="x64" (
    set TOOLS=Microsoft.VisualStudio.Component.VC.Tools.x86.x64
    set BUILD=build\host-windows-%CONFIG%
    set CROSS=
) else if /i "%GT4_ARCH%"=="arm64" (
    set TOOLS=Microsoft.VisualStudio.Component.VC.Tools.ARM64
    set BUILD=build\host-windows-arm64-%CONFIG%
    set CROSS=1
) else (
    echo GT4_ARCH must be x64 or arm64, not %GT4_ARCH%
    exit /b 1
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires %TOOLS% -property installationPath`) do set VSPATH=%%i
if not defined VSPATH (echo Visual Studio C++ tools for %GT4_ARCH% not found ^(component %TOOLS%^) & exit /b 1)
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=%GT4_ARCH% -host_arch=x64 -no_logo || exit /b 1
cd /d "%~dp0.."
set EXTRA=
if defined CROSS (
    if not exist "%VULKAN_SDK%\Bin\glslangValidator.exe" (echo An arm64 build needs the Vulkan SDK's glslangValidator ^(VULKAN_SDK^) & exit /b 1)
    set EXTRA=-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=ARM64 "-DGT4_GLSLANG_EXECUTABLE=%VULKAN_SDK%\Bin\glslangValidator.exe"
)
cmake -S . -B %BUILD% -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% -DGT4_BUILD_HOST=ON -DBUILD_TESTING=ON %EXTRA% %2 %3 %4 %5 || exit /b 1
cmake --build %BUILD% --parallel || exit /b 1
if not defined CROSS (ctest --test-dir %BUILD% --output-on-failure || exit /b 1)
echo Built %BUILD%\gt4_host.exe
