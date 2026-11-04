@echo off
rem Compila con BodySlide-por-lotes de Skyrim NPC Studio los cuerpos y la ropa de la familia de un preset.
rem Solo LEE tu perfil de MO2. El resultado queda en out\body-batches: instálalo en MO2 como un mod más.
rem Uso: Compilar-Cuerpos.bat "nombre del preset" [perfil de MO2] [texto para filtrar proyectos]
setlocal
chcp 65001 >nul
cd /d "%~dp0"
set "PROBE=build\claude-body\bin\sns-probe.exe"
if not exist "%PROBE%" set "PROBE=build\debug\bin\sns-probe.exe"
if not exist "%PROBE%" (
  echo No encuentro sns-probe.exe: compila el proyecto primero.
  pause
  exit /b 1
)
if not defined SNS_MO2_INI set "SNS_MO2_INI=D:\Apps\ModOrganizer.ini"
set "PRESET=%~1"
if "%PRESET%"=="" set /p "PRESET=Nombre exacto del preset de BodySlide: "
if "%PRESET%"=="" exit /b 1
set "PROFILE_ARG="
if not "%~2"=="" set PROFILE_ARG=--profile "%~2"
set "ONLY_ARG="
if not "%~3"=="" set ONLY_ARG=--only "%~3"
set "AMBIGUOUS_ARG="
if defined SNS_INCLUDE_AMBIGUOUS set "AMBIGUOUS_ARG=--include-ambiguous 1"
for /f %%T in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd-HHmmss"') do set "STAMP=%%T"
set "OUT=%CD%\out\body-batches\%STAMP%"
"%PROBE%" bodyslide-batch --instance "%SNS_MO2_INI%" %PROFILE_ARG% %ONLY_ARG% %AMBIGUOUS_ARG% --preset "%PRESET%" --out "%OUT%"
set "CODE=%ERRORLEVEL%"
if exist "%OUT%\mod\meshes" (
  powershell -NoProfile -Command "Compress-Archive -LiteralPath '%OUT%\mod\meshes' -DestinationPath '%OUT%\Cuerpos.zip'"
  echo.
  echo Mod para instalar en MO2: %OUT%\Cuerpos.zip
  echo Ponlo por encima de los mods que construyen esas mallas.
  echo informe.csv: other-family necesita conversion; ambiguous no se compilo ^(SNS_INCLUDE_AMBIGUOUS=1 para incluirlo^).
  if exist "%OUT%\en-overwrite.txt" echo ATENCION: mueve de Overwrite a otro mod lo listado en en-overwrite.txt, o el cuerpo nuevo no se vera.
)
echo.
if "%CODE%"=="3" echo Terminado con errores en algunos proyectos: mira informe.csv.
if "%CODE%"=="0" echo Terminado.
pause
exit /b %CODE%
