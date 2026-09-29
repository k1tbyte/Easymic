cmake_minimum_required(VERSION 3.27)

set(CMAKE_CXX_STANDARD 23)
project(EasyLauncher)

set(IS_DEBUG FALSE)
set(IS_RELEASE FALSE)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(IS_DEBUG TRUE)
else()
    set(IS_RELEASE TRUE)
endif()

if(IS_RELEASE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

add_subdirectory(vendor)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

find_program(POWERSHELL_EXECUTABLE NAMES pwsh powershell)
if(NOT POWERSHELL_EXECUTABLE)
    message(FATAL_ERROR "PowerShell is required for the architecture check")
endif()

set(ARCHITECTURE_CHECK_COMMAND
        "${POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-architecture.ps1"
        -Root "${CMAKE_CURRENT_SOURCE_DIR}")

add_custom_target(EasyLauncherArchitectureCheck ALL
        COMMAND ${ARCHITECTURE_CHECK_COMMAND}
        VERBATIM)


file(GLOB_RECURSE CPP_FILES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp")
file(GLOB_RECURSE HPP_FILES CONFIGURE_DEPENDS "${SRC_DIR}/*.hpp")


add_executable(${PROJECT_NAME} WIN32)
add_dependencies(${PROJECT_NAME} EasyLauncherArchitectureCheck)

# UNICODE makes every unsuffixed Win32 call resolve to its W variant, so passing a narrow
# string to one stops compiling instead of silently going through the system codepage.
target_compile_definitions(${PROJECT_NAME} PRIVATE
        NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00 UNICODE _UNICODE)


target_link_libraries(${PROJECT_NAME} PRIVATE
        ole32
        uuid
        oleacc
        delayimp
        winmm
        gdiplus
        comctl32
        version
        wininet
        taskschd
        shell32
        Shcore
        runtimeobject
        dwmapi
        glaze::glaze
)



set(RESOURCE "${CMAKE_SOURCE_DIR}/src/Resources/Resource.rc")

# rc.exe embeds the .ico files at compile time and CMake does not scan ICON statements -
# without this, a changed .ico never reaches the exe until the .rc itself changes
set_property(SOURCE ${RESOURCE} PROPERTY OBJECT_DEPENDS
        ${CMAKE_SOURCE_DIR}/src/Resources/appicon.ico
        ${CMAKE_SOURCE_DIR}/src/Resources/mic-muted.ico
        ${CMAKE_SOURCE_DIR}/src/Resources/mic-unmuted.ico
        ${CMAKE_SOURCE_DIR}/src/Resources/mic-active.ico
)

target_sources(${PROJECT_NAME} PRIVATE ${RESOURCE}
        ${CPP_FILES}
        ${HPP_FILES}
)

target_include_directories(${PROJECT_NAME} PUBLIC
        src
        src/Core
        src/Platform
        src/UI
)

target_compile_options(${PROJECT_NAME} PRIVATE
        /GS-                     # disable buffer security checks
        /GR-                     # disable RTTI
)

# Resource.rc carries the manifest; wininet and version are used once, oleacc only for password checks
target_link_options(${PROJECT_NAME} PRIVATE /MANIFEST:NO
        /DELAYLOAD:wininet.dll /DELAYLOAD:version.dll /DELAYLOAD:oleacc.dll)

if(IS_RELEASE)
    # /Gw + /OPT:REF drop unreferenced data and code, /OPT:ICF folds the identical
    # std::function and template instantiations.
    # /GL + /LTCG measured 10 KB larger here - cross-object inlining beats the size win.
    target_compile_options(${PROJECT_NAME} PRIVATE /Gw)
    target_link_options(${PROJECT_NAME} PRIVATE /OPT:REF /OPT:ICF)
endif()

# .\build.ps1 -Test builds unit_tests and runs them with ctest
enable_testing()
add_custom_target(unit_tests)

function(add_unit_test name)
    cmake_parse_arguments(TEST "" "" "SOURCES;INCLUDES;LIBS;LINK_OPTIONS" ${ARGN})
    add_executable(${name} EXCLUDE_FROM_ALL ${TEST_SOURCES})
    target_include_directories(${name} PRIVATE ${TEST_INCLUDES})
    target_compile_definitions(${name} PRIVATE NOMINMAX UNICODE _UNICODE)
    target_compile_options(${name} PRIVATE /utf-8)
    if(TEST_LIBS)
        target_link_libraries(${name} PRIVATE ${TEST_LIBS})
    endif()
    if(TEST_LINK_OPTIONS)
        target_link_options(${name} PRIVATE ${TEST_LINK_OPTIONS})
    endif()
    add_dependencies(unit_tests ${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unit_test(RouterTest
    SOURCES tests/Input/RouterTest.cpp src/Core/Input/Router.cpp src/Core/Input/Hold.cpp
    INCLUDES src/Core)

add_unit_test(TextLayoutTest
    SOURCES tests/Platform/TextLayoutTest.cpp
    INCLUDES src
    LIBS gdiplus)

add_unit_test(KeyboardConfigTest
    SOURCES tests/Keyboard/KeyboardConfigTest.cpp
    INCLUDES src src/Core src/Platform
    LIBS glaze::glaze)

add_unit_test(AutocorrectTest
    SOURCES tests/Keyboard/AutocorrectTest.cpp
            src/Features/Keyboard/Convert/Rules.cpp src/Features/Keyboard/Convert/Detector.cpp
            src/Features/Keyboard/Convert/Pack.cpp src/Features/Keyboard/Convert/LayoutTable.cpp
            src/Features/Keyboard/Convert/Alphabet.cpp tools/Keyboard/langpack/PackBuilder.cpp
    INCLUDES src tools/Keyboard)

add_unit_test(UserRulesTest
    SOURCES tests/Keyboard/UserRulesTest.cpp src/Features/Keyboard/Autocorrect/Rules/UserRules.cpp
    INCLUDES src)

add_unit_test(TypedWordTest
    SOURCES tests/Keyboard/TypedWordTest.cpp
    INCLUDES src)

add_unit_test(PackTest
    SOURCES tests/Keyboard/PackTest.cpp src/Features/Keyboard/Convert/Pack.cpp
            src/Features/Keyboard/Convert/Alphabet.cpp
    INCLUDES src src/Core src/Platform)

add_unit_test(CommandLineTest
    SOURCES tests/Launcher/CommandLineTest.cpp src/Features/Launcher/CommandLine.cpp
    INCLUDES src)

add_unit_test(TargetTest
    SOURCES tests/Desktops/TargetTest.cpp src/Features/Desktops/Placement/Target.cpp
    INCLUDES src)

add_unit_test(TilesTest
    SOURCES tests/Desktops/TilesTest.cpp src/Features/Desktops/Editor/Tiles.cpp
    INCLUDES src)

file(GLOB KEYBOARD_SETTINGS_FILES CONFIGURE_DEPENDS "${SRC_DIR}/Features/Keyboard/Settings/*.cpp")
add_unit_test(KeyboardUiTest
    SOURCES tests/Keyboard/KeyboardUiTest.cpp ${RESOURCE}
            src/UI/Settings/SettingsWindow.cpp src/UI/Settings/SettingsRows.cpp
            ${KEYBOARD_SETTINGS_FILES}
            src/Features/Keyboard/Autocorrect/Rules/UserRules.cpp src/Features/Keyboard/Layout/InputLanguage.cpp
            src/Features/Keyboard/Convert/Pack.cpp src/Features/Keyboard/Convert/Alphabet.cpp
            src/Platform/Windowing/Foreground.cpp src/Platform/Windowing/WindowCatalog.cpp src/Platform/System/Version.cpp
            src/Platform/Diagnostics/Logger.cpp
    INCLUDES src src/Core src/Platform src/UI
    LIBS comctl32 comdlg32 dwmapi gdiplus
    LINK_OPTIONS /MANIFEST:NO)

# Pack builder and detection console: .\build.ps1 -Tools
add_subdirectory(tools/Keyboard/langpack)

if(IS_DEBUG)
    # Desktop-wide keyboard/mouse hooks under a debugger freeze input on every breakpoint
    target_compile_definitions(${PROJECT_NAME} PRIVATE APP_NO_GLOBAL_HOOKS)
endif()

