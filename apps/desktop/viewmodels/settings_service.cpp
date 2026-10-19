#include "settings_service.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSysInfo>
#include <algorithm>
#include <cmath>
#include <functional>

#include "workbench_state.hpp"

namespace sns::app {

namespace {

constexpr int kSaveDelayMs = 400;

QVariantList opts(std::initializer_list<std::pair<const char*, const char*>> list) {
  QVariantList out;
  for (const auto& [value, label] : list) out.push_back(QVariantMap{{"value", QString::fromUtf8(value)}, {"label", QString::fromUtf8(label)}});
  return out;
}

SettingDescriptor make(const char* id, const char* category, const char* title, const char* description, const char* keywords,
                       const char* type, const char* scope, const char* apply, const char* backend, const char* handler) {
  SettingDescriptor d;
  d.id = QString::fromUtf8(id);
  d.category = QString::fromUtf8(category);
  d.title = QString::fromUtf8(title);
  d.description = QString::fromUtf8(description);
  d.keywords = QString::fromUtf8(keywords);
  d.type = QString::fromUtf8(type);
  d.scope = QString::fromUtf8(scope);
  d.apply = QString::fromUtf8(apply);
  d.backend = QString::fromUtf8(backend);
  d.handler = QString::fromUtf8(handler);
  return d;
}

// Key names a settings document must never carry (credentials of future integrations): the import refuses it.
bool looks_secret(const QString& key) {
  const auto k = key.toLower();
  for (const auto* word : {"password", "passwd", "secret", "token", "apikey", "api_key", "api-key", "credential"})
    if (k.contains(QLatin1String(word))) return true;
  return false;
}

struct Palette {
  const char *bg, *surface, *panel, *panel_alt, *control, *panel_header, *header, *border, *border_subtle;
  const char *text, *text_dim, *text_muted;
  const char *ok, *ok_dim, *warn, *warn_dim, *error, *error_dim;
};

// "graphite" is the original studio palette, value for value.
const Palette* palette_of(const QString& theme) {
  static const Palette graphite{"#16171a", "#1c1e22", "#22252a", "#282c33", "#282c33", "#1a1c20", "#181a1d", "#30343d", "#262930",
                                "#e1e4ea", "#8c94a2", "#616875", "#48b26e", "#1c3826", "#d49638", "#3d2e18", "#d9534f", "#3d1e1e"};
  static const Palette deep{"#0d0e10", "#111214", "#16171a", "#1d1f23", "#1d1f23", "#101113", "#0e0f11", "#272a30", "#1b1d21",
                            "#e6e8ec", "#959ca8", "#6a707b", "#4cbb74", "#173020", "#dc9e3e", "#36290f", "#e05c58", "#371a1a"};
  static const Palette steel{"#121821", "#161e28", "#1b2430", "#22303f", "#22303f", "#16202a", "#111720", "#2d3c4e", "#222e3c",
                             "#e3e9f1", "#93a4b9", "#66778c", "#4fbf7c", "#183526", "#d9a24a", "#3a2d15", "#e2615d", "#3b1d1f"};
  static const Palette contrast{"#000000", "#000000", "#0b0b0b", "#1c1c1c", "#141414", "#000000", "#000000", "#9a9a9a", "#6a6a6a",
                                "#ffffff", "#e0e0e0", "#bdbdbd", "#5fe891", "#0f2e1b", "#ffc24d", "#3a2a00", "#ff716b", "#3d0f0d"};
  if (theme == QLatin1String("deep")) return &deep;
  if (theme == QLatin1String("steel")) return &steel;
  if (theme == QLatin1String("contrast")) return &contrast;
  return &graphite;
}

QColor accent_of(const QString& accent) {
  if (accent == QLatin1String("teal")) return QColor("#2fa59a");
  if (accent == QLatin1String("violet")) return QColor("#8b72d6");
  if (accent == QLatin1String("amber")) return QColor("#cf8a2e");
  if (accent == QLatin1String("crimson")) return QColor("#cf5560");
  if (accent == QLatin1String("green")) return QColor("#4c9f5f");
  return QColor("#4382c4");
}

QColor mix(const QColor& a, const QColor& b, double t) {  // t of a, 1 - t of b
  return QColor::fromRgbF(float(a.redF() * t + b.redF() * (1 - t)), float(a.greenF() * t + b.greenF() * (1 - t)),
                          float(a.blueF() * t + b.blueF() * (1 - t)));
}

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

QJsonObject read_document(const QUrl& file, QString* error) {
  const auto path = file.isLocalFile() ? file.toLocalFile() : file.toString();
  QFile in(path);
  if (!in.open(QIODevice::ReadOnly)) {
    *error = QCoreApplication::translate("SettingsService", "No se pudo leer: %1").arg(in.errorString());
    return {};
  }
  if (in.size() > 1024 * 1024) {
    *error = QCoreApplication::translate("SettingsService", "El archivo es demasiado grande para ser una configuración");
    return {};
  }
  QJsonParseError parse{};
  const auto doc = QJsonDocument::fromJson(in.readAll(), &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
    *error = QCoreApplication::translate("SettingsService", "No es un JSON válido: %1").arg(parse.errorString());
    return {};
  }
  return doc.object();
}

}  // namespace

// ------------------------------------------------------------------------------ catalogue

std::vector<SettingDescriptor> SettingsService::build_registry() {
  std::vector<SettingDescriptor> r;
  // General
  {
    auto d = make("general.startWorkspace", "general", "Espacio de trabajo al iniciar",
                  "Qué espacio abre la aplicación: el último que usaste (se recuerda al cambiar de pestaña) o siempre uno fijo.",
                  "inicio arranque workspace pestaña startup", "enum", "global", "restart", "app", "Main.qml: workspaceTabs.Component.onCompleted");
    d.def = QStringLiteral("last");
    d.options = opts({{"last", "El último usado"}, {"creator", "Personalizar personaje"}, {"outfit", "Estudio de atuendos"}, {"forge", "Character Forge"}});
    r.push_back(d);
  }
  {
    auto d = make("general.confirmUnsavedClose", "general", "Confirmar al salir con cambios sin guardar",
                  "Al cerrar la ventana con un atuendo (.snps), un rostro editado o un proyecto Forge (.sncf) sin guardar, pregunta antes de salir.",
                  "cerrar salir guardar cambios dirty snps sncf", "bool", "global", "live", "app", "Main.qml: onClosing");
    d.def = true;
    r.push_back(d);
  }
  r.push_back(make("general.advancedMode", "general", "Modo avanzado",
                   "Muestra herramientas técnicas (diagnósticos, rutas y detalles de recursos). Se guarda con la ventana.",
                   "experto técnico avanzado advanced", "panel", "global", "live", "panel", "Theme.advancedMode (QtCore.Settings «window»)"));
  r.push_back(make("general.settingsTransfer", "general", "Exportar e importar configuración",
                   "Guarda los ajustes en un JSON versionado (sin rutas ni secretos) o los carga de uno con vista previa de los cambios.",
                   "exportar importar json copia backup respaldo", "panel", "global", "live", "panel", "SettingsService::exportTo / applyImport"));
  r.push_back(make("general.settingsDiff", "general", "Comparar con los valores predeterminados",
                   "Lista los ajustes que cambiaste, con su valor actual y el predeterminado, y permite restablecer uno solo.",
                   "diferencias predeterminado default comparar restablecer", "panel", "global", "live", "panel", "SettingsService::diffFromDefaults"));
  // Appearance
  {
    auto d = make("appearance.theme", "appearance", "Tema",
                  "Paleta de toda la interfaz (paneles, listas, menús, diálogos). No cambia texturas, modelos ni el visor 3D.",
                  "tema color oscuro contraste paleta theme dark", "enum", "global", "confirm", "app", "Theme.qml (paleta reactiva) + palette de Main.qml");
    d.def = QStringLiteral("graphite");
    d.options = opts({{"graphite", "Grafito oscuro (original)"}, {"deep", "Oscuro profundo"}, {"steel", "Azul acero"}, {"contrast", "Alto contraste"}});
    r.push_back(d);
  }
  {
    auto d = make("appearance.accent", "appearance", "Color de acento",
                  "Color de botones destacados, pestañas, foco, selección y deslizadores. Los colores de error, aviso y correcto no cambian.",
                  "acento color resaltado selección foco accent", "enum", "global", "confirm", "app", "Theme.qml: accent*, selection, borderFocus");
    d.def = QStringLiteral("steel");
    d.options = opts({{"steel", "Azul acero"}, {"teal", "Verde azulado"}, {"violet", "Violeta"}, {"amber", "Ámbar"}, {"crimson", "Carmesí"}, {"green", "Verde"}});
    r.push_back(d);
  }
  {
    auto d = make("appearance.uiScale", "appearance", "Escala de interfaz",
                  "Tamaño de controles, márgenes y paneles (Ctrl++ / Ctrl+- / Ctrl+0). Se suma a la escala de Windows.",
                  "escala zoom tamaño interfaz dpi scale", "int", "global", "confirm", "workbench", "WorkbenchState::uiScale -> Theme.uiFactor");
    d.def = 100; d.min = WorkbenchState::kMinUiScale; d.max = WorkbenchState::kMaxUiScale; d.step = 5; d.unit = QStringLiteral("%");
    r.push_back(d);
  }
  {
    auto d = make("appearance.fontScale", "appearance", "Tamaño del texto",
                  "Tamaño de la letra respecto a la escala de interfaz.", "letra fuente texto tamaño font size",
                  "int", "global", "confirm", "workbench", "WorkbenchState::fontScale -> Theme.fontFactor");
    d.def = 100; d.min = WorkbenchState::kMinFontScale; d.max = WorkbenchState::kMaxFontScale; d.step = 5; d.unit = QStringLiteral("%");
    r.push_back(d);
  }
  {
    auto d = make("appearance.density", "appearance", "Densidad",
                  "Espaciado de controles y listas.", "densidad espaciado compacto cómodo padding", "enum", "global", "live", "workbench",
                  "WorkbenchState::density -> Theme.densityFactor");
    d.def = QStringLiteral("normal");
    d.options = opts({{"compact", "Compacta"}, {"normal", "Normal"}, {"comfortable", "Cómoda"}});
    r.push_back(d);
  }
  {
    auto d = make("appearance.fontFamily", "appearance", "Fuente de interfaz",
                  "Familia tipográfica de toda la interfaz, entre las instaladas en Windows.", "fuente tipografía letra familia font family",
                  "font", "global", "live", "workbench", "WorkbenchState::fontFamily -> Theme.fontFamily");
    d.def = QString();
    r.push_back(d);
  }
  // Workspaces (page widgets over WorkbenchState layouts)
  r.push_back(make("workspace.panels", "workspace", "Paneles del espacio actual",
                   "Mostrar u ocultar exploración, propiedades y consola, y su tamaño, para cada espacio de trabajo por separado.",
                   "paneles panel ancho alto consola inspector explorador layout", "panel", "workspace", "live", "panel",
                   "WorkspaceLayout (WorkbenchState::layout) -> WorkbenchSplit"));
  r.push_back(make("workspace.builtinLayouts", "workspace", "Distribuciones predefinidas",
                   "Character Creator, edición facial, vestuario, exploración, visor maximizado o compacto, aplicadas al espacio actual.",
                   "distribución layout predefinido preset vista", "panel", "workspace", "live", "panel", "WorkbenchState::applyBuiltinLayout"));
  r.push_back(make("workspace.userLayouts", "workspace", "Distribuciones guardadas",
                   "Guarda la distribución de los tres espacios con un nombre, aplícala o bórrala.",
                   "guardar distribución layout usuario nombre", "panel", "global", "live", "panel", "WorkbenchState::saveLayout/applyLayout/deleteLayout"));
  // 3D viewer
  {
    auto d = make("viewport.background", "viewport", "Fondo del visor",
                  "Color de fondo del visor 3D. Solo cambia la vista: no se guarda en texturas ni en proyectos.",
                  "fondo color visor background clear", "enum", "global", "live", "app", "CharacterViewport.qml: SceneEnvironment.clearColor");
    d.def = QStringLiteral("graphite");
    d.options = opts({{"graphite", "Grafito (original)"}, {"black", "Negro"}, {"charcoal", "Carbón"}, {"grey", "Gris medio"}, {"slate", "Pizarra azul"}, {"light", "Gris claro"}});
    r.push_back(d);
  }
  {
    auto d = make("viewport.antiAliasing", "viewport", "Antialiasing (MSAA)",
                  "Suavizado de bordes del visor 3D con MSAA de Qt Quick 3D. Más calidad cuesta más GPU.",
                  "antialiasing msaa suavizado bordes calidad aa", "enum", "global", "live", "app",
                  "CharacterViewport.qml: SceneEnvironment.antialiasingMode/Quality");
    d.def = QStringLiteral("high");
    d.options = opts({{"off", "Desactivado"}, {"medium", "MSAA medio"}, {"high", "MSAA alto (original)"}, {"veryhigh", "MSAA muy alto"}});
    r.push_back(d);
  }
  {
    auto d = make("viewport.cameraFov", "viewport", "Campo de visión de la cámara",
                  "Ángulo vertical de la cámara del visor 3D.", "fov campo visión ángulo cámara perspectiva",
                  "int", "global", "live", "app", "CharacterViewport.qml: PerspectiveCamera.fieldOfView");
    d.def = 45; d.min = 25; d.max = 75; d.step = 1; d.unit = QStringLiteral("°");
    r.push_back(d);
  }
  {
    auto d = make("viewport.orbitSensitivity", "viewport", "Sensibilidad de órbita",
                  "Velocidad al girar la cámara con el botón derecho o central.", "órbita sensibilidad ratón velocidad girar cámara mouse",
                  "int", "global", "live", "app", "CharacterViewport.qml: OrbitCameraController.xSpeed/ySpeed");
    d.def = 100; d.min = 25; d.max = 300; d.step = 5; d.unit = QStringLiteral("%");
    r.push_back(d);
  }
  {
    auto d = make("viewport.invertOrbitX", "viewport", "Invertir órbita horizontal", "Invierte el giro horizontal de la cámara.",
                  "invertir eje x horizontal órbita", "bool", "global", "live", "app", "OrbitCameraController.xInvert");
    d.def = false;
    r.push_back(d);
  }
  {
    auto d = make("viewport.invertOrbitY", "viewport", "Invertir órbita vertical", "Invierte el giro vertical de la cámara.",
                  "invertir eje y vertical órbita", "bool", "global", "live", "app", "OrbitCameraController.yInvert");
    d.def = false;
    r.push_back(d);
  }
  {
    auto d = make("viewport.autoFitOnNpc", "viewport", "Encuadrar al cargar un NPC",
                  "Al terminar de cargar una escena la cámara vuelve al encuadre predeterminado. Desactivado conserva el encuadre actual.",
                  "encuadre recentrar cámara automático fit", "bool", "global", "live", "app", "CharacterViewport.qml: onReady");
    d.def = true;
    r.push_back(d);
  }
  {
    auto d = make("viewport.defaultCamera", "viewport", "Cámara predeterminada",
                  "Encuadre que se aplica al cargar una escena (si «Encuadrar al cargar» está activo) y al restablecer la cámara.",
                  "cámara encuadre rostro cuerpo frontal lateral", "enum", "global", "live", "app", "CharacterViewport.qml: applyCameraPreset");
    d.def = QStringLiteral("body");
    d.options = opts({{"body", "Cuerpo entero"}, {"front", "Frontal"}, {"side", "Lateral"}, {"back", "Posterior"}, {"face", "Rostro"}});
    r.push_back(d);
  }
  {
    auto d = make("viewport.lightIntensity", "viewport", "Intensidad de la iluminación",
                  "Multiplica el brillo de las tres luces del visor. No cambia materiales ni texturas.",
                  "luz iluminación brillo intensidad light", "int", "global", "live", "app", "CharacterViewport.qml: DirectionalLight.brightness");
    d.def = 100; d.min = 50; d.max = 160; d.step = 5; d.unit = QStringLiteral("%");
    r.push_back(d);
  }
  // NPC browser and portraits
  {
    auto d = make("npc.browserDefaultView", "npc", "Vista del explorador de NPC",
                  "Lista o galería de retratos. Se recuerda al cambiarla en el explorador.", "lista galería vista explorador npc gallery",
                  "enum", "global", "live", "app", "NpcBrowser.qml: viewMode");
    d.def = QStringLiteral("list");
    d.options = opts({{"list", "Lista"}, {"gallery", "Galería"}});
    r.push_back(d);
  }
  {
    auto d = make("npc.galleryCardSize", "npc", "Tamaño de las tarjetas de la galería",
                  "Ancho mínimo de cada tarjeta; el número de columnas se adapta al panel.", "tarjeta tamaño galería columnas miniatura retrato",
                  "enum", "global", "live", "app", "NpcBrowser.qml: GridView.columns");
    d.def = QStringLiteral("medium");
    d.options = opts({{"small", "Pequeña"}, {"medium", "Mediana (original)"}, {"large", "Grande"}});
    r.push_back(d);
  }
  {
    auto d = make("npc.donorView", "npc", "Vista de la búsqueda de donantes",
                  "Lista o galería de rostros en «Transferencia de apariencia» (pestaña Presets).", "donante galería rostros replacer transferencia",
                  "enum", "global", "live", "app", "PresetManager.qml: donorView");
    d.def = QStringLiteral("gallery");
    d.options = opts({{"list", "Lista"}, {"gallery", "Galería de rostros"}});
    r.push_back(d);
  }
  {
    auto d = make("npc.searchDebounceMs", "npc", "Espera de la búsqueda",
                  "Tiempo desde la última tecla hasta buscar. Más corto responde antes; más largo hace menos búsquedas al worker.",
                  "búsqueda espera debounce retardo teclear", "int", "global", "live", "app", "NpcBrowser.qml: debounce.interval");
    d.def = 200; d.min = 150; d.max = 500; d.step = 25; d.unit = QStringLiteral("ms");
    r.push_back(d);
  }
  {
    auto d = make("npc.portraitInstantCache", "npc", "Mostrar al instante los retratos ya generados",
                  "Una tarjeta muestra enseguida el retrato guardado en una sesión anterior, marcado «verificando», mientras se comprueba "
                  "contra el perfil; si está desactualizado se rehace y se reemplaza.",
                  "retrato caché instantáneo rápido cargar portrait cache", "bool", "mo2-profile", "live", "app",
                  "PortraitRenderer::instantCache (índice de caché leído al indexar)");
    d.def = true;
    r.push_back(d);
  }
  r.push_back(make("npc.portraitCache", "npc", "Caché de retratos de este perfil",
                   "Carpeta, número de retratos y espacio usado; vaciar borra solo los renders del estudio (las capturas de Skyrim se conservan).",
                   "caché retratos vaciar borrar espacio disco portrait", "panel", "mo2-profile", "live", "panel",
                   "PortraitRenderer::cacheUsage / clearRenders"));
  // Performance
  {
    auto d = make("performance.portraitPrefetchRows", "performance", "Filas de retratos preparadas por adelantado",
                  "Mientras no hay tarjetas visibles esperando, verifica y genera los retratos de las filas siguientes de la galería. 0 lo desactiva.",
                  "precarga prefetch adelantado retratos rendimiento filas", "int", "global", "live", "app", "NpcBrowser.qml -> PortraitRenderer::prefetch");
    d.def = 2; d.min = 0; d.max = 6; d.step = 1; d.unit = QStringLiteral("filas");
    r.push_back(d);
  }
  r.push_back(make("performance.portraitStats", "performance", "Medidas del motor de retratos",
                   "Cola, aciertos de caché, verificaciones en segundo plano, retratos precargados y tiempos medidos de esta sesión.",
                   "rendimiento medidas estadísticas retratos cola tiempos", "panel", "global", "live", "panel", "PortraitRenderer::stats"));
  // MO2 and system (read-only pages)
  r.push_back(make("mo2.status", "mo2", "Instancia y perfil de MO2",
                   "Instancia, perfil, juego, plugins y NPC indexados, tal como los leyó el estudio. Solo lectura: nada se escribe en MO2.",
                   "mo2 mod organizer perfil instancia plugins", "panel", "mo2-profile", "live", "panel", "StudioController (summary, instanceName)"));
  r.push_back(make("system.info", "system", "Información del sistema",
                   "Versión del estudio y de Qt, compilación, sistema operativo, arquitectura y API gráfica detectada.",
                   "sistema versión qt gpu acerca about", "panel", "global", "live", "panel", "SettingsService::systemInfo + GraphicsInfo"));
  return r;
}

QVariantList SettingsService::categories() const {
  return {QVariantMap{{"key", "general"}, {"title", tr("General")}},
          QVariantMap{{"key", "appearance"}, {"title", tr("Apariencia")}},
          QVariantMap{{"key", "workspace"}, {"title", tr("Espacios de trabajo")}},
          QVariantMap{{"key", "viewport"}, {"title", tr("Visor 3D")}},
          QVariantMap{{"key", "npc"}, {"title", tr("NPC y retratos")}},
          QVariantMap{{"key", "performance"}, {"title", tr("Rendimiento")}},
          QVariantMap{{"key", "mo2"}, {"title", tr("MO2 y recursos")}},
          QVariantMap{{"key", "system"}, {"title", tr("Sistema")}}};
}

QString SettingsService::category_title(const QString& key) const {
  for (const auto& c : categories())
    if (c.toMap().value("key").toString() == key) return c.toMap().value("title").toString();
  return key;
}

QVariantMap SettingsService::descriptor(const QString& id) const {
  const auto* d = find(id);
  if (!d) return {};
  return {{"id", d->id},           {"category", d->category},       {"categoryTitle", category_title(d->category)},
          {"title", d->title},     {"description", d->description}, {"type", d->type},
          {"scope", d->scope},     {"apply", d->apply},             {"backend", d->backend},
          {"handler", d->handler}, {"unit", d->unit},               {"default", d->def},
          {"min", d->min},         {"max", d->max},                 {"step", d->step},
          {"options", d->options}};
}

QVariantList SettingsService::registry() const {
  QVariantList out;
  for (const auto& d : registry_) out.push_back(descriptor(d.id));
  return out;
}

QVariantList SettingsService::settingsOf(const QString& category) const {
  QVariantList out;
  for (const auto& d : registry_)
    if (d.category == category) out.push_back(descriptor(d.id));
  return out;
}

const SettingDescriptor* SettingsService::find(const QString& id) const {
  const auto it = std::find_if(registry_.begin(), registry_.end(), [&](const SettingDescriptor& d) { return d.id == id; });
  return it == registry_.end() ? nullptr : &*it;
}

// ------------------------------------------------------------------------------ lifetime and storage

SettingsService::SettingsService(QObject* parent) : QObject(parent), settings_(std::make_unique<QSettings>()) { init(); }

SettingsService::SettingsService(const QString& ini_path, QObject* parent)
    : QObject(parent), settings_(std::make_unique<QSettings>(ini_path, QSettings::IniFormat)) {
  init();
}

SettingsService::~SettingsService() { flush(); }

void SettingsService::init() {
  registry_ = build_registry();
  save_timer_.setSingleShot(true);
  save_timer_.setInterval(kSaveDelayMs);
  connect(&save_timer_, &QTimer::timeout, this, &SettingsService::save);
  preview_timer_.setInterval(1000);
  connect(&preview_timer_, &QTimer::timeout, this, [this] {
    if (--preview_seconds_left_ <= 0) {
      revertPreview();
      return;
    }
    emit previewChanged();
  });
  load();
}

void SettingsService::load() {
  loading_ = true;
  for (const auto& d : registry_)
    if (d.backend == QLatin1String("app")) values_.insert(d.id, d.def);
  settings_->beginGroup(kGroup);
  const auto stored_version = settings_->value("schemaVersion", 0).toInt();
  if (stored_version > kSchemaVersion) {
    read_only_ = true;
    load_issues_ << tr("La configuración guardada es de una versión más nueva (esquema %1); se usan los valores predeterminados y no se "
                       "sobrescribe.").arg(stored_version);
  } else {
    for (const auto& key : settings_->childKeys()) {
      if (key == QLatin1String("schemaVersion")) continue;
      const auto* d = find(key);
      if (!d || d->backend != QLatin1String("app")) {
        load_issues_ << tr("Clave desconocida ignorada: %1").arg(key);
        continue;
      }
      QString error;
      if (const auto v = validate(*d, settings_->value(key), &error)) values_.insert(d->id, *v);
      else load_issues_ << tr("%1: valor guardado no válido (%2); se usa el predeterminado").arg(d->id, error);
    }
  }
  settings_->endGroup();
  loading_ = false;
}

void SettingsService::save() {
  save_timer_.stop();
  if (read_only_) return;
  settings_->beginGroup(kGroup);
  settings_->setValue("schemaVersion", kSchemaVersion);
  for (const auto& d : registry_) {
    if (d.backend != QLatin1String("app")) continue;
    // During a preview the stored theme stays the confirmed one until "keep".
    QVariant v = values_.value(d.id);
    if (preview_ && d.id == QLatin1String("appearance.theme")) v = preview_->theme;
    if (preview_ && d.id == QLatin1String("appearance.accent")) v = preview_->accent;
    if (v == d.def) settings_->remove(d.id);
    else settings_->setValue(d.id, v);
  }
  settings_->endGroup();
  settings_->sync();
}

void SettingsService::schedule_save() {
  if (!loading_) save_timer_.start();
}

void SettingsService::flush() {
  if (save_timer_.isActive()) save();
}

void SettingsService::set_workbench(WorkbenchState* workbench) {
  if (workbench_ == workbench) return;
  if (workbench_) disconnect(workbench_, nullptr, this, nullptr);
  workbench_ = workbench;
  if (workbench_) {
    connect(workbench_, &WorkbenchState::appearanceChanged, this, [this] {
      ++revision_;
      emit valuesChanged();
    });
  }
  ++revision_;
  emit workbenchChanged();
  emit valuesChanged();
}

// ------------------------------------------------------------------------------ values

std::optional<QVariant> SettingsService::validate(const SettingDescriptor& d, const QVariant& value, QString* error) const {
  const auto fail = [error](const QString& why) {
    if (error) *error = why;
    return std::optional<QVariant>{};
  };
  if (d.type == QLatin1String("bool")) {
    if (value.typeId() == QMetaType::Bool) return value.toBool();
    if (value.typeId() == QMetaType::QString) {  // INI files keep booleans as text
      const auto s = value.toString().trimmed().toLower();
      if (s == QLatin1String("true")) return true;
      if (s == QLatin1String("false")) return false;
    }
    return fail(tr("se esperaba sí/no"));
  }
  if (d.type == QLatin1String("int")) {
    bool ok = false;
    double n = 0;
    const auto t = value.typeId();
    if (t == QMetaType::Int || t == QMetaType::LongLong || t == QMetaType::Double || t == QMetaType::UInt || t == QMetaType::ULongLong) {
      n = value.toDouble(&ok);
    } else if (t == QMetaType::QString) {
      n = value.toString().trimmed().toDouble(&ok);
    }
    if (!ok || !std::isfinite(n)) return fail(tr("no es un número"));
    if (n != std::floor(n)) return fail(tr("debe ser un número entero"));
    if (n < d.min || n > d.max) return fail(tr("fuera del rango %1–%2").arg(d.min).arg(d.max));
    return static_cast<int>(n);
  }
  if (d.type == QLatin1String("enum")) {
    if (value.typeId() != QMetaType::QString) return fail(tr("se esperaba una opción"));
    const auto s = value.toString();
    for (const auto& o : d.options)
      if (o.toMap().value("value").toString() == s) return s;
    return fail(tr("opción desconocida «%1»").arg(s.left(40)));
  }
  if (d.type == QLatin1String("font")) {
    if (value.typeId() != QMetaType::QString) return fail(tr("se esperaba un nombre de fuente"));
    const auto s = value.toString().simplified();
    if (s.size() > 80 || s.contains(QLatin1Char('/')) || s.contains(QLatin1Char('\\'))) return fail(tr("nombre de fuente no válido"));
    return s;
  }
  return fail(tr("este ajuste no tiene un valor único"));
}

QString SettingsService::string_of(const QString& id) const { return values_.value(id).toString(); }
int SettingsService::int_of(const QString& id) const { return values_.value(id).toInt(); }
bool SettingsService::bool_of(const QString& id) const { return values_.value(id).toBool(); }

int SettingsService::gallery_card_width() const {
  const auto s = string_of("npc.galleryCardSize");
  return s == QLatin1String("small") ? 76 : s == QLatin1String("large") ? 132 : 92;
}

QColor SettingsService::viewport_background() const {
  const auto s = string_of("viewport.background");
  if (s == QLatin1String("black")) return QColor("#000000");
  if (s == QLatin1String("charcoal")) return QColor("#262626");
  if (s == QLatin1String("grey")) return QColor("#5a5d63");
  if (s == QLatin1String("slate")) return QColor("#1e2a38");
  if (s == QLatin1String("light")) return QColor("#b9bcc2");
  return QColor("#16171a");
}

int SettingsService::anti_aliasing() const {
  const auto s = string_of("viewport.antiAliasing");
  return s == QLatin1String("off") ? 0 : s == QLatin1String("medium") ? 1 : s == QLatin1String("veryhigh") ? 3 : 2;
}

int SettingsService::default_camera_preset() const {
  const auto s = string_of("viewport.defaultCamera");
  return s == QLatin1String("front") ? 1 : s == QLatin1String("side") ? 2 : s == QLatin1String("back") ? 3 : s == QLatin1String("face") ? 4 : 0;
}

QVariant SettingsService::workbench_value(const QString& id) const {
  if (!workbench_) return {};
  if (id == QLatin1String("appearance.uiScale")) return workbench_->ui_scale();
  if (id == QLatin1String("appearance.fontScale")) return workbench_->font_scale();
  if (id == QLatin1String("appearance.fontFamily")) return workbench_->font_family();
  if (id == QLatin1String("appearance.density")) {
    const int d = workbench_->density();
    return d == WorkbenchState::Compact ? QStringLiteral("compact") : d == WorkbenchState::Comfortable ? QStringLiteral("comfortable") : QStringLiteral("normal");
  }
  return {};
}

QString SettingsService::set_workbench_value(const QString& id, const QVariant& value) {
  if (!workbench_) return tr("Preferencias de ventana no disponibles");
  if (id == QLatin1String("appearance.uiScale")) workbench_->set_ui_scale(value.toInt());
  else if (id == QLatin1String("appearance.fontScale")) workbench_->set_font_scale(value.toInt());
  else if (id == QLatin1String("appearance.fontFamily")) workbench_->set_font_family(value.toString());
  else if (id == QLatin1String("appearance.density")) {
    const auto s = value.toString();
    workbench_->set_density(s == QLatin1String("compact") ? WorkbenchState::Compact
                            : s == QLatin1String("comfortable") ? WorkbenchState::Comfortable : WorkbenchState::Normal);
  } else {
    return tr("Ajuste desconocido: %1").arg(id);
  }
  return {};
}

QVariant SettingsService::value(const QString& id) const {
  const auto* d = find(id);
  if (!d) return {};
  if (d->backend == QLatin1String("workbench")) return workbench_value(id);
  if (d->backend == QLatin1String("app")) return values_.value(id);
  return {};
}

QVariant SettingsService::defaultValue(const QString& id) const {
  const auto* d = find(id);
  return d ? d->def : QVariant();
}

bool SettingsService::isDefault(const QString& id) const {
  const auto* d = find(id);
  if (!d || d->backend == QLatin1String("panel")) return true;
  const auto v = value(id);
  return !v.isValid() || v == d->def;
}

QString SettingsService::valueText(const QString& id) const {
  const auto* d = find(id);
  if (!d) return {};
  const auto v = value(id);
  if (d->type == QLatin1String("bool")) return v.toBool() ? tr("Sí") : tr("No");
  if (d->type == QLatin1String("enum")) {
    for (const auto& o : d->options)
      if (o.toMap().value("value") == v) return o.toMap().value("label").toString();
  }
  if (d->type == QLatin1String("font")) return v.toString().isEmpty() ? tr("Predeterminada (Segoe UI)") : v.toString();
  if (d->type == QLatin1String("int")) return QStringLiteral("%1 %2").arg(v.toInt()).arg(d->unit).trimmed();
  return v.toString();
}

void SettingsService::set_internal(const SettingDescriptor& d, const QVariant& value) {
  if (values_.value(d.id) == value) return;
  values_.insert(d.id, value);
  ++revision_;
  if (d.id == QLatin1String("appearance.theme") || d.id == QLatin1String("appearance.accent")) emit themeChanged();
  emit valuesChanged();
  emit settingChanged(d.id);
  schedule_save();
}

QString SettingsService::setValue(const QString& id, const QVariant& value) {
  const auto* d = find(id);
  if (!d) return tr("Ajuste desconocido: %1").arg(id);
  if (d->backend == QLatin1String("panel")) return tr("«%1» se cambia desde su página").arg(d->title);
  QString error;
  const auto v = validate(*d, value, &error);
  if (!v) return tr("%1: %2").arg(d->title, error);
  if (d->backend == QLatin1String("workbench")) return set_workbench_value(id, *v);
  set_internal(*d, *v);
  return {};
}

void SettingsService::resetSetting(const QString& id) {
  const auto* d = find(id);
  if (!d || d->backend == QLatin1String("panel")) return;
  setValue(id, d->def);
}

void SettingsService::resetCategory(const QString& category) {
  for (const auto& d : registry_)
    if (d.category == category) resetSetting(d.id);
}

void SettingsService::resetInterface() {
  resetSetting("appearance.theme");
  resetSetting("appearance.accent");
  if (workbench_) workbench_->resetAppearance();  // scale, text, density, font and the zone fonts it keeps
}

void SettingsService::resetAll() {
  for (const auto& d : registry_)
    if (d.backend == QLatin1String("app")) resetSetting(d.id);
  if (workbench_) workbench_->resetAll();
}

int SettingsService::changed_count() const {
  int n = 0;
  for (const auto& d : registry_)
    if (d.backend != QLatin1String("panel") && !isDefault(d.id)) ++n;
  return n;
}

QVariantList SettingsService::diffFromDefaults() const {
  QVariantList out;
  for (const auto& d : registry_) {
    if (d.backend == QLatin1String("panel") || isDefault(d.id)) continue;
    QString def_text;
    if (d.type == QLatin1String("enum")) {
      for (const auto& o : d.options)
        if (o.toMap().value("value") == d.def) def_text = o.toMap().value("label").toString();
    } else if (d.type == QLatin1String("bool")) {
      def_text = d.def.toBool() ? tr("Sí") : tr("No");
    } else if (d.type == QLatin1String("font")) {
      def_text = tr("Predeterminada (Segoe UI)");
    } else {
      def_text = QStringLiteral("%1 %2").arg(d.def.toInt()).arg(d.unit).trimmed();
    }
    out.push_back(QVariantMap{{"id", d.id}, {"title", d.title}, {"categoryTitle", category_title(d.category)},
                              {"current", valueText(d.id)}, {"default", def_text}, {"scope", d.scope}});
  }
  return out;
}

// ------------------------------------------------------------------------------ search

QString SettingsService::fold(const QString& text) {
  const auto decomposed = text.normalized(QString::NormalizationForm_D);
  QString out;
  out.reserve(decomposed.size());
  for (const auto ch : decomposed) {
    if (ch.category() == QChar::Mark_NonSpacing) continue;
    out.append(ch.toLower());
  }
  return out;
}

QVariantList SettingsService::search(const QString& text) const {
  const auto words = fold(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (words.isEmpty()) return {};
  struct Hit {
    int score;
    size_t order;
    QVariantMap row;
  };
  std::vector<Hit> hits;
  for (size_t i = 0; i < registry_.size(); ++i) {
    const auto& d = registry_[i];
    const auto title = fold(d.title), keywords = fold(d.keywords + QLatin1Char(' ') + d.id), description = fold(d.description),
               category = fold(category_title(d.category));
    int score = 0;
    QString match;
    bool all = true;
    for (const auto& w : words) {
      QString where;
      if (title.contains(w)) { score += 3; where = QStringLiteral("title"); }
      else if (keywords.contains(w)) { score += 2; where = QStringLiteral("keywords"); }
      else if (category.contains(w)) { score += 2; where = QStringLiteral("category"); }
      else if (description.contains(w)) { score += 1; where = QStringLiteral("description"); }
      else { all = false; break; }
      if (match.isEmpty()) match = where;
    }
    if (!all) continue;
    hits.push_back({score, i,
                    {{"id", d.id}, {"category", d.category}, {"categoryTitle", category_title(d.category)}, {"title", d.title},
                     {"description", d.description}, {"match", match}, {"apply", d.apply}, {"backend", d.backend}}});
  }
  std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score != b.score ? a.score > b.score : a.order < b.order; });
  QVariantList out;
  for (const auto& h : hits) out.push_back(h.row);
  return out;
}

