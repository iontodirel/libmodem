// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// modem.h
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

#include <cmath>
#include <algorithm>
#include <optional>

#include "audio_stream.h"
#include "modulator.h"
#include "bitstream.h"
#include "io.h"

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

// **************************************************************** //
//                                                                  //
//                                                                  //
// modem_events                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct modem_events
{
    virtual void transmit(const packet& packet, uint64_t id) = 0;
    virtual void receive(const packet& packet, uint64_t id) = 0;
    virtual void transmit(const std::vector<uint8_t>& bitstream, uint64_t id) = 0;
    virtual void receive(const std::vector<uint8_t>& bitstream, uint64_t id) = 0;
    virtual void ptt(bool state, uint64_t id) = 0;
    virtual void data_carrier_detected(uint64_t id) = 0;
    virtual void render_audio(const std::vector<uint8_t>& samples, uint64_t id) = 0;
    virtual void capture_audio(const std::vector<uint8_t>& samples, uint64_t id) = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// ptt_control_base                                                 //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct ptt_control_base
{
    virtual ~ptt_control_base() = default;
    virtual void ptt(bool enable) = 0;
    virtual bool ptt() = 0;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// modem                                                            //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct modem
{
    void initialize();
    void initialize(audio_stream_base& stream, modulator_base& modulator);
    void initialize(audio_stream_base& stream, modulator_base& modulator, bitstream_converter_base& converter);
    void initialize(audio_stream_base& stream, modulator_base& modulator, bitstream_converter_base& converter, ptt_control_base& ptt_control);

    void output_stream(audio_stream_base& stream);
    void modulator(modulator_base& modulator);
    void converter(bitstream_converter_base& converter);
    void ptt_control(ptt_control_base& ptt_control);

    void transmit();
    void transmit(packet p);
    void transmit(const std::vector<uint8_t>& bits, bool reset_modulator = true);

    void start();
    void stop();

    template<typename Func, typename... Args>
        requires std::invocable<std::decay_t<Func>, const packet&, uint64_t, std::decay_t<Args>...> ||
                 std::invocable<std::decay_t<Func>, const std::vector<uint8_t>&, uint64_t, std::decay_t<Args>...>
    uint32_t add_on_received(Func&& f, Args&&... args);

    bool remove_on_received(uint32_t cookie);

    size_t receive(std::vector<packet>& packets);

    void reset();

    void preemphasis(bool);
    bool preemphasis() const;
    void gain(double);
    double gain() const;
    void start_silence(int ms);
    int start_silence() const;
    void end_silence(int ms);
    int end_silence() const;
    void tx_delay(int ms);
    double tx_delay() const;
    void tx_tail(int ms);
    double tx_tail() const;
    void baud_rate(int baud_rate);
    int baud_rate() const;

private:
    struct received_callable_base
    {
        virtual void invoke(const struct packet& packet, uint64_t id) = 0;
        virtual void invoke(const std::vector<uint8_t>& bitstream, uint64_t id) = 0;
        virtual ~received_callable_base() = default;
    };

    template<typename Func, typename... Args>
    struct received_callable : public received_callable_base
    {
        template<typename F, typename... A>
        received_callable(F&& f, A&&... a) : func_(std::forward<F>(f)), args_(std::forward<A>(a)...)
        {
        }

        void invoke(const struct packet& packet, uint64_t id) override
        {
            if constexpr (std::invocable<Func, const struct packet&, uint64_t, Args...>)
            {
                std::apply([&](auto&&... a) { func_(packet, id, a...); }, args_);
            }
        }

        void invoke(const std::vector<uint8_t>& bitstream, uint64_t id) override
        {
            if constexpr (std::invocable<Func, const std::vector<uint8_t>&, uint64_t, Args...>)
            {
                std::apply([&](auto&&... a) { func_(bitstream, id, a...); }, args_);
            }
        }

    private:
        Func func_;
        std::tuple<Args...> args_;
    };

    void postprocess_audio(std::vector<double>& audio_buffer);
    void render_audio(const std::vector<double>& audio_buffer);
    void modulate_bitstream(const std::vector<uint8_t>& bitstream, std::vector<double>& audio_buffer, bool reset_modulator);
    void ptt(bool enable);

    std::optional<std::reference_wrapper<audio_stream_base>> audio;
    std::optional<std::reference_wrapper<modulator_base>> mod;
    std::optional<std::reference_wrapper<bitstream_converter_base>> conv;
    std::optional<std::reference_wrapper<ptt_control_base>> ptt_control_;
    int start_silence_duration_ms = 0;
    int end_silence_duration_ms = 0;
    bool preemphasis_enabled = false;
    double gain_value = 1.0; // Linear scale (1.0 = no change)
    int tx_delay_ms = 0;
    int tx_tail_ms = 0;
    int baud_rate_ = 1200;
    int preamble_flags = 1; // Number of HDLC flags before frame
    int postamble_flags = 1; // Number of HDLC flags after frame
    std::unordered_map<uint32_t, std::unique_ptr<received_callable_base>> received_callbacks_;
    uint32_t next_receiver_callback_cookie_ = 0;
    uint64_t next_data_id = 0;
};

template<typename Func, typename... Args>
    requires std::invocable<std::decay_t<Func>, const packet&, uint64_t, std::decay_t<Args>...> ||
             std::invocable<std::decay_t<Func>, const std::vector<uint8_t>&, uint64_t, std::decay_t<Args>...>
LIBMODEM_INLINE uint32_t modem::add_on_received(Func&& f, Args&&... args)
{
    uint32_t cookie = next_receiver_callback_cookie_++;

    if constexpr (std::is_lvalue_reference_v<Func>)
    {
        received_callbacks_[cookie] = std::make_unique<received_callable<std::decay_t<Func>*, std::decay_t<Args>...>>(&f, std::forward<Args>(args)...);
    }
    else
    {
        received_callbacks_[cookie] = std::make_unique<received_callable<std::decay_t<Func>, std::decay_t<Args>...>>(std::forward<Func>(f), std::forward<Args>(args)...);
    }

    return cookie;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// silence, gain, preemphasis                                       //
//                                                                  //
//                                                                  //
// **************************************************************** //

template<typename OutputIt>
LIBMODEM_INLINE void insert_silence(OutputIt out, int sample_rate, double duration_seconds = 0.1)
{
    int silence_samples = static_cast<int>(duration_seconds * sample_rate);

    for (int i = 0; i < silence_samples; ++i)
    {
        *out++ = 0.0;
    }
}

template<typename It>
LIBMODEM_INLINE void apply_gain(It first, It last, double gain)
{
    for (auto it = first; it != last; ++it)
    {
        *it *= gain;
    }
}

template<typename It>
LIBMODEM_INLINE void apply_preemphasis(It first, It last, int sample_rate, double tau = 75e-6)
{
    if (first == last) return;

    // Calculate filter coefficient from time constant
    // For 75μs at 48kHz: alpha_pre ≈ 0.845
    // This controls the pole location in the IIR filter
    double alpha_pre = std::exp(-1.0 / (sample_rate * tau));

    // Initialize filter state with first sample
    // Prevents startup transient
    double x_prev = *first;  // Previous input sample
    double y_prev = *first;  // Previous output sample
    ++first;  // Skip first sample (already used for initialization)

    // Apply first-order IIR high-pass filter
    // Transfer function: H(z) = (1 - z^-1) / (1 - alpha*z^-1)
    // This emphasizes high frequencies for FM pre-emphasis
    for (auto it = first; it != last; ++it)
    {
        double x = *it;  // Current input

        // Difference equation: y[n] = x[n] - x[n-1] + alpha * y[n-1]
        // Zero at DC (blocks low frequencies)
        // Pole at alpha (creates rising frequency response)
        double y = x - x_prev + alpha_pre * y_prev;

        // Update state for next iteration
        x_prev = x;
        y_prev = y;

        // Write filtered output back to input
        *it = y;
    }
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// null_ptt_control                                                 //
//                                                                  //
//                                                                  //
// **************************************************************** //

class null_ptt_control : public ptt_control_base
{
public:
    void ptt(bool enable) override;
    bool ptt() override;

private:
    bool ptt_ = false;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// serial_port_ptt_control                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

enum class serial_port_ptt_line
{
    rts,
    dtr
};

enum class serial_port_ptt_trigger
{
    off,
    on
};

class serial_port_ptt_control : public ptt_control_base
{
public:
    serial_port_ptt_control();
    serial_port_ptt_control(serial_port_base& serial_port);
    serial_port_ptt_control(serial_port_base& serial_port, serial_port_ptt_line line, serial_port_ptt_trigger trigger);

    void ptt(bool enable) override;
    bool ptt() override;

private:
    std::optional<std::reference_wrapper<serial_port_base>> serial_port_;
    serial_port_ptt_line line_ = serial_port_ptt_line::rts;
    serial_port_ptt_trigger trigger_ = serial_port_ptt_trigger::on;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// library_ptt_control                                              //
//                                                                  //
//                                                                  //
// **************************************************************** //

class library_ptt_control : public ptt_control_base
{
public:
    library_ptt_control() = default;
    library_ptt_control(ptt_control_library& library);

    void ptt(bool enable) override;
    bool ptt() override;

private:
    std::optional<std::reference_wrapper<ptt_control_library>> library_;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_ptt_control                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

class tcp_ptt_control : public ptt_control_base
{
public:
    tcp_ptt_control() = default;
    tcp_ptt_control(tcp_ptt_control_client& client);

    void ptt(bool enable) override;
    bool ptt() override;

private:
    std::optional<std::reference_wrapper<tcp_ptt_control_client>> client_;
};

LIBMODEM_NAMESPACE_END