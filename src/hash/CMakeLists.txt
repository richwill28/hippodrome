cmake_minimum_required(VERSION 3.0.0)

set(CMAKE_C_COMPILER "clang-17")
set(CMAKE_CXX_COMPILER "clang++-17")

project("hippodrome")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D DEBUG -Wpedantic -Wall -Wextra -Wconversion -Wshadow -Wunused-parameter -O3")

find_package(unordered_dense CONFIG REQUIRED)

add_executable(hippodrome hippodrome.cpp)
target_link_libraries(hippodrome unordered_dense::unordered_dense)
