@echo off
setlocal EnableExtensions
title YEdo Mod Pro - make patch
set "HERE=%~dp0"
for %%I in ("%~dp0..") do set "YEDO=%%~fI"
set "PROJ=C:\YEdoAndroid"
set "LOG=%HERE%patch.log"
echo YEdo Mod Pro patch > "%LOG%"
echo.
echo  ===== YEdo Mod Pro : make update patch =====
echo.
if not exist "%HERE%apk-baseline.txt" (echo  [!] Build the APK once with build-android.bat first & goto :fail)
set /p BUILDID=<"%HERE%apk-build-id.txt"
echo  [1/2] Collecting game data...
call "%HERE%copy-data.bat" || goto :fail
> "%AD%\config\yedo_build.txt" echo %BUILDID%
echo  [2/2] Packing the files that changed since the APK...
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%make-patch.ps1" -Assets "%AD%" -Baseline "%HERE%apk-baseline.txt" -BuildId "%BUILDID%" -Out "%HERE%yedo-patch.zip" > "%HERE%patch-result.txt" 2>>"%LOG%"
if errorlevel 1 (type "%HERE%patch-result.txt" & goto :fail)
type "%HERE%patch-result.txt"
echo.
echo  ===== DONE =====
echo  Upload %HERE%yedo-patch.zip to Google Drive
echo  (replace the old file: right click - File information - Manage versions - Upload new version)
echo  then on the phone: main menu - Update (patch) button
explorer /select,"%HERE%yedo-patch.zip"
pause
exit /b 0

:fail
echo.
echo  [!] Patch failed. Tell Claude - it can read patch.log in this folder.
pause
exit /b 1
