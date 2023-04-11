cmake_minimum_required(VERSION 3.24)
project(Vault)

set(CMAKE_CXX_STANDARD 20)

add_library(range_v3 INTERFACE IMPORTED Serializer.hpp Serializer.hpp)

set_target_properties(range_v3 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/range-v3/include")


add_executable(Vault main.cpp)
target_link_libraries(Vault PUBLIC range_v3)
target_compile_options(Vault PUBLIC -fconcepts-diagnostics-depth=100)

# add tests
add_subdirectory(tests)
