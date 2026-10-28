cmake_minimum_required(VERSION 3.24)
project(LiberOmniaBackend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
# Resolve CMake's SQLite target before Drogon extends the module search path.
find_package(SQLite3 REQUIRED)
find_package(Drogon 1.9 CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL 7.85 REQUIRED)

add_library(liberomnia_core
    src/config.cpp
    src/models.cpp
    src/source_catalog.cpp
    src/provider_registry.cpp
    src/normalization.cpp
    src/search_coordinator.cpp
)
target_link_libraries(liberomnia_core PUBLIC Threads::Threads)
target_include_directories(liberomnia_core PUBLIC include)
target_compile_features(liberomnia_core PUBLIC cxx_std_20)
set_target_properties(liberomnia_core PROPERTIES CXX_EXTENSIONS OFF)

add_library(liberomnia_http src/api.cpp src/search_api.cpp src/google_books_provider.cpp src/books_http_client.cpp)
target_link_libraries(liberomnia_http PUBLIC liberomnia_core Drogon::Drogon CURL::libcurl)

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
    add_executable(liberomnia_tests tests/google_books_test.cpp tests/config_test.cpp tests/models_test.cpp tests/api_test.cpp
        tests/coordinator_test.cpp tests/search_api_test.cpp tests/normalization_test.cpp)
    target_link_libraries(liberomnia_tests PRIVATE liberomnia_http GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(liberomnia_tests)

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME http_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/http_integration.py
                $<TARGET_FILE:liberomnia_server>)
    set_tests_properties(http_integration PROPERTIES TIMEOUT 30)
    add_executable(liberomnia_http_test_server tests/http_test_server.cpp)
    target_link_libraries(liberomnia_http_test_server PRIVATE liberomnia_http)
    add_test(NAME search_http_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/search_http_integration.py
                $<TARGET_FILE:liberomnia_http_test_server>)
    set_tests_properties(search_http_integration PROPERTIES TIMEOUT 30)
    add_executable(liberomnia_books_http_test_server tests/google_books_http_server.cpp)
    target_link_libraries(liberomnia_books_http_test_server PRIVATE liberomnia_http)
    add_test(NAME google_books_http_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/google_books_http_integration.py
                $<TARGET_FILE:liberomnia_books_http_test_server>)
    set_tests_properties(google_books_http_integration PROPERTIES TIMEOUT 30)
endif()

install(TARGETS liberomnia_server RUNTIME DESTINATION bin)

option(LIBEROMNIA_LIVE_CHECK "Build the optional Google Books network diagnostic (never registered in CTest)" OFF)
if(LIBEROMNIA_LIVE_CHECK)
    add_executable(liberomnia_books_live tests/google_books_live.cpp)
    target_link_libraries(liberomnia_books_live PRIVATE liberomnia_http)
endif()
