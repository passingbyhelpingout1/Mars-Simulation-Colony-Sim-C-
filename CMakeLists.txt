cmake_minimum_required(VERSION 3.16)
project(MarsSimulationColonySimC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Sources
set(APP_SOURCES
    ${CMAKE_SOURCE_DIR}/mars_colony.cpp
)

# If a proper CLI implementation exists, build and link it.
set(MARS_CLI_SRC "${CMAKE_SOURCE_DIR}/ui/cli/cli.cpp")
if (EXISTS "${MARS_CLI_SRC}")
    add_library(mars_cli STATIC "${MARS_CLI_SRC}")
    target_include_directories(mars_cli PUBLIC "${CMAKE_SOURCE_DIR}/ui/cli")

    add_executable(mars_colony ${APP_SOURCES})
    target_link_libraries(mars_colony PRIVATE mars_cli)
    target_compile_definitions(mars_colony PRIVATE MARS_HAS_EXTERNAL_CLI=1)
else()
    message(STATUS "ui/cli/cli.cpp not found; building with fallback CLI in mars_colony.cpp")
    add_executable(mars_colony ${APP_SOURCES})
endif()

# Warnings
if (MSVC)
    target_compile_options(mars_colony PRIVATE /W4 /permissive-)
else()
    target_compile_options(mars_colony PRIVATE -Wall -Wextra -Wshadow -Wconversion)
endif()

# Install target (optional)
install(TARGETS mars_colony RUNTIME DESTINATION bin)
