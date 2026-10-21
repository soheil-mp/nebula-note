cmake_minimum_required(VERSION 3.21)

project(NebulaNote VERSION 1.1.0 DESCRIPTION "Nebula Note — notes, docs and databases in deep space" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(NEBULA_APP_ID "io.github.soheil_mp.NebulaNote")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Svg Sql Network Test)
find_package(ZLIB REQUIRED)
find_package(KF6WindowSystem 6.0 QUIET)

qt_standard_project_setup()

# ---- core: data model, storage, import/export (no widgets) -----------------
file(GLOB NEBULA_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.h)
add_library(nebula_core STATIC ${NEBULA_CORE_SOURCES})
target_include_directories(nebula_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(nebula_core PUBLIC
    NEBULA_VERSION="${PROJECT_VERSION}"
    NEBULA_APP_ID="${NEBULA_APP_ID}")
target_link_libraries(nebula_core PUBLIC Qt6::Core Qt6::Gui Qt6::Sql ZLIB::ZLIB)

# ---- ui: everything with a widget --------------------------------------------
file(GLOB NEBULA_UI_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/editor/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/views/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/views/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.h)
add_library(nebula_ui STATIC ${NEBULA_UI_SOURCES})
target_link_libraries(nebula_ui PUBLIC nebula_core Qt6::Widgets Qt6::Svg Qt6::Network)

include(GNUInstallDirs)
# Video embeds play inside pages through a plugin built on Qt WebEngine
# (qt6-webengine-dev), loaded the first time a video plays: the app itself
# doesn't link the browser engine. Without it videos open in the browser.
target_compile_definitions(nebula_ui PRIVATE NEBULA_PLUGIN_DIR="${CMAKE_INSTALL_FULL_LIBDIR}/nebula-note")
find_package(Qt6 QUIET OPTIONAL_COMPONENTS WebEngineWidgets)
if(Qt6WebEngineWidgets_FOUND)
    message(STATUS "Qt WebEngine found: building the video player plugin")
    add_library(nebula-webplayer MODULE src/plugins/webplayer/WebPlayerPlugin.cpp)
    target_link_libraries(nebula-webplayer PRIVATE Qt6::WebEngineWidgets Qt6::Widgets)
    set_target_properties(nebula-webplayer PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    add_dependencies(nebula_ui nebula-webplayer)
    install(TARGETS nebula-webplayer LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/nebula-note)
else()
    message(STATUS "Qt WebEngine not found: video embeds open in the browser (install qt6-webengine-dev to play them in pages)")
endif()

if(KF6WindowSystem_FOUND)
    message(STATUS "KF6WindowSystem found: native KWin shadows and blur enabled")
    target_link_libraries(nebula_ui PUBLIC KF6::WindowSystem)
    target_compile_definitions(nebula_ui PUBLIC NEBULA_HAVE_KWINDOWSYSTEM)
else()
    message(STATUS "KF6WindowSystem not found: using portable fallbacks")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(t nebula_core nebula_ui)
        target_compile_options(${t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endforeach()
endif()

file(GLOB NEBULA_FONTS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/*.otf
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/*.ttf)
# Small images the stylesheet uses (dropdown and submenu arrows, check marks).
file(GLOB NEBULA_UI_IMAGES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/ui/*.svg)

qt_add_executable(nebula-note WIN32 src/main.cpp)
target_link_libraries(nebula-note PRIVATE nebula_ui)
qt_add_resources(nebula-note "nebula_assets"
    PREFIX "/"
    BASE resources
    FILES ${NEBULA_FONTS} ${NEBULA_UI_IMAGES} resources/icons/app/nebula.svg)

install(TARGETS nebula-note RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES resources/icons/app/nebula.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME ${NEBULA_APP_ID}.svg)

# ---- tests -------------------------------------------------------------------
enable_testing()
qt_add_executable(nebula_tests tests/test_core.cpp)
target_link_libraries(nebula_tests PRIVATE nebula_core Qt6::Test)
add_test(NAME nebula_tests COMMAND nebula_tests)
set_tests_properties(nebula_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;NEBULA_NO_NOTIFY=1")

qt_add_executable(nebula_db_tests tests/test_db.cpp)
target_link_libraries(nebula_db_tests PRIVATE nebula_core Qt6::Test)
add_test(NAME nebula_db_tests COMMAND nebula_db_tests)
set_tests_properties(nebula_db_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;NEBULA_NO_NOTIFY=1")

qt_add_executable(nebula_io_tests tests/test_io.cpp)
target_link_libraries(nebula_io_tests PRIVATE nebula_core Qt6::Test)
add_test(NAME nebula_io_tests COMMAND nebula_io_tests)
set_tests_properties(nebula_io_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;NEBULA_NO_NOTIFY=1")

qt_add_executable(nebula_editor_tests tests/test_editor.cpp)
target_link_libraries(nebula_editor_tests PRIVATE nebula_ui Qt6::Test)
qt_add_resources(nebula_editor_tests "nebula_test_assets"
    PREFIX "/"
    BASE resources
    FILES ${NEBULA_FONTS} ${NEBULA_UI_IMAGES} resources/icons/app/nebula.svg)
add_test(NAME nebula_editor_tests COMMAND nebula_editor_tests)
set_tests_properties(nebula_editor_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;NEBULA_NO_NOTIFY=1")

qt_add_executable(nebula_blocks_tests tests/test_blocks.cpp)
target_link_libraries(nebula_blocks_tests PRIVATE nebula_ui Qt6::Test)
qt_add_resources(nebula_blocks_tests "nebula_test_assets"
    PREFIX "/"
    BASE resources
    FILES ${NEBULA_FONTS} ${NEBULA_UI_IMAGES} resources/icons/app/nebula.svg)
add_test(NAME nebula_blocks_tests COMMAND nebula_blocks_tests)
set_tests_properties(nebula_blocks_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;NEBULA_NO_NOTIFY=1")

qt_add_executable(nebula_dbui_tests tests/test_dbui.cpp)
target_link_libraries(nebula_dbui_tests PRIVATE nebula_ui Qt6::Test)
qt_add_resources(nebula_dbui_tests "nebula_dbui_test_assets"
    PREFIX "/"
    BASE resources
    FILES ${NEBULA_FONTS} ${NEBULA_UI_IMAGES} resources/icons/app/nebula.svg)
add_test(NAME nebula_dbui_tests COMMAND nebula_dbui_tests)
set_tests_properties(nebula_dbui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;NEBULA_NO_NOTIFY=1")

qt_add_executable(nebula_app_tests tests/test_app.cpp)
target_link_libraries(nebula_app_tests PRIVATE nebula_ui Qt6::Test)
qt_add_resources(nebula_app_tests "nebula_app_test_assets"
    PREFIX "/"
    BASE resources
    FILES ${NEBULA_FONTS} ${NEBULA_UI_IMAGES} resources/icons/app/nebula.svg)
add_test(NAME nebula_app_tests COMMAND nebula_app_tests)
set_tests_properties(nebula_app_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;NEBULA_NO_NOTIFY=1")
