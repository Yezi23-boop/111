# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 抑制第三方库的编译警告
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-attributes -Wno-unused-function")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-attributes -Wno-unused-function")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(111)
