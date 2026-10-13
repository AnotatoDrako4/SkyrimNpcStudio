# Estado de las variantes de material

El editor puede guardar en el borrador `.snps` un color y rutas de texturas para una prenda y
mostrar sus valores en el panel. **No cambia todavía la malla del visor ni es exportable a Skyrim.** El
panel de material y Compatibility Guardian la identifican como `MaterialVariantPreviewOnly`.
Los modos A (atuendo) y C (NPC completo) rechazan la exportación mientras alguna pieza incluida
tenga una variante. Restablecer el material de la pieza permite exportar el atuendo sin ella.

## Motivo del bloqueo

El exportador anterior creaba un TXST con las rutas que escribió el usuario y clonaba ARMA/ARMO,
pero no generaba ni incorporaba DDS. La textura alternativa de ARMA usaba el EditorID del ARMA
como `Name` en lugar de comprobar el nombre de la forma del NIF. Un ESP releído por Mutagen podía
parecer válido aunque Skyrim siguiera mostrando la textura original o encontrara una ruta ausente.
Esa rama se retiró. Las pruebas de exportación comprueban que los modos A y C devuelven
`ValidationFailed`/`MaterialVariantPreviewOnly` y no publican un mod.

## Condiciones para habilitar una variante real

Una implementación posterior debe producir DDS propios válidos, comprobar sus hashes en staging,
resolver la malla efectiva de cada ARMA en MO2, identificar las formas y materiales que admite
Skyrim, aplicar una referencia de textura por forma y sexo, clonar solo los registros necesarios,
releer el ESP y los recursos del paquete, y comparar el resultado con la vista previa. También
debe separar los recursos compartidos para no recolorear otros NPC y comprobar permisos antes de
distribuir contenido de terceros. Los shaders o mallas que no admitan una equivalencia comprobable
seguirán siendo solo previsualizables.

Esta corrección evita una afirmación falsa de exportabilidad; **no** constituye aún el entregable
de recoloración comprobada dentro de Skyrim.

Verificación del 2026-10-13: 49 pruebas .NET, 84 pruebas unitarias C++, la integración sintética
de atuendo normal y el escenario `material-preview-block` en la distribución Release pasaron. El
escenario real utiliza a Lydia en el perfil MO2 activo y comprueba bloqueo, diagnóstico y
restauración. El atuendo original de ese perfil tiene conflictos de slots propios, por lo que la
prueba compara la exportabilidad antes y después de la solicitud de material.
