cmake_minimum_required(VERSION 3.20)
project(MarsColonySim LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
add_library(mars_core
    src/core/sim.cpp
    src/core/persist.cpp    # when you move save/load v2 here
    src/core/replay.cpp     # when you move record/replay here
)
target_include_directories(mars_core PUBLIC src)
add_executable(mars_cli src/app/main.cpp)
target_link_libraries(mars_cli PRIVATE mars_core)
