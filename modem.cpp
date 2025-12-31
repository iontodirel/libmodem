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

LIBMODEM_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// modem                                                            //
//                                                                  //
//                                                                  //
// **************************************************************** //

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

void modem::transmit()
{
    std::vector<uint8_t> bitstream({ 0 });

    transmit(bitstream);
}

void modem::transmit(packet_type p)
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

void modem::transmit(const std::vector<uint8_t>& bits)
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
    struct audio_stream_base& audio_stream = audio.value().get();

    // A(FSK) modulation

    std::vector<double> audio_buffer;

    modulate_bitstream(bits, audio_buffer);

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

    int silence_samples = static_cast<int>(start_silence_duration_s * audio_stream.sample_rate());

    if (preemphasis_enabled)
    {
        apply_preemphasis(audio_buffer.begin() + silence_samples, audio_buffer.end(), audio_stream.sample_rate(), /*tau*/ 75e-6);
    }

    apply_gain(audio_buffer.begin() + silence_samples, audio_buffer.end(), gain_value);

    insert_silence(audio_buffer.begin(), audio_stream.sample_rate(), start_silence_duration_s);

    insert_silence(std::back_inserter(audio_buffer), audio_stream.sample_rate(), end_silence_duration_s);
}

void modem::modulate_bitstream(const std::vector<uint8_t>& bitstream, std::vector<double>& audio_buffer)
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
    struct audio_stream_base& audio_stream = audio.value().get();

    int silence_samples = static_cast<int>(start_silence_duration_s * audio_stream.sample_rate());

    audio_buffer.reserve(silence_samples + static_cast<int>(bitstream.size() * 1.5));

    audio_buffer.insert(audio_buffer.end(), silence_samples, 0.0);

    int write_pos = silence_samples;

    for (uint8_t bit : bitstream)
    {
        int samples_per_bit = modulator.next_samples_per_bit();

        for (int i = 0; i < samples_per_bit; ++i)
        {
            audio_buffer.push_back(modulator.modulate_double(bit));
        }
    }

    modulator.reset();
}

void modem::render_audio(const std::vector<double>& audio_buffer)
{
    if (!audio.has_value())
    {
        throw std::runtime_error("No audio stream");
    }

    struct audio_stream_base& audio_stream = audio.value().get();

    // Start the playback
    audio_stream.start();

    try
    {
        // Write audio samples to output device
        // The stream will automatically handle buffering, the stream typically has a 200ms buffer
        // This effectively starts the audible playback of the packet
        audio_stream.write(audio_buffer.data(), audio_buffer.size());

        // Actually wait for all samples to be played
        audio_stream.wait_write_completed();
    }
    catch (...)
    {
        audio_stream.stop();
        throw;
    }

    // Stop the playback
    audio_stream.stop();
}

size_t modem::receive(std::vector<packet_type>& packets)
{
    return 0;
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

void modem::start_silence(double d)
{
    if (d < 0.0) d = 0.0;
    start_silence_duration_s = d;
}

double modem::start_silence() const
{
    return start_silence_duration_s;
}

void modem::end_silence(double d)
{
    if (d < 0.0) d = 0.0;
    end_silence_duration_s = d;
}

double modem::end_silence() const
{
    return end_silence_duration_s;
}

void modem::tx_delay(double d)
{
    if (d < 0.0) d = 0.0;
    tx_delay_ms = d;
}

double modem::tx_delay() const
{
    return tx_delay_ms;
}

void modem::tx_tail(double d)
{
    if (d < 0.0) d = 0.0;
    tx_tail_ms = d;
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

LIBMODEM_NAMESPACE_END