cmake_minimum_required(VERSION 3.21)
project(TrajectoryBufferMod)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create the mod library from your source file
add_library(${PROJECT_NAME} SHARED main.cpp)

# Tell CMake where the Geode SDK lives (GitHub Actions sets this automatically)
if (NOT GEODE_SDK)
    set(GEODE_SDK $ENV{GEODE_SDK})
endif()

# Load the Geode SDK. This defines setup_geode_mod().
add_subdirectory(${GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Now the command exists and can be called
setup_geode_mod(${PROJECT_NAME})
