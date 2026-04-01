// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// demodulator.h
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

#include <cstdint>

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

// **************************************************************** //
//                                                                  //
//                                                                  //
// sdft_afsk_demodulator_double                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// biquad_bandpass                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct biquad_bandpass
{
    biquad_bandpass() = default;
    biquad_bandpass(double f_center, double bandwidth, int sample_rate);

    double process(double sample) noexcept;
    void reset() noexcept;

private:
    double b0_ = 0;
    double b2_ = 0;
    double a1_ = 0;
    double a2_ = 0;
    double w1_ = 0;
    double w2_ = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// pll_gardner                                                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct pll_gardner
{
    pll_gardner() = default;
    pll_gardner(double samples_per_bit, double alpha);

    bool advance(double soft) noexcept;
    void reset() noexcept;

private:
    double phase_ = 0;
    double freq_ = 0;
    double freq_nominal_ = 0;
    double alpha_ = 0;

    // Gardner TED state
    double mid_soft_ = 0;
    double prev_soft_ = 0;
    bool mid_captured_ = false;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// sdft_afsk_demodulator_double                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct sdft_afsk_demodulator_double
{
    sdft_afsk_demodulator_double(double f_mark = 1200.0, double f_space = 2200.0, int bitrate = 1200, int sample_rate = 48000);

    bool try_demodulate(double sample, uint8_t& bit) noexcept;
    void reset() noexcept;

private:
    double samples_per_bit_ = 0; // exact ratio (e.g. 36.75)
    static constexpr int sdft_window_length = 51;
    static constexpr double bpf_extra_bandwidth = 600.0;
    static constexpr double pll_alpha = -0.07;
    int current_bit_samples_ = sdft_window_length;

    // Band-pass filter
    biquad_bandpass bpf_;

    // Circular buffer for sliding DFT
    static constexpr int max_window_size = 64;
    double sdft_window_[max_window_size] = {};
    int sdft_window_pos_ = 0;
    int sdft_samples_fed_ = 0;

    // Sliding DFT complex state
    double re_mark_ = 0;
    double im_mark_ = 0;
    double re_space_ = 0;
    double im_space_ = 0;

    // Twiddle factors
    double tw_re_mark_ = 0;
    double tw_im_mark_ = 0;
    double tw_re_space_ = 0;
    double tw_im_space_ = 0;

    // W^N correction
    double wn_re_mark_ = 0;
    double wn_im_mark_ = 0;
    double wn_re_space_ = 0;
    double wn_im_space_ = 0;

    // PLL bit timing recovery
    pll_gardner pll_;
};

LIBMODEM_NAMESPACE_END