// ------------------------------------------------------------------------------ preview

void SettingsService::beginPreview() {
  if (!preview_) {
    Preview p;
    p.theme = theme();
    p.accent = accent();
    if (workbench_) {
      p.ui_scale = workbench_->ui_scale();
      p.font_scale = workbench_->font_scale();
      p.density = workbench_->density();
      p.font_family = workbench_->font_family();
    }
    preview_ = p;
  }
  preview_seconds_left_ = kPreviewSeconds;
  preview_timer_.start();
  emit previewChanged();
}

void SettingsService::keepPreview() {
  if (!preview_) return;
  preview_.reset();
  preview_timer_.stop();
  preview_seconds_left_ = 0;
  save();  // the previewed theme becomes the stored one now
  emit previewChanged();
}

void SettingsService::revertPreview() {
  if (!preview_) return;
  const auto p = *preview_;
  preview_.reset();
  preview_timer_.stop();
  preview_seconds_left_ = 0;
  setValue("appearance.theme", p.theme);
  setValue("appearance.accent", p.accent);
  if (workbench_) {
    workbench_->set_ui_scale(p.ui_scale);
    workbench_->set_font_scale(p.font_scale);
    workbench_->set_density(p.density);
    workbench_->set_font_family(p.font_family);
  }
  save();
  emit previewChanged();
}

