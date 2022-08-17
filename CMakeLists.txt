cmake_minimum_required(VERSION 3.10)
project(sane-tst)

add_executable(sane-tst main.cpp)
target_link_libraries(sane-tst sane)
set_property(TARGET sane-tst PROPERTY CXX_STANDARD 20)
