cmake_minimum_required(VERSION 3.21)
project(TrajectoryBufferMod)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(${PROJECT_NAME} SHARED main.cpp)

setup_geode_mod(${PROJECT_NAME})
