cmake_minimum_required(VERSION 3.10)
project(sane-pp)

option(SANE_PP_STUB "do not invoke real sane library, provide some stub ops")
add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE .)
target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)

# SANE C library should be installed on the system somehow, there is no proper FindPackage yet for it
target_link_libraries(${PROJECT_NAME} INTERFACE sane)

if (SANE_PP_STUB)
    target_compile_definitions(${PROJECT_NAME} INTERFACE SANE_PP_STUB)
endif()
