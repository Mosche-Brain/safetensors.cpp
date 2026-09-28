cmake_minimum_required(VERSION 3.14)
project(safetensors_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)

FILE(GLOB_RECURSE SOURCES "src/*.cpp" "src/.c")

add_library(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE cum::Core)
target_include_directories(${PROJECT_NAME} PRIVATE "include/")