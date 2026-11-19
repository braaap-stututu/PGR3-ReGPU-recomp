@echo off
rem Starts the PGR3 launcher (launcher\PGR3Launcher) for this checkout, with the same folders as play.cmd: game data in
rem game\disc (PGR3_GAME_DIR overrides it; the launcher's own game folder pick applies only when it isn't set), saves
rem and settings in out\user. Builds the launcher first if it isn't built yet (needs the .NET 9 SDK). The launcher
rem starts build\rel\pgr3.exe in out\run, so pgr3.log lands where play.cmd puts it.
setlocal
set "ROOT=%~dp0"
set "PGR3_USER_DIR=%ROOT%out\user"
set "EXE=%ROOT%launcher\PGR3Launcher\bin\Release\net9.0\win-x64\PGR3Launcher.exe"
if not exist "%EXE%" (
    dotnet build "%ROOT%launcher\PGR3Launcher" -c Release --nologo || (pause & exit /b 1)
)
start "" "%EXE%"