// ------------------------------------------------------------------------------ export / import

QJsonObject SettingsService::export_document() const {
  QJsonObject values;
  for (const auto& d : registry_) {
    if (d.backend == QLatin1String("panel")) continue;
    const auto v = value(d.id);
    if (!v.isValid()) continue;
    values.insert(d.id, QJsonValue::fromVariant(v));
  }
  QJsonObject doc{{"format", kExportFormat}, {"schemaVersion", kSchemaVersion}, {"settings", values}};
  if (workbench_) {
    QJsonArray layouts;
    for (const auto& id : WorkbenchState::kWorkspaces) {
      const auto* l = workbench_->layout(id);
      if (!l) continue;
      const auto& v = l->values();
      layouts.append(QJsonObject{{"workspace", id},             {"left", v.left_visible},     {"right", v.right_visible},
                                 {"bottom", v.bottom_visible},  {"maximized", v.maximized},   {"leftWidth", v.left_width},
                                 {"rightWidth", v.right_width}, {"bottomHeight", v.bottom_height}});
    }
    doc.insert("layouts", layouts);
  }
  return doc;
}

QString SettingsService::exportTo(const QUrl& file) const {
  const auto path = file.isLocalFile() ? file.toLocalFile() : file.toString();
  if (path.isEmpty()) return tr("Sin archivo de destino");
  QSaveFile out(path);
  const auto bytes = QJsonDocument(export_document()).toJson(QJsonDocument::Indented);
  if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) return tr("No se pudo escribir: %1").arg(out.errorString());
  return {};
}

