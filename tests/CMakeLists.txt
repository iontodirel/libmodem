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

project ("libmodem" LANGUAGES C CXX)

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

FetchContent_Declare(
    sndfile
    GIT_REPOSITORY https://github.com/libsndfile/libsndfile.git
    GIT_TAG        1.2.2
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG master)
FetchContent_MakeAvailable(fmt)

# Fetch libsndfile
FetchContent_Declare(
    sndfile
    GIT_REPOSITORY https://github.com/libsndfile/libsndfile.git
    GIT_TAG        1.2.2
    GIT_SHALLOW    TRUE
)

# Configure options before fetching
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_EXTERNAL_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_MPEG OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(sndfile)

# Fetch libcorrect
FetchContent_Declare(
  libcorrect
  GIT_REPOSITORY https://github.com/quiet/libcorrect.git
  GIT_TAG        f5a28c7
  GIT_SHALLOW    TRUE
)

# Get the properties but don't automatically make available
FetchContent_GetProperties(libcorrect)

if(NOT libcorrect_POPULATED)
  FetchContent_Populate(libcorrect)
  
  # List ALL the C source files from libcorrect
  set(CORRECT_SOURCES
    ${libcorrect_SOURCE_DIR}/src/reed-solomon/reed-solomon.c
    ${libcorrect_SOURCE_DIR}/src/reed-solomon/encode.c
    ${libcorrect_SOURCE_DIR}/src/reed-solomon/decode.c
  )
  
  # Also add convolutional sources if they exist (in case they're needed)
  if(EXISTS ${libcorrect_SOURCE_DIR}/src/convolutional/convolutional.c)
    list(APPEND CORRECT_SOURCES
      ${libcorrect_SOURCE_DIR}/src/convolutional/convolutional.c
      ${libcorrect_SOURCE_DIR}/src/convolutional/encode.c
      ${libcorrect_SOURCE_DIR}/src/convolutional/decode.c
      ${libcorrect_SOURCE_DIR}/src/convolutional/bit.c
      ${libcorrect_SOURCE_DIR}/src/convolutional/metric.c
      ${libcorrect_SOURCE_DIR}/src/convolutional/history_buffer.c
      ${libcorrect_SOURCE_DIR}/src/convolutional/error_buffer.c
      ${libcorrect_SOURCE_DIR}/src/convolutional/lookup.c
    )
  endif()
  
  # Check if polynomial.c exists
  if(EXISTS ${libcorrect_SOURCE_DIR}/src/reed-solomon/polynomial.c)
    list(APPEND CORRECT_SOURCES ${libcorrect_SOURCE_DIR}/src/reed-solomon/polynomial.c)
  endif()
  
  # CRITICAL: Force these files to be compiled as C, not C++
  set_source_files_properties(${CORRECT_SOURCES} PROPERTIES LANGUAGE C)
  
  # Create the static library
  add_library(correct STATIC ${CORRECT_SOURCES})
  
  # Set the library to use C linkage
  set_target_properties(correct PROPERTIES LINKER_LANGUAGE C)
  
  # Include directories
  target_include_directories(correct PUBLIC 
    ${libcorrect_SOURCE_DIR}/include
  )
  
  # Also add the src directory for internal headers
  target_include_directories(correct PRIVATE
    ${libcorrect_SOURCE_DIR}/src
  )
  
  if(MSVC)
    # Use C compiler flags for C files
    target_compile_options(correct PRIVATE 
      $<$<COMPILE_LANGUAGE:C>:/TC>  # Force compile as C
      /W3
    )
    target_compile_definitions(correct PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
endif()

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

add_executable(tests "tests.cpp" "../audio_stream.cpp" "../modulator.cpp" "../modem.cpp" "../bitstream.h" "../bitstream.cpp" "../io.cpp")
target_include_directories(tests PRIVATE "../" )
target_link_libraries(tests PRIVATE GTest::gtest_main gtest gtest_main nlohmann_json::nlohmann_json Boost::asio Boost::thread Boost::beast Boost::process Boost::circular_buffer Boost::interprocess fmt::fmt sndfile correct)
target_compile_definitions(tests PRIVATE ATEST_EXE_PATH="${ATEST_EXE_PATH}"  PYTHON_EXE_PATH="${Python3_EXECUTABLE}" LIBMODEM_PACKET_NAMESPACE_REFERENCE=aprs::router:: APRS_ROUTER_ENABLE_PACKET_SUPPORT=true)
set_property(TARGET tests PROPERTY CXX_STANDARD 20)

add_custom_target(bitstream_stress_test
    COMMAND tests --gtest_filter=bitstream.try_decode_basic_bitstream_1005:bitstream.encode_basic_bitstream_1005 --gtest_repeat=500 --gtest_break_on_failure
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS tests
    USES_TERMINAL
)

add_custom_target(tests_show_deps
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/pe_imports.py --simple "$<TARGET_FILE:tests>"
    DEPENDS tests
    COMMENT "PE Dependencies for tests.exe"
    VERBATIM
)

# Packet type customization
# A packet type has to be supplied externally, and it allows sharing of the type across various projects
# 
# The packet type has the following interface:
#
# struct packet
# {
#     std::string from;
#     std::string to;
#     std::vector<std::string> path;
#     std::string data;
# };
#
# The packet is defined in aprsroute.hpp
# APRS_ROUTER_ENABLE_PACKET_SUPPORT=true enables packet export from aprsroute.hpp
# LIBMODEM_PACKET_NAMESPACE_REFERENCE=aprs::router:: defines the namespace the type is expected to be found in
#
# We include aprsroute.hpp in all TUs using CMAKE_CXX_FLAGS
# And use target_compile_definitions to set APRS_ROUTER_ENABLE_PACKET_SUPPORT and LIBMODEM_PACKET_NAMESPACE_REFERENCE macros

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /FI${CMAKE_SOURCE_DIR}/external/aprsroute.hpp")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include ${CMAKE_SOURCE_DIR}/external/aprsroute.hpp")
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(tests PRIVATE asound)
endif()

if(MSVC)
    #target_compile_options(tests PRIVATE /Wall /analyze /external:W0)
endif()

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET tests PROPERTY CXX_STANDARD 20)   
endif()

include(GoogleTest)

gtest_discover_tests(tests)

configure_file(${CMAKE_SOURCE_DIR}/test_assets/bitstream_1.txt ${CMAKE_BINARY_DIR}/bitstream_1.txt COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/test_assets/packets_1d.txt ${CMAKE_BINARY_DIR}/packets_1d.txt COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/fft.py ${CMAKE_BINARY_DIR}/fft.py COPYONLY)