// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// kiss.cpp
//
// MIT License
//
// Copyright (c) 2026 Ion Todirel
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "kiss.h"

#include <vector>
#include <string>
#include <cstdint>

LIBMODEM_NAMESPACE_BEGIN

LIBMODEM_KISS_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// frame_marker                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool operator==(unsigned char left, frame_marker right)
{
    return left == (unsigned char)right;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// decode                                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool decode(uint8_t byte, uint8_t& result, decoder_state& state)
{
    //
    // KISS frame structure:
    //
    //                  frame 1                                   frame n
    //  ---------------------------------------------------------------------------------
    //    frame begin            frame end           frame begin             frame end
    //    FEND (0xc0)    data    FEND (0xc0)  ...    FEND (0xc0)    data    FEND (0xc0)
    //  ---------------------------------------------------------------------------------
    //         1           n         1        ...         1           n          1
    //
    // KISS frame escaping:
    //
    // So say we want to store a 0xc0 byte somewhere in our data,
    // but that byte is also the magic byte used to mark the start of a frame,
    // what do we do? We use 0xDC (TFEND), which has special meaning to
    // insert 0xc0 in the stream when encountered, but only when
    // prefixed by 0xDB (FESC), so that in normal circumstances we
    // can also have 0xDB bytes in the data stream.
    //
    //                             unescaped frame
    //  ------------------------------------------------------------------------
    //    frame begin                                              frame end
    //    FEND (0xc0)    ...    FESC (0xDB) TFEND (0xDC)    ...    FEND (0xc0)
    //  ------------------------------------------------------------------------
    //         1         ...               2                ...         1
    //
    //
    // FESC TFEND becomes 0xC0 after the frame is escaped
    //
    // 0xDC by itself has no special meaning
    //
    //                 just a regular frame
    //  ------------------------------------------------------
    //    frame begin                          frame end
    //    FEND (0xc0)    ...    0xDC    ...    FEND (0xc0)
    //  ------------------------------------------------------
    //         1         ...     1      ...         1
    //
    // But now what if we want a 0xDB byte in the data stream?
    // Then we have an escape for 0xDB by prefixing 0xDD with 0xDB
    //
    //
    //                             unescaped frame
    //  ------------------------------------------------------------------------
    //    frame begin                                              frame end
    //    FEND (0xc0)    ...    FESC (0xDB) TFESC (0xDD)    ...    FEND (0xc0)
    //  ------------------------------------------------------------------------
    //         1         ...               2                ...         1
    //
    // FESC TFESC becomes 0xDB after the frame is escaped
    //
    // The first byte of the data contains the command and port information
    //
    //                          data encoding
    //      command byte
    //  ------------------------------------------------------------------------
    //    command      port       data
    //  ------------------------------------------------------------------------
    //    c c c c     p p p p      N
    //
    //  The lower 4 bits contain the command, while the higher 4 bits contain the port
    //

    if (state.in_kiss_frame)
    {
        if (state.in_escape_mode)
        {
            if (byte == frame_marker::tfend)
            {
                result = static_cast<unsigned char>(frame_marker::fend);
            }
            if (byte == frame_marker::tfesc)
            {
                result = static_cast<unsigned char>(frame_marker::fesc);
            }
            state.in_escape_mode = false;
            return true;
        }

        if (byte == frame_marker::fend)
        {
            state.completed = true;
            state.in_kiss_frame = false;
        }
        else if (byte == frame_marker::fesc)
        {
            state.in_escape_mode = true;
        }
        else
        {
            result = byte;
            return true;
        }
    }
    else
    {
        if (byte == frame_marker::fend)
        {
            state.in_kiss_frame = true;
        }
    }

    return false;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// decoder                                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

const std::vector<frame>& decoder::frames() const
{
    return data_;
}

size_t decoder::count() const
{
    return data_.size();
}

void decoder::reset()
{
    data_.clear();
    buffer_.clear();
    state_ = {};
}

void decoder::clear()
{
    data_.clear();
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// encode                                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

template <std::input_iterator InputIt, std::output_iterator<unsigned char> OutputIt>
std::pair<OutputIt, bool> encode(InputIt input_it_begin, InputIt input_it_end, OutputIt output_it);

template <std::input_iterator InputIt, std::output_iterator<unsigned char> OutputIt, bool Command>
std::pair<OutputIt, bool> encode(uint8_t command_byte, InputIt input_it_begin, InputIt input_it_end, OutputIt output_it);

std::vector<uint8_t> encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> encode(uint8_t command_byte, const std::vector<uint8_t>& data);

std::vector<uint8_t> encode(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> result;

    encode(data.begin(), data.end(), std::back_inserter(result));

    return result;
}

std::vector<uint8_t> encode(uint8_t command_byte, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> result;

    encode(command_byte, data.begin(), data.end(), std::back_inserter(result));

    return result;
}

LIBMODEM_KISS_NAMESPACE_END

LIBMODEM_NAMESPACE_END