QVariantMap SettingsService::preview_document(const QJsonObject& doc) const {
  const auto fail = [](const QString& why) { return QVariantMap{{"ok", false}, {"error", why}, {"changes", QVariantList{}}}; };
  if (doc.value("format").toString() != kExportFormat) return fail(tr("No es una configuración de Skyrim NPC Studio"));
  const int version = doc.value("schemaVersion").toInt(-1);
  if (version < 1) return fail(tr("Falta la versión del esquema"));
  if (version > kSchemaVersion)
    return fail(tr("Configuración de una versión más nueva (esquema %1); esta versión entiende hasta el %2").arg(version).arg(kSchemaVersion));
  // Anything that looks like a credential refuses the whole document: settings never carry secrets.
  std::function<QString(const QJsonObject&)> secret_in = [&](const QJsonObject& o) -> QString {
    for (auto it = o.begin(); it != o.end(); ++it) {
      if (looks_secret(it.key())) return it.key();
      if (it->isObject()) {
        if (auto k = secret_in(it->toObject()); !k.isEmpty()) return k;
      }
    }
    return {};
  };
  if (const auto key = secret_in(doc); !key.isEmpty()) return fail(tr("El documento contiene una clave de credenciales («%1»): rechazado").arg(key));
  QVariantList changes;
  const auto values = doc.value("settings").toObject();
  for (auto it = values.begin(); it != values.end(); ++it) {
    const auto* d = find(it.key());
    if (!d || d->backend == QLatin1String("panel")) return fail(tr("Ajuste desconocido: %1").arg(it.key()));
    QString error;
    const auto v = validate(*d, it->toVariant(), &error);
    if (!v) return fail(tr("%1: %2").arg(it.key(), error));
    if (value(d->id) != *v) changes.push_back(QVariantMap{{"id", d->id}, {"title", d->title}, {"from", valueText(d->id)}, {"to", v->toString()}});
  }
  const auto layouts = doc.value("layouts");
  if (!layouts.isUndefined() && !layouts.isArray()) return fail(tr("«layouts» debe ser una lista"));
  for (const auto& l : layouts.toArray()) {
    const auto o = l.toObject();
    if (!WorkbenchState::kWorkspaces.contains(o.value("workspace").toString())) return fail(tr("Distribución de un espacio desconocido"));
    for (const auto* k : {"leftWidth", "rightWidth", "bottomHeight"})
      if (!o.value(QLatin1String(k)).isDouble()) return fail(tr("Distribución sin tamaños numéricos"));
    changes.push_back(QVariantMap{{"id", "layout." + o.value("workspace").toString()},
                                  {"title", tr("Distribución de «%1»").arg(o.value("workspace").toString())},
                                  {"from", QString()},
                                  {"to", tr("paneles y tamaños del archivo")}});
  }
  return {{"ok", true}, {"error", QString()}, {"changes", changes}};
}

