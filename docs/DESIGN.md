---
name: Skyrim NPC Studio Ultimate — Universal Mod Asset Workbench
colors:
  surface: '#11141a'
  surface-dim: '#0b0e12'
  surface-bright: '#1c212b'
  surface-container-lowest: '#0b0e12'
  surface-container-low: '#141820'
  surface-container: '#181d26'
  surface-container-high: '#1f2632'
  surface-container-highest: '#262f3d'
  on-surface: '#e8edf5'
  on-surface-variant: '#9ba6b8'
  inverse-surface: '#e8edf5'
  inverse-on-surface: '#11141a'
  outline: '#2b3444'
  outline-variant: '#1e2531'
  surface-tint: '#4f8ff7'
  primary: '#4f8ff7'
  on-primary: '#ffffff'
  primary-container: '#1c355e'
  on-primary-container: '#cde0fe'
  inverse-primary: '#2359b8'
  secondary: '#6b7d99'
  on-secondary: '#ffffff'
  secondary-container: '#222b3a'
  on-secondary-container: '#c3d0e5'
  tertiary: '#b886d4'
  on-tertiary: '#281e33'
  tertiary-container: '#46305a'
  on-tertiary-container: '#ebd6f7'
  error: '#ef5350'
  on-error: '#ffffff'
  error-container: '#3d1919'
  on-error-container: '#ffcdd2'
  background: '#0e1116'
  on-background: '#e8edf5'
  surface-variant: '#1c222d'
typography:
  headline-lg:
    fontFamily: Segoe UI
    fontSize: 20px
    fontWeight: '600'
    lineHeight: 28px
  headline-md:
    fontFamily: Segoe UI
    fontSize: 17px
    fontWeight: '600'
    lineHeight: 24px
  headline-sm:
    fontFamily: Segoe UI
    fontSize: 15px
    fontWeight: '600'
    lineHeight: 20px
  body-md:
    fontFamily: Segoe UI
    fontSize: 13px
    fontWeight: '400'
    lineHeight: 18px
  body-sm:
    fontFamily: Segoe UI
    fontSize: 11px
    fontWeight: '400'
    lineHeight: 16px
  label-sm:
    fontFamily: Segoe UI
    fontSize: 10px
    fontWeight: '600'
    lineHeight: 14px
  mono-data:
    fontFamily: Consolas
    fontSize: 11px
    fontWeight: '400'
    lineHeight: 16px
rounded:
  sm: 3px
  DEFAULT: 4px
  lg: 6px
  full: 9999px
spacing:
  xs: 4px
  sm: 6px
  md: 8px
  lg: 12px
  xl: 16px
---

# Skyrim NPC Studio Ultimate — Universal Mod Asset Workbench (`DESIGN.md`)

**Stitch Project:** `projects/9939426075158331863` (*Skyrim NPC Studio Ultimate - Universal Mod Asset Workbench*)
**Target Runtime:** Native Windows 11 C++20 + Qt 6.11 QML + Qt Quick 3D (`apps/desktop/qml/Theme.qml` & `WorkbenchState`)

## 1. Brand & Architectural Philosophy

Skyrim NPC Studio Ultimate combines the structural flexibility of **Visual Studio Code** (activity bar, resizable `WorkbenchSplit` panels, collapsible explorer/inspector/console, layout presets, DPI-safe UI & font zoom) with a **3D-first character & asset workbench** for Skyrim SE/AE and Mod Organizer 2.

Key design invariants:
- **Zero Decorative Controls:** Every button, toggle, badge, and filter is wired to real C++ viewmodels (`StudioController`, `LooseResourceLibrary`, `SceneViewModel`, `OutfitEditor`, `AppearanceEditor`, `BodyDirector`).
- **Truthful Asset Provenance & Preview:**
  - **Renderable 3D Assets (`NIF`, `ARMO`, `FaceGeom`, `BodySlide`):** Rendered directly in `CharacterViewport.qml` via `SceneViewModel` (`selectArmor` for `ARMO` FormKeys, `selectMesh` for loose NIF paths).
  - **Renderable 2D Assets (`DDS`, `FaceTint`):** Decoded via `sns::textures::load_dds` (BC1/BC2/BC3/BC4/BC5/BC7/RGBA8) and displayed with dimensions, format, mipmaps, alpha channel status, and winning MO2 mod provider.
  - **Archive & Container Assets (`BSA`, `ESP`/`ESM`/`ESL`, `BodySlide .osp`):** Inspected with real metadata, record counts, archive entry lists, and winning MO2 provider.
  - **Non-Renderable Mod Files (`HKX`, `.pex`, `.seq`, `.fuz`/`.wav`):** Clearly badged as `UNSUPPORTED_PREVIEW` with explicit diagnostic reason, file size, relative VFS path, and winning MO2 mod. Never fake a visual preview.
  - **SMP / FSMP Physics:** Reported as static metadata badges (`SMP` / `hdtSMP64.dll` runtime check). Never claim real-time cloth/hair physics simulation in the viewport.

## 2. Token Mapping (`Theme.qml` & `WorkbenchState`)

All QML components consume the singleton `Theme.qml`, backed by `SettingsService` (`AppSettings.palette`) and `WorkbenchState` (`Workbench`):

