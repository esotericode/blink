cmake_minimum_required(VERSION 3.22)
project(ConsoleObservatory VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
include(CTest)
find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
option(OBSERVATORY_STRICT_DEPENDENCIES "Require the reference Qt 6.4.2 version" OFF)
if(OBSERVATORY_STRICT_DEPENDENCIES)
    find_package(Qt6 6.4.2 EXACT REQUIRED COMPONENTS Core Gui Widgets)
else()
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
endif()

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
    src/teaching/annotations.cpp ${ROM_OUT}/teaching_rom.hpp)
target_include_directories(observatory_engine PUBLIC src ${ROM_OUT})
target_link_libraries(observatory_engine PRIVATE sameboy)
target_compile_options(observatory_engine PRIVATE -Wall -Wextra -Wpedantic)

add_library(observatory_widgets src/ui/main_window.cpp src/ui/game_view.cpp)
target_include_directories(observatory_widgets PUBLIC src)
target_link_libraries(observatory_widgets PUBLIC observatory_engine Qt6::Widgets)
target_compile_options(observatory_widgets PRIVATE -Wall -Wextra -Wpedantic)
add_executable(console-observatory src/main.cpp)
target_link_libraries(console-observatory PRIVATE observatory_widgets)
install(TARGETS console-observatory RUNTIME DESTINATION bin)
install(FILES README.md THIRD_PARTY_NOTICES.md
    DESTINATION share/console-observatory)
install(FILES LICENSE DESTINATION share/console-observatory/licenses/project)
install(FILES third_party/sameboy/LICENSE DESTINATION share/console-observatory/licenses/sameboy)
install(DIRECTORY licenses/ DESTINATION share/console-observatory/licenses)
install(DIRECTORY rom/ DESTINATION share/console-observatory/rom)
install(FILES tools/assemble_rom.py DESTINATION share/console-observatory/tools)
install(FILES ${ROM_OUT}/teaching.gb ${ROM_OUT}/teaching.sym ${ROM_OUT}/manifest.json
    DESTINATION share/console-observatory/rom)
install(FILES packaging/console-observatory.desktop DESTINATION share/applications)
install(FILES packaging/console-observatory.svg DESTINATION share/icons/hicolor/scalable/apps)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(engine_tests tests/engine_tests.cpp)
    target_link_libraries(engine_tests PRIVATE observatory_engine)
    add_test(NAME engine COMMAND engine_tests)
    add_executable(widget_tests tests/widget_tests.cpp)
    target_link_libraries(widget_tests PRIVATE observatory_widgets Qt6::Test)
    add_test(NAME widgets COMMAND widget_tests)
    set_tests_properties(widgets PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME rom_reproducibility COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/rom_tests.py ${ROM_OUT})
    add_test(NAME vendor_integrity COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/vendor_tests.py)
endif()

# First packaging target: Ubuntu 24.04's dynamically linked Qt 6 runtime.
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME console-observatory)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "Console Observatory contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native Game Boy teaching lab with synchronized CPU and memory inspection")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6t64 (>= 6.4.2), libqt6gui6t64 (>= 6.4.2), libqt6core6t64 (>= 6.4.2), qt6-qpa-plugins, libstdc++6, libgcc-s1, libc6 (>= 2.38)")
include(CPack)
