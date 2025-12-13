// **************************************************************** //
// modem - APRS modem                                               // 
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

#include <cassert>

// **************************************************************** //
//                                                                  //
//                                                                  //
// dds_afsk_modulator                                               //
//                                                                  //
//                                                                  //
// **************************************************************** //

dds_afsk_modulator_f64::dds_afsk_modulator_f64(double f_mark = 1200.0, double f_space = 2200.0, int bitrate = 1200, int sample_rate = 48000, double alpha = 1.0)
{
    this->f_mark = f_mark;
    this->f_space = f_space;
    this->sample_rate = sample_rate;
    this->alpha = alpha;

    freq_smooth = f_mark;
    samples_per_bit_ = static_cast<double>(sample_rate) / bitrate;
	samples_per_bit_error_ = 0.0;
}

double dds_afsk_modulator_f64::modulate(uint8_t bit)
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

void dds_afsk_modulator_f64::reset()
{
    // WARNING: Calling this during transmission will create phase discontinuities!
    // Only call reset() before starting a new independent transmission where
    // phase continuity with previous data is not required.

    freq_smooth = f_mark;
    phase = 0.0;
}

int dds_afsk_modulator_f64::next_samples_per_bit()
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
// modulator_base                                                   //
//                                                                  //
//                                                                  //
// **************************************************************** //

double modulator_base::modulate_double(uint8_t bit)
{
	(void)bit;
    return 0.0;
}

float modulator_base::modulate_float(uint8_t bit)
{
	(void)bit;
    return 0.0f;
}

int16_t modulator_base::modulate_int(uint8_t bit)
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

dds_afsk_modulator_f64_adapter::dds_afsk_modulator_f64_adapter(double f_mark, double f_space, int bitrate, int sample_rate, double alpha) : dds_mod(f_mark, f_space, bitrate, sample_rate, alpha)
{
}

double dds_afsk_modulator_f64_adapter::modulate_double(uint8_t bit)
{
    return dds_mod.modulate(bit);
}

void dds_afsk_modulator_f64_adapter::reset()
{
    dds_mod.reset();
}

int dds_afsk_modulator_f64_adapter::next_samples_per_bit()
{
    return dds_mod.next_samples_per_bit();
}
