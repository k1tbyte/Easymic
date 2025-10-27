cmake_minimum_required(VERSION 3.27)

set(CMAKE_CXX_STANDARD 23)
project(Easymic)


set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

file(GLOB_RECURSE CPP_FILES "${SRC_DIR}/*.cpp")
file(GLOB_RECURSE HPP_FILES "${SRC_DIR}/*.hpp")

add_executable(${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME} PRIVATE
        ole32
        uuid
        wsock32
        ws2_32
        winmm
        gdiplus
        uxtheme
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
    target_link_options(${PROJECT_NAME} PRIVATE
            -static-libgcc
            -static-libstdc++
            -Wl,--allow-multiple-definition
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)

        target_link_options(${PROJECT_NAME} PRIVATE
                -Wl,-Bstatic,--whole-archive -lpthread -Wl,--no-whole-archive
        )

        target_compile_options(${PROJECT_NAME} PRIVATE -O3)
    endif()
endif()




