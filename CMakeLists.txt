cmake_minimum_required(VERSION 3.10)
project(sane-tst)

# add_executable(sane-tst src/main.cpp src/sane_wrap.cpp src/sane_wrap2.cpp src/sane_wrap2_device_descr.cpp)
add_executable(sane-tst src/main.cpp src/sane_wrap2.cpp src/sane_wrap2_device_descr.cpp)
target_link_libraries(sane-tst sane)
set_property(TARGET sane-tst PROPERTY CXX_STANDARD 20)
target_compile_options(sane-tst PRIVATE "-fconcepts-diagnostics-depth=4")
