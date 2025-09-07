cmake_minimum_required(VERSION 3.12)
project(MarsSimCli CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-O2 -Wall -Wextra)

include_directories(${CMAKE_SOURCE_DIR})

add_library(mars_sim
    src/sim/World.cpp
    src/sim/Simulation.cpp
    src/sim/Recorder.cpp
)
target_include_directories(mars_sim PUBLIC src)

add_executable(mars_colony src/main.cpp)
target_link_libraries(mars_colony PRIVATE mars_sim)

enable_testing()
add_executable(determinism_test tests/determinism.cpp)
target_link_libraries(determinism_test PRIVATE mars_sim)
add_test(NAME determinism COMMAND determinism_test)
