find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Quick3D Quick3DAssetUtils Test)
qt_standard_project_setup(REQUIRES 6.11)

qt_add_executable(SkyrimNpcStudio WIN32
  main.cpp
  components/viewport_verifier.cpp components/viewport_verifier.hpp
  components/appearance_stress.cpp components/appearance_stress.hpp
  components/forge_preview_check.cpp components/forge_preview_check.hpp
  diagnostics/crash_diagnostics.cpp diagnostics/crash_diagnostics.hpp
  viewmodels/json_bridge.hpp
  viewmodels/mo2_snapshot.hpp
  viewmodels/console_model.cpp viewmodels/console_model.hpp
  viewmodels/variant_list_model.cpp viewmodels/variant_list_model.hpp
  viewmodels/worker_client.cpp viewmodels/worker_client.hpp
  viewmodels/edit_history.cpp viewmodels/edit_history.hpp
  viewmodels/resource_context.cpp viewmodels/resource_context.hpp
  viewmodels/outfit_editor.cpp viewmodels/outfit_editor.hpp
  viewmodels/appearance_editor.cpp viewmodels/appearance_editor.hpp
  viewmodels/scene_viewmodel.cpp viewmodels/scene_viewmodel.hpp
  viewmodels/studio_controller.cpp viewmodels/studio_controller.hpp
  viewmodels/character_forge_viewmodel.cpp viewmodels/character_forge_viewmodel.hpp
  viewmodels/forge_asset_library.cpp viewmodels/forge_asset_library.hpp
  viewmodels/forge_blender_bridge.cpp viewmodels/forge_blender_bridge.hpp
  viewmodels/forge_scene_viewmodel.cpp viewmodels/forge_scene_viewmodel.hpp)

set_source_files_properties(${PROJECT_SOURCE_DIR}/tools/blender/forge_bridge.py PROPERTIES QT_RESOURCE_ALIAS forge_bridge.py)
qt_add_resources(SkyrimNpcStudio forge_bridge_script PREFIX "/forge"
  FILES ${PROJECT_SOURCE_DIR}/tools/blender/forge_bridge.py)

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
    qml/components/StatusBadge.qml
    qml/charactercreator/FaceSliderGroup.qml
    qml/charactercreator/FaceCustomizer.qml
    qml/charactercreator/PresetManager.qml
    qml/charactercreator/HairEyeCustomizer.qml
    qml/charactercreator/BodyCustomizer.qml
    qml/charactercreator/CharacterToolbar.qml
    qml/charactercreator/ExportWizardDialog.qml
    qml/charactercreator/CharacterCreator.qml
    qml/characterforge/AIGenerationPanel.qml
    qml/characterforge/ForgeAppearanceEditor.qml
    qml/characterforge/BlenderConnectionPanel.qml
    qml/characterforge/BlenderOperations.qml
    qml/characterforge/CharacterForge.qml
    qml/characterforge/CharacterInventory.qml
    qml/characterforge/CharacterProperties.qml
    qml/characterforge/ForgeAdvancedInspector.qml
    qml/characterforge/ForgeEditorSection.qml
    qml/characterforge/ForgeExportWizard.qml
    qml/characterforge/ForgeModelLibrary.qml
    qml/characterforge/ForgeNavigation.qml
    qml/characterforge/ForgeOutfitEditor.qml
    qml/characterforge/ForgePlacementEditor.qml
    qml/characterforge/ForgeRecordSelector.qml
    qml/characterforge/ForgeToolbar.qml
    qml/characterforge/ForgeValidationPanel.qml
    qml/characterforge/ForgeViewport.qml
    qml/characterforge/MaterialsEditor.qml
    qml/characterforge/ModelImporter.qml
    qml/characterforge/NativeNpcEditor.qml
    qml/characterforge/SkeletonEditor.qml
    qml/characterforge/StatsEditor.qml
  RESOURCES
    qml/shaders/alpha_channel.vert
    qml/shaders/alpha_channel.frag)

# The generated QML type registration includes view model headers by file name.
target_include_directories(SkyrimNpcStudio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/viewmodels)
target_compile_definitions(SkyrimNpcStudio PRIVATE SNS_VERSION="${PROJECT_VERSION}")
# qmlcachegen's AOT-compiled QML inlines Qt headers that trigger C4702 (unreachable code) in the
# compiler back end, where /external:W0 does not apply. Only this warning is relaxed, only here.
target_compile_options(SkyrimNpcStudio PRIVATE /wd4702)
target_link_libraries(SkyrimNpcStudio PRIVATE
  Qt6::Quick Qt6::QuickControls2 Qt6::Quick3D Qt6::Quick3DAssetUtils
  Qt6::Test  # QTest::mouseClick for the --forge-preview automation (platform input path)
  sns::ipc sns::mo2 sns::projects sns::creation sns::gltf sns::outfit
  sns::appearance sns::presets sns::face
  sns::rendering
  sns::compile_options
  dbghelp)

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
