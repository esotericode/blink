cmake_minimum_required(VERSION 3.22)
# Normalize install destinations such as "." (the Windows portable root).
if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()
project(ConsoleObservatory VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)

# SameBoy's core relies on GNU C extensions (case ranges, statement expressions,
# __attribute__). MSVC's cl.exe cannot compile it; MinGW-w64 GCC can.
if(MSVC AND NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR
        "MSVC (cl.exe) cannot compile SameBoy's GNU C core. On Windows, configure "
        "with Qt's MinGW-w64 kit instead; see README.md, \"Build and run — Windows\".")
endif()

# A Windows install is a portable folder: the executable, its deployed Qt DLLs,
# and qt.conf sit together at the root. Linux keeps the FHS bin/ + share/ layout.
if(WIN32)
    set(CMAKE_INSTALL_BINDIR "." CACHE PATH "Executable directory inside the install prefix")
    set(OBSERVATORY_DATA_DIR ".")
else()
    set(OBSERVATORY_DATA_DIR "share/console-observatory")
endif()
include(GNUInstallDirs)
include(CTest)

option(OBSERVATORY_BUILD_GUI "Build the Qt desktop application and its widget tests" ON)
option(OBSERVATORY_STRICT_DEPENDENCIES "Require the reference Qt 6.4.2 version" OFF)
option(OBSERVATORY_DEPLOY_QT "Copy Qt runtime DLLs/plugins into a Windows install (needs windeployqt)" ${WIN32})

find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
if(OBSERVATORY_BUILD_GUI)
    if(OBSERVATORY_STRICT_DEPENDENCIES)
        find_package(Qt6 6.4.2 EXACT REQUIRED COMPONENTS Core Gui Widgets)
    else()
        find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
    endif()
endif()

set(observatory_warnings
    "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>"
    "$<$<CXX_COMPILER_ID:MSVC>:/W4>")

set(SAMEBOY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sameboy)
set(SAMEBOY_SOURCES apu camera display gb joypad mbc memory printer random
    rumble save_state sgb sm83_cpu timing workboy)
list(TRANSFORM SAMEBOY_SOURCES PREPEND ${SAMEBOY_DIR}/Core/)
list(TRANSFORM SAMEBOY_SOURCES APPEND .c)
add_library(sameboy STATIC ${SAMEBOY_SOURCES})
target_include_directories(sameboy SYSTEM PUBLIC ${SAMEBOY_DIR}/Core)
target_compile_definitions(sameboy PRIVATE GB_INTERNAL _GNU_SOURCE GB_VERSION="1.0.3" GB_COPYRIGHT_YEAR="2026")
# Public flags must match the layout used by every consumer of raw gb.h.
target_compile_definitions(sameboy PUBLIC GB_DISABLE_TIMEKEEPING GB_DISABLE_REWIND
    GB_DISABLE_DEBUGGER GB_DISABLE_CHEATS GB_DISABLE_CHEAT_SEARCH)
# Known-benign upstream diagnostics: four-character save-state block tags, and
# ELF visibility attributes that PE/COFF (Windows) ignores.
target_compile_options(sameboy PRIVATE
    "$<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-multichar>"
    "$<$<AND:$<BOOL:${WIN32}>,$<C_COMPILER_ID:GNU,Clang>>:-Wno-attributes>")
if(UNIX)
    target_link_libraries(sameboy PUBLIC m)
endif()

