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

if(MSVC)
  cmake_policy(SET CMP0091 NEW)
  # Static for Release, Dynamic for Debug (faster iteration)
  #set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  #set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Release>:>$<$<CONFIG:Debug>:DebugDLL>")
endif()

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

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG        v5.0.0
  GIT_SHALLOW    TRUE
)

# Configure for static linking and disable extras
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(ftxui)

add_library(libmodem STATIC  "audio_stream.cpp" "modulator.cpp" "modem.cpp" "bitstream.cpp" "io.cpp" "kiss.cpp" "pipeline.cpp" "config.cpp" "data_stream.cpp")
target_link_libraries(libmodem PRIVATE nlohmann_json::nlohmann_json Boost::asio Boost::thread Boost::beast Boost::process Boost::circular_buffer Boost::interprocess fmt::fmt sndfile correct ftxui::screen ftxui::dom ftxui::component)
target_include_directories(libmodem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET libmodem PROPERTY CXX_STANDARD 20)
set_target_properties(libmodem PROPERTIES PREFIX "")

if(UNIX AND NOT APPLE)
  find_package(ALSA REQUIRED)
  target_link_libraries(libmodem PRIVATE asound ALSA::ALSA)
endif()

if(APPLE)
  target_compile_options(libmodem PRIVATE -fexperimental-library)
  target_link_options(libmodem PRIVATE -fexperimental-library)
endif()

if(MSVC)
  target_compile_options(libmodem PRIVATE /W4 /WX /permissive-)
endif()