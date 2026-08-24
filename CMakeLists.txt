cmake_minimum_required(VERSION 3.27)

set(CMAKE_CXX_STANDARD 23)
project(Easymic)

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


file(GLOB_RECURSE CPP_FILES "${SRC_DIR}/*.cpp")
file(GLOB_RECURSE HPP_FILES "${SRC_DIR}/*.hpp")


add_executable(${PROJECT_NAME} #[[WIN32]])

if(WIN32)
    # Win10 API surface (GetDpiForWindow etc) - MinGW headers gate these behind _WIN32_WINNT
    target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
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
        glaze::glaze
)



set(RESOURCE "${CMAKE_SOURCE_DIR}/src/Resources/Resource.rc")

target_sources(${PROJECT_NAME} PRIVATE ${RESOURCE}
        ${CPP_FILES}
        ${HPP_FILES}
)

target_include_directories(${PROJECT_NAME} PUBLIC
        src
        src/Lib
        src/Audio
        src/View
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
    target_compile_definitions(${PROJECT_NAME} PRIVATE EASYMIC_NO_GLOBAL_HOOKS)
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

