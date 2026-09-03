cmake_minimum_required(VERSION 3.22)
project(mx5dash LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enforce 16 KB ELF segment alignment for Android 15+ compliance
set(CMAKE_SHARED_LINKER_FLAGS
    "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384")

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
get_filename_component(MX5_UI "${REPO_ROOT}/lib/mx5_ui" ABSOLUTE)
get_filename_component(MX5_CONFIG "${REPO_ROOT}/lib/mx5_config" ABSOLUTE)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/android_bridge
    ${MX5_UI}
    ${MX5_CONFIG}
)

# Build LVGL 9 static library
set(LV_CONF_INCLUDE_SIMPLE ON)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON)
set(LV_CONF_BUILD_DISABLE_DEMOS ON)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl lvgl_build)
target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Engine sources
add_library(mx5_engine STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/android_ui/AndroidMx5UI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android_ui/mx5_rf_tpms_img.c
    ${CMAKE_CURRENT_SOURCE_DIR}/android_ui/mx5_rf_cal_img.c
    ${MX5_UI}/lv_font_mono_48.c
    ${MX5_UI}/lv_font_mono_96.c
    ${MX5_UI}/lv_font_mono_120.c
    ${MX5_CONFIG}/DtcDatabase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android_bridge/UserPrefs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android_bridge/SdLogger.cpp
)
target_include_directories(mx5_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/android_ui ${MX5_UI} ${MX5_CONFIG})
target_link_libraries(mx5_engine PUBLIC lvgl)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/android_ui/AndroidMx5UI.cpp
    PROPERTIES COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/lvgl_v9compat.h")

# Shared library exposed to Android JNI
add_library(mx5dash SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/NativeBridge.cpp
)
target_include_directories(mx5dash PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${MX5_UI} ${MX5_CONFIG})
target_link_libraries(mx5dash
    mx5_engine
    lvgl
    log
    android
)
