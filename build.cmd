@echo off
rem Builds tera-focusfix for both TERA clients with Visual Studio 2019/2022:
rem   build\x86\dinput8.dll   32-bit P31 client
rem   build\x64\dinput8.dll   64-bit v115 client
rem plus the helper tools in build\<arch>\tools\
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo Visual Studio with C++ tools not found & exit /b 1)

call :build x86 x86 || exit /b 1
call :build amd64 x64 || exit /b 1
echo BUILD OK
exit /b 0

:build
setlocal
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" %1 >nul 2>nul || exit /b 1
set "OUT=build\%2"
if not exist %OUT%\obj mkdir %OUT%\obj
if not exist %OUT%\tools mkdir %OUT%\tools
set CFLAGS=/nologo /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS
echo [%2] dinput8.dll
cl %CFLAGS% /EHsc /LD /Fo%OUT%\obj\ src\dllmain.cpp src\dinput_cache.cpp src\renderkick.cpp src\recorder.cpp src\gc_interval.cpp src\perf.cpp src\log.cpp src\mem.cpp ^
   /Fe:%OUT%\dinput8.dll /link /DEF:src\dinput8.def user32.lib >%OUT%\obj\dll.log || (type %OUT%\obj\dll.log & exit /b 1)
findstr /i /c:"warning" %OUT%\obj\dll.log
echo [%2] tools
cl %CFLAGS% /Fo%OUT%\obj\ tools\inject\inject.c       /Fe:%OUT%\tools\inject.exe    >nul || exit /b 1
cl %CFLAGS% /Fo%OUT%\obj\ tools\proxytest\proxytest.c /Fe:%OUT%\tools\proxytest.exe >nul || exit /b 1
cl %CFLAGS% /Fo%OUT%\obj\ tools\devtime\devtime.c     /Fe:%OUT%\tools\devtime.exe   >nul || exit /b 1
if "%2"=="x86" (
  cl %CFLAGS% /Fo%OUT%\obj\ tools\sampler\tsampler.c    /Fe:%OUT%\tools\tsampler.exe >nul || exit /b 1
  cl %CFLAGS% /LD /Fo%OUT%\obj\ tools\diag\diag.c       /Fe:%OUT%\tools\diag.dll     >nul || exit /b 1
)
del /q %OUT%\*.exp %OUT%\*.lib %OUT%\tools\*.exp %OUT%\tools\*.lib 2>nul
exit /b 0
