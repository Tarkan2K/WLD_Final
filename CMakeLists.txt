cmake_minimum_required(VERSION 3.16)
project(wld_live_lab VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-O3 -march=native -Wall -Wextra -pthread)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(wld_live_lab_engine src/main.cpp)
target_link_libraries(wld_live_lab_engine PRIVATE pthread)

add_executable(wld_live_lab_bench benchmarks/bench_pipeline.cpp)
target_link_libraries(wld_live_lab_bench PRIVATE pthread)
