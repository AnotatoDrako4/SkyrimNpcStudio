# INFORME FORENSE: RESOLUCIÓN Y RENDERIZADO DEL ATUENDO DE SERANA (DLC1Serana)

**Identificador:** `SNS-DOC-SERANA-FORENSICS-2026-10`  
**Fecha:** 2026-10-24  
**Objetivo:** Diagnóstico y resolución definitiva del problema de atuendo de Serana (`002B6C:Dawnguard.esm`) y NPCs vampiro en entornos MO2 con cuerpos CBBE/3BA.  
**Estado:** `VERIFICADO_EN_SKYRIM_STUDIO_OK`

---

## 1. DESCRIPCIÓN DEL PROBLEMA

En instalaciones con Mod Organizer 2 y replacers modernos (específicamente *ESP-FE Servant Priscilla high poly follower healer - Serana replacer* y *Obi's Crimson Outfit 2K 3BA BHUNP SMP*), el visor 3D de personajes mostraba a Serana con el torso vestido, botas, guantes y capa, pero **sin pantalones / piernas desnudas o invisibles**.

### Síntomas Observados:
- La parte superior del atuendo (*Temio Shirt Red*, *Temio Corset Red*) y los accesorios (*Crimson Boots*, *Crimson Gloves*, *Twilight Princess Cloak*) se renderizaban correctamente.
- La prenda inferior *Crimson Pants* (`000803:Obi's Crimson Outfit.esp`) no aparecía en la lista de mallas equipadas ni en el visor.
- Las piernas aparecían sin la prenda asignada en el registro `OTFT` (*01_PriscilaSuit*, `000814:Priscilla.Serana.Replacer.esp`).

---

## 2. HIPÓTESIS EVALUADAS Y DIAGNÓSTICO FORENSE

| # | Hipótesis | Resultado | Evidencia / Análisis |
|---|-----------|-----------|----------------------|
| **H1** | **Malla inexistente o ruta rota en VFS** | **Descartada** | El archivo `meshes\Obicnii\Crimson Outfit\Legs_1.nif` existe físicamente en el VFS de MO2 y es una malla válida con 4,216 triángulos y texturas válidas. |
| **H2** | **Conflicto / Colisión de slots Biped** | **Descartada** | Los slots biped asignados en los registros `ARMO`/`ARMA` eran ortogonales: Camisa (32), Guantes (33), Botas (37), Corsé (45), Capa (46), Pantalones (49). No existía colisión entre prendas. |
| **H3** | **Ocultamiento por shader flag o alpha** | **Descartada** | La malla no tiene shaders ocultos ni transparencias que eliminen la geometría. |
| **H4** | **Resolución no transitiva de ArmorRace en BethesdaWorker** | **CONFIRMADA (Causa Raíz)** | Los registros `ARMA` de mods modernos asignan compatibilidad a `DefaultRace` (`000019:Skyrim.esm`). Serana pertenece a `NordRaceVampire` (`088794:Skyrim.esm`). La cadena de herencia de razas en Skyrim es transitiva: `NordRaceVampire` -> `NordRace` -> `DefaultRace`. La consulta en C# solo evaluaba 1 salto directo, fallando en asociar los `ArmorAddon` de `DefaultRace`. |

---

## 3. CAUSA RAÍZ TÉCNICA DETALLADA

En el motor BethesdaWorker (C# / Mutagen), las consultas de apariencia (`AppearanceQueries.cs`), validación de atuendos (`OutfitValidator.cs`) y consultas de cuerpo (`BodyQueries.cs`) determinaban la compatibilidad entre una armadura (`ArmorAddon` / `ARMA`) y un NPC mediante una comprobación superficial:

```csharp
// CÓDIGO ANTERIOR (INCORRECTO - SÓLO 1 SALTO)
if (arma.Race.FormKey == npcRaceKey) return true;
if (npcRace.ArmorRace.TryResolve(session.LinkCache, out var armorRace) && arma.Race.FormKey == armorRace.FormKey) return true;
if (npcRace.MorphRace.TryResolve(session.LinkCache, out var morphRace) && arma.Race.FormKey == morphRace.FormKey) return true;
```

### La Cadena Genealógica en Skyrim:
1. `DLC1Serana` (`002B6C:Dawnguard.esm`) tiene `Race = NordRaceVampire` (`088794:Skyrim.esm`).
2. `NordRaceVampire` define `ArmorRace = NordRace` (`000013:Skyrim.esm`).
3. `NordRace` define a su vez `ArmorRace = DefaultRace` (`000019:Skyrim.esm`).
4. *Obi's Crimson Outfit* registra `1_CrimsonPantsAA` (`000802:Obi's Crimson Outfit.esp`) con `Race = DefaultRace`.

Al evaluar únicamente el primer salto, `NordRaceVampire.ArmorRace` resolvía a `NordRace`, que **no coincidía** con `DefaultRace`. Por tanto, el worker descartaba la armadura completa por supuesta incompatibilidad de raza, provocando que los pantalones nunca se enviasen al pipeline de renderizado C++.

---

## 4. SOLUCIÓN IMPLEMENTADA

Se implementó el servicio de resolución genealógica de razas transitivas en `services/BethesdaWorker/NpcCatalog/RaceAncestry.cs`:

```csharp
public static class RaceAncestry
{
    public static HashSet<FormKey> CollectRaces(ISession session, FormKey raceKey)
    {
        var result = new HashSet<FormKey> { raceKey };
        Traverse(session, raceKey, result);
        return result;
    }

    private static void Traverse(ISession session, FormKey currentKey, HashSet<FormKey> result)
    {
        if (!session.LinkCache.TryResolve<IRaceGetter>(currentKey, out var race)) return;

        if (race.ArmorRace.TryResolve(session.LinkCache, out var armorRace) && result.Add(armorRace.FormKey))
        {
            Traverse(session, armorRace.FormKey, result);
        }

        if (race.MorphRace.TryResolve(session.LinkCache, out var morphRace) && result.Add(morphRace.FormKey))
        {
            Traverse(session, morphRace.FormKey, result);
        }
    }
}
```

Este servicio fue integrado en:
- `AppearanceQueries.cs`: Selección de `ArmorAddon` y mallas de atuendo.
- `OutfitValidator.cs`: Validación estricta de atuendos asignados.
- `BodyQueries.cs`: Selección de mallas de piel y anatomía para razas vampiro y derivadas.

---

## 5. EVIDENCIA EMPÍRICA Y VERIFICACIÓN EN VIVO

Se ejecutó la prueba de estrés y diagnóstico contra la instancia real de Mod Organizer 2 (`D:\Apps\ModOrganizer.ini`, perfil `Default`) con el NPC `002B6C:Dawnguard.esm`:

### 5.1. Registro de Ejecución en Vivo:
```
smoke: phase=3 npcs=8683 armors=8303 plugins=415 files=89570 npc=DLC1Serana
screenshot saved: D:\.Proyectos Games\SkyrimNpcStudio-ui2\out\smoke.png
outfit-diagnosis run: out\outfit-diagnosis\20261024-191617
```

### 5.2. Mallas y Prenda Equipada:
Del archivo `outfit-diagnosis.json`:
```json
{
    "armorFormKey": "000803:Obi's Crimson Outfit.esp",
    "name": "1_CrimsonPants",
    "equipped": true,
    "slotMask": 524288,
    "slots": [49],
    "meshPaths": ["meshes\\Obicnii\\Crimson Outfit\\Legs_1.nif"],
    "meshProviders": ["loose:Obi's Crimson Outfit 2K 3BA BHUNP SMP"],
    "meshesExist": true
}
```

En la escena 3D generada:
- **Pieza:** `1_CrimsonPants`
- **FormKey:** `000803:Obi's Crimson Outfit.esp`
- **Malla NIF:** `meshes\Obicnii\Crimson Outfit\Legs_1.nif`
- **Triángulos:** 4,216
- **Límites Z:** `-14.26` a `7.20`
- **Estado de dibujado:** `shown: true`, `hiddenReason: ""`
- **Diagnóstico de cobertura:** `legsCoveredByOutfit: true`, `lowestVisibleOutfitZ: -23.52`.

### 5.3. Inspección Visual:
La captura de pantalla generada en `out\outfit-diagnosis\20261024-191617\outfit-002B6C_Dawnguard.esm.png` (225 KB) confirma de manera visual irrefutable:
- Los pantalones `Crimson Pants` cubren completamente la cadera y piernas hasta unirse con las botas `Crimson Boots`.
- No hay roturas de malla, ausencia de texturas críticas ni espacios vacíos.
- La silueta y proporciones coinciden exactamente con la configuración 3BA del mod.

---

## 6. CONCLUSIÓN

El defecto de Serana y NPCs vampiro sin pantalones ha quedado **completamente solucionado y verificado en vivo**. La resolución transitiva de `ArmorRace` garantiza que cualquier atuendo creado para `DefaultRace` o razas base sea equipado correctamente por razas derivadas, transformadas o vampíricas sin requerir parches adicionales en los mods de los usuarios.
