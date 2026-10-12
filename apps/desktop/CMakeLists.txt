find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Quick3D)
qt_standard_project_setup(REQUIRES 6.11)

qt_add_executable(SkyrimNpcStudio WIN32
  main.cpp
  components/viewport_verifier.cpp components/viewport_verifier.hpp
  viewmodels/json_bridge.hpp
  viewmodels/mo2_snapshot.hpp
  viewmodels/console_model.cpp viewmodels/console_model.hpp
  viewmodels/variant_list_model.cpp viewmodels/variant_list_model.hpp
  viewmodels/worker_client.cpp viewmodels/worker_client.hpp
  viewmodels/outfit_editor.cpp viewmodels/outfit_editor.hpp
  viewmodels/scene_viewmodel.cpp viewmodels/scene_viewmodel.hpp
  viewmodels/studio_controller.cpp viewmodels/studio_controller.hpp)

# Must be set BEFORE qt_add_qml_module, otherwise Theme is registered as a plain type.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(SkyrimNpcStudio
  URI SkyrimNpcStudio
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/CharacterViewport.qml
    qml/Theme.qml
    qml/WelcomeOverlay.qml
    qml/NpcBrowser.qml
    qml/ModBrowser.qml
    qml/ArmorCatalog.qml
    qml/NpcOverview.qml
    qml/OutfitPanel.qml
    qml/DependenciesPanel.qml
    qml/BottomPanel.qml
    qml/ExportDialog.qml
    qml/components/SectionHeader.qml
    qml/components/KeyValue.qml
    qml/components/IssueList.qml
    qml/components/StatusBadge.qml)

# The generated QML type registration includes view model headers by file name.
target_include_directories(SkyrimNpcStudio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/viewmodels)
target_compile_definitions(SkyrimNpcStudio PRIVATE SNS_VERSION="${PROJECT_VERSION}")
# qmlcachegen's AOT-compiled QML inlines Qt headers that trigger C4702 (unreachable code) in the
# compiler back end, where /external:W0 does not apply. Only this warning is relaxed, only here.
target_compile_options(SkyrimNpcStudio PRIVATE /wd4702)
target_link_libraries(SkyrimNpcStudio PRIVATE
  Qt6::Quick Qt6::QuickControls2 Qt6::Quick3D
  sns::ipc sns::mo2 sns::projects sns::outfit
  sns::rendering
  sns::compile_options)

if(SNS_BUILD_WORKER)
  add_dependencies(SkyrimNpcStudio bethesda_worker)
endif()

# Copies the Qt runtime next to the executable so it runs outside the dev shell:
#   cmake --build --preset debug --target deploy
find_program(SNS_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
if(SNS_WINDEPLOYQT)
  add_custom_target(deploy
    COMMAND ${SNS_WINDEPLOYQT} --qmldir ${CMAKE_CURRENT_SOURCE_DIR}/qml --no-translations
            --no-system-d3d-compiler $<TARGET_FILE:SkyrimNpcStudio>
    DEPENDS SkyrimNpcStudio
    COMMENT "Deploying Qt runtime with windeployqt")
endif()
