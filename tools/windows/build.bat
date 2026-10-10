@echo off
rem Builds Strawberry (MSVC x64 Release) into <repository>\build, ready to run: double-click build\strawberry.exe.
rem
rem Needs Visual Studio 2022 Build Tools (C++ workload, Windows SDK 10.0.22621), CMake and Ninja, and the prebuilt
rem dependencies from https://github.com/strawberrymusicplayer/strawberry-msvc-dependencies/releases
rem (strawberry-msvc-x86_64-release.tar.xz extracted to C:\, or set STRAWBERRY_DEPS to where they are).
rem
rem Usage: tools\windows\build.bat [extra cmake --build arguments, e.g. --target strawberry_lib]
setlocal
if defined STRAWBERRY_DEPS (set "DEPS=%STRAWBERRY_DEPS%") else (set "DEPS=C:\strawberry_msvc_x86_64_release")
set "DEPS_FS=%DEPS:\=/%"
rem The repository root is two levels above this script.
for %%i in ("%~dp0..\..") do set "SRC=%%~fi"
set "BUILD=%SRC%\build"

if not exist "%DEPS%\bin" ( echo Dependencies not found in %DEPS%, see the comments at the top of this script & exit /b 1 )

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
if "%VSPATH%"=="" ( echo Visual Studio C++ tools not found & exit /b 1 )
call "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 10.0.22621.0 || exit /b 1

rem Ninja installed with winget doesn't always get a link on the PATH.
set PATH=%DEPS%\bin;%ProgramFiles%\CMake\bin;%LOCALAPPDATA%\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;%PATH%

if not exist "%BUILD%\build.ninja" (
  cmake -S "%SRC%" -B "%BUILD%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="%DEPS_FS%/lib/cmake" ^
    -DPKG_CONFIG_EXECUTABLE="%DEPS_FS%/bin/pkg-config.exe" ^
    -DICU_ROOT="%DEPS_FS%" ^
    -DENABLE_WIN32_CONSOLE=OFF ^
    -DENABLE_GIO=OFF -DENABLE_AUDIOCD=OFF -DENABLE_MTP=OFF -DENABLE_GPOD=OFF -DENABLE_SPOTIFY=OFF ^
    -DCREATE_SOURCE_API_CREDENTIALS=OFF || exit /b 1
)

rem A running or locked strawberry.exe can't be overwritten, but it can be renamed: move it aside so linking always works.
if exist "%BUILD%\strawberry.exe.old" del /f /q "%BUILD%\strawberry.exe.old" >nul 2>&1
if exist "%BUILD%\strawberry.exe" move /y "%BUILD%\strawberry.exe" "%BUILD%\strawberry.exe.old" >nul 2>&1

cmake --build "%BUILD%" --parallel %* || exit /b 1

rem Runtime plugins next to the exe (only the first time)
if not exist "%BUILD%\gstreamer-plugins" (
  for %%d in (gio-modules platforms styles tls sqldrivers imageformats gstreamer-plugins) do mkdir "%BUILD%\%%d"
  copy /y "%DEPS%\lib\gio\modules\*.dll" "%BUILD%\gio-modules\" >nul
  copy /y "%DEPS%\plugins\platforms\qwindows*.dll" "%BUILD%\platforms\" >nul
  copy /y "%DEPS%\plugins\styles\*.dll" "%BUILD%\styles\" >nul
  copy /y "%DEPS%\plugins\tls\*.dll" "%BUILD%\tls\" >nul
  copy /y "%DEPS%\plugins\sqldrivers\qsqlite*.dll" "%BUILD%\sqldrivers\" >nul
  copy /y "%DEPS%\plugins\imageformats\*.dll" "%BUILD%\imageformats\" >nul
  copy /y "%DEPS%\lib\gstreamer-1.0\*.dll" "%BUILD%\gstreamer-plugins\" >nul
)
rem Dependency DLLs next to strawberry.exe, so it can be started directly.
xcopy /D /Y /Q "%DEPS%\bin\*.dll" "%BUILD%\" >nul

echo BUILD OK
