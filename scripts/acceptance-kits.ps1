<#
.SYNOPSIS
  In-game acceptance kits (D3/D22) for Serana, Lydia, Shady Sam, Bolli and Brand-Shei, generated with this
  build: the app's harness exports each case from the MO2 profile (read only), a test run manifest records
  build, profile, Skyrim version and artifacts, and each kit carries a form a person fills in inside Skyrim.

  Nothing is installed, MO2 and Skyrim/Data are not touched (the profile files are hashed before and
  after), and nothing is marked verified: every kit stays WAITING_HUMAN. An ambiguous case (D1) gets no
  installable package: the harness only approves it in a temporary review book.

.EXAMPLE
  .\scripts\acceptance-kits.ps1 -Out D:\kits -Instance D:\Apps\ModOrganizer.ini -MO2Profile Default
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Out,
    [Parameter(Mandatory = $true)][string]$Instance,
    [string]$MO2Profile = 'Default',
    [string]$Preset = 'debug',
    [string[]]$Only = @(),
    [string]$Size = '1100x700'
)

$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'TestRunManifest.psm1') -Force
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root "build\$Preset\bin"
$buildFiles = @((Join-Path $bin 'SkyrimNpcStudio.exe'), (Join-Path $bin 'worker\SkyrimNpcStudio.BethesdaWorker.dll'))
$Out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Out)
New-Item -ItemType Directory -Force $Out | Out-Null

$cases = @(
    [ordered]@{ id = '1-serana'; name = 'Serana'; npc = '002B6C:Dawnguard.esm'; scenario = 'hair-safe'
                goal = 'Solo el color de cabello (Hair Color Safe Mode): el FaceGen ganador del replacer con otro HairTint.'
                look = @('El cabello tiene el color nuevo', 'La forma de la cara y el peinado del replacer no cambian') }
    [ordered]@{ id = '2-lydia'; name = 'Lydia'; npc = '0A2C8E:Skyrim.esm'; scenario = 'easy-mod-outfit'
                goal = 'Atuendo nuevo (modo A) y atuendo + color de cabello (modo C), en dos rondas.'
                look = @('Lleva el atuendo nuevo (modo A y C)', 'Modo C: color de cabello nuevo con la cara de su replacer') }
    [ordered]@{ id = '3-shady-sam'; name = 'Shady Sam'; npc = '0370FB:alternate start - live another life.esp'; scenario = 'easy-mod-outfit'
                goal = 'Atuendo nuevo (modo A) y atuendo + color de cabello (modo C); su FaceGen está en el BSA de Alternate Start.'
                look = @('Lleva el atuendo nuevo', 'Modo C: color de cabello nuevo, cara igual que antes') }
    [ordered]@{ id = '4-bolli'; name = 'Bolli'; npc = '01334E:Skyrim.esm'; scenario = 'face-resolution'
                goal = 'Rostro de NITHI sobre el registro de AI Overhaul («Usar el rostro de X»). Caso ambiguo: descarta ediciones faciales propias de AI Overhaul.'
                look = @('Cara de NITHI sin oscurecer', 'Lo que AI Overhaul cambiaba fuera del rostro sigue igual') }
    [ordered]@{ id = '5-brand-shei'; name = 'Brand-Shei'; npc = '01334F:Skyrim.esm'; scenario = 'face-resolution'
                goal = 'Rostro de NITHI sobre el registro ganador («Usar el rostro de X»), sin ambigüedad.'
                look = @('Cara de NITHI con el mismo tono que el cuerpo', 'Lo que el registro ganador cambiaba fuera del rostro sigue igual') }
)
if ($Only.Count) { $cases = @($cases | Where-Object { $_.id -in $Only -or $_.name -in $Only }) }

function Get-ProfileHashes {
    $dir = (Read-SnsMO2Instance $Instance $MO2Profile).ProfileDir
    @(Get-ChildItem -LiteralPath $dir -File | Sort-Object Name | ForEach-Object { "{0}  {1}" -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash, $_.Name })
}
function Write-Utf8([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false)) }

