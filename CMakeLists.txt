cmake_minimum_required(VERSION 3.19)

project(EllohimMQ CXX)

# Source Directory
set(SRC_DIR "${PROJECT_SOURCE_DIR}/src")

include(CheckIncludeFileCXX)

# Fetch modules
message("\nFetching modules")
include(cmake/json.cmake)
include(cmake/spdlog.cmake)
include(cmake/dotenv.cmake)

# EllohimMQ source files
message(STATUS "EllohimMQ")
file(GLOB_RECURSE SRC_MAIN
  "${SRC_DIR}/**.hpp"
  "${SRC_DIR}/**.h"
  "${SRC_DIR}/**.cpp"
  "${SRC_DIR}/**.cc"
  "${SRC_DIR}/**.cxx"
  "${SRC_DIR}/**.asm"
)

# Compiler options for MSVC and others
if (MSVC)
  add_compile_options(/bigobj)
else ()
  #add_compile_options(-Wa,-mbig-obj)
endif ()

# Define targets and their properties
add_executable(EllohimMQ  "${SRC_MAIN}")  # Use STATIC or SHARED depending on requirements

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(TARGET EllohimMQ PROPERTY CXX_STANDARD 23)

# Organize the source tree in IDE
source_group(TREE ${SRC_DIR} PREFIX "src" FILES ${SRC_MAIN})

# Include directories for the target
target_include_directories(EllohimMQ PRIVATE 
  "${SRC_DIR}"
  "${SRC_DIR}/commands"
  "${SRC_DIR}/logger"
  "${dotenv_SOURCE_DIR}/include/laserpants/dotenv"
  "${nlohmann_json_SOURCE_DIR}/single_include"
  "${spdlog_SOURCE_DIR}/include"
  ${redis++_INCLUDE_DIRS}
)

# Precompiled headers
target_precompile_headers(EllohimMQ PRIVATE "${SRC_DIR}/common.hpp")

# Link with external libraries
#target_link_libraries(EllohimMQ PRIVATE redis++::redis++_static)

# Disable warnings as errors (optional)
set_property(TARGET EllohimMQ PROPERTY COMPILE_WARNING_AS_ERROR OFF)

# Add compiler definitions for Windows specifics
if (WIN32)
	add_compile_definitions(EllohimMQ 
	  "_CRT_SECURE_NO_WARNINGS"
	  "NOMINMAX"
	  "WIN32_LEAN_AND_MEAN"
	)
endif()