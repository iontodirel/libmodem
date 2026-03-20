// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// formatter.cpp
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

#include "formatter.h"

#include <algorithm>
#include <cstdio>

// **************************************************************** //
//                                                                  //
//                                                                  //
// KISS IMPLEMENTATION                                              //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_NAMESPACE_BEGIN

LIBMODEM_KISS_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// frame_marker                                                     //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool operator==(unsigned char left, frame_marker right)
{
    return left == (unsigned char)right;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// decode                                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool decode(uint8_t byte, uint8_t& result, decoder_state& state)
{
    //
    // KISS frame structure:
    //
    //                  frame 1                                   frame n
    //  ---------------------------------------------------------------------------------
    //    frame begin            frame end           frame begin             frame end
    //    FEND (0xc0)    data    FEND (0xc0)  ...    FEND (0xc0)    data    FEND (0xc0)
    //  ---------------------------------------------------------------------------------
    //         1           n         1        ...         1           n          1
    //
    // KISS frame escaping:
    //
    // So say we want to store a 0xc0 byte somewhere in our data,
    // but that byte is also the magic byte used to mark the start of a frame,
    // what do we do? We use 0xDC (TFEND), which has special meaning to
    // insert 0xc0 in the stream when encountered, but only when
    // prefixed by 0xDB (FESC), so that in normal circumstances we
    // can also have 0xDB bytes in the data stream.
    //
    //                             unescaped frame
    //  ------------------------------------------------------------------------
    //    frame begin                                              frame end
    //    FEND (0xc0)    ...    FESC (0xDB) TFEND (0xDC)    ...    FEND (0xc0)
    //  ------------------------------------------------------------------------
    //         1         ...               2                ...         1
    //
    //
    // FESC TFEND becomes 0xC0 after the frame is escaped
    //
    // 0xDC by itself has no special meaning
    //
    //                 just a regular frame
    //  ------------------------------------------------------
    //    frame begin                          frame end
    //    FEND (0xc0)    ...    0xDC    ...    FEND (0xc0)
    //  ------------------------------------------------------
    //         1         ...     1      ...         1
    //
    // But now what if we want a 0xDB byte in the data stream?
    // Then we have an escape for 0xDB by prefixing 0xDD with 0xDB
    //
    //
    //                             unescaped frame
    //  ------------------------------------------------------------------------
    //    frame begin                                              frame end
    //    FEND (0xc0)    ...    FESC (0xDB) TFESC (0xDD)    ...    FEND (0xc0)
    //  ------------------------------------------------------------------------
    //         1         ...               2                ...         1
    //
    // FESC TFESC becomes 0xDB after the frame is escaped
    //
    // The first byte of the data contains the command and port information
    //
    //                          data encoding
    //      command byte
    //  ------------------------------------------------------------------------
    //    command      port       data
    //  ------------------------------------------------------------------------
    //    c c c c     p p p p      N
    //
    //  The lower 4 bits contain the command, while the higher 4 bits contain the port
    //

    if (state.in_kiss_frame)
    {
        if (state.in_escape_mode)
        {
            if (byte == frame_marker::tfend)
            {
                result = static_cast<unsigned char>(frame_marker::fend);
            }
            if (byte == frame_marker::tfesc)
            {
                result = static_cast<unsigned char>(frame_marker::fesc);
            }
            state.in_escape_mode = false;
            return true;
        }

        if (byte == frame_marker::fend)
        {
            state.completed = true;
            state.in_kiss_frame = false;
        }
        else if (byte == frame_marker::fesc)
        {
            state.in_escape_mode = true;
        }
        else
        {
            result = byte;
            return true;
        }
    }
    else
    {
        if (byte == frame_marker::fend)
        {
            state.in_kiss_frame = true;
        }
    }

    return false;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// decoder                                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

const std::vector<frame>& decoder::frames() const
{
    return data_;
}

size_t decoder::count() const
{
    return data_.size();
}

void decoder::reset()
{
    data_.clear();
    buffer_.clear();
    state_ = {};
}

void decoder::clear()
{
    data_.clear();
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

template <std::input_iterator InputIt, std::output_iterator<unsigned char> OutputIt, bool Command>
std::pair<OutputIt, bool> encode(uint8_t command_byte, InputIt input_it_begin, InputIt input_it_end, OutputIt output_it);

std::vector<uint8_t> encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> encode(uint8_t command_byte, const std::vector<uint8_t>& data);

std::vector<uint8_t> encode(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> result;

    encode(data.begin(), data.end(), std::back_inserter(result));

    return result;
}

std::vector<uint8_t> encode(uint8_t command_byte, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> result;

    encode(command_byte, data.begin(), data.end(), std::back_inserter(result));

    return result;
}

LIBMODEM_KISS_NAMESPACE_END

LIBMODEM_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// AGWPE IMPLEMENTATION                                             //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_NAMESPACE_BEGIN

LIBMODEM_AGWPE_NAMESPACE_BEGIN

LIBMODEM_ANONYMOUS_NAMESPACE_BEGIN

void write_address(char(&field)[sizeof(header::from)], const std::string& address)
{
    std::memset(field, 0, sizeof(header::from));
    std::size_t len = std::min(address.size(), sizeof(header::from) - 1);
    std::memcpy(field, address.c_str(), len);
}

std::string read_address(const char(&field)[sizeof(header::from)])
{
    return std::string(field, strnlen(field, sizeof(header::from)));
}

LIBMODEM_ANONYMOUS_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// frame                                                            //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> to_bytes(const frame& f)
{
    std::vector<uint8_t> result(sizeof(header) + f.data.size());

    std::memcpy(result.data(), &f.header, sizeof(header));
    if (!f.data.empty())
    {
        std::memcpy(result.data() + sizeof(header), f.data.data(), f.data.size());
    }

    return result;
}

bool is_data_frame(const frame& f)
{
    char kind = static_cast<char>(f.header.datakind);
    switch (kind)
    {
        case 'V': // UI frame via digipeaters
        case 'M': // UI frame
        case 'K': // raw AX.25 frame
            return true;
        default:
            return false;
    }
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// encode                                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

frame encode_version_response_frame(uint32_t major, uint32_t minor)
{
    frame f;

    f.header.datakind = static_cast<uint8_t>(data_kind::version_request);
    f.header.data_length = 8;

    f.data.resize(8);
    std::memcpy(f.data.data(), &major, 4);
    std::memcpy(f.data.data() + 4, &minor, 4);

    return f;
}

frame encode_port_info_response_frame(const std::vector<std::string>& port_descriptions)
{
    std::string info = std::to_string(port_descriptions.size()) + ";";
    for (const auto& desc : port_descriptions)
    {
        info += desc + ";";
    }

    frame f;

    f.header.datakind = static_cast<uint8_t>(data_kind::port_info_request);
    f.header.data_length = static_cast<uint32_t>(info.size() + 1);

    f.data.assign(info.begin(), info.end());
    f.data.push_back(0);

    return f;
}

frame encode_port_capabilities_response_frame(uint8_t port, uint8_t on_air_baud_rate, uint8_t traffic_level, uint8_t tx_delay, uint8_t tx_tail, uint8_t persist, uint8_t slottime, uint8_t maxframe, uint8_t active_connections, uint32_t bytes_received)
{
    frame f;

    f.header.datakind = static_cast<uint8_t>(data_kind::port_cap_request);
    f.header.port = port;
    f.header.data_length = 12;

    f.data.resize(12, 0);
    f.data[0] = on_air_baud_rate;    // 0=1200, 1=2400, 2=4800, 3=9600
    f.data[1] = traffic_level;       // 0xff = not in autoupdate
    f.data[2] = tx_delay;
    f.data[3] = tx_tail;
    f.data[4] = persist;
    f.data[5] = slottime;
    f.data[6] = maxframe;
    f.data[7] = active_connections;
    std::memcpy(f.data.data() + 8, &bytes_received, 4);

    return f;
}

frame encode_register_response_frame(const std::string& address, bool success)
{
    frame f;

    f.header.datakind = static_cast<uint8_t>(data_kind::register_call);

    write_address(f.header.from, address);

    f.header.data_length = 1;

    f.data.push_back(success ? 0x01 : 0x00);

    return f;
}

frame encode_outstanding_frames_response_frame(uint8_t port, uint32_t count)
{
    frame f;

    f.header.datakind = static_cast<uint8_t>(data_kind::query_frames_port);
    f.header.port = port;
    f.header.data_length = 4;

    f.data.resize(4);
    std::memcpy(f.data.data(), &count, 4);

    return f;
}

frame encode_ax25_response_frame(uint8_t port, const std::string& from_address, const std::string& to_address, uint8_t pid, const std::vector<uint8_t>& ax25_bytes)
{
    frame f;

    f.header.datakind = static_cast<uint8_t>(data_kind::transmit_raw);
    f.header.port = port;

    write_address(f.header.from, from_address);
    write_address(f.header.to, to_address);

    f.header.pid = pid;
    f.header.data_length = static_cast<uint32_t>(ax25_bytes.size());
    f.data = ax25_bytes;

    return f;
}

frame encode_monitor_ui_response_frame(uint8_t port, const std::string& from_address, const std::string& to_address, const std::string& addresses, uint8_t pid, const std::vector<uint8_t>& info_field)
{
    frame f;

    std::string text_header = " " + std::to_string(port + 1) + ":Fm " + from_address + " To " + to_address;
    if (!addresses.empty())
    {
        text_header += " Via " + addresses;
    }

    char pid_string[8];
    std::snprintf(pid_string, sizeof(pid_string), "%02X", pid);

    text_header += " <UI pid=" + std::string(pid_string) + " Len=" + std::to_string(info_field.size()) + " >";
    text_header += "[--:--:--]\r";

    f.header.datakind = static_cast<uint8_t>(data_kind::monitor_ui);
    f.header.port = port;

    write_address(f.header.from, from_address);
    write_address(f.header.to, to_address);

    f.header.pid = pid;

    std::vector<uint8_t> payload;
    payload.insert(payload.end(), text_header.begin(), text_header.end());
    payload.insert(payload.end(), info_field.begin(), info_field.end());
    payload.push_back('\r');
    payload.push_back('\r');

    f.header.data_length = static_cast<uint32_t>(payload.size());
    f.data = std::move(payload);

    return f;
}

frame encode_connection_frames_response_frame(uint8_t port, const std::string& from_address, const std::string& to_address, uint32_t count)
{
    frame f;

    f.header.datakind = static_cast<uint8_t>(data_kind::query_frames_conn);
    f.header.port = port;

    write_address(f.header.from, from_address);
    write_address(f.header.to, to_address);

    f.header.data_length = 4;

    f.data.resize(4);
    std::memcpy(f.data.data(), &count, 4);

    return f;
}

frame encode_heard_stations_response_frame(uint8_t port)
{
    frame f;

    f.header.datakind = static_cast<uint8_t>(data_kind::heard_stations);
    f.header.port = port;

    return f;
}

bool try_encode_ax25_response_frames(const std::vector<uint8_t>& ax25_bytes, uint8_t port, frame& ax25_response_frame, frame& monitor_response_frame)
{
    ax25::frame ax25_frame;
    if (!ax25::try_decode_frame_no_fcs(ax25_bytes, ax25_frame))
    {
        return false;
    }

    std::string from_address = to_string(ax25_frame.from, true);
    std::string to_address = to_string(ax25_frame.to, true);

    std::string addresses;
    for (size_t i = 0; i < ax25_frame.path.size(); i++)
    {
        if (i > 0)
        {
            addresses += ",";
        }
        addresses += to_string(ax25_frame.path[i]);
    }

    ax25_response_frame = encode_ax25_response_frame(port, from_address, to_address, ax25_frame.pid, ax25_bytes);
    monitor_response_frame = encode_monitor_ui_response_frame(port, from_address, to_address, addresses, ax25_frame.pid, ax25_frame.data);

    return true;
}


// **************************************************************** //
//                                                                  //
//                                                                  //
// decoder                                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

const std::vector<frame>& decoder::frames() const
{
    return data_;
}

size_t decoder::count() const
{
    return data_.size();
}

void decoder::reset()
{
    data_.clear();
    buffer_.clear();
    reading_header_ = true;
    current_header_ = {};
}

void decoder::clear()
{
    data_.clear();
}

bool decode_via_frame(const frame& f, v_frame& result)
{
    if (f.data.empty())
    {
        return false;
    }

    const uint8_t addresses_count = f.data[0];
    if (addresses_count > 8)
    {
        return false;
    }

    constexpr size_t address_size = sizeof(header::from);
    const size_t addresses_section_size = 1 + (addresses_count * address_size);

    if (f.data.size() < addresses_section_size)
    {
        return false;
    }

    result.addresses.clear();
    result.addresses.reserve(addresses_count);

    for (uint8_t i = 0; i < addresses_count; i++)
    {
        const size_t offset = 1 + (i * address_size);
        std::string_view address{
            reinterpret_cast<const char*>(f.data.data() + offset),
            strnlen(reinterpret_cast<const char*>(f.data.data() + offset), address_size)
        };
        result.addresses.emplace_back(address);
    }

    result.info.assign(f.data.begin() + addresses_section_size, f.data.end());

    return true;
}

bool try_decode(const frame& f, packet& p)
{
    char kind = static_cast<char>(f.header.datakind);

    if (kind == 'K')
    {
        if (f.data.size() < 2)
        {
            return false;
        }
        std::vector<uint8_t> ax25_data(f.data.begin() + 1, f.data.end());
        return ax25::try_decode_frame_no_fcs(ax25_data, p);
    }
    else if (kind == 'V')
    {
        struct v_frame via_frame;

        if (!decode_via_frame(f, via_frame))
        {
            return false;
        }

        p.from = std::string(f.header.from, strnlen(f.header.from, sizeof(header::from)));
        p.to = std::string(f.header.to, strnlen(f.header.to, sizeof(header::to)));
        p.path = via_frame.addresses;
        p.data = std::string(via_frame.info.begin(), via_frame.info.end());

        return true;
    }
    else if (kind == 'M')
    {
        p.from = std::string(f.header.from, strnlen(f.header.from, sizeof(header::from)));
        p.to = std::string(f.header.to, strnlen(f.header.to, sizeof(header::to)));
        p.data = std::string(f.data.begin(), f.data.end());

        return true;
    }

    return false;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// session                                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<frame> handle_frame(const frame& f, session& s)
{
    std::vector<frame> responses;

    switch (static_cast<char>(f.header.datakind))
    {
        case 'P':
        {
            break;
        }

        case 'R':
        {
            responses.push_back(encode_version_response_frame(s.version_major, s.version_minor));
            break;
        }

        case 'G':
        {
            responses.push_back(encode_port_info_response_frame(s.port_descriptions));
            break;
        }

        case 'g':
        {
            responses.push_back(encode_port_capabilities_response_frame(f.header.port, 0, 0xff, 50, 0, 63, 10, 7, 0, 0));
            break;
        }

        case 'X':
        {
            s.callsign = std::string(f.header.from, strnlen(f.header.from, sizeof(header::from)));
            responses.push_back(encode_register_response_frame(s.callsign, true));
            break;
        }

        case 'x':
        {
            s.callsign.clear();
            break;
        }

        case 'm':
        {
            s.monitoring_enabled = !s.monitoring_enabled;
            break;
        }

        case 'k':
        {
            s.raw_enabled = !s.raw_enabled;
            break;
        }

        case 'y':
        {
            responses.push_back(encode_outstanding_frames_response_frame(f.header.port, 0));
            break;
        }

        case 'Y':
        {
            std::string from = std::string(f.header.from, strnlen(f.header.from, sizeof(header::from)));
            std::string to = std::string(f.header.to, strnlen(f.header.to, sizeof(header::to)));
            responses.push_back(encode_connection_frames_response_frame(f.header.port, from, to, 0));
            break;
        }

        case 'H':
        {
            responses.push_back(encode_heard_stations_response_frame(f.header.port));
            break;
        }

        default:
        {
            break;
        }
    }

    return responses;
}

processed_frame process_frame(const frame& f, session& s)
{
    processed_frame result;

    if (is_data_frame(f))
    {
        std::vector<uint8_t> bytes = to_bytes(f);
        result.data.insert(result.data.end(), bytes.begin(), bytes.end());
        result.has_data = true;
    }
    else
    {
        for (auto& response : handle_frame(f, s))
        {
            result.responses.push_back(std::move(response));
            result.has_responses = true;
        }

    }

    return result;
}

LIBMODEM_AGWPE_NAMESPACE_END

LIBMODEM_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// formatter                                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_NAMESPACE_BEGIN

formatter::formatter() = default;

formatter::formatter(const formatter& other)
{
    if (other.on_command_callable_)
    {
        on_command_callable_ = other.on_command_callable_->clone();
    }
}

formatter::~formatter() = default;

void formatter::invoke_on_command(const kiss::frame& frame)
{
    if (on_command_callable_)
    {
        on_command_callable_->invoke(frame);
    }
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// ax25_kiss_formatter                                              //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> ax25_kiss_formatter::encode(packet p)
{
    std::vector<uint8_t> kiss_bytes;
    std::vector<uint8_t> ax25_frame_bytes = ax25::encode_frame(p);
    kiss::encode(0, ax25_frame_bytes.begin(), ax25_frame_bytes.end() - 2, std::back_inserter(kiss_bytes));
    return kiss_bytes;
}

bool ax25_kiss_formatter::try_decode(const std::vector<uint8_t>& data, size_t count, packet& p, uint8_t& port)
{
    kiss::frame frame;
    if (try_decode(data, count, frame))
    {
        port = frame.command_byte >> 4;

        uint8_t command = frame.command_byte & 0x0F;

        if (command == 0)
        {
            return ax25::try_decode_frame_no_fcs(frame.data, p);
        }
        else
        {
            invoke_on_command(frame);
            return false;
        }
    }
    return false;
}

bool ax25_kiss_formatter::try_decode(const std::vector<uint8_t>& data, size_t count, kiss::frame& p)
{
    if (count > 0)
    {
        kiss_decoder_.decode(data.begin(), data.begin() + count);
        for (const auto& frame : kiss_decoder_.frames())
        {
            pending_frames_.push(frame);
        }
        kiss_decoder_.clear();
    }

    if (pending_frames_.empty())
    {
        return false;
    }

    p = std::move(pending_frames_.front());
    pending_frames_.pop();

    return true;
}

void ax25_kiss_formatter::context(std::size_t client_id)
{
    (void)client_id;
}

std::unique_ptr<formatter> ax25_kiss_formatter::clone() const
{
    return std::make_unique<ax25_kiss_formatter>(*this);
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// agwpe_formatter                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> agwpe_formatter::encode(packet p)
{
    std::vector<uint8_t> ax25_frame_bytes = ax25::encode_frame(p);
    if (ax25_frame_bytes.size() >= 2)
    {
        // Remove FCS as AGWPE expects raw AX.25 frames without FCS
        ax25_frame_bytes.resize(ax25_frame_bytes.size() - 2);
    }
    return ax25_frame_bytes;
}

bool agwpe_formatter::try_decode(const std::vector<uint8_t>& data, size_t count, packet& p, uint8_t& port)
{
    if (count > 0)
    {
        agwpe_decoder_.decode(data.begin(), data.begin() + count);

        for (const auto& f : agwpe_decoder_.frames())
        {
            packet decoded_packet;
            if (agwpe::try_decode(f, decoded_packet))
            {
                pending_packets_.push({ std::move(decoded_packet), f.header.port });
            }
        }

        agwpe_decoder_.clear();
    }

    if (pending_packets_.empty())
    {
        return false;
    }

    p = std::move(pending_packets_.front().first);
    port = pending_packets_.front().second;
    pending_packets_.pop();

    return true;
}

void agwpe_formatter::context(std::size_t client_id)
{
    client_id_ = client_id;
}

std::unique_ptr<formatter> agwpe_formatter::clone() const
{
    return std::make_unique<agwpe_formatter>(*this);
}

LIBMODEM_NAMESPACE_END