$profileBefore = Get-ProfileHashes
$summary = [Collections.Generic.List[object]]::new()
foreach ($case in $cases) {
    $dir = Join-Path $Out $case.id
    $run = Join-Path $dir 'run'
    $kit = Join-Path $dir 'kit'
    New-Item -ItemType Directory -Force $run, $kit | Out-Null
    Write-Host "== $($case.name) ($($case.npc)): $($case.scenario)"
    $smokeError = $null
    try {
        & (Join-Path $PSScriptRoot 'smoke-test.ps1') -Preset $Preset -Instance $Instance -MO2Profile $MO2Profile -Npc $case.npc -Size $Size `
            -StressAppearance $case.scenario -StressWork $run -Screenshot (Join-Path $run 'smoke.png') | Out-Host
    } catch {
        $smokeError = $_.Exception.Message   # recorded in the kit and fails this script; the other kits still run
        Write-Warning "$($case.name): $smokeError"
    }
    $problems = [Collections.Generic.List[string]]::new()
    if ($smokeError) { $problems.Add("SmokeFailed: $smokeError") }
    if ($env:SNS_REVIEW_DIR) { $problems.Add("ReviewDirLeaked: $env:SNS_REVIEW_DIR") }
    $created = New-SnsTestRunManifest -RunDir $run -MO2Instance $Instance -MO2Profile $MO2Profile -Npc $case.npc -Scenario $case.scenario `
        -SmokeLog (Join-Path $run 'smoke.log') -SmokeExitCode $(if ($smokeError) { 1 } else { 0 }) -BuildFiles $buildFiles `
        -Notes "Acceptance kit $($case.id)"
    $m = $created.Manifest
    foreach ($p in @($m.problems)) { $problems.Add($p) }

    # An approval in the harness's temporary book is not a human review (D1): its exports are not installable.
    $harnessApproved = @(Get-ChildItem -LiteralPath $run -Recurse -File -Filter '*.json' |
        Where-Object { $_.DirectoryName -like '*review-book-*' } |
        Where-Object { @((Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json).records).Count -gt 0 }).Count -gt 0
    $packages = [Collections.Generic.List[object]]::new()
    foreach ($e in @($m.exports)) {
        if (-not $e.zip) { continue }
        $name = '{0}-{1}-{2}.zip' -f $case.id, $e.mode, [IO.Path]::GetFileNameWithoutExtension($e.plugin)
        if (-not $harnessApproved) { Copy-Item -LiteralPath (Join-Path $run $e.zip) -Destination (Join-Path $kit $name) }
        $packages.Add([ordered]@{
            zip = if ($harnessApproved) { $null } else { $name }; sourceZip = "run/$($e.zip)"; sha256 = $e.zipSha256; plugin = $e.plugin; mode = $e.mode
            loadAfter = @($e.loadAfter); installAbove = @($e.installAbove); installable = -not $harnessApproved
        })
    }
    $installable = @($packages | Where-Object { $_.installable })
    $sums = @($installable | ForEach-Object { "$($_.sha256)  $($_.zip)" })
    Write-Utf8 (Join-Path $kit 'SHA256SUMS.txt') (($sums -join "`n") + "`n")
    $blocked = if ($harnessApproved) {
        'Caso ambiguo (D1): el arnés solo lo exportó con una aprobación de prueba en un libro temporal. Revísalo tú en la pestaña ' +
        'Origen (Aprobar, Rechazar o Aplazar); si lo apruebas, expórtalo desde el asistente y usa ese ZIP con esta acta.'
    } elseif ($packages.Count -eq 0) {
        'La ejecución no produjo ningún paquete (ver run/smoke.log y run/test-run-manifest.json).'
    } else { $null }

    $exe = @($m.build.files | Where-Object { $_.name -eq 'SkyrimNpcStudio.exe' })[0]
    $worker = @($m.build.files | Where-Object { $_.name -like '*BethesdaWorker*' })[0]
    $rows = if ($installable.Count) {
        ($installable | ForEach-Object {
            "| ``$($_.zip)`` | ``$($_.sha256)`` | ``$($_.plugin)`` | $($_.mode) | $(if ($_.loadAfter.Count) { $_.loadAfter -join ', ' } else { '—' }) | $(if ($_.installAbove.Count) { $_.installAbove -join ', ' } else { '—' }) |"
        }) -join "`n"
    } else { '| — | — | — | — | — | — |' }
    $form = @"
# Acta de aceptación en Skyrim — $($case.name) (``$($case.npc)``)

Estado: **WAITING_HUMAN**. El kit se generó y comprobó en Windows con este build; nadie lo ha probado dentro del juego.
Un resultado solo cuenta con capturas y esta acta registradas con ``Add-SnsHumanEvidence`` (ver el final).

## Qué se prueba

$($case.goal)

## Build y evidencia automática

- Manifiesto de la ejecución: ``run/test-run-manifest.json`` (SHA-256 ``$($created.Sha256)``)
- ``SkyrimNpcStudio.exe``: ``$(if ($exe) { $exe.sha256 } else { 'no registrado' })``
- ``SkyrimNpcStudio.BethesdaWorker.dll``: ``$(if ($worker) { $worker.sha256 } else { 'no registrado' })``
- Skyrim detectado: $(if ($m.skyrim.detected) { "``$($m.skyrim.fileVersion)``" } else { 'no detectado' }) · perfil MO2 leído (sin cambios): ``$($m.mo2.profileDir)``
- Resultado automático: estático **$($m.results.static)**, Windows **$($m.results.windows)** ($($m.smoke.pass) PASS / $($m.smoke.fail) FAIL, escenario ``$($case.scenario)``)
$(if ($problems.Count) { "- Problemas: " + ($problems -join '; ') })

## Paquetes

| ZIP | SHA-256 | Plugin | Modo | Cargar después de | Instalar por encima de |
| --- | --- | --- | --- | --- | --- |
$rows
$(if ($blocked) { "`n**Sin paquete instalable.** $blocked`n" })
## Preparación (sin tocar tu perfil habitual ni tus partidas)

1. En MO2, copia el perfil ``$MO2Profile`` a uno nuevo (Perfiles → Copiar), por ejemplo ``SNS-Aceptacion``. No pruebes en ``$MO2Profile``.
2. Instala el ZIP con «Instalar un mod desde archivo» y actívalo solo en el perfil de prueba. Si hay dos paquetes con el mismo plugin, pruébalos en rondas separadas.
3. Prioridad en MO2: el mod por encima de los mods de «Instalar por encima de». Si el paquete trae FaceGen, en «Conflictos» debe ganar el ``.nif`` de ``facegeom`` del NPC; si otro mod lo sobrescribe, la prueba no vale.
4. El plugin debe cargar después de sus masters; no debe aparecer el aviso de master ausente.
5. Usa una partida de prueba (nueva o un guardado desechable), nunca una partida personal.

## Prueba A/B

1. **A** (mod desactivado): busca a $($case.name), con una iluminación que puedas repetir (mejor en interior), y guarda capturas de frente y de perfil (``A-frente.png``, ``A-perfil.png``).
2. **B** (mod activado, mismo lugar y cámara parecida): ``B-frente.png``, ``B-perfil.png``.

## Lo esperado (marca lo que se cumple en B)

- [ ] $($case.look -join "`n- [ ] ")

## Problemas (marca lo que aparece en B y no en A)

- [ ] Cara oscura o de otro tono que el cuerpo
- [ ] Costura en el cuello
- [ ] Ojos, boca o dientes raros
- [ ] Peinado o color de cabello inesperado
- [ ] Atuendo (piezas que faltan, recortes)
- [ ] Crash o cierre del juego
- [ ] Diálogo o comportamiento distinto

Notas:

## Resultado

- [ ] PASS_IN_GAME: se cumple todo lo esperado y no aparece ningún problema
- [ ] FAIL_IN_GAME: describe qué falla
- [ ] Sin efecto observable (no es un PASS)

Observador: ______________________  Fecha: ______________

Para registrar la evidencia ligada a este build (comprueba antes que el manifiesto sigue siendo válido):

``````powershell
Import-Module '$root\scripts\TestRunManifest.psm1'
Add-SnsHumanEvidence -Path '$run\test-run-manifest.json' -File A-frente.png, B-frente.png, '$kit\ACCEPTANCE_FORM.md' -Observer 'tu nombre' -Result PASS_IN_GAME
``````
"@
    Write-Utf8 (Join-Path $kit 'ACCEPTANCE_FORM.md') $form
    $entry = [ordered]@{
        id = $case.id; name = $case.name; npc = $case.npc; scenario = $case.scenario; status = 'WAITING_HUMAN'
        results = $m.results; smoke = [ordered]@{ pass = $m.smoke.pass; fail = $m.smoke.fail; summary = $m.smoke.summary; error = $smokeError }
        testRunManifest = [ordered]@{ path = $created.Path; sha256 = $created.Sha256 }
        packages = @($packages); blocked = $blocked; problems = @($problems)
    }
    Write-Utf8 (Join-Path $kit 'kit.json') ($entry | ConvertTo-Json -Depth 8)
    $summary.Add($entry)
}

