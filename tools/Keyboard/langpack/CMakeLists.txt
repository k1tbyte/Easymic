cmake_minimum_required(VERSION 3.27)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(langpack LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 23)
endif()

get_filename_component(LANGPACK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(KEYBOARD "${LANGPACK_ROOT}/src/Features/Keyboard")
set(ENGINE "${KEYBOARD}/Convert")

add_executable(langpack EXCLUDE_FROM_ALL
        Main.cpp Console.cpp PackBuilder.cpp
        "${ENGINE}/Pack.cpp" "${ENGINE}/Alphabet.cpp"
        "${ENGINE}/Detector.cpp" "${ENGINE}/LayoutTable.cpp" "${ENGINE}/Rules.cpp"
        "${KEYBOARD}/Layout/InputLanguage.cpp")
target_include_directories(langpack PRIVATE "${LANGPACK_ROOT}/src")
target_compile_definitions(langpack PRIVATE NOMINMAX UNICODE _UNICODE)
if(MSVC)
    target_compile_options(langpack PRIVATE /utf-8)
endif()
target_link_libraries(langpack PRIVATE shell32)
set_target_properties(langpack PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

set(LANGPACK_EN_INPUT "" CACHE FILEPATH "Ranked English word list from hackerb9/gwordlist")
set(LANGPACK_RU_INPUT "" CACHE FILEPATH "UTF-8 Russian word list")
set(LANGPACK_EN_TOP "25000" CACHE STRING "Number of English words to include")
set(PACK_DIR "${CMAKE_BINARY_DIR}/packs")

if(LANGPACK_EN_INPUT)
    if(NOT EXISTS "${LANGPACK_EN_INPUT}")
        message(FATAL_ERROR "English list not found: ${LANGPACK_EN_INPUT}")
    endif()
    set(EN_WORDS "${CMAKE_CURRENT_BINARY_DIR}/en_filtered.txt")
    set(EN_MERGED "${CMAKE_CURRENT_BINARY_DIR}/en_words.txt")
    add_custom_command(OUTPUT "${PACK_DIR}/en.pack"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${PACK_DIR}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT_FILE=${LANGPACK_EN_INPUT}"
                    "-DOUTPUT_FILE=${EN_WORDS}" "-DTOP_N=${LANGPACK_EN_TOP}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/filter_en.cmake"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT_FILE=${EN_WORDS}"
                    "-DEXTRA_FILE=${CMAKE_CURRENT_SOURCE_DIR}/words/en.txt" "-DOUTPUT_FILE=${EN_MERGED}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/supplement.cmake"
            COMMAND "$<TARGET_FILE:langpack>" pack "${EN_MERGED}" en "${PACK_DIR}/en.pack" --fp 0.00001
            DEPENDS langpack "${LANGPACK_EN_INPUT}" "${CMAKE_CURRENT_SOURCE_DIR}/filter_en.cmake"
                    "${CMAKE_CURRENT_SOURCE_DIR}/supplement.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/words/en.txt"
            VERBATIM)
    add_custom_target(langpack_en DEPENDS "${PACK_DIR}/en.pack")
endif()

if(LANGPACK_RU_INPUT)
    if(NOT EXISTS "${LANGPACK_RU_INPUT}")
        message(FATAL_ERROR "Russian list not found: ${LANGPACK_RU_INPUT}")
    endif()
    set(RU_MERGED "${CMAKE_CURRENT_BINARY_DIR}/ru_words.txt")
    add_custom_command(OUTPUT "${PACK_DIR}/ru.pack"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${PACK_DIR}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT_FILE=${LANGPACK_RU_INPUT}"
                    "-DEXTRA_FILE=${CMAKE_CURRENT_SOURCE_DIR}/words/ru.txt" "-DOUTPUT_FILE=${RU_MERGED}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/supplement.cmake"
            COMMAND "$<TARGET_FILE:langpack>" pack "${RU_MERGED}" ru "${PACK_DIR}/ru.pack"
            DEPENDS langpack "${LANGPACK_RU_INPUT}" "${CMAKE_CURRENT_SOURCE_DIR}/supplement.cmake"
                    "${CMAKE_CURRENT_SOURCE_DIR}/words/ru.txt"
            VERBATIM)
    add_custom_target(langpack_ru DEPENDS "${PACK_DIR}/ru.pack")
endif()
