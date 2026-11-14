@echo off
setlocal EnableExtensions
title YEdo Mod Pro - Android build
set "HERE=%~dp0"
for %%I in ("%~dp0..") do set "YEDO=%%~fI"
set "PROJ=C:\YEdoAndroid"
set "NDKDIR=%PROJ%\ndk\android-ndk-r21e"
if exist "%LOCALAPPDATA%\Android\Sdk\ndk\21.4.7075529\ndk-build.cmd" set "NDKDIR=%LOCALAPPDATA%\Android\Sdk\ndk\21.4.7075529"
set "LOG=%HERE%build.log"
set "SDK=%LOCALAPPDATA%\Android\Sdk"
set "JBR=%ProgramFiles%\Android\Android Studio\jbr"
echo YEdo Mod Pro Android build > "%LOG%"
echo.
echo  ===== YEdo Mod Pro : Android build =====
echo  (log: %LOG%)
echo.

rem ---- checks
where git >nul 2>&1 || if exist "%ProgramFiles%\Git\cmd\git.exe" set "PATH=%ProgramFiles%\Git\cmd;%PATH%"
where git >nul 2>&1 || (echo  [!] Git not found & echo GIT MISSING>>"%LOG%" & goto :fail)
if not exist "%SDK%" (echo  [!] Android SDK not found at %SDK% - open Android Studio once and finish the setup wizard & echo SDK MISSING>>"%LOG%" & goto :fail)
if not exist "%JBR%\bin\java.exe" (echo  [!] Android Studio Java not found at %JBR% & echo JBR MISSING>>"%LOG%" & goto :fail)
set "JAVA_HOME=%JBR%"

rem ---- 1. project
echo  [1/6] Downloading the EDOPro Android project (about 800 MB, first time only)...
if not exist "%PROJ%\build.gradle" (
  git clone --depth 1 https://github.com/edo9300/edopro-android "%PROJ%" >>"%LOG%" 2>&1 || goto :fail
)

rem ---- 2. YEdo source + settings
echo  [2/6] Adding YEdo Mod Pro source code...
if exist "%PROJ%\deps\edopro\gframe" rmdir /s /q "%PROJ%\deps\edopro\gframe"
if exist "%PROJ%\deps\edopro\ocgcore" rmdir /s /q "%PROJ%\deps\edopro\ocgcore"
tar -xf "%HERE%yedo-android-overlay.zip" -C "%PROJ%" >>"%LOG%" 2>&1 || goto :fail
if not exist "%PROJ%\libs" mkdir "%PROJ%\libs"
if not exist "%PROJ%\libs\libWindbot.aar" (
  echo        downloading WindBot for Android...
  curl -L --retry 5 -o "%PROJ%\libs\libWindbot.aar" https://github.com/ProjectIgnis/windbot/releases/download/20260711/libWindbot.aar >>"%LOG%" 2>&1 || goto :fail
)
set "SDKESC=%SDK:\=\\%"
set "SDKESC=%SDKESC::=\:%"
> "%PROJ%\local.properties" echo sdk.dir=%SDKESC%

rem ---- 3. NDK
echo  [3/6] Android NDK r21e (about 1 GB, first time only)...
if not exist "%NDKDIR%\ndk-build.cmd" (
  if not exist "%PROJ%\ndk" mkdir "%PROJ%\ndk"
  curl -L --retry 5 -o "%PROJ%\ndk\ndk.zip" https://dl.google.com/android/repository/android-ndk-r21e-windows-x86_64.zip >>"%LOG%" 2>&1 || goto :fail
  echo        extracting... ^(takes a few minutes^)
  tar -xf "%PROJ%\ndk\ndk.zip" -C "%PROJ%\ndk" >>"%LOG%" 2>&1 || goto :fail
  del "%PROJ%\ndk\ndk.zip"
)

rem ---- 4. game data -> APK assets
echo  [4/6] Copying game data (cards, scripts, story, bots, music)...
call "%HERE%copy-data.bat" || goto :fail
set "AD=%PROJ%\src\main\assets\defaults"
rem build id: the in-game updater only accepts patches made for this APK
for /f %%T in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "BUILDID=%%T"
> "%AD%\config\yedo_build.txt" echo %BUILDID%
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%make-filelist.ps1" -Assets "%PROJ%\src\main\assets" >>"%LOG%" 2>&1 || goto :fail
if not exist "%PROJ%\src\main\assets\filelist.txt" goto :fail

rem ---- 5. native code
echo  [5/6] Compiling the game for Android (10-30 minutes the first time)...
pushd "%PROJ%"
rem card image download servers (same as the Windows build); touch the file that uses them so they get compiled in
powershell -NoProfile -Command "(Get-Item '%PROJ%\deps\edopro\gframe\data_handler.cpp').LastWriteTime=Get-Date" >>"%LOG%" 2>&1
call "%NDKDIR%\ndk-build.cmd" -j%NUMBER_OF_PROCESSORS% "PICS_URL=https://pics.projectignis.org:2096/pics/{}.jpg" "FIELDS_URL=https://pics.projectignis.org:2096/field/{}.png" "COVERS_URL=https://pics.projectignis.org:2096/pics/cover/{}.jpg" >>"%LOG%" 2>&1
if errorlevel 1 (popd & goto :fail)

rem ---- 6. APK
echo  [6/6] Building the APK...
call gradlew.bat assembleDebug --no-daemon --stacktrace >>"%LOG%" 2>&1
if errorlevel 1 (popd & goto :fail)
popd
set "APK="
for %%f in ("%PROJ%\build\outputs\apk\debug\*.apk") do set "APK=%%f"
if not defined APK goto :fail
copy /Y "%APK%" "%HERE%YEdo-Mod-Pro.apk" >nul
rem remember what this APK contains, for make-patch.bat
> "%HERE%apk-build-id.txt" echo %BUILDID%
powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%make-patch.ps1" -Assets "%AD%" -Baseline "%HERE%apk-baseline.txt" -WriteBaseline >>"%LOG%" 2>&1
echo BUILD OK>>"%LOG%"
echo.
echo  ===== DONE =====
echo  APK: %HERE%YEdo-Mod-Pro.apk
explorer /select,"%HERE%YEdo-Mod-Pro.apk"
pause
exit /b 0

:fail
echo BUILD FAILED>>"%LOG%"
echo.
echo  [!] Build failed. Tell Claude - it can read build.log in this folder.
pause
exit /b 1
