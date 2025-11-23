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

