// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// modem.cpp
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

#include "modem.h"

#include "modulator.h"
#include "bitstream.h"
#include "audio_stream.h"

#include <memory>
#include <thread>
#include <algorithm>

// Turns out opening the port can assert the RTS line to on
// If this macro is defined, we will call ptt(false) in the ctor of serial_port_ptt_control
#ifndef LIBMODEM_AUTO_PTT_DISABLE
#define LIBMODEM_AUTO_PTT_DISABLE 1
#endif // LIBMODEM_AUTO_PTT_DISABLE

LIBMODEM_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// modem                                                            //
//                                                                  //
//                                                                  //
// **************************************************************** //

void modem::initialize()
{
    double ms_per_flag = (8.0 * 1000.0) / baud_rate_;

    preamble_flags = (std::max)(static_cast<int>(tx_delay_ms / ms_per_flag), 1);
    postamble_flags = (std::max)(static_cast<int>(tx_tail_ms / ms_per_flag), 1);
}

void modem::initialize(audio_stream_base& stream, modulator_base& modulator)
{
    audio = std::ref(stream);
    mod = std::ref(modulator);

    double ms_per_flag = (8.0 * 1000.0) / baud_rate_;

    preamble_flags = (std::max)(static_cast<int>(tx_delay_ms / ms_per_flag), 1);
    postamble_flags = (std::max)(static_cast<int>(tx_tail_ms / ms_per_flag), 1);
}

void modem::initialize(audio_stream_base& stream, modulator_base& modulator, bitstream_converter_base& converter)
{
    conv = std::ref(converter);

    initialize(stream, modulator);
}

void modem::initialize(audio_stream_base& stream, modulator_base& modulator, bitstream_converter_base& converter, ptt_control_base& ptt_control)
{
    ptt_control_ = std::ref(ptt_control);

    initialize(stream, modulator, converter);
}

void modem::output_stream(audio_stream_base& stream)
{
    audio = std::ref(stream);
}

audio_stream_base& modem::output_stream()
{
    if (!audio.has_value())
    {
        throw std::runtime_error("No audio stream");
    }
    return audio.value().get();
}

void modem::modulator(modulator_base& modulator)
{
    mod = std::ref(modulator);
}

modulator_base& modem::modulator()
{
    if (!mod.has_value())
    {
        throw std::runtime_error("No modulator");
    }
    return mod.value().get();
}

void modem::converter(bitstream_converter_base& converter)
{
    conv = std::ref(converter);
}

bitstream_converter_base& modem::converter()
{
    if (!conv.has_value())
    {
        throw std::runtime_error("No bitstream converter");
    }
    return conv.value().get();
}

void modem::ptt_control(ptt_control_base& ptt_control)
{
    ptt_control_ = std::ref(ptt_control);
}

ptt_control_base& modem::ptt_control()
{
    if (!ptt_control_.has_value())
    {
        throw std::runtime_error("No PTT control");
    }
    return ptt_control_.value().get();
}

void modem::transmit()
{
    std::vector<uint8_t> bitstream({ 0 });

    transmit(bitstream);
}

void modem::transmit(packet p)
{
    if (!conv.has_value())
    {
        throw std::runtime_error("No bitstream converter");
    }

    bitstream_converter_base& converter = conv.value().get();

    // Convert packet to bitstream
    // 
    // - Compute CRC
    // - Append CRC to the AX.25 frame
    // - Convert bytes to bits (LSB-first)
    // - Bit-stuffing (insert 0 after five consecutive 1s)
    // - Add HDLC flags (0x7E) at start and end
    // - NRZI encoding (invert on 1, no change on 0)

    std::vector<uint8_t> bitstream = converter.encode(p, preamble_flags, postamble_flags);

    transmit(bitstream);
}

void modem::transmit(const std::vector<uint8_t>& bits, bool reset_modulator)
{
    if (!mod.has_value())
    {
        throw std::runtime_error("No modulator");
    }

    if (!audio.has_value())
    {
        throw std::runtime_error("No audio stream");
    }

    // A(FSK) modulation

    std::vector<double> audio_buffer;

    modulate_bitstream(bits, audio_buffer, reset_modulator);

    // Apply pre-emphasis filter and gain

    postprocess_audio(audio_buffer);

    // Render audio to output audio device

    render_audio(audio_buffer);
}

