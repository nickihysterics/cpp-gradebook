@echo off
setlocal enableextensions

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo vswhere not found. Install Visual Studio Build Tools or Community with C++ workload.
  exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
  echo MSVC build tools not found. Install the C++ workload.
  exit /b 1
)

call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 (
  echo Failed to initialize MSVC environment.
  exit /b 1
)

if not exist "build" mkdir build

rc /nologo /fo build\app.res resources\app.rc
if errorlevel 1 (
  echo Resource compilation failed.
  exit /b 1
)

cl /EHsc /std:c++17 /utf-8 /I third_party\sqlite src\main.cpp third_party\sqlite\sqlite3.c build\app.res /Fo:build\ /Fe:build\cpp-gradebook.exe
exit /b %errorlevel%


