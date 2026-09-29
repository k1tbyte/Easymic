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

execute_process(
        COMMAND ${ARCHITECTURE_CHECK_COMMAND}
        RESULT_VARIABLE ARCHITECTURE_CHECK_RESULT
)
if(NOT ARCHITECTURE_CHECK_RESULT EQUAL 0)
    message(FATAL_ERROR "architecture check failed")
endif()

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

# Resource.rc carries the manifest
target_link_options(${PROJECT_NAME} PRIVATE /MANIFEST:NO)

if(IS_RELEASE)
    # /Gw + /OPT:REF drop unreferenced data and code, /OPT:ICF folds the identical
    # std::function and template instantiations.
    # /GL + /LTCG measured 10 KB larger here - cross-object inlining beats the size win.
    target_compile_options(${PROJECT_NAME} PRIVATE /Gw)
    target_link_options(${PROJECT_NAME} PRIVATE /OPT:REF /OPT:ICF)
endif()

# The input router's rules without a hook: .\build.ps1 -Test
add_executable(RouterTest EXCLUDE_FROM_ALL tests/Input/RouterTest.cpp src/Core/Input/Router.cpp
               src/Core/Input/Hold.cpp)
target_include_directories(RouterTest PRIVATE src/Core)
target_compile_definitions(RouterTest PRIVATE NOMINMAX UNICODE _UNICODE)

add_executable(KeyboardConfigTest EXCLUDE_FROM_ALL tests/Keyboard/KeyboardConfigTest.cpp)
target_include_directories(KeyboardConfigTest PRIVATE src src/Core src/Platform)
target_compile_definitions(KeyboardConfigTest PRIVATE NOMINMAX UNICODE _UNICODE)
target_link_libraries(KeyboardConfigTest PRIVATE glaze::glaze)

add_executable(AutocorrectTest EXCLUDE_FROM_ALL tests/Keyboard/AutocorrectTest.cpp
               src/Features/Keyboard/Convert/Rules.cpp src/Features/Keyboard/Convert/Detector.cpp
               src/Features/Keyboard/Convert/Pack.cpp src/Features/Keyboard/Convert/LayoutTable.cpp
               src/Features/Keyboard/Convert/Alphabet.cpp
               tools/Keyboard/langpack/PackBuilder.cpp tools/Keyboard/langpack/Console.cpp)
target_include_directories(AutocorrectTest PRIVATE src tools/Keyboard)
target_compile_definitions(AutocorrectTest PRIVATE NOMINMAX UNICODE _UNICODE)
target_compile_options(AutocorrectTest PRIVATE /utf-8)

add_executable(UserRulesTest EXCLUDE_FROM_ALL tests/Keyboard/UserRulesTest.cpp
               src/Features/Keyboard/Autocorrect/UserRules.cpp)
target_include_directories(UserRulesTest PRIVATE src)
target_compile_definitions(UserRulesTest PRIVATE NOMINMAX UNICODE _UNICODE)
target_compile_options(UserRulesTest PRIVATE /utf-8)

file(GLOB KEYBOARD_SETTINGS_FILES CONFIGURE_DEPENDS "${SRC_DIR}/Features/Keyboard/Settings/*.cpp")
add_executable(KeyboardUiTest EXCLUDE_FROM_ALL tests/Keyboard/KeyboardUiTest.cpp ${RESOURCE}
               src/UI/Settings/SettingsWindow.cpp src/UI/Settings/SettingsRows.cpp
               src/Features/Keyboard/KeyboardPage.cpp ${KEYBOARD_SETTINGS_FILES}
               src/Features/Keyboard/Autocorrect/UserRules.cpp src/Features/Keyboard/InputLanguage.cpp
               src/Features/Keyboard/Convert/Pack.cpp src/Features/Keyboard/Convert/Alphabet.cpp
               src/Platform/Foreground.cpp src/Platform/WindowCatalog.cpp src/Platform/Version.cpp)
target_include_directories(KeyboardUiTest PRIVATE src src/Core src/Platform src/UI)
target_compile_definitions(KeyboardUiTest PRIVATE NOMINMAX UNICODE _UNICODE)
target_compile_options(KeyboardUiTest PRIVATE /utf-8)
target_link_options(KeyboardUiTest PRIVATE /MANIFEST:NO)
target_link_libraries(KeyboardUiTest PRIVATE comctl32 comdlg32 dwmapi gdiplus)

# Pack builder and detection console: .\build.ps1 -Tools
add_subdirectory(tools/Keyboard/langpack)

if(IS_DEBUG)
    # Desktop-wide keyboard/mouse hooks under a debugger freeze input on every breakpoint
    target_compile_definitions(${PROJECT_NAME} PRIVATE APP_NO_GLOBAL_HOOKS)
endif()

