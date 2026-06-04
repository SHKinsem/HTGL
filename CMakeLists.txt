cmake_minimum_required(VERSION 3.15)
project(htgl C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(htgl_engine STATIC engine/htgl.c engine/draw.c)
target_include_directories(htgl_engine PUBLIC engine)

add_executable(htgl_sim port/sim/main.c port/sim/hal_png.c)
target_include_directories(htgl_sim PRIVATE port/sim)
target_link_libraries(htgl_sim PRIVATE htgl_engine)

enable_testing()
# tests are registered by later tasks

add_executable(test_draw tests/test_draw.c engine/draw.c)
target_include_directories(test_draw PRIVATE engine tests)
add_test(NAME draw COMMAND test_draw)
