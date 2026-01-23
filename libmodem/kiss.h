// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// kiss.h
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

#pragma once

#include <vector>
#include <string>
#include <iterator>
#include <utility>
#include <cstdint>

#ifndef LIBMODEM_KISS_NAMESPACE_BEGIN
#define LIBMODEM_KISS_NAMESPACE_BEGIN namespace kiss {
#endif
#ifndef LIBMODEM_KISS_NAMESPACE_END
#define LIBMODEM_KISS_NAMESPACE_END }
#endif
#ifndef LIBMODEM_KISS_USING_NAMESPACE
#define LIBMODEM_KISS_USING_NAMESPACE using namespace kiss;
#endif
#ifndef LIBMODEM_KISS_NAMESPACE_REFERENCE
#define LIBMODEM_KISS_NAMESPACE_REFERENCE kiss :: 
#endif

#ifndef LIBMODEM_NAMESPACE
#define LIBMODEM_NAMESPACE libmodem
#endif
#ifndef LIBMODEM_NAMESPACE_BEGIN
#define LIBMODEM_NAMESPACE_BEGIN namespace LIBMODEM_NAMESPACE {
#endif
#ifndef LIBMODEM_NAMESPACE_REFERENCE
#define LIBMODEM_NAMESPACE_REFERENCE libmodem :: 
#endif
#ifndef LIBMODEM_NAMESPACE_END
#define LIBMODEM_NAMESPACE_END }
#endif

#ifndef LIBMODEM_INLINE
#define LIBMODEM_INLINE inline
#endif

LIBMODEM_NAMESPACE_BEGIN

LIBMODEM_KISS_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// frame_marker                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

enum class frame_marker : unsigned char
{
    fend = 0xC0,
    fesc = 0xDB,
    tfend = 0xDC,
    tfesc = 0xDD
};

bool operator==(unsigned char left, frame_marker right);

// **************************************************************** //
//                                                                  //
//                                                                  //
// decode                                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct decoder_state
{
    bool in_kiss_frame = false;
    bool in_escape_mode = false;
    bool completed = false;
};

bool decode(uint8_t byte, uint8_t& result, decoder_state& state);

// **************************************************************** //
//                                                                  //
//                                                                  //
// decoder                                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

enum class command : unsigned char
{
    data_frame = 0,
    tx_delay = 1,
    p = 2,
    slot_time = 3,
    tx_tail = 4,
    full_duplex = 5,
    set_hw = 6,
    ret = 0xff
};

struct frame
{
    unsigned char command_byte;
    std::vector<unsigned char> data;
};

struct decoder
{
    template <std::input_iterator InputIterator>
    bool decode(InputIterator input_it_begin, InputIterator input_it_end);

    const std::vector<struct frame>& frames() const;

    size_t count() const;

    void reset();
    void clear();

private:
    std::vector<struct frame> data_;
    std::vector<unsigned char> buffer_;
    decoder_state state_;
};

template <std::input_iterator InputIterator>
LIBMODEM_INLINE bool decoder::decode(InputIterator input_it_begin, InputIterator input_it_end)
{
    size_t current_data_count = data_.size();

    for (auto it = input_it_begin; it != input_it_end; ++it)
    {
        uint8_t byte = *it;

        uint8_t decoded_byte;

        bool result = LIBMODEM_KISS_NAMESPACE_REFERENCE decode(byte, decoded_byte, state_);

        if (result)
        {
            buffer_.push_back(decoded_byte);
        }

        if (state_.completed)
        {
            state_ = {};

            frame f;
            f.command_byte = buffer_[0];
            f.data = std::vector<unsigned char>(buffer_.begin() + 1, buffer_.end());
            data_.emplace_back(f);

            buffer_.clear();
        }
    }

    // True if we do not have incomplete data
    return (current_data_count < data_.size()) && buffer_.empty() && !state_.in_kiss_frame;
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

template <std::input_iterator InputIt, std::output_iterator<unsigned char> OutputIt, bool Command = true>
std::pair<OutputIt, bool> encode(uint8_t command_byte, InputIt input_it_begin, InputIt input_it_end, OutputIt output_it);

std::vector<uint8_t> encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> encode(uint8_t command_byte, const std::vector<uint8_t>& data);

template <std::input_iterator InputIt, std::output_iterator<unsigned char> OutputIt>
LIBMODEM_INLINE std::pair<OutputIt, bool> encode(InputIt input_it_begin, InputIt input_it_end, OutputIt output_it)
{
    return encode<InputIt, OutputIt, false>(0, input_it_begin, input_it_end, output_it);
}

template <std::input_iterator InputIt, std::output_iterator<unsigned char> OutputIt, bool Command>
LIBMODEM_INLINE std::pair<OutputIt, bool> encode(uint8_t command_byte, InputIt input_it_begin, InputIt input_it_end, OutputIt output_it)
{
    if (input_it_begin == input_it_end)
    {
        return { output_it, false };
    }

    *output_it++ = static_cast<unsigned char>(frame_marker::fend);

    if constexpr (Command)
    {
        *output_it++ = command_byte;
    }

    for (auto input_it = input_it_begin; input_it != input_it_end; ++input_it)
    {
        unsigned char b = *input_it;

        if (b == frame_marker::fend)
        {
            *output_it++ = static_cast<unsigned char>(frame_marker::fesc);
            *output_it++ = static_cast<unsigned char>(frame_marker::tfend);
        }
        else if (b == frame_marker::fesc)
        {
            *output_it++ = static_cast<unsigned char>(frame_marker::fesc);
            *output_it++ = static_cast<unsigned char>(frame_marker::tfesc);
        }
        else
        {
            *output_it++ = b;
        }
    }

    *output_it++ = static_cast<unsigned char>(frame_marker::fend);

    return { output_it, true };
}

LIBMODEM_KISS_NAMESPACE_END

LIBMODEM_NAMESPACE_END
