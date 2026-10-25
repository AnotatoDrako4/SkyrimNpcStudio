# INFORME DE ARQUITECTURA: MOTOR DE DIAGNÓSTICO DE VISIBILIDAD DE ATUENDOS Y PARTES DEL CUERPO

**Identificador:** `SNS-DOC-OUTFIT-VISIBILITY-DIAGNOSTICS-2026-10`  
**Fecha:** 2026-10-24  
**Objetivo:** Especificación del motor de diagnóstico de visibilidad de piezas de vestimenta, resolución de colisiones de slots y partes corporales cubiertas.  
**Estado:** `IMPLEMENTADO_Y_VERIFICADO_OK`

---

## 1. MOTIVACIÓN Y OBJETIVO

En Skyrim, el sistema de armaduras y atuendos opera mediante una máscara de bits de ranuras biped (`Biped Slots` del 30 al 61). Cuando se asigna un conjunto de armaduras (`OTFT`) a un NPC:
1. **Colisión de Slots de Atuendo:** Si dos piezas solicitadas compiten por la misma ranura (ej. dos guanteletes en el slot 33), el motor del juego equipa una y desequipa automáticamente la otra.
2. **Ocultamiento de Anatomía Base:** Cuando una prenda cubre una ranura corporal (ej. ranura 32 - Cuerpo, 33 - Manos, 37 - Pies), el juego oculta la malla de piel desnuda del NPC para evitar que atraviese la ropa (*clipping*).

**Problema Previo en la Aplicación:**
Las piezas desequipadas o las mallas corporales cubiertas eran descartadas de forma silenciosa en la capa C++. El usuario no tenía forma de saber:
- Si una pieza faltante no existía en disco o había sido desplazada por otra prenda con mayor prioridad.
- Qué prendas estaban cubriendo la piel del NPC y en qué ranuras exactas.
- No era posible alternar la visibilidad de prendas o anatomía cubierta en el Inspector 3D.

---

## 2. DISEÑO DEL MOTOR DE DIAGNÓSTICOS EN C++

En `engines/rendering/include/sns/rendering/scene.hpp` y `engines/rendering/src/scene.cpp`, se diseñó e implementó la infraestructura de diagnóstico estructurado:

### 2.1. Estructuras de Datos
```cpp
struct OutfitPieceDiagnostic {
    std::string name;
    std::string armor_form_key;
    uint32_t slot_mask{0};
    std::vector<int> slot_indices;
    bool equipped{false};
    std::string unequipped_by_form_key;
    std::string unequipped_by;
    std::vector<int> conflicting_slots;
    std::string reason;
    std::vector<std::string> mesh_paths;
    std::vector<std::string> mesh_providers;
    bool meshes_exist{false};
};

struct CoveredBodyDiagnostic {
    std::string piece_name;
    std::string addon_form_key;
    uint32_t slot_mask{0};
    std::vector<int> slots;
    std::string covered_by_piece;
    std::string covered_by_form_key;
    std::vector<int> covering_slots;
    std::string mesh_path;
    std::string mesh_provider;
    bool mesh_exists{false};
};
```

### 2.2. Preservación de Geometría con `hidden_reason`
En lugar de descartar las mallas no dibujadas:
1. Las piezas de atuendo en conflicto se cargan con `shown_by_default = false` y su razón de ocultamiento explícita:  
   `"unequipped due to slot collision on slot(s) 33 (Hands) with 1_CrimsonGloves"`.
2. Las partes del cuerpo cubiertas se conservan en `CharacterScene::parts` con `shown_by_default = false` y su razón:  
   `"covered by outfit (aaa_TemioShirtRed) on slot(s) 32 (Body)"`.
3. Esto mantiene la fidelidad visual idéntica a Skyrim de fábrica, pero otorga al Inspector 3D la capacidad de conmutar (`toggle`) cualquier pieza o forma a voluntad del usuario.

---

## 3. EXPOSICIÓN AL FRONTEND QML Y VIEWMODELS

En `apps/desktop/viewmodels/scene_viewmodel.hpp` y `.cpp`, se expusieron propiedades y métodos invokables para el visor QML:

```cpp
Q_PROPERTY(QVariantList outfitDiagnostics READ outfitDiagnostics NOTIFY outfitDiagnosticsChanged)
Q_PROPERTY(QVariantList coveredBodyDiagnostics READ coveredBodyDiagnostics NOTIFY coveredBodyDiagnosticsChanged)

Q_INVOKABLE QVariantMap explainPieceVisibility(const QString& armorFormKey) const;
Q_INVOKABLE QString explainShapeVisibility(const QString& shapeId) const;
```

### Funciones Auxiliares de Nomenclatura Biped:
Se implementaron formateadores deterministas según los estándares oficiales del Creation Kit:
- `slot_name(30)` -> `"Head"`
- `slot_name(32)` -> `"Body"`
- `slot_name(33)` -> `"Hands"`
- `slot_name(37)` -> `"Feet"`
- `slot_name(49)` -> `"Pelvis / Pants"`
- `describe_slots({32, 34, 38})` -> `"32 (Body), 34 (Forearms), 38 (Calves)"`

---

## 4. VALIDACIÓN CON PRUEBAS UNITARIAS Y DE ESTRÉS

1. **Prueba Unitaria C++:**
   En `tests/unit/engines/test_assets_rendering.cpp`, se verificó que:
   - Una armadura en conflicto de slot (ej. dos piezas en slot 32) genera un diagnóstico `equipped = false`, identifica a la pieza ganadora y preserva ambas en escena.
   - Las partes de piel cubiertas registran a la prenda que las cubre y sus ranuras correspondientes.
2. **Prueba en Vivo en MO2:**
   El escenario `outfit-diagnosis` ejecutado contra Serana generó un reporte completo de diagnóstico:
   - 6 piezas solicitadas, 6 piezas equipadas, 0 piezas desequipadas.
   - 3 diagnósticos de piel cubierta detectados y trazados a sus prendas correspondientes.
   - Todas las mallas y ranuras correctamente descritas y disponibles para el usuario.
