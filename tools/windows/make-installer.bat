@echo off
rem Builds the one-click installer (build\installer\Stabewwy-<version>-Setup-x64.exe) from what build.bat built.
rem Needs Inno Setup 6 (winget install JRSoftware.InnoSetup).
rem
rem Usage: tools\windows\make-installer.bat [version]   (default: 1.0.0-<git commit>)
setlocal
for %%i in ("%~dp0..\..") do set "SRC=%%~fi"
set "BUILD=%SRC%\build"

if not exist "%BUILD%\strawberry.exe" ( echo Build Stabewwy first with tools\windows\build.bat & exit /b 1 )

set "ISCC="
for %%p in ("%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" "%ProgramFiles%\Inno Setup 6\ISCC.exe") do if exist %%p set "ISCC=%%~p"
if not defined ISCC ( echo Inno Setup 6 not found, install it with: winget install JRSoftware.InnoSetup & exit /b 1 )

set "VERSION=%~1"
if not defined VERSION (
  for /f %%c in ('git -C "%SRC%" rev-parse --short HEAD') do set "VERSION=1.0.0-%%c"
)
if not defined VERSION set "VERSION=1.0.0"

rem Visual C++ runtime to bundle next to the exe
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set "VSPATH=%%i"
set "CRTDIR="
for /d %%d in ("%VSPATH%\VC\Redist\MSVC\*") do if exist "%%d\x64\Microsoft.VC143.CRT\vcruntime140.dll" set "CRTDIR=%%d\x64\Microsoft.VC143.CRT"
if not defined CRTDIR echo Warning: Visual C++ runtime not found, the installer won't include it.

set CRTARG=
if defined CRTDIR set CRTARG="/DCrtDir=%CRTDIR%"
"%ISCC%" /Q "/DAppVersion=%VERSION%" "/DBuildDir=%BUILD%" "/DOutputDir=%BUILD%\installer" %CRTARG% "%~dp0installer.iss" || exit /b 1
echo INSTALLER OK: %BUILD%\installer
dir /b "%BUILD%\installer\*.exe"
