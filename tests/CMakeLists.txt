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

project ("tests" LANGUAGES C CXX)

include(FetchContent)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(json)

set(BOOST_INCLUDE_LIBRARIES asio thread beast circular_buffer interprocess process)
set(BOOST_ENABLE_CMAKE ON)
set(BOOST_USE_STATIC_LIBS ON)

FetchContent_Declare(
  Boost
  URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.xz
  URL_MD5 d55d43218e81ca3d0fc14436b7665bf1
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_MakeAvailable(boost)

FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG master)
FetchContent_MakeAvailable(fmt)

FetchContent_Declare(googletest URL https://github.com/google/googletest/archive/refs/tags/v1.13.0.zip)
FetchContent_MakeAvailable(googletest)

enable_testing()

# We use Direwolf's atest utility for demodulator testing
# On Windows, we download a precompiled package
# On other platforms, we expect atest to be installed
# For Ubuntu systems, use the install_dependencies.sh script
if(WIN32)
    FetchContent_Declare(
        direwolf
        URL "https://github.com/wb2osz/direwolf/releases/download/1.8.1/direwolf-1.8.1-a231971_x86_64.zip"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(direwolf)
    set(ATEST_EXE_PATH "${direwolf_SOURCE_DIR}/atest.exe")
else()
    find_program(ATEST_EXE_PATH atest REQUIRED)
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(DOWNLOAD https://raw.githubusercontent.com/iontodirel/libaprsroute/main/aprsroute.hpp ${CMAKE_SOURCE_DIR}/external/aprsroute.hpp)
file(DOWNLOAD https://raw.githubusercontent.com/iontodirel/libaprstrack/refs/heads/main/aprstrack.hpp ${CMAKE_SOURCE_DIR}/external/aprstrack.hpp)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../libmodem ${CMAKE_BINARY_DIR}/libmodem)

add_executable(tests "tests.cpp")
target_link_libraries(tests PRIVATE libmodem GTest::gtest_main fmt::fmt Boost::process)
target_compile_definitions(tests PRIVATE ATEST_EXE_PATH="${ATEST_EXE_PATH}" PYTHON_EXE_PATH="${Python3_EXECUTABLE}")
set_property(TARGET tests PROPERTY CXX_STANDARD 20)

add_library(ptt_library_example SHARED ptt_library_example.c)

if(NOT WIN32)
    target_compile_options(ptt_library_example PRIVATE -fvisibility=hidden)
endif()

add_custom_target(bitstream_stress_test
    COMMAND tests --gtest_filter=bitstream.try_decode_basic_bitstream_1005:bitstream.encode_basic_bitstream_1005 --gtest_repeat=500 --gtest_break_on_failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS tests
    USES_TERMINAL
)

if(WIN32)
    add_custom_target(tests_show_deps
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/pe_imports.py --simple "$<TARGET_FILE:tests>"
        DEPENDS tests
        COMMENT "PE Dependencies for tests.exe"
        VERBATIM
    )
endif()

include(GoogleTest)

gtest_discover_tests(tests)

configure_file(${CMAKE_SOURCE_DIR}/test_assets/bitstream_1.txt ${CMAKE_BINARY_DIR}/bitstream_1.txt COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/test_assets/packets_1d.txt ${CMAKE_BINARY_DIR}/packets_1d.txt COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/fft.py ${CMAKE_BINARY_DIR}/fft.py COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/settings.json ${CMAKE_BINARY_DIR}/settings.json COPYONLY)