$profileAfter = Get-ProfileHashes
$profileUnchanged = (($profileBefore -join "`n") -eq ($profileAfter -join "`n"))
$index = [ordered]@{
    format = 'skyrim-npc-studio/acceptance-kits'; createdAt = [DateTimeOffset]::Now.ToString('o')
    instance = $Instance; profile = $MO2Profile; profileUnchanged = $profileUnchanged
    profileHashesBefore = $profileBefore; profileHashesAfter = $profileAfter
    kits = @($summary)
}
Write-Utf8 (Join-Path $Out 'kits.json') ($index | ConvertTo-Json -Depth 10)
$lines = @('# Kits de aceptación en Skyrim', '',
    "Generados $($index.createdAt) con el perfil ``$MO2Profile`` (solo lectura; hashes del perfil iguales antes y después: **$profileUnchanged**).",
    'Ningún kit está verificado en el juego: todos esperan la prueba humana (WAITING_HUMAN).', '',
    '| Kit | NPC | Escenario | Estático | Windows | En el juego | Paquetes instalables | Nota |', '| --- | --- | --- | --- | --- | --- | --- | --- |')
foreach ($k in $summary) {
    $zips = @($k.packages | Where-Object { $_.installable } | ForEach-Object { $_.zip })
    $note = if ($k.blocked) { $k.blocked } elseif ($k.problems.Count) { $k.problems -join '; ' } else { '' }
    $lines += "| $($k.id) | $($k.name) ``$($k.npc)`` | $($k.scenario) | $($k.results.static) | $($k.results.windows) | $($k.results.inGame) | $(if ($zips.Count) { $zips -join '<br>' } else { '—' }) | $note |"
}
Write-Utf8 (Join-Path $Out 'KITS.md') (($lines -join "`n") + "`n")
Write-Host "kits: $Out (profile unchanged: $profileUnchanged)"
$failed = @($summary | Where-Object { $_.results.windows -ne 'PASS_WINDOWS' -or @($_.problems).Count -gt 0 })
if (-not $profileUnchanged) { Write-Warning 'The MO2 profile changed during the run.'; exit 1 }
if ($failed.Count) { Write-Warning "Kits with problems: $(($failed | ForEach-Object { $_.id }) -join ', ')"; exit 1 }
exit 0
