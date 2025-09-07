cmake_minimum_required(VERSION 3.12)
project(MarsSimCli CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-O2 -Wall -Wextra)

include_directories(${CMAKE_SOURCE_DIR})

add_executable(mars
    mars_colony.cpp            # new tiny main
    ui/cli/app.cpp             # new CLI module
    engine/power.cpp
    engine/events.cpp
    engine/step.cpp
    engine/persist.cpp
)
