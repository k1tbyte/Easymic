cmake_minimum_required(VERSION 3.27)

set(CMAKE_CXX_STANDARD 23)
project(Easymic)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")


if(MSVC)
    enable_language(CXX ASM_MASM)
    # This fixes a bug in linking cmake with .asm. (LNK1181: cannot open input file 'cr').
    # Cmake inserts cr between <CMAKE_AR> and <TARGET>.
    # This is a flag for Unix-like systems that is used with the ar utility (Unix archiver), but it is not compatible with Microsoft.
    set(CMAKE_ASM_MASM_CREATE_STATIC_LIBRARY "<CMAKE_AR> /OUT:<TARGET> <LINK_FLAGS> <OBJECTS>")
    file(GLOB_RECURSE ASM_FILES "${SRC_DIR}/*.asm")
endif()

file(GLOB_RECURSE CPP_FILES "${SRC_DIR}/*.cpp")
file(GLOB_RECURSE HPP_FILES "${SRC_DIR}/*.hpp")


add_executable(${PROJECT_NAME} WIN32)


target_link_libraries(${PROJECT_NAME} PRIVATE
        ole32
        uuid
        wsock32
        ws2_32
        winmm
        gdiplus
        comctl32
)



set(RESOURCE "${CMAKE_SOURCE_DIR}/src/Resources/Resource.rc")

target_sources(${PROJECT_NAME} PRIVATE ${RESOURCE}
        ${CPP_FILES}
        ${HPP_FILES}
        ${ASM_FILES}
)

target_include_directories(${PROJECT_NAME} PUBLIC
        src
        src/Lib
        src/Audio
        src/View
)

if(MINGW)
#[[    target_compile_options(${PROJECT_NAME} PRIVATE
            -ffunction-sections
            -fdata-sections
            -fno-rtti
            -fno-unwind-tables
    )]]

    target_link_options(${PROJECT_NAME} PRIVATE
            -static-libgcc
            -static-libstdc++
            -Wl,--allow-multiple-definition
#[[            -Wl,--gc-sections         # remove unused sections
            -Wl,--strip-all          # remove debugging symbols
            -Wl,--strip-debug
            -s                       # additional stripping]]
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)

        target_link_options(${PROJECT_NAME} PRIVATE
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