QVariantMap SettingsService::previewImport(const QUrl& file) const {
  QString error;
  const auto doc = read_document(file, &error);
  if (!error.isEmpty()) return {{"ok", false}, {"error", error}, {"changes", QVariantList{}}};
  return preview_document(doc);
}

QVariantMap SettingsService::apply_document(const QJsonObject& doc) {
  const auto check = preview_document(doc);  // the whole document is valid before anything changes
  if (!check.value("ok").toBool()) return {{"ok", false}, {"error", check.value("error")}, {"applied", 0}};
  // Snapshot for a rollback if any single apply fails.
  const auto before_values = values_;
  const auto before_doc = export_document();
  int applied = 0;
  QString failure;
  const auto values = doc.value("settings").toObject();
  for (auto it = values.begin(); it != values.end() && failure.isEmpty(); ++it) {
    QString ignored;
    const auto* d = find(it.key());
    const auto v = d ? validate(*d, it->toVariant(), &ignored) : std::nullopt;
    if (v && value(it.key()) == *v) continue;
    failure = setValue(it.key(), it->toVariant());
    if (failure.isEmpty()) ++applied;
  }
  if (failure.isEmpty() && workbench_) {
    for (const auto& l : doc.value("layouts").toArray()) {
      const auto o = l.toObject();
      auto* layout = workbench_->layout(o.value("workspace").toString());
      if (!layout) continue;
      LayoutValues v = layout->defaults();
      v.left_visible = o.value("left").toBool(v.left_visible);
      v.right_visible = o.value("right").toBool(v.right_visible);
      v.bottom_visible = o.value("bottom").toBool(v.bottom_visible);
      v.maximized = o.value("maximized").toBool(v.maximized);
      v.left_width = o.value("leftWidth").toInt(v.left_width);
      v.right_width = o.value("rightWidth").toInt(v.right_width);
      v.bottom_height = o.value("bottomHeight").toInt(v.bottom_height);
      layout->set_values(v);  // clamped by WorkspaceLayout
      ++applied;
    }
  }
  if (!failure.isEmpty()) {
    for (auto it = before_values.begin(); it != before_values.end(); ++it)
      if (const auto* d = find(it.key())) set_internal(*d, it.value());
    const auto old = before_doc.value("settings").toObject();
    for (auto it = old.begin(); it != old.end(); ++it)
      if (const auto* d = find(it.key()); d && d->backend == QLatin1String("workbench")) set_workbench_value(it.key(), it->toVariant());
    return {{"ok", false}, {"error", tr("Importación revertida: %1").arg(failure)}, {"applied", 0}};
  }
  return {{"ok", true}, {"error", QString()}, {"applied", applied}};
}

