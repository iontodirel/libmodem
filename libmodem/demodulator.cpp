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
#include <algorithm>

// **************************************************************** //
//                                                                  //
//                                                                  //
// biquad_bandpass                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_NAMESPACE_BEGIN

biquad_bandpass::biquad_bandpass(double f_center, double bandwidth, int sample_rate)
{
    constexpr double two_pi = 2.0 * 3.14159265358979323846;

    double omega0 = two_pi * f_center / sample_rate;
    double Q = f_center / bandwidth;
    double a = std::sin(omega0) / (2.0 * Q);
    double a0_inv = 1.0 / (1.0 + a);

    b0_ = a * a0_inv;
    b2_ = -a * a0_inv;
    a1_ = -2.0 * std::cos(omega0) * a0_inv;
    a2_ = (1.0 - a) * a0_inv;
}

double biquad_bandpass::process(double sample) noexcept
{
    double w = sample - a1_ * w1_ - a2_ * w2_;
    double out = b0_ * w + b2_ * w2_;

    w2_ = w1_;
    w1_ = w;

    return out;
}

void biquad_bandpass::reset() noexcept
{
    w1_ = 0;
    w2_ = 0;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// pll_gardner                                                      //
//                                                                  //
//                                                                  //
// **************************************************************** //

pll_gardner::pll_gardner(double samples_per_bit, double alpha)
{
    freq_ = 1.0 / samples_per_bit;
    freq_nominal_ = freq_;
    alpha_ = alpha;
}

bool pll_gardner::advance(double soft) noexcept
{
    phase_ += freq_;

    // Capture mid-bit soft value for Gardner TED
    if (!mid_captured_ && phase_ >= 0.5)
    {
        mid_soft_ = soft;
        mid_captured_ = true;
    }

    // Check if PLL triggers a bit decision
    if (phase_ < 1.0)
    {
        return false;
    }

    phase_ -= 1.0;
    mid_captured_ = false;

    // Gardner TED: only at transitions
    double prev_sign = (prev_soft_ > 0) ? 1.0 : -1.0;
    double curr_sign = (soft > 0) ? 1.0 : -1.0;

    if (prev_sign != curr_sign)
    {
        double error = mid_soft_ * (prev_sign - curr_sign);
        double norm = std::max(std::abs(prev_soft_), std::abs(soft));

        if (norm > 0)
        {
            error /= norm;
        }

        phase_ += alpha_ * error;

        // Clamp frequency
        double max_dev = freq_nominal_ * 0.05;

        freq_ = std::max(freq_nominal_ - max_dev, std::min(freq_nominal_ + max_dev, freq_));
    }

    prev_soft_ = soft;

    return true;
}

void pll_gardner::reset() noexcept
{
    phase_ = 0;
    mid_soft_ = 0;
    prev_soft_ = 0;
    mid_captured_ = false;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// sdft_afsk_demodulator_double                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

sdft_afsk_demodulator_double::sdft_afsk_demodulator_double(double f_mark, double f_space, int bitrate, int sample_rate)
{
    constexpr double two_pi = 2.0 * 3.14159265358979323846;
    constexpr int N = sdft_window_length;

    samples_per_bit_ = static_cast<double>(sample_rate) / bitrate;

    // DFT bin indices: k = f * N / sample_rate
    double k_mark = f_mark * N / static_cast<double>(sample_rate);
    double k_space = f_space * N / static_cast<double>(sample_rate);

    // Twiddle factors for sDFT rotation: e^{-j*2*pi*k/N}
    tw_re_mark_ =  std::cos(two_pi * k_mark / N);
    tw_im_mark_ = -std::sin(two_pi * k_mark / N);
    tw_re_space_ =  std::cos(two_pi * k_space / N);
    tw_im_space_ = -std::sin(two_pi * k_space / N);

    // W^N correction for fractional bins: e^{-j*2*pi*k}
    wn_re_mark_ =  std::cos(two_pi * k_mark);
    wn_im_mark_ = -std::sin(two_pi * k_mark);
    wn_re_space_ =  std::cos(two_pi * k_space);
    wn_im_space_ = -std::sin(two_pi * k_space);

    // Band-pass filter
    double f_center = (f_mark + f_space) / 2.0;
    double bw = (f_space - f_mark) + bpf_extra_bandwidth;
    bpf_ = biquad_bandpass(f_center, bw, sample_rate);

    // PLL bit timing recovery
    pll_ = pll_gardner(samples_per_bit_, pll_alpha);
}

bool sdft_afsk_demodulator_double::try_demodulate(double sample, uint8_t& bit) noexcept
{
    int N = current_bit_samples_;

    sample = bpf_.process(sample);

    // Update sDFT circular buffer
    double x_old = sdft_window_[sdft_window_pos_];
    sdft_window_[sdft_window_pos_] = sample;
    // Advance circular buffer position, wrapping around the window
    sdft_window_pos_ = (sdft_window_pos_ + 1) % N;

    // Sliding DFT update: Z[n] = twiddle * Z[n-1] + x_new - x_old * W^N
    //
    // z_re/z_im = twiddle rotation of previous bin (complex multiply)
    // + sample (real only) = new sample entering the window
    // - x_old * wn (complex) = old sample leaving the window, corrected by W^N

    double z_re_mark = tw_re_mark_ * re_mark_ - tw_im_mark_ * im_mark_;
    double z_im_mark = tw_im_mark_ * re_mark_ + tw_re_mark_ * im_mark_;
    re_mark_ = z_re_mark + sample - x_old * wn_re_mark_;
    im_mark_ = z_im_mark          - x_old * wn_im_mark_;

    double z_re_space = tw_re_space_ * re_space_ - tw_im_space_ * im_space_;
    double z_im_space = tw_im_space_ * re_space_ + tw_re_space_ * im_space_;
    re_space_ = z_re_space + sample - x_old * wn_re_space_;
    im_space_ = z_im_space          - x_old * wn_im_space_;

    // Compute mark_space_diff from sDFT magnitudes
    double mag2_mark  = re_mark_ * re_mark_ + im_mark_ * im_mark_;
    double mag2_space = re_space_ * re_space_ + im_space_ * im_space_;
    double mark_space_diff = mag2_mark - mag2_space;

    // Wait for buffer to fill
    if (sdft_samples_fed_ < N)
    {
        sdft_samples_fed_++;
    }

    // PLL decides when to output a bit
    if (sdft_samples_fed_ < N || !pll_.advance(mark_space_diff))
    {
        return false;
    }

    bit = (mark_space_diff > 0) ? 1 : 0;

    return true;
}

void sdft_afsk_demodulator_double::reset() noexcept
{
    std::fill(std::begin(sdft_window_), std::end(sdft_window_), 0.0);
    sdft_window_pos_ = 0;
    sdft_samples_fed_ = 0;

    re_mark_ = 0;
    im_mark_ = 0;
    re_space_ = 0;
    im_space_ = 0;

    bpf_.reset();
    pll_.reset();
}

LIBMODEM_NAMESPACE_END