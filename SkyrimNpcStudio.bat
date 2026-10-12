@echo off
rem ============================================================================
rem  Skyrim NPC Studio Ultimate — Character Creator 3.0
rem  FaceGen Fidelity + Outfit Designer (Release autónomo / Debug dev)
rem ============================================================================
title Skyrim NPC Studio Ultimate — Character Creator 3.0
setlocal

cd /d "%~dp0"
set "DIST=%~dp0dist\SkyrimNpcStudio"
set "RELEASE_APP=%DIST%\SkyrimNpcStudio.exe"
set "DEBUG_APP=%~dp0build\debug\bin\SkyrimNpcStudio.exe"
set "VERSION_FILE=%DIST%\VERSION.txt"

rem ----------------------------------------------------------------------------
rem Opciones por linea de comandos:
rem   SkyrimNpcStudio.bat                -> Ejecuta version Release autonoma
rem   SkyrimNpcStudio.bat --dev          -> Ejecuta version Debug (desarrollo rapido)
rem   SkyrimNpcStudio.bat --rebuild      -> Recompila Release y empaqueta en dist\
rem   SkyrimNpcStudio.bat --build-debug  -> Recompila Debug y ejecuta
rem ----------------------------------------------------------------------------

if /I "%~1"=="--dev" goto RUN_DEBUG
if /I "%~1"=="--debug" goto RUN_DEBUG
if /I "%~1"=="--build-debug" goto BUILD_DEBUG
if /I "%~1"=="--rebuild" goto DO_REBUILD
if /I "%~1"=="--package" goto DO_REBUILD

rem Modo Release por defecto: Comprobar si dist\ existe y esta al dia
if not exist "%RELEASE_APP%" goto DO_PACKAGE
if not exist "%VERSION_FILE%" goto DO_PACKAGE

set "SOURCE_COMMIT="
set "PACKAGE_COMMIT="
for /f %%H in ('git rev-parse --short HEAD 2^>nul') do set "SOURCE_COMMIT=%%H"
for /f "usebackq tokens=5" %%V in ("%VERSION_FILE%") do set "PACKAGE_COMMIT=%%V"

if defined SOURCE_COMMIT (
    if defined PACKAGE_COMMIT (
        if /I not "%SOURCE_COMMIT%"=="%PACKAGE_COMMIT%" (
            echo Actualizando paquete: commit %SOURCE_COMMIT% difiere de %PACKAGE_COMMIT%
            goto DO_PACKAGE
        )
    )
)

:LAUNCH_RELEASE
echo Iniciando Skyrim NPC Studio Ultimate...
start "" /D "%DIST%" "%RELEASE_APP%" %*
exit /b 0

:DO_PACKAGE
echo =======================================================================
echo  Preparando Skyrim NPC Studio (Character Creator 3.0)
echo  Compilando Release y desplegando runtime en dist\SkyrimNpcStudio...
echo =======================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package.ps1"
if errorlevel 1 (
    echo.
    echo [ERROR] No se pudo empaquetar la aplicacion.
    pause
    exit /b 1
)
goto LAUNCH_RELEASE

:DO_REBUILD
echo =======================================================================
echo  Recompilando Release y empaquetando dist\SkyrimNpcStudio...
echo =======================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\package.ps1"
if errorlevel 1 (
    echo.
    echo [ERROR] Fallo la reconstruccion del paquete Release.
    pause
    exit /b 1
)
goto LAUNCH_RELEASE

:BUILD_DEBUG
echo =======================================================================
echo  Compilando Skyrim NPC Studio (Modo Debug)...
echo =======================================================================
powershell -NoProfile -ExecutionPolicy Bypass -Command ". '%~dp0scripts\dev-env.ps1'; cmake --build --preset debug --target SkyrimNpcStudio"
if errorlevel 1 (
    echo.
    echo [ERROR] Error durante la compilacion de depuracion.
    pause
    exit /b 1
)
goto RUN_DEBUG

:RUN_DEBUG
if not exist "%DEBUG_APP%" (
    echo [AVISO] El ejecutable Debug no existe todavia. Compilando primero...
    powershell -NoProfile -ExecutionPolicy Bypass -Command ". '%~dp0scripts\dev-env.ps1'; cmake --build --preset debug --target SkyrimNpcStudio"
    if errorlevel 1 (
        echo [ERROR] No se pudo compilar el binario Debug.
        pause
        exit /b 1
    )
)
echo Iniciando Skyrim NPC Studio (Modo Debug / Dev)...
powershell -NoProfile -ExecutionPolicy Bypass -Command ". '%~dp0scripts\dev-env.ps1'; Start-Process -FilePath '%DEBUG_APP%' -WorkingDirectory '%~dp0build\debug\bin'"
exit /b 0
