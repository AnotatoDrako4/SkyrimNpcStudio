@echo off
rem Skyrim NPC Studio - lanzador.
rem   SkyrimNpcStudio.bat                 ejecuta la app (la prepara o actualiza si hace falta)
rem   SkyrimNpcStudio.bat --rebuild       recompila y vuelve a copiar el runtime en dist\
rem   SkyrimNpcStudio.bat --instance D:\Apps\ModOrganizer.ini --profile Default --npc 0A2C8E:Skyrim.esm
setlocal
set "ROOT=%~dp0"
set "DIST=%ROOT%dist\SkyrimNpcStudio"
set "APP=%DIST%\SkyrimNpcStudio.exe"
set "VERSION=%DIST%\VERSION.txt"

set "REBUILD="
if /I "%~1"=="--rebuild" set "REBUILD=1"
if not exist "%APP%" set "REBUILD=1"
if not exist "%VERSION%" set "REBUILD=1"

rem El paquete guarda el commit con el que se creo. Si el codigo avanzo, no abras una copia antigua.
if not defined REBUILD (
    for /f %%H in ('git -C "%ROOT%." rev-parse --short HEAD 2^>nul') do set "SOURCE_COMMIT=%%H"
    for /f "usebackq tokens=5" %%V in ("%VERSION%") do set "PACKAGE_COMMIT=%%V"
)
if defined SOURCE_COMMIT if /I not "%SOURCE_COMMIT%"=="%PACKAGE_COMMIT%" set "REBUILD=1"

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
    start "" /D "%DIST%" "%APP%"
) else (
    start "" /D "%DIST%" "%APP%" %*
)
endlocal
