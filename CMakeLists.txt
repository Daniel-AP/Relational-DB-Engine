cmake_minimum_required(VERSION 3.20)

project(
    DanDB
    VERSION 0.0.0
    DESCRIPTION "A learning-focused mini relational database engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DANDB_BUILD_TESTS "Build the DanDB test executable" ON)
option(DANDB_ENABLE_SANITIZERS "Enable compiler sanitizers when supported" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Sanitizers)

set(DANDB_LIBRARY_HEADERS
    include/dandb/core/Result.h
    include/dandb/core/Status.h
    include/dandb/core/version.h
)

set(DANDB_LIBRARY_SOURCES
    src/core/version.cpp
)

add_library(DanDBLib
    ${DANDB_LIBRARY_HEADERS}
    ${DANDB_LIBRARY_SOURCES}
)

add_library(DanDB::Lib ALIAS DanDBLib)

target_compile_features(DanDBLib PUBLIC cxx_std_20)

target_include_directories(DanDBLib
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

dandb_apply_compiler_warnings(DanDBLib)
dandb_enable_sanitizers(DanDBLib)

add_executable(DanDB
    src/main.cpp
)

target_link_libraries(DanDB
    PRIVATE
        DanDBLib
)

dandb_apply_compiler_warnings(DanDB)
dandb_enable_sanitizers(DanDB)

if(DANDB_BUILD_TESTS)
    enable_testing()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/catch2/CMakeLists.txt")
        add_subdirectory(third_party/catch2)
    else()
        message(WARNING "Catch2 was not found at third_party/catch2. DanDBTests will be a placeholder target until Catch2 is vendored.")
    endif()

    add_subdirectory(tests)
endif()
