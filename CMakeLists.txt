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

add_executable(test_load tests/test_load.c engine/htgl.c engine/draw.c)
target_include_directories(test_load PRIVATE engine tests)
add_test(NAME load COMMAND test_load)

add_executable(test_layout tests/test_layout.c engine/htgl.c engine/draw.c)
target_include_directories(test_layout PRIVATE engine tests)
add_test(NAME layout COMMAND test_layout)

add_executable(test_render tests/test_render.c engine/htgl.c engine/draw.c)
target_include_directories(test_render PRIVATE engine tests)
add_test(NAME render COMMAND test_render)

add_executable(test_text tests/test_text.c engine/htgl.c engine/draw.c)
target_include_directories(test_text PRIVATE engine tests)
add_test(NAME text COMMAND test_text)

add_executable(test_anim tests/test_anim.c engine/htgl.c engine/draw.c)
target_include_directories(test_anim PRIVATE engine tests)
add_test(NAME anim COMMAND test_anim)

find_program(SH_PROGRAM sh)
if(SH_PROGRAM)
  add_test(NAME e2e COMMAND ${SH_PROGRAM} ${CMAKE_SOURCE_DIR}/tests/test_e2e.sh)
endif()
