# Sample plugin DLLs, built only with -DSC4RS_BUILD_SAMPLES=ON. Included from the main CMakeLists.

# ImGui sample DLL
set(IMGUI_SAMPLE_SOURCES
        ${SC4RS_ROOT}/src/sample/ImGuiSampleDirector.cpp
        ${SC4RS_ROOT}/src/utils/Logger.cpp
)

add_library(SC4ImGuiSample SHARED ${IMGUI_SAMPLE_SOURCES} ${SC4_COMMON_DIRECTOR_DEF})

target_include_directories(SC4ImGuiSample PRIVATE
        ${SC4RS_ROOT}/src
        ${SC4RS_ROOT}/src/service
        ${IMGUI_DIR}
)

target_link_libraries(SC4ImGuiSample PRIVATE
        gzcom2
        spdlog
)
sc4rs_use_imgui(SC4ImGuiSample)

set_target_properties(SC4ImGuiSample PROPERTIES
        OUTPUT_NAME "SC4ImGuiSample"
        SUFFIX ".dll"
        PREFIX ""
)


# ImGui city-only sample DLL
set(IMGUI_SAMPLE_CITY_SOURCES
        ${SC4RS_ROOT}/src/sample/ImGuiSampleCityDirector.cpp
        ${SC4RS_ROOT}/src/utils/Logger.cpp
)

add_library(SC4ImGuiSampleCity SHARED ${IMGUI_SAMPLE_CITY_SOURCES} ${SC4_COMMON_DIRECTOR_DEF})

target_include_directories(SC4ImGuiSampleCity PRIVATE
        ${SC4RS_ROOT}/src
        ${SC4RS_ROOT}/src/service
        ${IMGUI_DIR}
)

target_link_libraries(SC4ImGuiSampleCity PRIVATE
        gzcom2
        spdlog
)

sc4rs_use_imgui(SC4ImGuiSampleCity)

set_target_properties(SC4ImGuiSampleCity PROPERTIES
        OUTPUT_NAME "SC4ImGuiSampleCity"
        SUFFIX ".dll"
        PREFIX ""
)


# ImGui demo sample DLL
set(IMGUI_SAMPLE_DEMO_SOURCES
        ${SC4RS_ROOT}/src/sample/ImGuiSampleDemoDirector.cpp
        ${SC4RS_ROOT}/src/utils/Logger.cpp
)

add_library(SC4ImGuiSampleDemo SHARED ${IMGUI_SAMPLE_DEMO_SOURCES} ${SC4_COMMON_DIRECTOR_DEF})

target_include_directories(SC4ImGuiSampleDemo PRIVATE
        ${SC4RS_ROOT}/src
        ${SC4RS_ROOT}/src/service
        ${IMGUI_DIR}
)

target_link_libraries(SC4ImGuiSampleDemo PRIVATE
        gzcom2
        spdlog
)

sc4rs_use_imgui(SC4ImGuiSampleDemo)

set_target_properties(SC4ImGuiSampleDemo PROPERTIES
        OUTPUT_NAME "SC4ImGuiSampleDemo"
        SUFFIX ".dll"
        PREFIX ""
)


# ImGui texture sample DLL
set(IMGUI_TEXTURE_SAMPLE_SOURCES
        ${SC4RS_ROOT}/src/sample/ImGuiTextureSample.cpp
        ${SC4RS_ROOT}/src/utils/Logger.cpp
)

add_library(SC4ImGuiTextureSample SHARED ${IMGUI_TEXTURE_SAMPLE_SOURCES} ${SC4_COMMON_DIRECTOR_DEF})

target_include_directories(SC4ImGuiTextureSample PRIVATE
        ${SC4RS_ROOT}/src
        ${SC4RS_ROOT}/src/service
        ${IMGUI_DIR}
)

target_link_libraries(SC4ImGuiTextureSample PRIVATE
        gzcom2
        spdlog
)

sc4rs_use_imgui(SC4ImGuiTextureSample)

set_target_properties(SC4ImGuiTextureSample PROPERTIES
        OUTPUT_NAME "SC4ImGuiTextureSample"
        SUFFIX ".dll"
        PREFIX ""
)


# World Projection sample DLL
set(WORLD_PROJECTION_SAMPLE_SOURCES
        ${SC4RS_ROOT}/src/sample/WorldProjectionSampleDirector.cpp
        ${SC4RS_ROOT}/src/DX7InterfaceHook.cpp
        ${SC4RS_ROOT}/src/utils/Logger.cpp
)

add_library(SC4WorldProjectionSample SHARED ${WORLD_PROJECTION_SAMPLE_SOURCES} ${SC4_COMMON_DIRECTOR_DEF})

target_include_directories(SC4WorldProjectionSample PRIVATE
        ${SC4RS_ROOT}/src
        ${SC4RS_ROOT}/src/service
        ${IMGUI_DIR}
)

