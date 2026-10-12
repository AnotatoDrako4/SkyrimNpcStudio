@echo off
rem Skyrim NPC Studio - lanzador.
rem   SkyrimNpcStudio.bat                 ejecuta la app (la prepara la primera vez)
rem   SkyrimNpcStudio.bat --rebuild       recompila y vuelve a copiar el runtime en dist\
rem   SkyrimNpcStudio.bat --instance D:\Apps\ModOrganizer.ini --profile Default --npc 0A2C8E:Skyrim.esm
setlocal
set "ROOT=%~dp0"
set "APP=%ROOT%dist\SkyrimNpcStudio\SkyrimNpcStudio.exe"

set "REBUILD="
if /I "%~1"=="--rebuild" set "REBUILD=1"
if not exist "%APP%" set "REBUILD=1"

if defined REBUILD (
    echo Preparando Skyrim NPC Studio: compilacion Release y copia del runtime en dist\ ...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\package.ps1"
    if errorlevel 1 (
        echo.
        echo ERROR: no se pudo preparar la aplicacion. Revisa los mensajes anteriores.
        pause
        exit /b 1
    )
)

if /I "%~1"=="--rebuild" (
    start "" "%APP%"
) else (
    start "" "%APP%" %*
)
endlocal