| Design Token | `Theme.qml` Property | Role |
|---|---|---|
| `background` | `Theme.bg` | Root window background |
| `surface` | `Theme.surface` | 3D viewport & 2D texture canvas |
| `surface-container` | `Theme.panel` | Explorer, Inspector, and Bottom Console panels |
| `surface-container-high` | `Theme.panelAlt` | Cards, list hover state, elevated sections |
| `primary` | `Theme.accent` | Primary CTAs, active tabs, selected items |
| `outline` | `Theme.border` / `Theme.borderSubtle` | 1 px structural dividers |
| `on-surface` | `Theme.text` / `Theme.textDim` / `Theme.textMuted` | Primary, secondary, and caption text |
| `ok` / `warn` / `error` | `Theme.ok` / `Theme.warn` / `Theme.error` | Semantic validation & conflict badges |
| `body-md` / `body-sm` / `label-sm` | `Theme.normal` (13px) / `Theme.small` (11px) / `Theme.micro` (10px) | Scaled by `Theme.fs(base)` with legibility floor |
| `controlHeight` / `rowHeight` | `Theme.controlHeight` (32px / 28px compact) / `Theme.rowHeight` | Accessible hit targets across 85%–150% UI scale |

## 3. Core Screens & Component Translation

### 3.1. Explorador Universal de Recursos (`LooseResourceSection.qml` + `ArmorCatalog.qml`)
- **Dual Catalog Tabs:**
  - **Armaduras (`ARMO`):** Searchable, slot-filterable, SMP-filterable virtualized `ListView` of plugin armor records.
  - **Explorador de Recursos (`LooseResourceLibrary`):** Universal VFS + BSA + BodySlide asset browser with kind filters:
    - `Todos`, `Packs BodySlide`, `Mallas sin ESP` (`loose-mesh`), `Replacers ARMO` (`replacer-mesh`), `Armas/Objetos NIF` (`weapon-mesh`, `object-mesh`), `FaceGen` (`facegen-mesh`, `facegen-tint`), `Texturas DDS` (`texture-dds`), `Archivos BSA` (`bsa-archive`), and `Otros archivos` (`unsupported-file`).
- **Contract Separation:**
  - Clicking **Ver 3D** on any NIF or BodySlide pack emits `meshPreviewRequested(dataRelativePath)` -> `SceneViewModel::selectMesh(dataRelativePath)`.
  - Clicking **Ver 3D** on an `ARMO` record emits `previewRequested(formKey)` -> `SceneViewModel::selectArmor(formKey)`.
  - Clicking **Ver DDS** on a texture opens the integrated 2D Texture Inspector (`inspectDdsTexture(dataRelativePath)`), displaying a real decoded preview image, pixel dimensions, DXGI/FourCC format, mipmap count, alpha coverage, and winning MO2 mod.
  - For `replacer-mesh` items with a verified `armorFormKey` association, an **Equipar ARMO** action equips the linked record via `OutfitEditor::addPiece`. Pure `loose-mesh` items without an `ARMO` record remain non-equippable with an explicit badge (`Sin ARMO — Solo vista previa 3D`).

### 3.2. Visualizador 3D Avanzado (`CharacterViewport.qml` + `SceneViewModel`)
- **Camera & Framing:** Orbit, pan, zoom, piece auto-framing (`framePiece()`), and 1-click presets (`Cuerpo`, `Frontal`, `Lateral`, `Posterior`, `Rostro`).
- **Lighting & Background:** Neutral studio lighting slider (`lightBrightness`), rotatable light azimuth (`lightYaw`), and background selector (`Oscuro`, `Gris neutro`, `Claro`).
- **Submesh & Material Inspector:**
  - Displays `selectedMesh` / `selectedArmor` provenance (`NIF path`, `winning MO2 provider`, `ARMO FormKey`).
  - Lists every `NiTriShape` / `BSTriShape` submesh with individual visibility toggles (`setShapeVisible`), shader flags, alpha mode, and texture slots (`diffuse`, `normal`, `skin`, `specular`) with 1-click DDS inspection.
  - Displays **Diagnóstico de Slots y Cuerpo Cubierto** (`scene.outfitDiagnostics` and `scene.coveredBodyDiagnostics`) with 1-click piece visibility toggles (`setPieceVisible`).

### 3.3. Flujo Visual: NPC → Rostro → Cabello → Cuerpo → Vestuario → Inventario → Exportar (`CharacterCreator.qml` & `OutfitPanel.qml`)
- **Step Bar & Guided Workflow:**
  1. **NPC & Origen (`AppearanceSourceSection`):** Select NPC, inspect override chain and FaceGen provenance.
  2. **Rostro (`FaceCustomizer` & `PresetManager`):** Edit morph sliders, presets, skin tone, and RaceMenu presets.
  3. **Cabello y Ojos (`HairEyeCustomizer` & `HeadPartGallery`):** Virtualized gallery of hair, eyes, brows, scars, and beards with search and instant 3D preview.
  4. **Cuerpo (`BodyCustomizer` & `BodyPresetSection`):** Body weight, height, and BodySlide preset selection (`BodyDirector`).
  5. **Vestuario (`OutfitPanel`):** Equipped armor pieces, slot badges, preset library, and Compatibility Guardian report.
  6. **Inventario (`OutfitPanel` / `InventorySection`):** Inspect the NPC's base container inventory (`CNTO`) from the winning record and manage custom inventory items & quantities.
  7. **Exportar Mod (`ExportWizardDialog` / `ExportDialog`):** Staged atomic export via `BethesdaWorker` with `NpcSafePatch` semantic diff verification and ZIP packaging.

## 4. Responsive & Accessibility Guarantees
- **Resolutions:** Verified at `1100×700`, `1366×768`, `1920×1080`, and `150%` UI scale without text clipping or overlapping controls.
- **Virtualization:** All large catalogs (`NpcBrowser`, `ArmorCatalog`, `LooseResourceSection`, `HeadPartGallery`, `ModBrowser`) use virtualized `ListView` / `GridView` with `clip: true` and debounced filtering.
- **Accessibility:** Every interactive control and `ScrollBar` defines an explicit `Accessible.name` and keyboard focus ring (`Theme.focusWidth`).