void modem::postprocess_audio(std::vector<double>& audio_buffer)
{
    if (!audio.has_value())
    {
        throw std::runtime_error("No audio stream");
    }

    struct audio_stream_base& audio_stream = audio.value().get();

    if (preemphasis_enabled)
    {
        apply_preemphasis(audio_buffer.begin(), audio_buffer.end(), audio_stream.sample_rate(), /*tau*/ 75e-6);
    }

    // Apply gain

    apply_gain(audio_buffer.begin(), audio_buffer.end(), gain_value);

    // Handle silence without inserting at the begining
    // Use audio_buffer_with_start_silence as copy buffer

    insert_silence(std::back_inserter(audio_buffer), audio_stream.sample_rate(), end_silence_duration_ms / 1000.0);

    std::vector<double> audio_buffer_with_start_silence;

    size_t start_silence_samples = static_cast<size_t>(start_silence_duration_ms / 1000.0 * audio_stream.sample_rate());

    audio_buffer_with_start_silence.reserve(start_silence_samples + audio_buffer.size());

    insert_silence(std::back_inserter(audio_buffer_with_start_silence), audio_stream.sample_rate(), start_silence_duration_ms / 1000.0);

    audio_buffer_with_start_silence.insert(audio_buffer_with_start_silence.end(), audio_buffer.begin(), audio_buffer.end());

    audio_buffer = std::move(audio_buffer_with_start_silence);
}

void modem::modulate_bitstream(const std::vector<uint8_t>& bitstream, std::vector<double>& audio_buffer, bool reset_modulator)
{
    if (!mod.has_value())
    {
        throw std::runtime_error("No modulator");
    }

    if (!audio.has_value())
    {
        throw std::runtime_error("No audio stream");
    }

    modulator_base& modulator = mod.value().get();

    audio_buffer.reserve(static_cast<int>(bitstream.size() * 1.5));

    for (uint8_t bit : bitstream)
    {
        int samples_per_bit = modulator.next_samples_per_bit();

        for (int i = 0; i < samples_per_bit; ++i)
        {
            audio_buffer.push_back(modulator.modulate_double(bit));
        }
    }

    if (reset_modulator)
    {
        modulator.reset();
    }
}

void modem::ptt(bool enable)
{
    if (ptt_control_.has_value())
    {
        ptt_control_base& ptt_control = ptt_control_.value().get();
        ptt_control.ptt(enable);
    }
}

void modem::render_audio(const std::vector<double>& audio_buffer)
{
    if (!audio.has_value())
    {
        throw std::runtime_error("No audio stream");
    }

    struct audio_stream_base& audio_stream = audio.value().get();

    try
    {
        // Start the playback
        audio_stream.start();

        ptt(true);

        // Write audio samples to output device
        // The stream will automatically handle buffering, the stream typically has a 200ms buffer
        //
        // This effectively starts the audible playback of the packet

        size_t written = 0;
        while (written < audio_buffer.size())
        {
            written += audio_stream.write(audio_buffer.data() + written, audio_buffer.size() - written);
        }

        // Actually wait for all samples to be played
        audio_stream.wait_write_completed();
    }
    catch (...)
    {
        ptt(false);
        audio_stream.stop();
        throw;
    }

    ptt(false);

    // Stop the playback
    audio_stream.stop();
}

void modem::start()
{
    // Not yet supported
}

void modem::stop()
{
    // Not yet supported
}

bool modem::remove_on_received(uint32_t cookie)
{
    return received_callbacks_.erase(cookie) > 0;
}

size_t modem::receive(std::vector<packet>& packets)
{
    (void)packets;
    return 0;
}

void modem::reset()
{
    if (mod.has_value())
    {
        mod.value().get().reset();
    }
}

void modem::preemphasis(bool enable)
{
    preemphasis_enabled = enable;
}

bool modem::preemphasis() const
{
    return preemphasis_enabled;
}

void modem::gain(double g)
{
    gain_value = g;
}

double modem::gain() const
{
    return gain_value;
}

void modem::start_silence(int ms)
{
    if (ms < 0) ms = 0;
    start_silence_duration_ms = ms;
}

int modem::start_silence() const
{
    return start_silence_duration_ms;
}

void modem::end_silence(int ms)
{
    if (ms < 0) ms = 0;
    end_silence_duration_ms = ms;
}

int modem::end_silence() const
{
    return end_silence_duration_ms;
}

void modem::tx_delay(int ms)
{
    if (ms < 0) ms = 0;
    tx_delay_ms = ms;
}

double modem::tx_delay() const
{
    return tx_delay_ms;
}

void modem::tx_tail(int ms)
{
    if (ms < 0) ms = 0;
    tx_tail_ms = ms;
}

double modem::tx_tail() const
{
    return tx_tail_ms;
}

void modem::baud_rate(int b)
{
    if (b <= 0) b = 1200;
    baud_rate_ = b;
}

