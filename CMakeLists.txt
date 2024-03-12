cmake_minimum_required(VERSION 3.27)
project(Easymic)

set(CMAKE_CXX_STANDARD 23)
set(IS_DEBUG CMAKE_BUILD_TYPE STREQUAL "Debug")
set(IS_RELEASE CMAKE_BUILD_TYPE STREQUAL "Release")

set(RESOURCE "${CMAKE_SOURCE_DIR}/src/Resources/Resource.rc"
        src/main.hpp
        src/AudioManager.hpp
        src/Utils.hpp
        src/View/MainWindow.cpp
        src/View/MainWindow.h
        src/config.h
        src/View/SettingsWindow.h
        src/View/AbstractWindow.h
)

set(WIN32   23)

set(CMAKE_CXX_STANDARD_LIBRARIES "-static-libgcc -static-libstdc++ -lwsock32 -lws2_32 -lwinmm ${CMAKE_CXX_STANDARD_LIBRARIES}")

if (${IS_RELEASE})
    set(CMAKE_EXE_LINKER_FLAGS "-Wl,-Bstatic,--whole-archive -lpthread -Wl,--no-whole-archive ${CMAKE_EXE_LINKER_FLAGS}")

    #[[ We indicate that we do not need a console ]]
    target_link_options(${PROJECT_NAME} PRIVATE -mwindows )

    #[[ !! You must put FLAGS and LINKER FLAGS at the end, otherwise the WIN PTHREADS version will be used !! ]]
    set(CMAKE_CXX_LINK_EXECUTABLE "<CMAKE_CXX_COMPILER> <OBJECTS> <LINK_LIBRARIES> -o <TARGET> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS>")
endif ()


add_executable(${PROJECT_NAME} ${RESOURCE} src/main.cpp)
