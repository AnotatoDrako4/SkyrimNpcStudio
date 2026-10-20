@echo off
rem ============================================================================
rem  Skyrim NPC Studio Ultimate — Outfit Studio 3D + Character Creator 3.0
rem  FaceGen Fidelity + Outfit Designer + HDT-SMP / FSMP (Release autónomo / Debug dev)
rem ============================================================================
title Skyrim NPC Studio Ultimate — Creative Workspace
setlocal

rem UTF-8 console: soporte para caracteres internacionales y logs MSVC/Ninja
chcp 65001 >nul

set "ROOT=%~dp0"
cd /d "%ROOT%"
set "DIST=%ROOT%dist\SkyrimNpcStudio"
set "RELEASE_EXE=%DIST%\SkyrimNpcStudio.exe"
set "DEBUG_EXE=%ROOT%build\debug\bin\SkyrimNpcStudio.exe"
set "LOCAL_DIR=%ROOT%build\package\bin"
set "LOCAL_EXE=%LOCAL_DIR%\SkyrimNpcStudio.exe"
set "VERSION_FILE=%DIST%\VERSION.txt"

rem ----------------------------------------------------------------------------
rem Opciones por linea de comandos:
rem   SkyrimNpcStudio.bat                 : Ejecuta la Release autonoma en dist\ (empaqueta si no existe o cambio commit)
rem   SkyrimNpcStudio.bat --dist          : Ejecuta la Release autonoma de dist\ directamente sin comprobaciones
rem   SkyrimNpcStudio.bat --rebuild       : Fuerza recompilacion Release y empaquetado en dist\
rem   SkyrimNpcStudio.bat --package       : Alias de --rebuild
rem   SkyrimNpcStudio.bat --dev           : Ejecuta version Debug (desarrollo rapido)
rem   SkyrimNpcStudio.bat --build-debug   : Recompila Debug y ejecuta
rem   SkyrimNpcStudio.bat --local         : Ejecuta la Release local de build\package (con entorno Qt de desarrollo)
rem   SkyrimNpcStudio.bat --build-release : Recompila la Release local y la ejecuta
rem ----------------------------------------------------------------------------

:RECHECK_INSTANCE
rem Comprobacion de instancia unica: evita conflictos de base de datos y cache entre dos instancias
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

rem ----------------------------------------------------------------------------
rem Modo por defecto: Verificar si dist\SkyrimNpcStudio existe y esta actualizado
rem ----------------------------------------------------------------------------
if not exist "%RELEASE_EXE%" (
    echo [INFO] No se encontro el paquete Release en dist\SkyrimNpcStudio.
    echo Generando compilacion Release autonoma inicial...
    goto DO_PACKAGE
)

if not exist "%VERSION_FILE%" goto LAUNCH_RELEASE

set "SOURCE_COMMIT="
set "PACKAGE_COMMIT="
for /f %%H in ('git -C "%ROOT%" rev-parse --short HEAD 2^>nul') do set "SOURCE_COMMIT=%%H"
if exist "%VERSION_FILE%" for /f "usebackq tokens=5" %%V in ("%VERSION_FILE%") do set "PACKAGE_COMMIT=%%V"

if defined SOURCE_COMMIT (
    if defined PACKAGE_COMMIT (
        if /I not "%SOURCE_COMMIT%"=="%PACKAGE_COMMIT%" (
            echo =======================================================================
            echo  [ACTUALIZACION] Codigo actualizado detectado: %SOURCE_COMMIT% vs %PACKAGE_COMMIT%
            echo  Actualizando paquete Release en dist\SkyrimNpcStudio...
            echo =======================================================================
            goto DO_PACKAGE
        )
    )
)

goto LAUNCH_RELEASE

:LAUNCH_RELEASE
if not exist "%RELEASE_EXE%" (
    echo.
    echo [ERROR] No se encontro el ejecutable en:
    echo   %RELEASE_EXE%
    echo Ejecuta: SkyrimNpcStudio.bat --rebuild
    pause
    exit /b 1
)
echo Iniciando Skyrim NPC Studio Ultimate...
pushd "%DIST%"
start "" "SkyrimNpcStudio.exe" %*
popd
ping -n 2 127.0.0.1 >nul
exit /b 0