int modem::baud_rate() const
{
    return baud_rate_;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// null_ptt_control                                                 //
//                                                                  //
//                                                                  //
// **************************************************************** //

void null_ptt_control::ptt(bool enable)
{
    ptt_ = enable;
}

bool null_ptt_control::ptt()
{
    return ptt_;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// chained_ptt_control                                              //
//                                                                  //
//                                                                  //
// **************************************************************** //

chained_ptt_control::chained_ptt_control() = default;

chained_ptt_control::chained_ptt_control(std::initializer_list<std::reference_wrapper<ptt_control_base>> controls) : controls_(controls)
{
}

void chained_ptt_control::add(ptt_control_base& control)
{
    controls_.push_back(control);
}

void chained_ptt_control::remove(ptt_control_base& control)
{
    controls_.erase(std::remove_if(controls_.begin(), controls_.end(), [&control](const auto& ref) {
        return &ref.get() == &control;
    }), controls_.end());
}

void chained_ptt_control::clear()
{
    controls_.clear();
}

void chained_ptt_control::ptt(bool enable)
{
    for (auto& control : controls_)
    {
        control.get().ptt(enable);
    }
}

bool chained_ptt_control::ptt()
{
    // Returns true if any of the PTT controls is true
    return std::any_of(controls_.begin(), controls_.end(), [](const auto& control) { return control.get().ptt(); });
}

size_t chained_ptt_control::size() const
{
    return controls_.size();
}

bool chained_ptt_control::empty() const
{
    return controls_.empty();
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// serial_port_ptt_control                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

serial_port_ptt_control::serial_port_ptt_control() = default;

serial_port_ptt_control::serial_port_ptt_control(serial_port_base& serial_port) : serial_port_(std::ref(serial_port))
{
#if LIBMODEM_AUTO_PTT_DISABLE
    if (serial_port.is_open())
    {
        ptt(false);
    }
#endif // LIBMODEM_AUTO_PTT_DISABLE
}

serial_port_ptt_control::serial_port_ptt_control(serial_port_base& serial_port, serial_port_ptt_line line, serial_port_ptt_trigger trigger) : serial_port_(std::ref(serial_port)), line_(line), trigger_(trigger)
{
#if LIBMODEM_AUTO_PTT_DISABLE
    if (serial_port.is_open())
    {
        ptt(false);
    }
#endif // LIBMODEM_AUTO_PTT_DISABLE
}

void serial_port_ptt_control::ptt(bool enable)
{
    if (!serial_port_.has_value())
    {
        throw std::runtime_error("No serial port assigned for PTT control");
    }

    serial_port_base& serial_port = serial_port_.value().get();

    bool line_state = (trigger_ == serial_port_ptt_trigger::on) ? enable : !enable;

    switch (line_)
    {
    case serial_port_ptt_line::rts:
        serial_port.rts(line_state);
        break;
    case serial_port_ptt_line::dtr:
        serial_port.dtr(line_state);
        break;
    default:
        throw std::runtime_error("Invalid PTT line");
    }
}

bool serial_port_ptt_control::ptt()
{
    if (!serial_port_.has_value())
    {
        throw std::runtime_error("No serial port assigned for PTT control");
    }

    serial_port_base& serial_port = serial_port_.value().get();

    bool line_state = false;

    switch (line_)
    {
    case serial_port_ptt_line::rts:
        line_state = serial_port.rts();
        break;
    case serial_port_ptt_line::dtr:
        line_state = serial_port.dtr();
        break;
    default:
        throw std::runtime_error("Invalid PTT line");
    }

    return (trigger_ == serial_port_ptt_trigger::on) ? line_state : !line_state;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// library_ptt_control                                              //
//                                                                  //
//                                                                  //
// **************************************************************** //

library_ptt_control::library_ptt_control(ptt_control_library& library) : library_(library)
{
}

void library_ptt_control::ptt(bool enable)
{
    if (library_.has_value())
    {
        library_->get().ptt(enable);
    }
}

bool library_ptt_control::ptt()
{
    if (library_.has_value())
    {
        return library_->get().ptt();
    }
    return false;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// tcp_ptt_control                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

tcp_ptt_control::tcp_ptt_control(tcp_ptt_control_client& client) : client_(client)
{
}

void tcp_ptt_control::ptt(bool enable)
{
    if (client_.has_value())
    {
        client_->get().ptt(enable);
    }
}

bool tcp_ptt_control::ptt()
{
    if (client_.has_value())
    {
        return client_->get().ptt();
    }
    return false;
}

LIBMODEM_NAMESPACE_END