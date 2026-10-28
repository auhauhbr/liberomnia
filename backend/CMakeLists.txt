cmake_minimum_required(VERSION 3.24)
project(LiberOmniaBackend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
# Resolve CMake's SQLite target before Drogon extends the module search path.
find_package(SQLite3 REQUIRED)
find_package(Drogon 1.9 CONFIG REQUIRED)

add_library(liberomnia_core
    src/config.cpp
    src/models.cpp
    src/source_catalog.cpp
)
target_include_directories(liberomnia_core PUBLIC include)
target_compile_features(liberomnia_core PUBLIC cxx_std_20)
set_target_properties(liberomnia_core PROPERTIES CXX_EXTENSIONS OFF)

add_library(liberomnia_http src/api.cpp)
target_link_libraries(liberomnia_http PUBLIC liberomnia_core Drogon::Drogon)

# A linkage boundary for future repositories; no database is opened at startup.
add_library(liberomnia_persistence INTERFACE)
target_link_libraries(liberomnia_persistence INTERFACE SQLite::SQLite3)

add_executable(liberomnia_server src/main.cpp)
target_link_libraries(liberomnia_server PRIVATE liberomnia_http)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target liberomnia_core liberomnia_http liberomnia_server)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
endif()

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    add_executable(liberomnia_tests tests/config_test.cpp tests/models_test.cpp tests/api_test.cpp)
    target_link_libraries(liberomnia_tests PRIVATE liberomnia_http GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(liberomnia_tests)

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME http_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/http_integration.py
                $<TARGET_FILE:liberomnia_server>)
    set_tests_properties(http_integration PROPERTIES TIMEOUT 30)
endif()

install(TARGETS liberomnia_server RUNTIME DESTINATION bin)
