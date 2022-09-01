cmake_minimum_required(VERSION 3.10)
project(cli-test)

add_executable(${PROJECT_NAME} main.cpp)
target_compile_options(${PROJECT_NAME} PRIVATE "-fconcepts-diagnostics-depth=4")

add_subdirectory(../sane-pp sane-pp)
target_link_libraries(${PROJECT_NAME} sane-pp)
