@echo off
rem ============================================================================
rem  Skyrim NPC Studio Ultimate — Outfit Studio 3D + Character Creator 3.0
rem  FaceGen Fidelity + Outfit Designer + HDT-SMP / FSMP (Release autónomo / Debug dev)
rem ============================================================================
title Skyrim NPC Studio Ultimate — Outfit Studio 3D + Character Creator 3.0
setlocal

rem UTF-8 console: a Spanish MSVC writes its /showIncludes notes in the console code page and
rem Ninja only recognises them in UTF-8. Without it, header changes stop recompiling the files
rem that include them (stale objects, memory corruption). See docs/CRASH_DIAGNOSTICS.md.
chcp 65001 >nul

cd /d "%~dp0"
set "DIST=%~dp0dist\SkyrimNpcStudio"
set "RELEASE_APP=%DIST%\SkyrimNpcStudio.exe"
set "DEBUG_APP=%~dp0build\debug\bin\SkyrimNpcStudio.exe"
set "LOCAL_DIR=%~dp0build\package\bin"
set "LOCAL_APP=%LOCAL_DIR%\SkyrimNpcStudio.exe"
set "VERSION_FILE=%DIST%\VERSION.txt"

rem ----------------------------------------------------------------------------
rem Opciones por linea de comandos:
rem   SkyrimNpcStudio.bat                 -> Ejecuta la Release mas reciente: build\package (si es mas nueva
rem                                          que dist\) o la Release autonoma de dist\
rem   SkyrimNpcStudio.bat --local         -> Ejecuta la Release local de build\package (Qt del entorno de desarrollo)
rem   SkyrimNpcStudio.bat --build-release -> Recompila la Release local (sin Git ni empaquetar) y la ejecuta
rem   SkyrimNpcStudio.bat --dist          -> Ejecuta la Release autonoma de dist\ aunque sea antigua
rem   SkyrimNpcStudio.bat --dev           -> Ejecuta version Debug (desarrollo rapido)
rem   SkyrimNpcStudio.bat --rebuild       -> Recompila Release y empaqueta en dist\ (scripts\package.ps1, usa Git)
rem   SkyrimNpcStudio.bat --build-debug   -> Recompila Debug y ejecuta
rem ----------------------------------------------------------------------------

rem Una sola instancia: dos versiones distintas sobre el mismo perfil reconstruyen una el catalogo de la otra.
tasklist /FI "IMAGENAME eq SkyrimNpcStudio.exe" 2>nul | find /I "SkyrimNpcStudio.exe" >nul
if not errorlevel 1 goto ALREADY_OPEN

if /I "%~1"=="--dev" goto RUN_DEBUG
if /I "%~1"=="--debug" goto RUN_DEBUG
if /I "%~1"=="--build-debug" goto BUILD_DEBUG
if /I "%~1"=="--rebuild" goto DO_REBUILD
if /I "%~1"=="--package" goto DO_REBUILD
if /I "%~1"=="--local" goto RUN_LOCAL_RELEASE
if /I "%~1"=="--build-release" goto BUILD_LOCAL_RELEASE
if /I "%~1"=="--dist" goto LAUNCH_RELEASE

rem Release local mas nueva que dist\ (compilada sin empaquetar, p. ej. mientras Git esta en espera): se usa esa.
if not exist "%LOCAL_APP%" goto CHECK_DIST
powershell -NoProfile -Command "$l = Get-Item -LiteralPath '%LOCAL_APP%'; $d = Get-Item -LiteralPath '%RELEASE_APP%' -ErrorAction SilentlyContinue; if (-not $d -or $l.LastWriteTime -gt $d.LastWriteTime) { exit 10 } else { exit 0 }"
if %errorlevel% EQU 10 goto RUN_LOCAL_RELEASE

:CHECK_DIST
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
if %errorlevel% EQU 3 goto ALREADY_RUNNING
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
if %errorlevel% EQU 3 goto ALREADY_RUNNING
if errorlevel 1 (
    echo.
    echo [ERROR] Fallo la reconstruccion del paquete Release.
    pause
    exit /b 1
)
goto LAUNCH_RELEASE

:ALREADY_OPEN
echo.
echo [AVISO] Skyrim NPC Studio ya esta abierto. Cierralo antes de abrir otra copia: dos versiones distintas
echo         sobre el mismo perfil reconstruyen una el catalogo de la otra.
pause
exit /b 0

:BUILD_LOCAL_RELEASE
echo =======================================================================
echo  Compilando la Release local (build\package, sin empaquetar ni Git)...
echo =======================================================================
powershell -NoProfile -ExecutionPolicy Bypass -Command ". '%~dp0scripts\dev-env.ps1'; cmake --build '%~dp0build\package'; exit $LASTEXITCODE"
if errorlevel 1 (
    echo.
    echo [ERROR] Error al compilar la Release local.
    pause
    exit /b 1
)
goto RUN_LOCAL_RELEASE

:RUN_LOCAL_RELEASE
if not exist "%LOCAL_APP%" (
    echo [AVISO] No existe la Release local: usa --build-release para compilarla.
    pause
    exit /b 1
)
echo Iniciando Skyrim NPC Studio (Release local de build\package)...
powershell -NoProfile -ExecutionPolicy Bypass -Command ". '%~dp0scripts\dev-env.ps1' *> $null; Start-Process -FilePath '%LOCAL_APP%' -WorkingDirectory '%LOCAL_DIR%'"
exit /b 0

:ALREADY_RUNNING
echo.
echo [AVISO] Skyrim NPC Studio ya esta abierto: no se cierra para no perder cambios sin guardar.
echo         Cierralo y vuelve a ejecutar este archivo para actualizar el paquete.
pause
exit /b 0

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
