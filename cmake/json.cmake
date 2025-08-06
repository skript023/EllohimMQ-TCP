include(FetchContent)

set(JSON_MultipleHeaders OFF)

if (WIN32)
    # toolchain-msvc-auto.cmake

    # Cari cl.exe di PATH
    execute_process(
        COMMAND where cl
        OUTPUT_VARIABLE CL_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if (NOT CL_PATH)
        message(FATAL_ERROR "MSVC compiler 'cl.exe' not found in PATH. Pastikan sudah menjalankan vcvarsall.bat.")
    endif()

    # Pakai hasil path yang ditemukan
    set(CMAKE_C_COMPILER "${CL_PATH}" CACHE FILEPATH "" FORCE)
    set(CMAKE_CXX_COMPILER "${CL_PATH}" CACHE FILEPATH "" FORCE)

    message(STATUS "Detected cl.exe at: ${CL_PATH}")
endif()

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/ArthurSonzogni/nlohmann_json_cmake_fetchcontent.git
    GIT_TAG        67e6070f9d9a44b4dec79ebe6b591f39d2285593
    GIT_PROGRESS TRUE
)
message("nlohmann_json")
FetchContent_MakeAvailable(nlohmann_json)