QVariantMap SettingsService::applyImport(const QUrl& file) {
  QString error;
  const auto doc = read_document(file, &error);
  if (!error.isEmpty()) return {{"ok", false}, {"error", error}, {"applied", 0}};
  return apply_document(doc);
}

// ------------------------------------------------------------------------------ system and palettes

QVariantMap SettingsService::systemInfo() const {
  return {
#ifdef SNS_VERSION
      {"appVersion", QStringLiteral(SNS_VERSION)},
#else
      {"appVersion", QCoreApplication::applicationVersion()},
#endif
      {"qtRuntime", QString::fromLatin1(qVersion())},
      {"qtBuild", QStringLiteral(QT_VERSION_STR)},
#ifdef QT_DEBUG
      {"buildType", QStringLiteral("Debug")},
#else
      {"buildType", QStringLiteral("Release")},
#endif
      {"os", QSysInfo::prettyProductName()},
      {"kernel", QSysInfo::kernelType() + QLatin1Char(' ') + QSysInfo::kernelVersion()},
      {"cpuArchitecture", QSysInfo::currentCpuArchitecture()},
      {"buildAbi", QSysInfo::buildAbi()},
      {"settingsStorage", settings_->format() == QSettings::IniFormat ? tr("archivo INI") : tr("registro de Windows")},
      {"loadIssues", load_issues_}};
}

