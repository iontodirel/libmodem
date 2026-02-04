# **************************************************************** #
# libmodem - APRS modem                                            #
# Version 0.1.0                                                    #
# https://github.com/iontodirel/libmodem                           #
# Copyright (c) 2025 Ion Todirel                                   #
# **************************************************************** #
#
# CMakeLists.txt
#
# MIT License
#
# Copyright (c) 2025 Ion Todirel
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files(the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

cmake_minimum_required (VERSION 3.28)

set(CMAKE_POLICY_VERSION_MINIMUM "3.5" CACHE STRING "")
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

project ("modem" LANGUAGES C CXX)

include(FetchContent)

FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG        v3.1.1
)
FetchContent_MakeAvailable(cxxopts)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../libmodem ${CMAKE_BINARY_DIR}/libmodem)

add_executable(modem "main.cpp")
target_link_libraries(modem PRIVATE libmodem cxxopts)
set_property(TARGET modem PROPERTY CXX_STANDARD 20)