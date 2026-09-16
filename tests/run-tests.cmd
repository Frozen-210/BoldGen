@echo off
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "BOLDGEN_VS=%%i"
if not defined BOLDGEN_VS exit /b 1
"%BOLDGEN_VS%\MSBuild\Current\Bin\MSBuild.exe" "%~dp0HotkeyTests.vcxproj" /p:Configuration=Debug /p:Platform=x64 /v:minimal /nologo
if errorlevel 1 exit /b 1
"%~dp0bin\HotkeyTests.exe"
exit /b %errorlevel%