QStringList SettingsService::theme_ids() { return {"graphite", "deep", "steel", "contrast"}; }
QStringList SettingsService::accent_ids() { return {"steel", "teal", "violet", "amber", "crimson", "green"}; }

QVariantMap SettingsService::palette_for(const QString& theme, const QString& accent_id) {
  const auto* p = palette_of(theme);
  const QColor panel(p->panel), bg(p->bg);
  const QColor accent = accent_of(accent_id);
  const bool contrast = theme == QLatin1String("contrast");
  QVariantMap m{{"bg", p->bg},           {"surface", p->surface},          {"panel", p->panel},     {"panelAlt", p->panel_alt},
                {"control", p->control}, {"panelHeader", p->panel_header}, {"header", p->header},   {"border", p->border},
                {"borderSubtle", p->border_subtle}, {"text", p->text},     {"textDim", p->text_dim}, {"textMuted", p->text_muted},
                {"ok", p->ok},           {"okDim", p->ok_dim},             {"warn", p->warn},       {"warnDim", p->warn_dim},
                {"error", p->error},     {"errorDim", p->error_dim}};
  // Colors of the Qt Quick Controls palette (Fusion). Graphite keeps main.cpp's original dark_palette() exactly.
  if (theme == QLatin1String("graphite")) {
    m.insert({{"qtWindow", "#1f2227"}, {"qtBase", "#17191d"}, {"qtAlternateBase", "#22252b"}, {"qtButton", "#2a2e35"},
              {"qtText", "#dcdfe4"}, {"qtMid", "#33373e"}, {"qtDark", "#121417"}, {"qtPlaceholder", "#808690"},
              {"qtDisabled", "#6b7078"}, {"qtHighlight", accent_id == QLatin1String("steel") ? QStringLiteral("#4f8cc9") : hex(accent_of(accent_id))}});
  } else {
    m.insert({{"qtWindow", hex(mix(panel, bg, 0.6))}, {"qtBase", p->surface}, {"qtAlternateBase", p->panel_alt}, {"qtButton", p->panel_alt},
              {"qtText", p->text}, {"qtMid", p->border}, {"qtDark", p->bg}, {"qtPlaceholder", p->text_dim},
              {"qtDisabled", p->text_muted}, {"qtHighlight", hex(contrast ? accent.lighter(130) : accent)}});
  }
  if (theme == QLatin1String("graphite") && accent_id == QLatin1String("steel")) {
    // The original palette, unchanged.
    // White on #4382c4 is 4.0:1 (kept: it is the studio's identity; documented in SETTINGS_CENTER.md).
    m.insert({{"accent", "#4382c4"}, {"accentHover", "#5595db"}, {"accentPressed", "#376da6"}, {"accentDim", "#1d3248"},
              {"accentSubtle", "#264382c4"}, {"accentBorder", "#3b6fa7"}, {"selection", "#2f4f72"}, {"borderFocus", "#4382c4"},
              {"onAccent", "#ffffff"}});
    return m;
  }
  const QColor base = contrast ? accent.lighter(130) : accent;
  QColor subtle = base;
  subtle.setAlpha(0x26);
  // Text on the accent (highlighted buttons, selected rows): white or near black, whichever reads better.
  const QColor dark("#101214");
  const auto on_accent = contrastRatio(Qt::white, base) >= contrastRatio(dark, base) ? QStringLiteral("#ffffff") : hex(dark);
  m.insert({{"onAccent", on_accent},
            {"accent", hex(base)},
            {"accentHover", hex(base.lighter(115))},
            {"accentPressed", hex(base.darker(118))},
            {"accentDim", hex(mix(base, bg, 0.28))},
            {"accentSubtle", subtle.name(QColor::HexArgb)},
            {"accentBorder", hex(base.darker(112))},
            {"selection", hex(mix(base, panel, contrast ? 0.55 : 0.42))},
            {"borderFocus", contrast ? QStringLiteral("#ffffff") : hex(base)}});
  return m;
}

double SettingsService::contrastRatio(const QColor& a, const QColor& b) {
  const auto lum = [](const QColor& c) {
    const auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
  };
  const double la = lum(a), lb = lum(b);
  return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

}  // namespace sns::app
