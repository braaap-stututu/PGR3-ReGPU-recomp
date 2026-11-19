@echo off
rem Starts our build of PGR3 (build\rel\pgr3.exe) for testing: saves and settings in out\user, the log in
rem out\run\pgr3.log, and ReGPU's debug server on (port 7342, tools\pgr3ctl.ps1; 7341 is left to FM3 builds).
rem Game data: the extracted Europe disc in game\disc (PGR3_GAME_DIR overrides it).
setlocal
set "ROOT=%~dp0"
if not defined PGR3_GAME_DIR set "PGR3_GAME_DIR=%ROOT%game\disc"
set "PGR3_USER_DIR=%ROOT%out\user"
set "REGPU_DEBUG=1"
set "REGPU_DEBUG_PORT=7342"
if not defined PGR3_BUILD set "PGR3_BUILD=rel"
if not exist "%ROOT%build\%PGR3_BUILD%\pgr3.exe" (
    echo build\%PGR3_BUILD%\pgr3.exe is missing: build it first.
    pause
    exit /b 1
)
if not exist "%ROOT%out\run" mkdir "%ROOT%out\run"
cd /d "%ROOT%out\run"
start "" "%ROOT%build\%PGR3_BUILD%\pgr3.exe" "%PGR3_GAME_DIR%"