target_link_libraries(SC4WorldProjectionSample PRIVATE
        gzcom2
        spdlog
)

if(WIN32)
    target_link_libraries(SC4WorldProjectionSample PRIVATE gdiplus)
endif()

sc4rs_use_imgui(SC4WorldProjectionSample)

set_target_properties(SC4WorldProjectionSample PROPERTIES
        OUTPUT_NAME "SC4WorldProjectionSample"
        SUFFIX ".dll"
        PREFIX ""
)

# S3D Camera debug sample DLL
set(S3D_CAMERA_DEBUG_SAMPLE_SOURCES
        ${SC4RS_ROOT}/src/sample/S3DCameraDebugSampleDirector.cpp
        ${SC4RS_ROOT}/src/utils/Logger.cpp
)

add_library(SC4S3DCameraDebugSample SHARED ${S3D_CAMERA_DEBUG_SAMPLE_SOURCES} ${SC4_COMMON_DIRECTOR_DEF})

target_include_directories(SC4S3DCameraDebugSample PRIVATE
        ${SC4RS_ROOT}/src
        ${SC4RS_ROOT}/src/service
        ${IMGUI_DIR}
)

target_link_libraries(SC4S3DCameraDebugSample PRIVATE
        gzcom2
        spdlog
)

sc4rs_use_imgui(SC4S3DCameraDebugSample)

set_target_properties(SC4S3DCameraDebugSample PROPERTIES
        OUTPUT_NAME "SC4S3DCameraDebugSample"
        SUFFIX ".dll"
        PREFIX ""
)

# Overlay Manager sample DLL
set(OVERLAY_MANAGER_SAMPLE_SOURCES
        ${SC4RS_ROOT}/src/sample/OverlayManagerSampleDirector.cpp
        ${SC4RS_ROOT}/src/utils/Logger.cpp
)

add_library(SC4OverlayManagerSample SHARED ${OVERLAY_MANAGER_SAMPLE_SOURCES} ${SC4_COMMON_DIRECTOR_DEF})

target_include_directories(SC4OverlayManagerSample PRIVATE
        ${SC4RS_ROOT}/src
        ${SC4RS_ROOT}/src/service
        ${IMGUI_DIR}
)

target_link_libraries(SC4OverlayManagerSample PRIVATE
        gzcom2
        spdlog
)

sc4rs_use_imgui(SC4OverlayManagerSample)

set_target_properties(SC4OverlayManagerSample PROPERTIES
        OUTPUT_NAME "SC4OverlayManagerSample"
        SUFFIX ".dll"
        PREFIX ""
)

# Draw Service sample DLL
set(DRAW_SERVICE_SAMPLE_SOURCES
        ${SC4RS_ROOT}/src/sample/DrawServiceSampleDirector.cpp
        ${SC4RS_ROOT}/src/utils/Logger.cpp
)

add_library(SC4DrawServiceSample SHARED ${DRAW_SERVICE_SAMPLE_SOURCES} ${SC4_COMMON_DIRECTOR_DEF})

target_include_directories(SC4DrawServiceSample PRIVATE
        ${SC4RS_ROOT}/src
        ${SC4RS_ROOT}/src/service
        ${IMGUI_DIR}
)

target_link_libraries(SC4DrawServiceSample PRIVATE
        gzcom2
        spdlog
)

sc4rs_use_imgui(SC4DrawServiceSample)

set_target_properties(SC4DrawServiceSample PROPERTIES
        OUTPUT_NAME "SC4DrawServiceSample"
        SUFFIX ".dll"
        PREFIX ""
)

# Road Decal sample DLL
set(ROAD_DECAL_SAMPLE_SOURCES
        ${SC4RS_ROOT}/src/sample/road-decal/RoadDecalSampleDirector.cpp
        ${SC4RS_ROOT}/src/sample/road-decal/RoadDecalData.cpp
        ${SC4RS_ROOT}/src/sample/road-decal/RoadDecalInputControl.cpp
        ${SC4RS_ROOT}/src/utils/Logger.cpp
)

add_library(SC4RoadDecalSample SHARED ${ROAD_DECAL_SAMPLE_SOURCES} ${SC4_COMMON_DIRECTOR_DEF})

target_include_directories(SC4RoadDecalSample PRIVATE
        ${SC4RS_ROOT}/src
        ${SC4RS_ROOT}/src/service
        ${IMGUI_DIR}
)

target_link_libraries(SC4RoadDecalSample PRIVATE
        gzcom2
        spdlog
)

sc4rs_use_imgui(SC4RoadDecalSample)

set_target_properties(SC4RoadDecalSample PROPERTIES
        OUTPUT_NAME "SC4RoadDecalSample"
        SUFFIX ".dll"
        PREFIX ""
)

# World drawing sample DLL
set(WORLD_DRAW_SAMPLE_SOURCES
        ${SC4RS_ROOT}/src/sample/WorldDrawSampleDirector.cpp
        ${SC4RS_ROOT}/src/utils/Logger.cpp
)

