cmake_minimum_required(VERSION 3.27)

set(CMAKE_CXX_STANDARD 23)
project(EasyLauncher)

option(ENABLE_UPX_COMPRESSION "Enable UPX compression for Release builds" ON)

set(IS_DEBUG FALSE)
set(IS_RELEASE FALSE)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(IS_DEBUG TRUE)
else()
    set(IS_RELEASE TRUE)
endif()


if(MSVC)
    if(IS_RELEASE)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
    endif()
endif()

add_subdirectory(vendor)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(WIN32)
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
endif()


file(GLOB_RECURSE CPP_FILES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp")
file(GLOB_RECURSE HPP_FILES CONFIGURE_DEPENDS "${SRC_DIR}/*.hpp")


add_executable(${PROJECT_NAME} #[[WIN32]])

if(WIN32)
    add_dependencies(${PROJECT_NAME} EasyLauncherArchitectureCheck)
endif()

if(WIN32)
    # Win10 API surface (GetDpiForWindow etc) - MinGW headers gate these behind _WIN32_WINNT.
    # UNICODE makes every unsuffixed Win32 call resolve to its W variant, so passing a narrow
    # string to one stops compiling instead of silently going through the system codepage.
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00 UNICODE _UNICODE)
endif()


target_link_libraries(${PROJECT_NAME} PRIVATE
        ole32
        uuid
        wsock32
        ws2_32
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

if(MINGW)
    target_compile_options(${PROJECT_NAME} PRIVATE
            -ffunction-sections
            -fdata-sections
            -fno-rtti
    )

    if(IS_RELEASE)
        set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)

        target_link_options(${PROJECT_NAME} PRIVATE
                -static
                -static-libgcc
                -static-libstdc++
                -Wl,--allow-multiple-definition
                -Wl,--gc-sections
                -Wl,--strip-all
                -s
                -Wl,-Bstatic,--whole-archive -lpthread -Wl,--no-whole-archive
        )
    endif()
elseif(MSVC)

    target_compile_options(${PROJECT_NAME} PRIVATE
            /GS-                     # disable buffer security checks
            /GR-                     # disable RTTI
    )

    # MSVC specific settings for Windows application
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

    # Disable automatic manifest generation to avoid conflicts with Resource.rc
    target_link_options(${PROJECT_NAME} PRIVATE
        /MANIFEST:NO
    )

    if(IS_RELEASE)
        # /Gw + /OPT:REF drop unreferenced data and code, /OPT:ICF folds the identical
        # std::function and template instantiations.
        # /GL + /LTCG measured 10 KB larger here - cross-object inlining beats the size win.
        target_compile_options(${PROJECT_NAME} PRIVATE /Gw)
        target_link_options(${PROJECT_NAME} PRIVATE /OPT:REF /OPT:ICF)
    endif()
endif()

if(IS_DEBUG)
    # Desktop-wide keyboard/mouse hooks under a debugger freeze input on every breakpoint
    target_compile_definitions(${PROJECT_NAME} PRIVATE APP_NO_GLOBAL_HOOKS)
endif()

if(IS_RELEASE AND ENABLE_UPX_COMPRESSION)
    find_program(UPX_EXECUTABLE upx)

    if(UPX_EXECUTABLE)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${UPX_EXECUTABLE} --best --lzma $<TARGET_FILE:${PROJECT_NAME}>
                COMMENT "Compressing ${PROJECT_NAME} with UPX..."
        )
        message(STATUS "UPX found: ${UPX_EXECUTABLE}")
    else()
        message(STATUS "UPX not found - Release build will not be compressed")
    endif()
endif()

