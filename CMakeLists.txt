cmake_minimum_required(VERSION 3.10)
project(sol VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(mqtt src/mqtt.cpp src/pack.cpp src/network.cpp src/trie.cpp src/util.cpp src/config.cpp src/server.cpp)
target_include_directories(mqtt PUBLIC src)

add_executable(sol src/main.cpp)
target_link_libraries(sol PRIVATE mqtt)

if(APPLE)
    find_library(CORESERVICES_LIB CoreServices)
    target_link_libraries(sol PRIVATE ${CORESERVICES_LIB})
endif()
