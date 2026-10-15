@echo off
setlocal
set "TEST_ARCH=x64"
if "%1"=="Win32" set "TEST_ARCH=x64_x86"
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT exit /b 1
call "%VSROOT%\VC\Auxiliary\Build\vcvarsall.bat" %TEST_ARCH%
if errorlevel 1 exit /b %errorlevel%
if not exist "test-output\%1" mkdir "test-output\%1"
cl /nologo /std:c++17 /EHsc /utf-8 /W3 /WX /DUNICODE /D_UNICODE /Fe"test-output\%1\ChannelDisplayTests.exe" /Fo"test-output\%1\ChannelDisplayTests.obj" tests\ChannelDisplayTests.cpp
if errorlevel 1 exit /b %errorlevel%
"test-output\%1\ChannelDisplayTests.exe"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /EHsc /utf-8 /W3 /WX /DUNICODE /D_UNICODE /Fe"test-output\%1\PluginExportsTests.exe" /Fo"test-output\%1\PluginExportsTests.obj" tests\PluginExportsTests.cpp
if errorlevel 1 exit /b %errorlevel%
"test-output\%1\PluginExportsTests.exe" "%CD%\bin\%1\Release\ViewReservation.tvtp"
exit /b %errorlevel%
