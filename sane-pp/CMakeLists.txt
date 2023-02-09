cmake_minimum_required(VERSION 3.10)
project(sane-pp)

cmake_policy(SET CMP0077 NEW)
option(SANE_PP_STUB "do not invoke real sane library, provide some stub ops")
option(SANE_PP_CANCEL_VIA_SIGNAL_SUPPORT "support cancelling by sending a signal into a worker thread")

# Note, that SANE C library should be installed on the system somehow, there is no proper
# FindPackage for it yet

# Simple wrapper library
add_library(${PROJECT_NAME}-v1 v1/sane_wrapper.cpp)
target_include_directories(${PROJECT_NAME}-v1 PUBLIC v1)
target_compile_features(${PROJECT_NAME}-v1 PUBLIC cxx_std_20)
target_link_libraries(${PROJECT_NAME}-v1 PUBLIC sane)

# More complicated asynchronous library
add_library(${PROJECT_NAME}-v2 v2/sane_wrapper.cpp)
target_include_directories(${PROJECT_NAME}-v2 PUBLIC v2)
target_compile_features(${PROJECT_NAME}-v2 PUBLIC cxx_std_20)
target_link_libraries(${PROJECT_NAME}-v2 PUBLIC sane)

if (SANE_PP_STUB)
    target_compile_definitions(${PROJECT_NAME}-v1 PUBLIC SANE_PP_STUB)
    target_compile_definitions(${PROJECT_NAME}-v2 PUBLIC SANE_PP_STUB)
endif()

if (SANE_PP_CANCEL_VIA_SIGNAL_SUPPORT)
    target_compile_definitions(${PROJECT_NAME}-v1 PUBLIC SANE_PP_CANCEL_VIA_SIGNAL_SUPPORT)
    target_compile_definitions(${PROJECT_NAME}-v2 PUBLIC SANE_PP_CANCEL_VIA_SIGNAL_SUPPORT)
endif()
