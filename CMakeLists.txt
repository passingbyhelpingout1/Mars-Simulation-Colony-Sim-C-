cmake_minimum_required(VERSION 3.16)
project(MarsSimC LANGUAGES CXX)

# ---- Options ----
option(MARS_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(MARS_ENABLE_ASAN        "Enable ASan/UBSan in Debug (non-MSVC)" OFF)
option(MARS_ENABLE_LTO         "Enable link-time optimization (IPO/LTO) in Release" ON)
option(MARS_USE_EXTERNAL_CLI   "Link ui/cli/cli.cpp and disable fallback" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Sources ----
set(APP_SOURCES ${CMAKE_SOURCE_DIR}/mars_colony.cpp)

add_executable(mars ${APP_SOURCES})
set_target_properties(mars PROPERTIES OUTPUT_NAME "mars")

# If you later add a real CLI:
if (MARS_USE_EXTERNAL_CLI)
  set(MARS_CLI_SRC "${CMAKE_SOURCE_DIR}/ui/cli/cli.cpp")
  if (EXISTS "${MARS_CLI_SRC}")
    add_library(mars_cli STATIC "${MARS_CLI_SRC}")
    target_include_directories(mars_cli PUBLIC "${CMAKE_SOURCE_DIR}/ui/cli")
    target_link_libraries(mars PRIVATE mars_cli)
    target_compile_definitions(mars PRIVATE MARS_HAS_EXTERNAL_CLI=1)
  else()
    message(WARNING "MARS_USE_EXTERNAL_CLI=ON but ui/cli/cli.cpp not found; using fallback CLI")
  endif()
endif()

# ---- Warnings & toolchain niceties ----
if (MSVC)
  target_compile_options(mars PRIVATE /W4 /permissive-)
  if (MARS_WARNINGS_AS_ERRORS)
    target_compile_options(mars PRIVATE /WX)
  endif()
else()
  target_compile_options(mars PRIVATE -Wall -Wextra -Wshadow -Wconversion)
  if (MARS_WARNINGS_AS_ERRORS)
    target_compile_options(mars PRIVATE -Werror)
  endif()
endif()

# ---- Sanitizers in Debug (non-MSVC) ----
if (CMAKE_BUILD_TYPE MATCHES "Debug" AND MARS_ENABLE_ASAN AND NOT MSVC)
  target_compile_options(mars PRIVATE -fsanitize=address,undefined)
  target_link_options(mars PRIVATE -fsanitize=address,undefined)
endif()

# ---- IPO/LTO in Release ----
if (MARS_ENABLE_LTO AND CMAKE_BUILD_TYPE MATCHES "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_msg)
  if (_ipo_supported)
    set_property(TARGET mars PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  else()
    message(STATUS "IPO/LTO not supported: ${_ipo_msg}")
  endif()
endif()

# ---- Install (optional) ----
install(TARGETS mars RUNTIME DESTINATION bin)

# ---- Tests (optional, just ensures binary runs) ----
include(CTest)
if (BUILD_TESTING)
  add_test(NAME hash_only_runs COMMAND mars --hash-only)
endif()
