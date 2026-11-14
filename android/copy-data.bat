@echo off
rem YEdo Mod Pro: copies the game data that goes into the APK (used by build-android.bat and make-patch.bat)
rem needs YEDO, PROJ, LOG set by the caller
set "AD=%PROJ%\src\main\assets\defaults"
robocopy "%YEDO%" "%PROJ%\src\main\assets\defaults" /MIR /NFL /NDL /NJH /NJS /NP /R:1 /W:1 ^
  /XD pics _android _update replay screenshots "Claude outputs" .git puzzles rush _backup_old "%YEDO%\deck" ^
  /XF *.exe *.dll *.pdb *.config *.bat *.lnk *.old system.conf error.log .edopro_lock save.json progress.json _story_boss.ydk _boss.ydk crash.log >>"%LOG%" 2>&1
if %ERRORLEVEL% GEQ 8 exit /b 1
rem fresh start: no player decks in the APK (Duel World gives the starter deck); drop leftovers of older builds
if exist "%AD%\deck" rmdir /s /q "%AD%\deck"
mkdir "%AD%\deck"
for %%D in ("puzzles" "script\rush" "WindBot\_backup_old" "replay" "screenshots" "pics" "story\_update") do if exist "%AD%\%%~D" rmdir /s /q "%AD%\%%~D"
for %%F in ("story\world\save.json" "story\progress.json" "story\campaign\progress.json" "story\campaign\_boss.ydk" "story\decks\_story_boss.ydk" "config\system.conf" "WindBot\crash.log" "error.log") do if exist "%AD%\%%~F" del /f /q "%AD%\%%~F"
rem card images: official ones are downloaded in-game (PICS_URL below); ship only YEdo's own custom images
powershell -NoProfile -Command "$s='%YEDO%\pics'; $d='%AD%\pics'; if(Test-Path $s){ New-Item -ItemType Directory -Force $d | Out-Null; Get-ChildItem $s -File | Where-Object { $_.BaseName -notmatch '^\d{1,8}$' } | Copy-Item -Destination $d -Force }" >>"%LOG%" 2>&1
exit /b 0
