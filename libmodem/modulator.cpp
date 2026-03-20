// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// modulator.cpp
//
// MIT License
//
// Copyright (c) 2025 Ion Todirel
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

#include "modulator.h"

#include <cmath>
#include <cassert>
#include <algorithm>

LIBMODEM_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// dds_afsk_modulator                                               //
//                                                                  //
//                                                                  //
// **************************************************************** //

dds_afsk_modulator_double::dds_afsk_modulator_double(double f_mark, double f_space, int bitrate, int sample_rate, double alpha)
{
    this->f_mark = f_mark;
    this->f_space = f_space;
    this->sample_rate = sample_rate;
    this->alpha = alpha;

    freq_smooth = f_mark;
    samples_per_bit_ = static_cast<double>(sample_rate) / bitrate;
    samples_per_bit_error_ = 0.0;
}

double dds_afsk_modulator_double::modulate(uint8_t bit) noexcept
{
    assert(bit == 0 || bit == 1);

    // DDS (Direct Digital Synthesis) AFSK modulator - processes one sample at a time
    // This function implements the core modulation algorithm:
    //   - Map input bit to target frequency
    //   - Smooth frequency transitions using exponential smoothing (IIR filter)
    //   - Accumulate phase and wrap around to prevent overflow
    //   - Generate output sample using cosine of the current phase
    //
    // Process one bit and generate one audio sample
    // Call this function at the sample rate (e.g., 48000 times/second
    // Each bit must be held for samples_per_bit samples to achieve correct baud rate

    constexpr double two_pi = 2.0 * 3.14159265358979323846;

    // Select target frequency based on input bit
    // Mark (1) = lower frequency, Space (0) = higher frequency
    // For AFSK1200: mark=1200Hz, space=2200Hz
    double freq_target = (bit == 1) ? f_mark : f_space;

    // Exponential smoothing (single-pole IIR low-pass filter)
    // This smooths abrupt frequency changes to reduce spectral splatter
    // Formula: y[n] = α·x[n] + (1 - α)·y[n - 1]
    // where α (alpha) controls smoothing: lower = smoother, higher = sharper
    // Typical α = 0.08 balances clean spectrum with decoder timing requirements
    freq_smooth = alpha * freq_target + (1.0 - alpha) * freq_smooth;

    // Phase accumulation(the "DDS" core)
    // Phase advances by 2π·f/fs radians per sample
    // This creates the desired output frequency
    // fmod() wraps phase to [0, 2π) to prevent numerical precision loss
    // over long transmissions (phase would grow unbounded otherwise)
    phase = std::fmod(phase + two_pi * freq_smooth / sample_rate, two_pi);

    assert(phase >= 0.0 && phase < two_pi);

    // Generate output sample
    // Convert phase (0 to 2π radians) to amplitude using cosine function
    // cos(phase) oscillates between -1.0 and +1.0
    // Phase continuity ensures smooth transitions (no clicks/pops)
    return std::cos(phase);
}

void dds_afsk_modulator_double::reset() noexcept
{
    // WARNING: Calling this during transmission will create phase discontinuities!
    // Only call reset() before starting a new independent transmission where
    // phase continuity with previous data is not required.

    freq_smooth = f_mark;
    phase = 0.0;
}

