cmake_minimum_required(VERSION 3.10)
project(sol VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(mqtt src/mqtt.cpp src/pack.cpp)
target_include_directories(mqtt PUBLIC src)
