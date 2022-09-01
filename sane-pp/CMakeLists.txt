cmake_minimum_required(VERSION 3.10)
project(sane-pp)

add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE .)
#set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
#set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)

# SANE C library should be installed on the system somehow, there is no proper FindPackage yet for it
target_link_libraries(${PROJECT_NAME} INTERFACE sane)

# add_executable(sane-tst src/main.cpp src/sane_wrap.cpp src/sane_wrap2.cpp src/sane_wrap2_device_descr.cpp)
# add_executable(sane-tst src/main.cpp src/sane_wrap2.cpp src/sane_wrap2_device_descr.cpp)
#add_executable(sane-tst src/main.cpp)
#target_link_libraries(sane-tst sane)
#set_property(TARGET sane-tst PROPERTY CXX_STANDARD 20)
#target_compile_options(sane-tst PRIVATE "-fconcepts-diagnostics-depth=4")
