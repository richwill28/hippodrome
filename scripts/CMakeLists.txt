cmake_minimum_required(VERSION 3.0.0)

set(CMAKE_C_COMPILER "gcc-12")
set(CMAKE_CXX_COMPILER "g++-12")

project("transform_grammar")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wpedantic -Wall -Wextra -Wconversion -Wshadow -Wunused-parameter -O3")

find_package(unordered_dense CONFIG REQUIRED)

add_executable(transform_grammar transform_grammar.cpp)
target_link_libraries(transform_grammar unordered_dense::unordered_dense)