:DO_PACKAGE
echo =======================================================================
echo  Preparando Skyrim NPC Studio Ultimate (Creative Workspace)
echo  Compilando Release y desplegando runtime en dist\SkyrimNpcStudio...
echo =======================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\package.ps1"
if %errorlevel% EQU 3 goto ALREADY_RUNNING
if errorlevel 1 (
    echo.
    echo [ERROR] No se pudo empaquetar la aplicacion automaticamente.
    if exist "%RELEASE_EXE%" (
        echo Se encontro una version anterior funcional en dist\.
        echo Presiona cualquier tecla para intentar iniciarla, o cierra para salir.
        pause >nul
        goto LAUNCH_RELEASE
    )
    pause
    exit /b 1
)
goto LAUNCH_RELEASE

:DO_REBUILD
echo =======================================================================
echo  Recompilando Release y empaquetando dist\SkyrimNpcStudio...
echo =======================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\package.ps1"
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
echo =======================================================================
echo  [AVISO] Skyrim NPC Studio ya se encuentra en ejecucion.
echo =======================================================================
echo  Se detecto un proceso activo de SkyrimNpcStudio.exe.
echo.
set /p "USER_CHOICE=Deseas cerrar la instancia activa y reiniciar? [S/N]: "
if /I "%USER_CHOICE%"=="S" (
    echo Cerrando instancia previa...
    taskkill /F /IM "SkyrimNpcStudio.exe" 2>nul
    ping -n 2 127.0.0.1 >nul
    goto RECHECK_INSTANCE
)
echo Cancelado por el usuario.
exit /b 0

:ALREADY_RUNNING
echo.
echo [AVISO] Skyrim NPC Studio esta abierto desde dist\: cierralo antes de empaquetar.
pause
exit /b 0

:BUILD_LOCAL_RELEASE
echo =======================================================================
echo  Compilando Release local (build\package)...
echo =======================================================================
powershell -NoProfile -ExecutionPolicy Bypass -Command "& { . '%ROOT%scripts\dev-env.ps1'; cmake --build '%ROOT%build\package'; exit $LASTEXITCODE }"
if errorlevel 1 (
    echo.
    echo [ERROR] Error al compilar la Release local.
    pause
    exit /b 1
)
goto RUN_LOCAL_RELEASE

:RUN_LOCAL_RELEASE
if not exist "%LOCAL_EXE%" (
    echo [AVISO] No existe la Release local: usa --build-release para compilarla.
    pause
    exit /b 1
)
echo Iniciando Skyrim NPC Studio (Release local de build\package)...
powershell -NoProfile -ExecutionPolicy Bypass -Command "& { . '%ROOT%scripts\dev-env.ps1' | Out-Null; Start-Process -FilePath '%LOCAL_EXE%' -WorkingDirectory '%LOCAL_DIR%' }"
exit /b 0

:BUILD_DEBUG
echo =======================================================================
echo  Compilando Skyrim NPC Studio (Modo Debug)...
echo =======================================================================
powershell -NoProfile -ExecutionPolicy Bypass -Command "& { . '%ROOT%scripts\dev-env.ps1'; cmake --build --preset debug --target SkyrimNpcStudio }"
if errorlevel 1 (
    echo.
    echo [ERROR] Error durante la compilacion de depuracion.
    pause
    exit /b 1
)
goto RUN_DEBUG

:RUN_DEBUG
if not exist "%DEBUG_EXE%" (
    echo [AVISO] El ejecutable Debug no existe todavia. Compilando primero...
    powershell -NoProfile -ExecutionPolicy Bypass -Command "& { . '%ROOT%scripts\dev-env.ps1'; cmake --build --preset debug --target SkyrimNpcStudio }"
    if errorlevel 1 (
        echo [ERROR] No se pudo compilar el binario Debug.
        pause
        exit /b 1
    )
)
echo Iniciando Skyrim NPC Studio (Modo Debug / Dev)...
powershell -NoProfile -ExecutionPolicy Bypass -Command "& { . '%ROOT%scripts\dev-env.ps1' | Out-Null; Start-Process -FilePath '%DEBUG_EXE%' -WorkingDirectory '%ROOT%build\debug\bin' }"
exit /b 0
