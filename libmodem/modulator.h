// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// modulator.h
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

#pragma once

#include <cstdint>
#include <vector>
#include <array>

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

LIBMODEM_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// modulator_bit_clock                                              //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct modulator_bit_clock
{
    modulator_bit_clock(int sample_rate, int bitrate);
    int next_samples_per_bit() noexcept;
    void reset() noexcept;

private:
    double samples_per_bit_;
    double samples_per_bit_error_ = 0.0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// dds_afsk_modulator_double                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct dds_afsk_modulator_double
{
    dds_afsk_modulator_double(double f_mark = 1200.0, double f_space = 2200.0, int bitrate = 1200, int sample_rate = 48000, double alpha = 1.0);

    double modulate(uint8_t bit) noexcept;
    void reset() noexcept;
    int next_samples_per_bit() noexcept;

private:
    double f_mark;
    double f_space;
    int sample_rate;
    double alpha;
    double freq_smooth;
    double phase = 0.0;
    double samples_per_bit_;
    double samples_per_bit_error_ = 0.0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// gfsk_modulator_double                                            //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct gfsk_modulator_double
{
    gfsk_modulator_double(int bitrate = 9600, int sample_rate = 48000, double bt = 0.8);

    double modulate(uint8_t bit) noexcept;
    void reset() noexcept;
    int next_samples_per_bit() noexcept;

private:
    static constexpr int max_fir_taps = 256;
    std::array<double, max_fir_taps> fir_coeffs_{}; // Gaussian FIR pulse-shaping coefficients
    std::array<double, max_fir_taps> delay_line_{}; // Circular buffer for FIR filter
    int fir_taps_ = 0;
    int delay_pos_ = 0; // Current write position in circular buffer
    double samples_per_bit_;
    double samples_per_bit_error_ = 0.0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// sine_fsk_modulator_double                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct sine_fsk_modulator_double
{
    sine_fsk_modulator_double(int bitrate = 9600, int sample_rate = 192000);

    double modulate(uint8_t bit) noexcept;
    void reset() noexcept;
    int next_samples_per_bit() noexcept;

private:
    static constexpr int max_transition_samples = 256;
    std::array<double, max_transition_samples> transition_table_{};
    int transition_table_size_ = 0;
    double level_ = 1.0;
    int sample_index_ = 0;
    int current_bit_samples_ = 0;
    bool transitioning_ = false;
    double samples_per_bit_;
    double samples_per_bit_error_ = 0.0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// modulator_base                                                   //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct modulator_base
{
    modulator_base(int sample_rate, int bitrate);

    virtual double modulate_double(uint8_t bit) noexcept;
    virtual float modulate_float(uint8_t bit) noexcept;
    virtual int16_t modulate_int(uint8_t bit) noexcept;
    virtual std::vector<double> modulate_doubles(uint8_t bit) noexcept;
    virtual std::vector<float> modulate_floats(uint8_t bit) noexcept;
    virtual std::vector<int16_t> modulate_ints(uint8_t bit) noexcept;
    virtual void reset() noexcept;
    virtual int next_samples_per_bit() noexcept = 0;
    virtual ~modulator_base() = default;

private:
    modulator_bit_clock bit_clock_;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// dds_afsk_modulator_double_adapter                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct dds_afsk_modulator_double_adapter : public modulator_base
{
    dds_afsk_modulator_double_adapter(double f_mark = 1200.0, double f_space = 2200.0, int bitrate = 1200, int sample_rate = 48000, double alpha = 1.0);

    double modulate_double(uint8_t bit) noexcept override;
    void reset() noexcept override;
    int next_samples_per_bit() noexcept override;

private:
    dds_afsk_modulator_double modulator;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// gfsk_modulator_double_adapter                                    //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct gfsk_modulator_double_adapter : public modulator_base
{
    gfsk_modulator_double_adapter(int bitrate = 9600, int sample_rate = 48000, double bt = 0.8);

    double modulate_double(uint8_t bit) noexcept override;
    void reset() noexcept override;
    int next_samples_per_bit() noexcept override;

private:
    gfsk_modulator_double modulator;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// sine_fsk_modulator_double_adapter                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct sine_fsk_modulator_double_adapter : public modulator_base
{
    sine_fsk_modulator_double_adapter(int bitrate = 9600, int sample_rate = 192000);

    double modulate_double(uint8_t bit) noexcept override;
    void reset() noexcept override;
    int next_samples_per_bit() noexcept override;

private:
    sine_fsk_modulator_double modulator;
};

LIBMODEM_NAMESPACE_END