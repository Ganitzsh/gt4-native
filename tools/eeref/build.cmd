@echo off
rem Builds eeref, the original game code as a test reference (docs/DIFFERENTIAL_TESTING.md):
rem   tools\eeref\build.cmd C:\path\to\gt4-decomp
rem translates the MIPS code of its orig\SCUS-97328\CORE.GT4.elf to C++ with eerecomp (third_party/ps2-web-recomp)
rem into build\eeref\gen, every function of the decomp's build\functions.csv an entry; adds the bridge's hook at
rem every function entry (hook_entries.py) into build\eeref\genh, and compiles that with the replay tool into
rem build\eeref\eeref.exe (game_link.py --eeorig build\eeref\genh links the same code into the game). Needs the
rem VS 2022 C++ tools and clang-cl (on PATH, or CLANG_CL=path).
setlocal
if "%~1"=="" (echo usage: tools\eeref\build.cmd C:\path\to\gt4-decomp & exit /b 2)
set DECOMP=%~f1
set ELF=%DECOMP%\orig\SCUS-97328\CORE.GT4.elf
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
if not defined VSPATH (echo Visual Studio C++ tools not found & exit /b 1)
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo || exit /b 1
if not defined CLANG_CL for /f "delims=" %%i in ('where clang-cl 2^>nul') do if not defined CLANG_CL set CLANG_CL=%%i
if not defined CLANG_CL if exist "%USERPROFILE%\gt4tools\llvm\bin\clang-cl.exe" set CLANG_CL=%USERPROFILE%\gt4tools\llvm\bin\clang-cl.exe
if not defined CLANG_CL (echo clang-cl not found: set CLANG_CL & exit /b 1)
cd /d "%~dp0..\.."
set BUILD=build\eeref
cmake -S tools\eeref -B %BUILD%\cmake -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_COMPILER=%CLANG_CL%" || exit /b 1
cmake --build %BUILD%\cmake --target eerecomp || exit /b 1
py tools\eeref\entries.py "%DECOMP%\build\functions.csv" %BUILD%\entries.txt || exit /b 1
%BUILD%\cmake\eerecomp.exe "%ELF%" --entries %BUILD%\entries.txt --out %BUILD%\gen || exit /b 1
py tools\eeref\hook_entries.py %BUILD%\gen %BUILD%\genh --functions "%DECOMP%\build\functions.csv" || exit /b 1
cmake -S tools\eeref -B %BUILD%\cmake "-DGT4_EEREF_GEN=%CD%\%BUILD%\genh" || exit /b 1
cmake --build %BUILD%\cmake --target eeref || exit /b 1
copy /y %BUILD%\cmake\eeref.exe %BUILD%\eeref.exe >nul
echo Built %BUILD%\eeref.exe
