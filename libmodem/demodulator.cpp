// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// demodulator.cpp
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

#include "demodulator.h"

#include <cmath>

// **************************************************************** //
//                                                                  //
//                                                                  //
// goertzel_afsk_demodulator_double                                 //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_NAMESPACE_BEGIN

goertzel_afsk_demodulator_double::goertzel_afsk_demodulator_double(double f_mark, double f_space, int bitrate, int sample_rate) : f_mark_(f_mark), f_space_(f_space), sample_rate_(sample_rate)
{
    samples_per_bit_ = static_cast<double>(sample_rate) / bitrate;
    samples_per_bit_error_ = 0.0;

    constexpr double two_pi = 2.0 * 3.14159265358979323846;

    coeff_mark_ = 2.0 * std::cos(two_pi * f_mark / sample_rate);
    coeff_space_ = 2.0 * std::cos(two_pi * f_space / sample_rate);

    begin_bit();
}

void goertzel_afsk_demodulator_double::begin_bit() noexcept
{
    next_samples_per_bit();
    sample_count_ = 0;
    s1_mark_ = 0.0;
    s2_mark_ = 0.0;
    s1_space_ = 0.0;
    s2_space_ = 0.0;
}

int goertzel_afsk_demodulator_double::next_samples_per_bit() noexcept
{
    double v = samples_per_bit_ + samples_per_bit_error_;

    int n = static_cast<int>(std::round(v));

    samples_per_bit_error_ = v - static_cast<double>(n);

    current_bit_samples_ = n;

    return n;
}

bool goertzel_afsk_demodulator_double::try_demodulate(double sample, uint8_t& bit) noexcept
{
    double s0_mark = sample + coeff_mark_ * s1_mark_ - s2_mark_;
    s2_mark_ = s1_mark_;
    s1_mark_ = s0_mark;

    double s0_space = sample + coeff_space_ * s1_space_ - s2_space_;
    s2_space_ = s1_space_;
    s1_space_ = s0_space;

    if (++sample_count_ < current_bit_samples_)
    {
        return false;
    }

    double mag2_mark = s1_mark_ * s1_mark_ + s2_mark_ * s2_mark_ - coeff_mark_ * s1_mark_ * s2_mark_;

    double mag2_space = s1_space_ * s1_space_ + s2_space_ * s2_space_ - coeff_space_ * s1_space_ * s2_space_;

    bit = (mag2_mark > mag2_space) ? 1 : 0;

    begin_bit();

    return true;
}

void goertzel_afsk_demodulator_double::reset() noexcept
{
    samples_per_bit_error_ = 0.0;
    begin_bit();
}

LIBMODEM_NAMESPACE_END