int dds_afsk_modulator_double::next_samples_per_bit() noexcept
{
    // WARNING: Call only once per bit period
    // Calculate samples per bit with fractional error accumulation
    // This allows for non-integer samples per bit rates (e.g., 44100/1200 = 36.75)
    // While maintaining accurate timing over long transmissions

    double v = samples_per_bit_ + samples_per_bit_error_;

    int n = static_cast<int>(std::round(v));

    samples_per_bit_error_ = v - static_cast<double>(n);

    return n;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// gfsk_modulator                                                   //
//                                                                  //
//                                                                  //
// **************************************************************** //

gfsk_modulator_double::gfsk_modulator_double(int bitrate, int sample_rate, double bt)
{
    samples_per_bit_ = static_cast<double>(sample_rate) / bitrate;
    samples_per_bit_error_ = 0.0;

    // Gaussian lowpass FIR filter
    // Unlike windowed-sinc, Gaussian has zero overshoot (no Gibbs ringing)
    // BT product controls bandwidth: lower = smoother but more ISI
    // BT = 0.5 is a good balance for G3RUH 9600 baud
    //
    // The Gaussian impulse response is: h(t) = exp(-2*pi^2*BT^2*t^2/T^2)
    // where T = 1/bitrate (bit period), BT = bandwidth-time product

    constexpr double pi = 3.14159265358979323846;

    // Span: 4 bit periods is sufficient for Gaussian (decays to ~0 quickly)
    int span_bits = 4;
    int ntaps = static_cast<int>(span_bits * samples_per_bit_ + 0.5);

    if (ntaps < 9)
    {
        ntaps = 9;
    }

    if (ntaps % 2 == 0)
    {
        ntaps++;  // Force odd for symmetric FIR
    }

    fir_coeffs_.resize(ntaps);
    delay_line_.resize(ntaps, 0.0);
    delay_pos_ = 0;

    double sum = 0.0;
    double center = (ntaps - 1) / 2.0;
    double T = samples_per_bit_;  // samples per bit period

    for (int n = 0; n < ntaps; n++)
    {
        double t = (n - center) / T;  // time in bit periods

        // Gaussian: h(t) = exp(-2 * pi^2 * BT^2 * t^2)
        double h = std::exp(-2.0 * pi * pi * bt * bt * t * t);

        fir_coeffs_[n] = h;

        sum += h;
    }

    // Normalize so DC gain = 1.0
    for (int n = 0; n < ntaps; n++)
    {
        fir_coeffs_[n] /= sum;
    }
}

double gfsk_modulator_double::modulate(uint8_t bit) noexcept
{
    assert(bit == 0 || bit == 1);

    double nrz = (bit == 1) ? +1.0 : -1.0;

    // Write the current NRZ sample into the delay line at the current position.
    // The delay line is a circular buffer that holds the most recent 'ntaps'
    // input samples. As new samples arrive, they overwrite the oldest entry,
    // so the buffer always contains the last ntaps inputs without any shifting.
    delay_line_[delay_pos_] = nrz;

    // FIR convolution (Gaussian lowpass)
    // Shapes NRZ transitions with zero overshoot and smooth spectral rolloff.
    //
    // The output sample is the dot product of the FIR coefficients with the
    // delay line contents. We walk backwards from the current write position
    // through the circular buffer, wrapping around to the end when we pass
    // index 0. This produces the same result as a linear convolution but
    // avoids shifting the entire buffer on every sample.

    double output = 0.0;

    int ntaps = static_cast<int>(fir_coeffs_.size());
    int pos = delay_pos_;

    for (int i = 0; i < ntaps; i++)
    {
        output += fir_coeffs_[i] * delay_line_[pos];

        if (--pos < 0)
        {
            pos = ntaps - 1;
        }
    }

    // Advance the circular buffer write position by one sample.
    // Each call to modulate() processes one audio sample at the sample rate.
    // The write position cycles through 0 to ntaps-1, so after ntaps samples
    // we overwrite the oldest entry.
    if (++delay_pos_ >= ntaps)
    {
        delay_pos_ = 0;
    }

    return output;
}

void gfsk_modulator_double::reset() noexcept
{
    std::fill(delay_line_.begin(), delay_line_.end(), 0.0);
    delay_pos_ = 0;
}

int gfsk_modulator_double::next_samples_per_bit() noexcept
{
    // WARNING: Call only once per bit period
    // Calculate samples per bit with fractional error accumulation
    // This allows for non-integer samples per bit rates (e.g., 44100/1200 = 36.75)
    // While maintaining accurate timing over long transmissions

    double v = samples_per_bit_ + samples_per_bit_error_;

    int n = static_cast<int>(std::round(v));

    samples_per_bit_error_ = v - static_cast<double>(n);

    return n;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// sine_fsk_modulator                                               //
//                                                                  //
//                                                                  //
// **************************************************************** //

sine_fsk_modulator_double::sine_fsk_modulator_double(int bitrate, int sample_rate, double amplitude) : amplitude_(amplitude)
{
    samples_per_bit_ = static_cast<double>(sample_rate) / bitrate;
    samples_per_bit_error_ = 0.0;

    constexpr double pi = 3.14159265358979323846;

    int n = static_cast<int>(samples_per_bit_ + 0.5);

    // Half-cosine transition table: maps from +1 to -1 over one bit period.
    // On a 1->0 transition, output these values directly.
    // On a 0->1 transition, output them negated.
    // On a steady bit, output +/-1.0 directly -- no table needed.
    transition_table_.resize(n);

    for (int i = 0; i < n; i++)
    {
        transition_table_[i] = std::cos(pi * i / n);
    }

    level_ = 1.0;
}

double sine_fsk_modulator_double::modulate(uint8_t bit) noexcept
{
    assert(bit == 0 || bit == 1);

    // Called once per sample. On the first sample of a new bit,
    // decide whether we are transitioning or steady, then serve
    // one sample from the appropriate source.

    if (sample_index_ == 0)
    {
        double target = (bit == 1) ? 1.0 : -1.0;

        transitioning_ = (target != level_);

        if (!transitioning_)
        {
            level_ = target;
        }
    }

    double output;

    if (transitioning_)
    {
        // Walk through the half-cosine curve.
        // Table goes +1->-1; negate if we are going -1->+1.
        double t = transition_table_[sample_index_];

        output = (level_ > 0.0) ? t : -t;

        // On the last sample, commit the new level
        int n = static_cast<int>(transition_table_.size());

        if (sample_index_ == n - 1)
        {
            level_ = -level_;
        }
    }
    else
    {
        output = level_;
    }

    if (++sample_index_ >= static_cast<int>(transition_table_.size()))
    {
        sample_index_ = 0;
    }

    return output * amplitude_;
}

void sine_fsk_modulator_double::reset() noexcept
{
    level_ = 1.0;
    sample_index_ = 0;
    transitioning_ = false;
    samples_per_bit_error_ = 0.0;
}

int sine_fsk_modulator_double::next_samples_per_bit() noexcept
{
    double v = samples_per_bit_ + samples_per_bit_error_;

    int n = static_cast<int>(std::round(v));

    samples_per_bit_error_ = v - static_cast<double>(n);

    return n;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// modulator_base                                                   //
//                                                                  //
//                                                                  //
// **************************************************************** //

double modulator_base::modulate_double(uint8_t bit) noexcept
{
    (void)bit;
    return 0.0;
}

float modulator_base::modulate_float(uint8_t bit) noexcept
{
    (void)bit;
    return 0.0f;
}

int16_t modulator_base::modulate_int(uint8_t bit) noexcept
{
    (void)bit;
    return 0;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// dds_afsk_modulator_adapter                                       //
//                                                                  //
//                                                                  //
// **************************************************************** //

dds_afsk_modulator_double_adapter::dds_afsk_modulator_double_adapter(double f_mark, double f_space, int bitrate, int sample_rate, double alpha) : modulator(f_mark, f_space, bitrate, sample_rate, alpha)
{
}

double dds_afsk_modulator_double_adapter::modulate_double(uint8_t bit) noexcept
{
    return modulator.modulate(bit);
}

void dds_afsk_modulator_double_adapter::reset() noexcept
{
    modulator.reset();
}

int dds_afsk_modulator_double_adapter::next_samples_per_bit() noexcept
{
    return modulator.next_samples_per_bit();
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// gfsk_modulator_adapter                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

gfsk_modulator_double_adapter::gfsk_modulator_double_adapter(int bitrate, int sample_rate, double bt) : modulator(bitrate, sample_rate, bt)
{
}

double gfsk_modulator_double_adapter::modulate_double(uint8_t bit) noexcept
{
    return modulator.modulate(bit);
}

void gfsk_modulator_double_adapter::reset() noexcept
{
    modulator.reset();
}

int gfsk_modulator_double_adapter::next_samples_per_bit() noexcept
{
    return modulator.next_samples_per_bit();
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// sine_fsk_modulator_adapter                                       //
//                                                                  //
//                                                                  //
// **************************************************************** //

sine_fsk_modulator_double_adapter::sine_fsk_modulator_double_adapter(int bitrate, int sample_rate, double amplitude) : modulator(bitrate, sample_rate, amplitude)
{
}

double sine_fsk_modulator_double_adapter::modulate_double(uint8_t bit) noexcept
{
    return modulator.modulate(bit);
}

void sine_fsk_modulator_double_adapter::reset() noexcept
{
    modulator.reset();
}

int sine_fsk_modulator_double_adapter::next_samples_per_bit() noexcept
{
    return modulator.next_samples_per_bit();
}

LIBMODEM_NAMESPACE_END