add_library(SC4WorldDrawSample SHARED ${WORLD_DRAW_SAMPLE_SOURCES} ${SC4_COMMON_DIRECTOR_DEF})

target_include_directories(SC4WorldDrawSample PRIVATE
        ${SC4RS_ROOT}/src
        ${IMGUI_DIR}
)

target_link_libraries(SC4WorldDrawSample PRIVATE
        gzcom2
        spdlog
)

sc4rs_use_imgui(SC4WorldDrawSample)

set_target_properties(SC4WorldDrawSample PROPERTIES
        OUTPUT_NAME "SC4WorldDrawSample"
        SUFFIX ".dll"
        PREFIX ""
)

# Terrain Decal sample DLL
set(TERRAIN_DECAL_SAMPLE_SOURCES
        ${SC4RS_ROOT}/src/sample/TerrainDecalSampleDirector.cpp
        ${SC4RS_ROOT}/src/utils/Logger.cpp
)

add_library(SC4TerrainDecalSample SHARED ${TERRAIN_DECAL_SAMPLE_SOURCES} ${SC4_COMMON_DIRECTOR_DEF})

target_include_directories(SC4TerrainDecalSample PRIVATE
        ${SC4RS_ROOT}/src
        ${SC4RS_ROOT}/src/service
        ${IMGUI_DIR}
)

target_link_libraries(SC4TerrainDecalSample PRIVATE
        gzcom2
        spdlog
)

sc4rs_use_imgui(SC4TerrainDecalSample)

set_target_properties(SC4TerrainDecalSample PROPERTIES
        OUTPUT_NAME "SC4TerrainDecalSample"
        SUFFIX ".dll"
        PREFIX ""
)

# Camera View Input sample DLL
set(CAMERA_VIEW_INPUT_SAMPLE_SOURCES
        ${SC4RS_ROOT}/src/sample/camera-view-input/CameraViewInputSampleDirector.cpp
        ${SC4RS_ROOT}/src/sample/camera-view-input/CameraViewInputControl.cpp
        ${SC4RS_ROOT}/src/utils/Logger.cpp
)

add_library(SC4CameraViewInputSample SHARED ${CAMERA_VIEW_INPUT_SAMPLE_SOURCES} ${SC4_COMMON_DIRECTOR_DEF})

target_include_directories(SC4CameraViewInputSample PRIVATE
        ${SC4RS_ROOT}/src
        ${SC4RS_ROOT}/src/service
        ${IMGUI_DIR}
)

target_link_libraries(SC4CameraViewInputSample PRIVATE
        gzcom2
        spdlog
)

sc4rs_use_imgui(SC4CameraViewInputSample)

set_target_properties(SC4CameraViewInputSample PROPERTIES
        OUTPUT_NAME "SC4CameraViewInputSample"
        SUFFIX ".dll"
        PREFIX ""
)


# Date Jumper sample DLL (Ctrl+F9 to fast-forward to the next seasonal date)
set(DATE_JUMPER_SAMPLE_SOURCES
        ${SC4RS_ROOT}/src/sample/date-jumper/DateJumperDirector.cpp
        ${SC4RS_ROOT}/src/utils/Logger.cpp
)

add_library(SC4DateJumperSample SHARED ${DATE_JUMPER_SAMPLE_SOURCES} ${SC4_COMMON_DIRECTOR_DEF})

target_include_directories(SC4DateJumperSample PRIVATE
        ${SC4RS_ROOT}/src
)

target_link_libraries(SC4DateJumperSample PRIVATE
        gzcom2
        spdlog
)

set_target_properties(SC4DateJumperSample PROPERTIES
        OUTPUT_NAME "SC4DateJumperSample"
        SUFFIX ".dll"
        PREFIX ""
)

set(SC4RS_SAMPLE_TARGETS
        SC4ImGuiSample
        SC4ImGuiSampleCity
        SC4ImGuiSampleDemo
        SC4ImGuiTextureSample
        SC4WorldProjectionSample
        SC4S3DCameraDebugSample
        SC4OverlayManagerSample
        SC4DrawServiceSample
        SC4RoadDecalSample
        SC4WorldDrawSample
        SC4TerrainDecalSample
        SC4CameraViewInputSample
        SC4DateJumperSample
)

# SC4_INSTALL_DIR is only set when the main CMakeLists deploys to the Plugins folder.
if(SC4_INSTALL_DIR)
    foreach(sample IN LISTS SC4RS_SAMPLE_TARGETS)
        add_custom_command(TARGET ${sample} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${SC4_PLUGINS_DIR}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${sample}>
                "${SC4_PLUGINS_DIR}/"
                COMMENT "Deploying ${sample}.dll to Plugins folder"
        )
        message(STATUS "  - ${sample}.dll -> ${SC4_PLUGINS_DIR}/")
    endforeach()
endif()