set(ROM_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(OUTPUT ${ROM_OUT}/teaching_rom.hpp ${ROM_OUT}/teaching.gb
    ${ROM_OUT}/boot.bin ${ROM_OUT}/teaching.sym ${ROM_OUT}/manifest.json
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/assemble_rom.py
        --source ${CMAKE_CURRENT_SOURCE_DIR}/rom --output ${ROM_OUT}
    DEPENDS tools/assemble_rom.py rom/teaching.asm rom/boot.asm VERBATIM)
add_library(observatory_engine src/emulator/engine.cpp src/emulator/disassembly.cpp
    src/emulator/graphics.cpp src/teaching/annotations.cpp src/teaching/lesson.cpp
    ${ROM_OUT}/teaching_rom.hpp)
target_include_directories(observatory_engine PUBLIC src ${ROM_OUT})
target_link_libraries(observatory_engine PRIVATE sameboy)
target_compile_options(observatory_engine PRIVATE ${observatory_warnings})

if(OBSERVATORY_BUILD_GUI)
    add_library(observatory_widgets src/ui/main_window.cpp src/ui/game_view.cpp
        src/ui/tile_view.cpp src/ui/activity_map.cpp src/ui/system_diagram.cpp src/ui/style.cpp)
    target_include_directories(observatory_widgets PUBLIC src)
    target_link_libraries(observatory_widgets PUBLIC observatory_engine Qt6::Widgets)
    target_compile_definitions(observatory_widgets PUBLIC OBSERVATORY_VERSION="${PROJECT_VERSION}")
    target_compile_options(observatory_widgets PRIVATE ${observatory_warnings})
    # Q_OBJECT widgets: since Qt 6.5, findChild<T>() requires T's meta-object.
    set_target_properties(observatory_widgets PROPERTIES AUTOMOC ON)
    add_executable(console-observatory src/main.cpp)
    target_link_libraries(console-observatory PRIVATE observatory_widgets)
    if(WIN32)
        # GUI subsystem (no console window), plus icon and version resources.
        set(OBSERVATORY_ICON ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/console-observatory.ico)
        configure_file(packaging/windows/console-observatory.rc.in
            ${CMAKE_CURRENT_BINARY_DIR}/console-observatory.rc @ONLY)
        target_sources(console-observatory PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/console-observatory.rc)
        set_target_properties(console-observatory PROPERTIES WIN32_EXECUTABLE ON)
    endif()
    install(TARGETS console-observatory RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(WIN32 AND OBSERVATORY_DEPLOY_QT)
        # windeployqt copies the Qt DLLs, platform/style plugins, compiler runtime,
        # and writes qt.conf, so the installed folder runs on a machine without Qt.
        if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
            qt_generate_deploy_app_script(TARGET console-observatory
                OUTPUT_SCRIPT observatory_deploy NO_UNSUPPORTED_PLATFORM_ERROR NO_TRANSLATIONS)
        else()
            qt_generate_deploy_app_script(TARGET console-observatory
                FILENAME_VARIABLE observatory_deploy NO_UNSUPPORTED_PLATFORM_ERROR)
        endif()
        # Qt 6.5+ deployment needs an absolute prefix; resolve "--prefix out/install"
        # the same way CMake installs files: relative to the invoking directory.
        install(CODE [[
            if(NOT QT_DEPLOY_PREFIX)
                get_filename_component(QT_DEPLOY_PREFIX "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}" ABSOLUTE)
            endif()
        ]])
        install(SCRIPT ${observatory_deploy})
    endif()
endif()

install(FILES README.md THIRD_PARTY_NOTICES.md DESTINATION ${OBSERVATORY_DATA_DIR})
install(FILES LICENSE DESTINATION ${OBSERVATORY_DATA_DIR}/licenses/project)
install(FILES third_party/sameboy/LICENSE DESTINATION ${OBSERVATORY_DATA_DIR}/licenses/sameboy)
# The Ubuntu Qt copyright file describes OS packages, which the Windows ZIP does not use.
if(WIN32)
    set(observatory_license_filter PATTERN "Qt6-*-Ubuntu-copyright.txt" EXCLUDE)
endif()
install(DIRECTORY licenses/ DESTINATION ${OBSERVATORY_DATA_DIR}/licenses ${observatory_license_filter})
install(DIRECTORY rom/ DESTINATION ${OBSERVATORY_DATA_DIR}/rom)
install(FILES tools/assemble_rom.py DESTINATION ${OBSERVATORY_DATA_DIR}/tools)
install(FILES ${ROM_OUT}/teaching.gb ${ROM_OUT}/teaching.sym ${ROM_OUT}/manifest.json
    DESTINATION ${OBSERVATORY_DATA_DIR}/rom)
if(UNIX AND NOT APPLE)
    install(FILES packaging/console-observatory.desktop DESTINATION share/applications)
    install(FILES packaging/console-observatory.svg DESTINATION share/icons/hicolor/scalable/apps)
endif()

if(BUILD_TESTING)
    add_executable(engine_tests tests/engine_tests.cpp)
    target_link_libraries(engine_tests PRIVATE observatory_engine)
    target_compile_options(engine_tests PRIVATE ${observatory_warnings})
    add_test(NAME engine COMMAND engine_tests)
    if(OBSERVATORY_BUILD_GUI)
        find_package(Qt6 REQUIRED COMPONENTS Test)
        add_executable(widget_tests tests/widget_tests.cpp)
        target_link_libraries(widget_tests PRIVATE observatory_widgets Qt6::Test)
        add_test(NAME widgets COMMAND widget_tests)
        set_tests_properties(widgets PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    add_test(NAME rom_reproducibility COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/rom_tests.py ${ROM_OUT})
    add_test(NAME vendor_integrity COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vendor_tests.py)
endif()

set(CPACK_PACKAGE_NAME console-observatory)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "Console Observatory contributors")
set(CPACK_PACKAGE_CONTACT "Console Observatory contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native Game Boy teaching lab with synchronized CPU and memory inspection")
if(WIN32)
    # Portable folder in a ZIP; the archive's top-level directory is the app.
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_FILE_NAME "console-observatory-${PROJECT_VERSION}-windows-x64")
else()
    # Ubuntu 24.04 package using the OS's dynamically linked Qt 6 runtime.
    set(CPACK_GENERATOR DEB)
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6t64 (>= 6.4.2), libqt6gui6t64 (>= 6.4.2), libqt6core6t64 (>= 6.4.2), qt6-qpa-plugins, libstdc++6, libgcc-s1, libc6 (>= 2.38)")
endif()
include(CPack)
