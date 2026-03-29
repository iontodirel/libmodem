// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// bitstream.cpp
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

#include "bitstream.h"

#include <array>
#include <string>
#include <cassert>
#include <tuple>
#include <charconv>
#include <memory>

extern "C" {
#include <correct.h>
}

LIBMODEM_NAMESPACE_BEGIN

// **************************************************************** //
//                                                                  //
//                                                                  //
// address                                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

bool try_parse_address(std::string_view address_string, struct address& address)
{
    // This function is a wrapper around try_parse_address
    // it will parse the address and ssid from the address_string
    // and create an address type.

    std::string address_no_ssid;
    int ssid = 0;
    bool mark = false;

    if (!try_parse_address_with_used_flag(address_string, address_no_ssid, ssid, mark))
    {
        return false;
    }

    address.text = address_no_ssid;
    address.ssid = ssid;
    address.mark = mark;

    return true;
}

bool try_parse_address(std::string_view address, std::string& address_no_ssid, int& ssid)
{
    // Try parse an address like: ADDRESS[-SSID]
    //
    // Example:
    //
    // CALL1-10
    // ~~~~~ ~~
    // ^     ssid = 10
    // |
    // address_no_ssid = CALL1
    //
    // This functions expects a valid AX.25 address,
    // and will return false if the address is not valid.
    // An address with a non numeric ssid will be rejected, ex: CALL-AB

    ssid = 0;

    if (address.empty() || address.size() > 9)
    {
        return false;
    }

    auto sep_position = address.find("-");

    if (sep_position != std::string_view::npos)
    {
        // Check few error conditions
        // If packet ends with a separator but no ssid, ex: "CALL-"
        // If there are more than 2 character after the separator, ex: CALL-123
        if ((sep_position == (address.size() - 1)) || ((sep_position + 3) < address.size()))
        {
            return false;
        }

        address_no_ssid.assign(address.begin(), address.begin() + sep_position);

        std::string ssid_string;
        ssid_string.assign(address.begin() + sep_position + 1, address.end());

        if (ssid_string[0] == '0')
        {
            return false;
        }

        // Ensure the ssid is a number
        if (!std::isdigit(static_cast<unsigned char>(ssid_string[0])) ||
            (ssid_string.size() > 1 && !std::isdigit(static_cast<unsigned char>(ssid_string[1]))))
        {
            return false;
        }

        if (!try_parse_int({ ssid_string.data(), ssid_string.size() }, ssid))
        {
            return false;
        }

        if (ssid < 0 || ssid > 15)
        {
            ssid = 0;
            return false;
        }
    }
    else
    {
        address_no_ssid.assign(address.begin(), address.end());
        ssid = 0;
    }

    if (address_no_ssid.size() > 6)
    {
        return false;
    }

    for (char c : address_no_ssid)
    {
        // The address has to be alphanumeric and uppercase, or a digit
        if ((!std::isalnum(static_cast<unsigned char>(c)) || !std::isdigit(static_cast<unsigned char>(c))) &&
            !std::isupper(static_cast<unsigned char>(c)))
        {
            return false;
        }
    }

    return true;
}

bool try_parse_address_with_used_flag(std::string_view address, std::string& address_no_ssid, int& ssid, bool& mark)
{
    ssid = 0;
    mark = false;

    if (address.empty())
    {
        return false;
    }

    if (address.back() == '*')
    {
        mark = true;
        address.remove_suffix(1);
    }

    return try_parse_address(address, address_no_ssid, ssid);
}

std::string to_string(const struct address& address)
{
    return to_string(address, false);
}

std::string to_string(const struct address& address, bool ignore_mark)
{
    if (address.text.empty())
    {
        return "";
    }

    std::string result = address.text;

    if (address.ssid > 0)
    {
        result += '-';
        if (address.ssid < 10)
        {
            result += char('0' + address.ssid);
        }
        else
        {
            // 10 .. 15 => "1" + '0'..'5'
            result += '1';
            result += char('0' + (address.ssid - 10));
        }
    }

    if (address.mark && !ignore_mark)
    {
        result += '*';
    }

    return result;
}

bool try_parse_int(std::string_view string, int& value)
{
    // Attempt to parse an integer from the given string_view.
    // Returns true if parsing is successful, false otherwise.
    // If parsing fails, the value is set to 0.

    auto result = std::from_chars(string.data(), string.data() + string.size(), value);

    // Check if the parsing was successful and if the entire string was consumed
    // The result.ec should be std::errc() and result.ptr should point to the end of the string
    bool success = (result.ec == std::errc()) && (result.ptr == (string.data() + string.size()));

    // If parsing fails, set value to 0
    if (!success)
    {
        value = 0;
    }

    return success;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// packet                                                           //
//                                                                  //
//                                                                  //
// **************************************************************** //

packet::packet(const std::string& from, const std::string& to, const std::vector<std::string>& path, const std::string& data) : from(from), to(to), path(path), data(data)
{
}

packet::packet(const char* packet_string)
{
    bool result = try_decode_packet(packet_string, *this);
    (void)result;
    assert(result);
}

packet::packet(const std::string& packet_string)
{
    bool result = try_decode_packet(packet_string, *this);
    (void)result;
    assert(result);
}

packet::operator std::string() const
{
    return to_string(*this);
}

bool operator==(const packet& lhs, const packet& rhs)
{
    return lhs.from == rhs.from &&
        lhs.to == rhs.to &&
        lhs.path == rhs.path &&
        lhs.data == rhs.data;
}

std::string to_string(const struct packet& packet)
{
    // Does not guarantee formatting a correct packet string
    // if the input packet is invalid ex: missing path

    std::string result = packet.from + ">" + packet.to;

    if (!packet.path.empty())
    {
        for (const auto& address : packet.path)
        {
            result += "," + address;
        }
    }

    result += ":" + packet.data;

    return result;
}

bool try_decode_packet(std::string_view packet_string, packet& result)
{
    // Parse a packet: N0CALL>APRS,CALLA,CALLB*,CALLC,CALLD,CALLE,CALLF,CALLG:data
    //                 ~~~~~~ ~~~~ ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ ~~~~
    //                 from   to   path                                       data
    //
    // This function does the minimum required to parse a packet string.
    // If packet string is invalid, filling of the the packet fields is not guaranteed,
    // e.g. missing data separator ":", or missig "path"

    result.path.clear();

    // Find the from address, and the end of the packet header
    //
    // N0CALL>APRS,CALLA,CALLB*,CALLC,CALLD,CALLE,CALLF,CALLG:data
    //       ~                                               ~
    //       from_end_pos                                    colon_pos
    //
    // If we cannot find the from position, or the end of the header, we fail the parsing

    size_t from_end_pos = packet_string.find('>');

    if (from_end_pos == std::string_view::npos)
    {
        return false;
    }

    size_t colon_pos = packet_string.find(':', from_end_pos);

    if (colon_pos == std::string_view::npos)
    {
        return false;
    }

    result.from = packet_string.substr(0, from_end_pos);

    // Find the 'to' address, and the 'path'
    //
    // N0CALL>APRS,CALLA,CALLB*,CALLC,CALLD,CALLE,CALLF,CALLG:data
    //        ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    //        to_and_path

    std::string_view to_and_path = packet_string.substr(from_end_pos + 1, colon_pos - from_end_pos - 1);

    size_t comma_pos = to_and_path.find(',');

    // If we cannot find the comma, comma_pos will be set to the largest positive unsigned integer
    // with 'to' containing the remaining string, and with path being empty
    //
    // comma_pos = 18446744073709551615
    // 
    // N0CALL>APRS:data
    //        ~~~~
    //        to

    result.to = to_and_path.substr(0, comma_pos);

    if (comma_pos != std::string_view::npos)
    {
        std::string_view path = to_and_path.substr(comma_pos + 1);

        // Keep consuming the path until we reach the end of the header (colon_pos)
        // We use remove_prefix, which just changes the beginning of the string_view
        // It does not modify the string, nor copy it
        //
        // 1st iteration: CALLA,CALLB*,CALLC,CALLD,CALLE,CALLF,CALLG
        //                ~~~~~
        // 2nd iteration: CALLB*,CALLC,CALLD,CALLE,CALLF,CALLG
        //                ~~~~~~
        // 3rd iteration: CALLC,CALLD,CALLE,CALLF,CALLG
        //                ~~~~~
        // 4th iteration: CALLD,CALLE,CALLF,CALLG
        //                ~~~~~
        // 5th iteration: CALLE,CALLF,CALLG
        //                ~~~~~
        // 6th iteration: CALLF,CALLG
        //                ~~~~~
        // 7th iteration: CALLG
        //                ~~~~~

        while (!path.empty())
        {
            comma_pos = path.find(',');

            std::string_view address = path.substr(0, comma_pos);

            result.path.emplace_back(address);

            if (comma_pos == std::string_view::npos)
            {
                break;
            }

            // No copy or string modification, just update the string_view beginning
            path.remove_prefix(comma_pos + 1);
        }
    }

    // The remaining string after the colon_pos is the data

    result.data = packet_string.substr(colon_pos + 1);

    return true;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// frame                                                            //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_AX25_NAMESPACE_BEGIN

frame_type get_frame_type(uint8_t control)
{
    if ((control & 0x01u) == 0x00u)
    {
        return frame_type::i;
    }

    if ((control & 0x03u) == 0x01u)
    {
        switch (control & 0x0Fu)
        {
            case 0x01u: return frame_type::rr;
            case 0x05u: return frame_type::rnr;
            case 0x09u: return frame_type::rej;
            case 0x0Du: return frame_type::srej;
            default:    return frame_type::unknown;
        }
    }

    switch (control & 0xEFu)
    {
        case 0x03u: return frame_type::ui;
        case 0x2Fu: return frame_type::sabm;
        case 0x6Fu: return frame_type::sabme;
        case 0x43u: return frame_type::disc;
        case 0x0Fu: return frame_type::dm;
        case 0x63u: return frame_type::ua;
        case 0x87u: return frame_type::frmr;
        case 0xAFu: return frame_type::xid;
        case 0xE3u: return frame_type::test;
        default:    return frame_type::unknown;
    }
}

bool is_u_frame_type(frame_type type)
{
    switch (type)
    {
        case frame_type::ui:
        case frame_type::sabm:
        case frame_type::sabme:
        case frame_type::disc:
        case frame_type::dm:
        case frame_type::ua:
        case frame_type::frmr:
        case frame_type::xid:
        case frame_type::test:
            return true;
        default:
            return false;
    }
}

bool is_s_frame_type(frame_type type)
{
    switch (type)
    {
        case frame_type::rr:
        case frame_type::rnr:
        case frame_type::rej:
        case frame_type::srej:
            return true;
        default:
            return false;
    }
}

packet to_packet(const struct frame& frame)
{
    packet p;

    p.from = to_string(frame.from, true); // ignore mark in from address
    p.to = to_string(frame.to, true); // ignore mark in to address

    p.path.clear();
    for (const auto& path_address : frame.path)
    {
        p.path.push_back(to_string(path_address));
    }

    p.data = std::string(frame.data.begin(), frame.data.end());

    return p;
}

frame to_frame(const packet& p)
{
    static constexpr uint8_t ui_frame = 0x03;
    static constexpr uint8_t pid_no_layer3 = 0xF0;

    frame f;

    LIBMODEM_NAMESPACE_REFERENCE try_parse_address(p.from, f.from);
    LIBMODEM_NAMESPACE_REFERENCE try_parse_address(p.to, f.to);

    for (const auto& path_address_string : p.path)
    {
        address path_address;
        LIBMODEM_NAMESPACE_REFERENCE try_parse_address(path_address_string, path_address);
        f.path.push_back(path_address);
    }

    f.data = std::vector<uint8_t>(p.data.begin(), p.data.end());

    f.control = { ui_frame, 0x0 };
    f.pid = pid_no_layer3;

    return f;
}

LIBMODEM_AX25_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// bitstream_state                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_AX25_NAMESPACE_BEGIN

void bitstream_state::reset()
{
    searching = true;
    in_preamble = false;
    in_frame = false;
    complete = false;
    last_nrzi_level = 0;
    frame_start_index = 0;
    bitstream.clear();
    frame = {};
    global_preamble_start = 0;
    global_postamble_end = 0;
    frame_nrzi_level = 0;
    frame_size_bits = 0;
    global_bit_count = 0;
    global_preamble_start_pending = 0;
    frame_nrzi_level_pending = 0;
    preamble_count = 0;
    postamble_count = 0;
    preamble_count_pending = 0;
    postamble_count_pending = 0;
}

LIBMODEM_AX25_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// ax25_bitstream_converter                                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> ax25_bitstream_converter::encode(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, int preamble_flags, int postamble_flags) const
{
LIBMODEM_AX25_USING_NAMESPACE

    return encode_bitstream(f, preamble_flags, postamble_flags);
}

std::vector<uint8_t> ax25_bitstream_converter::encode(const packet& p, int preamble_flags, int postamble_flags) const
{
LIBMODEM_AX25_USING_NAMESPACE

    return encode_bitstream(p, preamble_flags, postamble_flags);
}

bool ax25_bitstream_converter::try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet& p, size_t& read)
{
LIBMODEM_AX25_USING_NAMESPACE

    return try_decode_bitstream(bitstream, offset, p, read, state);
}

bool ax25_bitstream_converter::try_decode(uint8_t bit, packet& p)
{
LIBMODEM_AX25_USING_NAMESPACE

    return try_decode_bitstream(bit, p, state);
}

void ax25_bitstream_converter::reset()
{
    state.reset();
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// ax25_scrambled_bitstream_converter                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> ax25_scrambled_bitstream_converter::encode(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, int preamble_flags, int postamble_flags) const
{
LIBMODEM_AX25_USING_NAMESPACE

    return encode_bitstream(nrz_scrambled, f, preamble_flags, postamble_flags);
}

std::vector<uint8_t> ax25_scrambled_bitstream_converter::encode(const packet& p, int preamble_flags, int postamble_flags) const
{
LIBMODEM_AX25_USING_NAMESPACE

    return encode_bitstream(nrz_scrambled, p, preamble_flags, postamble_flags);
}

bool ax25_scrambled_bitstream_converter::try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet& p, size_t& read)
{
    (void)bitstream;
    (void)offset;
    (void)p;
    (void)read;
    return false;
}

bool ax25_scrambled_bitstream_converter::try_decode(uint8_t bit, packet& p)
{
    (void)bit;
    (void)p;
    return false;
}

void ax25_scrambled_bitstream_converter::reset()
{
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// fx25_bitstream_converter                                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> fx25_bitstream_converter::encode(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, int preamble_flags, int postamble_flags) const
{
    return LIBMODEM_FX25_NAMESPACE_REFERENCE encode_bitstream(f, preamble_flags, postamble_flags);
}

std::vector<uint8_t> fx25_bitstream_converter::encode(const packet& p, int preamble_flags, int postamble_flags) const
{
LIBMODEM_FX25_USING_NAMESPACE

    return encode_bitstream(p, preamble_flags, postamble_flags);
}

bool fx25_bitstream_converter::try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet& p, size_t& read)
{
LIBMODEM_FX25_USING_NAMESPACE

    (void)bitstream;
    (void)offset;
    (void)p;
    (void)read;

    return false;
}

void fx25_bitstream_converter::reset()
{
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// fx25_scrambled_bitstream_converter                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> fx25_scrambled_bitstream_converter::encode(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, int preamble_flags, int postamble_flags) const
{
    return LIBMODEM_FX25_NAMESPACE_REFERENCE encode_bitstream(LIBMODEM_AX25_NAMESPACE_REFERENCE nrz_scrambled, f, preamble_flags, postamble_flags);
}

std::vector<uint8_t> fx25_scrambled_bitstream_converter::encode(const packet& p, int preamble_flags, int postamble_flags) const
{
    return fx25::encode_bitstream(LIBMODEM_AX25_NAMESPACE_REFERENCE nrz_scrambled, p, preamble_flags, postamble_flags);
}

bool fx25_scrambled_bitstream_converter::try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet& p, size_t& read)
{
    (void)bitstream;
    (void)offset;
    (void)p;
    (void)read;

    return false;
}

void fx25_scrambled_bitstream_converter::reset()
{
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// bitstream_converter_base                                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

bitstream_converter_base::~bitstream_converter_base()
{
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// ax25_bitstream_converter_adapter                                 //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> ax25_bitstream_converter_adapter::encode(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, int preamble_flags, int postamble_flags) const
{
    return converter.encode(f, preamble_flags, postamble_flags);
}

std::vector<uint8_t> ax25_bitstream_converter_adapter::encode(const packet& p, int preamble_flags, int postamble_flags) const
{
    return converter.encode(p, preamble_flags, postamble_flags);
}

bool ax25_bitstream_converter_adapter::try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet& p, size_t& read)
{
    return converter.try_decode(bitstream, offset, p, read);
}

bool ax25_bitstream_converter_adapter::try_decode(uint8_t bit, packet& p)
{
    return converter.try_decode(bit, p);
}

void ax25_bitstream_converter_adapter::reset()
{
    converter.reset();
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// ax25_scrambled_bitstream_converter_adapter                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> ax25_scrambled_bitstream_converter_adapter::encode(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, int preamble_flags, int postamble_flags) const
{
    return converter.encode(f, preamble_flags, postamble_flags);
}

std::vector<uint8_t> ax25_scrambled_bitstream_converter_adapter::encode(const packet& p, int preamble_flags, int postamble_flags) const
{
    return converter.encode(p, preamble_flags, postamble_flags);
}

bool ax25_scrambled_bitstream_converter_adapter::try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet& p, size_t& read)
{
    return converter.try_decode(bitstream, offset, p, read);
}

bool ax25_scrambled_bitstream_converter_adapter::try_decode(uint8_t bit, packet& p)
{
    return converter.try_decode(bit, p);
}

void ax25_scrambled_bitstream_converter_adapter::reset()
{
    converter.reset();
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// fx25_bitstream_converter_adapter                                 //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> fx25_bitstream_converter_adapter::encode(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, int preamble_flags, int postamble_flags) const
{
    return converter.encode(f, preamble_flags, postamble_flags);
}

std::vector<uint8_t> fx25_bitstream_converter_adapter::encode(const packet& p, int preamble_flags, int postamble_flags) const
{
    return converter.encode(p, preamble_flags, postamble_flags);
}

bool fx25_bitstream_converter_adapter::try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet& p, size_t& read)
{
    return converter.try_decode(bitstream, offset, p, read);
}

bool fx25_bitstream_converter_adapter::try_decode(uint8_t bit, packet& p)
{
    (void)bit;
    (void)p;
    return false;
}

void fx25_bitstream_converter_adapter::reset()
{
    converter.reset();
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// fx25_scrambled_bitstream_converter_adapter                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> fx25_scrambled_bitstream_converter_adapter::encode(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, int preamble_flags, int postamble_flags) const
{
    return converter.encode(f, preamble_flags, postamble_flags);
}

std::vector<uint8_t> fx25_scrambled_bitstream_converter_adapter::encode(const packet& p, int preamble_flags, int postamble_flags) const
{
    return converter.encode(p, preamble_flags, postamble_flags);
}

bool fx25_scrambled_bitstream_converter_adapter::try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet& p, size_t& read)
{
    return converter.try_decode(bitstream, offset, p, read);
}

bool fx25_scrambled_bitstream_converter_adapter::try_decode(uint8_t bit, packet& p)
{
    (void)bit;
    (void)p;
    return false;
}

void fx25_scrambled_bitstream_converter_adapter::reset()
{
    converter.reset();
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// trim                                                             //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::string_view trim(std::string_view str)
{
    size_t first = str.find_first_not_of(' ');
    if (first == std::string_view::npos)
    {
        return {};
    }
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, last - first + 1);
}

// **************************************************************** //
//                                                                  //
//                                                                  //
//                                                                  //
// bitstream routines                                               //
//                                                                  //
// nrzi_decode, ends_with_hdlc_flag                                 //
//                                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_AX25_NAMESPACE_BEGIN

uint8_t nrzi_decode(uint8_t bit, uint8_t last_nrzi_level)
{
    uint8_t decoded_bit = (bit == last_nrzi_level) ? 1 : 0;
    return decoded_bit;
}

uint8_t scramble_bit(uint8_t bit, uint32_t& state)
{
    // G3RUH self-synchronizing scrambler, polynomial 1 + X^12 + X^17.
    //
    // Long runs of identical bits produce DC bias at the radio's data port,
    // which upsets the FM modulator. The scrambler breaks up these runs by
    // XORing each input bit with two feedback taps from a 17-bit shift register,
    // producing a pseudo-random output regardless of the input pattern.
    //
    // Reset state to 0 at the start of each packet.
    //
    //
    //  tap[16]                tap[11]
    //     |                      |
    //     ↓                      ↓
    //  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
    //  |s16|s15|s14|s13|s12|s11|s10| s9| s8| s7| s6| s5| s4| s3| s2| s1| s0|  <- state in
    //  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
    //    |                       |
    //    +----> XOR <------------+
    //            |
    //           XOR <--- bit (input)
    //            |
    //           res = bit ^ s16 ^ s11
    //            |
    //   each bit shifts left, MSB dropped, res enters at bit 0 ----------+
    //                                                                    |
    //                                                                    ↓
    //  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
    //  |s15|s14|s13|s12|s11|s10| s9| s8| s7| s6| s5| s4| s3| s2| s1| s0|res|  -> state out
    //  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+

    uint8_t result = (bit ^ (state >> 16) ^ (state >> 11)) & 1;  // tap bits 16 and 11

    state = ((state << 1) | result) & 0x1FFFF;                   // shift result into 17-bit register

    return result;
}

bool ends_with_hdlc_flag(const std::vector<uint8_t>& bitstream)
{
    return ends_with_hdlc_flag(bitstream.begin(), bitstream.end());
}

uint16_t compute_crc_using_lut_init()
{
    return 0xFFFF;
}

uint16_t compute_crc_using_lut_update(uint8_t byte, uint16_t crc)
{
    // Hardcoded CRC-16-CCITT lookup table for polynomial 0x8408 (reversed)
    //
    // Table generation algorithm:
    //
    // for (int i = 0; i < 256; ++i)
    // {
    //     uint16_t crc = i;
    //     for (int j = 0; j < 8; ++j)
    //     {
    //         if (crc & 1)
    //         {
    //             crc = (crc >> 1) ^ 0x8408;
    //         }
    //         else
    //         {
    //             crc >>= 1;
    //         }
    //     }
    //     table[i] = crc;
    // }
    //
    // Each entry represents the CRC remainder when dividing that byte value
    // by the polynomial, processing bits LSB-first

    static constexpr uint16_t crc_table[256] =
    {
        0x0000, 0x1189, 0x2312, 0x329B, 0x4624, 0x57AD, 0x6536, 0x74BF,
        0x8C48, 0x9DC1, 0xAF5A, 0xBED3, 0xCA6C, 0xDBE5, 0xE97E, 0xF8F7,
        0x1081, 0x0108, 0x3393, 0x221A, 0x56A5, 0x472C, 0x75B7, 0x643E,
        0x9CC9, 0x8D40, 0xBFDB, 0xAE52, 0xDAED, 0xCB64, 0xF9FF, 0xE876,
        0x2102, 0x308B, 0x0210, 0x1399, 0x6726, 0x76AF, 0x4434, 0x55BD,
        0xAD4A, 0xBCC3, 0x8E58, 0x9FD1, 0xEB6E, 0xFAE7, 0xC87C, 0xD9F5,
        0x3183, 0x200A, 0x1291, 0x0318, 0x77A7, 0x662E, 0x54B5, 0x453C,
        0xBDCB, 0xAC42, 0x9ED9, 0x8F50, 0xFBEF, 0xEA66, 0xD8FD, 0xC974,
        0x4204, 0x538D, 0x6116, 0x709F, 0x0420, 0x15A9, 0x2732, 0x36BB,
        0xCE4C, 0xDFC5, 0xED5E, 0xFCD7, 0x8868, 0x99E1, 0xAB7A, 0xBAF3,
        0x5285, 0x430C, 0x7197, 0x601E, 0x14A1, 0x0528, 0x37B3, 0x263A,
        0xDECD, 0xCF44, 0xFDDF, 0xEC56, 0x98E9, 0x8960, 0xBBFB, 0xAA72,
        0x6306, 0x728F, 0x4014, 0x519D, 0x2522, 0x34AB, 0x0630, 0x17B9,
        0xEF4E, 0xFEC7, 0xCC5C, 0xDDD5, 0xA96A, 0xB8E3, 0x8A78, 0x9BF1,
        0x7387, 0x620E, 0x5095, 0x411C, 0x35A3, 0x242A, 0x16B1, 0x0738,
        0xFFCF, 0xEE46, 0xDCDD, 0xCD54, 0xB9EB, 0xA862, 0x9AF9, 0x8B70,
        0x8408, 0x9581, 0xA71A, 0xB693, 0xC22C, 0xD3A5, 0xE13E, 0xF0B7,
        0x0840, 0x19C9, 0x2B52, 0x3ADB, 0x4E64, 0x5FED, 0x6D76, 0x7CFF,
        0x9489, 0x8500, 0xB79B, 0xA612, 0xD2AD, 0xC324, 0xF1BF, 0xE036,
        0x18C1, 0x0948, 0x3BD3, 0x2A5A, 0x5EE5, 0x4F6C, 0x7DF7, 0x6C7E,
        0xA50A, 0xB483, 0x8618, 0x9791, 0xE32E, 0xF2A7, 0xC03C, 0xD1B5,
        0x2942, 0x38CB, 0x0A50, 0x1BD9, 0x6F66, 0x7EEF, 0x4C74, 0x5DFD,
        0xB58B, 0xA402, 0x9699, 0x8710, 0xF3AF, 0xE226, 0xD0BD, 0xC134,
        0x39C3, 0x284A, 0x1AD1, 0x0B58, 0x7FE7, 0x6E6E, 0x5CF5, 0x4D7C,
        0xC60C, 0xD785, 0xE51E, 0xF497, 0x8028, 0x91A1, 0xA33A, 0xB2B3,
        0x4A44, 0x5BCD, 0x6956, 0x78DF, 0x0C60, 0x1DE9, 0x2F72, 0x3EFB,
        0xD68D, 0xC704, 0xF59F, 0xE416, 0x90A9, 0x8120, 0xB3BB, 0xA232,
        0x5AC5, 0x4B4C, 0x79D7, 0x685E, 0x1CE1, 0x0D68, 0x3FF3, 0x2E7A,
        0xE70E, 0xF687, 0xC41C, 0xD595, 0xA12A, 0xB0A3, 0x8238, 0x93B1,
        0x6B46, 0x7ACF, 0x4854, 0x59DD, 0x2D62, 0x3CEB, 0x0E70, 0x1FF9,
        0xF78F, 0xE606, 0xD49D, 0xC514, 0xB1AB, 0xA022, 0x92B9, 0x8330,
        0x7BC7, 0x6A4E, 0x58D5, 0x495C, 0x3DE3, 0x2C6A, 0x1EF1, 0x0F78
    };

    uint8_t table_index = (crc ^ byte) & 0xFF;
    return (crc >> 8) ^ crc_table[table_index];
}

std::array<uint8_t, 2> compute_crc_using_lut_finalize(uint16_t crc)
{
    uint16_t final_crc = crc ^ 0xFFFF;
    return { static_cast<uint8_t>(final_crc & 0xFF), static_cast<uint8_t>((final_crc >> 8) & 0xFF) };
}

LIBMODEM_AX25_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// AX.25                                                            //
//                                                                  //
// encode_header, encode_addresses, encode_address, encode_frame    //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_AX25_NAMESPACE_BEGIN

bool try_parse_address(std::string_view data, std::string& address_text, int& ssid, bool& cr_or_h_bit)
{
    return try_parse_address(data.begin(), data.end(), address_text, ssid, cr_or_h_bit);
}

bool try_parse_address(std::string_view data, struct address& address)
{
    return try_parse_address(data.begin(), data.end(), address);
}

void parse_addresses(std::string_view data, std::vector<address>& addresses)
{
    addresses.clear();
    for (size_t i = 0; (i + 7) <= data.size(); i += 7)
    {
        struct address address;
        LIBMODEM_AX25_NAMESPACE_REFERENCE try_parse_address(data.substr(i, 7), address);
        address.command_response = false;
        addresses.push_back(address);
    }
}

std::vector<uint8_t> encode_frame(const packet& p)
{
    address to_address;
    LIBMODEM_NAMESPACE_REFERENCE try_parse_address(p.to, to_address);

    address from_address;
    LIBMODEM_NAMESPACE_REFERENCE try_parse_address(p.from, from_address);

    std::vector<address> path;
    for (const auto& address_string : p.path)
    {
        address path_address;
        LIBMODEM_NAMESPACE_REFERENCE try_parse_address(address_string, path_address);
        path.push_back(path_address);
    }

    return encode_frame(from_address, to_address, path, p.data.begin(), p.data.end());
}

std::vector<uint8_t> encode_frame(const struct frame& frame)
{
    return encode_frame(frame.from, frame.to, frame.path, frame.data.begin(), frame.data.end(), frame.control[0], frame.pid);
}

std::vector<uint8_t> encode_frame(const address& from, const address& to, const std::vector<address>& path, std::string_view data)
{
    return encode_frame(from, to, path, data.begin(), data.end());
}

bool try_decode_frame(const std::vector<uint8_t>& frame_bytes, packet& p)
{
    address from;
    address to;
    std::vector<address> path;
    std::vector<uint8_t> data;

    if (try_decode_frame(frame_bytes, from, to, path, data))
    {
        p.from = to_string(from);
        p.to = to_string(to);

        p.path.clear();

        for (const auto& path_address : path)
        {
            p.path.push_back(to_string(path_address));
        }

        p.data = std::string(data.begin(), data.end());

        return true;
    }

    return false;
}

bool try_decode_frame(const std::vector<uint8_t>& frame_bytes, struct frame& frame)
{
    frame.path.clear();
    frame.data.clear();

    uint8_t control = 0;
    uint8_t pid = 0;
    auto [path_out, data_out, result] = try_decode_frame(frame_bytes.begin(), frame_bytes.end(), frame.from, frame.to, std::back_inserter(frame.path), std::back_inserter(frame.data), control, pid, frame.crc);
    if (result)
    {
        frame.control[0] = control;
        frame.pid = pid;
    }
    return result;
}

bool try_decode_frame(const std::vector<uint8_t>& frame_bytes, address& from, address& to, std::vector<address>& path, std::vector<uint8_t>& data)
{
    std::array<uint8_t, 2> crc;
    return try_decode_frame(frame_bytes, from, to, path, data, crc);
}

bool try_decode_frame(const std::vector<uint8_t>& frame_bytes, address& from, address& to, std::vector<address>& path, std::vector<uint8_t>& data, std::array<uint8_t, 2>& crc)
{
    return try_decode_frame(frame_bytes.begin(), frame_bytes.end(), from, to, path, data, crc);
}

bool try_decode_frame_no_fcs(const std::vector<uint8_t>& frame_bytes, packet& p)
{
    address from;
    address to;
    std::vector<address> path;
    std::vector<uint8_t> data;

    if (try_decode_frame_no_fcs(frame_bytes, from, to, path, data))
    {
        p.from = to_string(from);
        p.to = to_string(to);

        p.path.clear();

        for (const auto& path_address : path)
        {
            p.path.push_back(to_string(path_address));
        }

        p.data = std::string(data.begin(), data.end());

        return true;
    }

    return false;
}

bool try_decode_frame_no_fcs(const std::vector<uint8_t>& frame_bytes, struct frame& frame)
{
    return try_decode_frame_no_fcs(frame_bytes, frame.from, frame.to, frame.path, frame.data);
}

bool try_decode_frame_no_fcs(const std::vector<uint8_t>& frame_bytes, address& from, address& to, std::vector<address>& path, std::vector<uint8_t>& data)
{
    return try_decode_frame_no_fcs(frame_bytes.begin(), frame_bytes.end(), from, to, path, data);
}

bool try_decode_frame_no_fcs(std::span<const uint8_t> frame_bytes, address& from, address& to, std::vector<address>& path, std::vector<uint8_t>& data)
{
    return try_decode_frame_no_fcs(frame_bytes.begin(), frame_bytes.end(), from, to, path, data);
}

bool try_decode_frame_no_fcs(std::span<const uint8_t> frame_bytes, struct frame& frame)
{
    return try_decode_frame_no_fcs(frame_bytes, frame.from, frame.to, frame.path, frame.data);
}

bool try_decode_frame_no_fcs(std::span<const uint8_t> frame_bytes, packet& p)
{
    address from;
    address to;
    std::vector<address> path;
    std::vector<uint8_t> data;

    if (try_decode_frame_no_fcs(frame_bytes, from, to, path, data))
    {
        p.from = to_string(from);
        p.to = to_string(to);

        p.path.clear();

        for (const auto& path_address : path)
        {
            p.path.push_back(to_string(path_address));
        }

        p.data = std::string(data.begin(), data.end());

        return true;
    }

    return false;
}

std::vector<uint8_t> encode_header(const address& from, const address& to, const std::vector<address>& path)
{
    std::vector<uint8_t> header;

    auto to_bytes = encode_address(to, false);
    header.insert(header.end(), to_bytes.begin(), to_bytes.end());

    // If there is no path, the from address is the last address
    // and should be marked as such
    auto from_bytes = encode_address(from, path.empty());
    header.insert(header.end(), from_bytes.begin(), from_bytes.end());

    std::vector<uint8_t> addresses = encode_addresses(path);
    header.insert(header.end(), addresses.begin(), addresses.end());

    return header;
}

std::vector<uint8_t> encode_addresses(const std::vector<address>& path)
{
    std::vector<uint8_t> result;
    encode_addresses(path, std::back_inserter(result));
    return result;
}

std::array<uint8_t, 7> encode_address(const struct address& address, bool last)
{
    return encode_address(address.text, address.ssid, (address.mark || address.command_response), last, address.reserved_bits);
}

std::array<uint8_t, 7> encode_address(std::string_view address, int ssid, bool cr_or_h_bit, bool last)
{
    // Typical reserved bits for AX.25 address encoding is 0b01100000 (0x60)
    return encode_address(address, ssid, cr_or_h_bit, last, {1, 1});
}

std::array<uint8_t, 7> encode_address(std::string_view address, int ssid, bool cr_or_h_bit, bool last, std::array<uint8_t, 2> reserved_bits)
{
    assert(ssid >= 0 && ssid <= 15);
    assert(reserved_bits[0] == 0 || reserved_bits[0] == 1);
    assert(reserved_bits[1] == 0 || reserved_bits[1] == 1);

    std::array<uint8_t, 7> data = {};

    // AX.25 addresses are always exactly 7 bytes:
    // 
    //  - Bytes 0-5: Callsign (6 characters, space-padded)
    //    - Each character is left-shifted by 1 bit
    //  - Byte 6: SSID + last used marker

    for (size_t i = 0; i < 6; i++)
    {
        if (i < address.length())
        {
            // Shift each character left by 1 bit
            // Example: 'W' (0x57 = 01010111) << 1 = 0xAE (10101110)
            // AX.25 uses 7-bit encoding, leaving the LSB for other purposes
            data[i] = static_cast<uint8_t>(static_cast<unsigned char>(address[i]) << 1); // shift left by 1 bit
        }
        else
        {
            // Pad remaining positions with space character
            // Space ' ' (0x20 = 00100000) << 1 = 0x40 (01000000)
            data[i] = ' ' << 1; // pad with spaces
        }
    }

    // Encode the SSID byte (byte 6)
    // This byte contains multiple fields.
    // 
    // Byte 6 initialized with 01100000 (0x60)
    // 
    //  - SSID is a 4-bit value (0-15) in bits 4-1, hence shifted left by 1
    //  - Bit 0 set to 1 if this is the last address in the path
    //  - Bit 7 (H - bit) set to 1 if the address is used(marked with*)
    //
    // Byte 6 Format:
    //
    //      H-bit  Reserved     SSID        Last
    //   ------------------------------------------
    //        7       6 5      4 3 2 1        0          bits
    //   ------------------------------------------
    //        1        2          4           1
    //
    // Examples:
    //
    //   Callsign    SSID       |  H-bit  | 0x60  | SSID      | last
    //   ------------------   --+---------+-------+-----------+-------
    //   W7ION-5*    = 5        |  1      |  1 1  | 0 1 0 1   | 0        = 0x6B
    //   W7ION-12    = 12       |  0      |  1 1  | 1 1 0 0   | 0        = 0x78
    //   APRS-0      = 0        |  0      |  1 1  | 0 0 0 0   | 0        = 0x60
    //   WIDE1-1*    = 1        |  1      |  1 1  | 0 0 0 1   | 0        = 0x63
    //   WIDE2-2     = 2        |  0      |  1 1  | 0 0 1 0   | 0        = 0x64
    //   RELAY-15*   = 15       |  1      |  1 1  | 1 1 1 1   | 0        = 0x7F 
    //
    // Example with W7ION-5*
    // 
    //   0 1 1 0 0 0 0 0 = 0x60                                            starting value
    //     ~~~
    //   0 1 0 1 = (ssid + '0') = 0x05                                     calculate ssid
    // 
    //   0 1 0 1 0 = (ssid + '0') << 1 = 0x0A                              shift SSID left by 1 bit
    //   ~~~~~~~
    //   0 1 1 0 1 0 1 0 = 0x60 | (ssid + '0') << 1 = 0x6A                 append ssid
    //         ~~~~~~~
    //   0 1 1 0 1 0 1 1 = 0x60 | (ssid + '0') << 1 | 0x01 = 0x6B          mark as last address
    //                 ~
    //   1 1 1 0 1 0 1 1 = 0x60 | (ssid + '0') << 1 | 0x01 | 0x80 = 0xEB   mark address as used
    //   ~

    // Typical reserved bits for AX.25 address encoding is 0b01100000 (0x60)
    // We provide a setter for testing purposes
    data[6] = (reserved_bits[0] << 5) | (reserved_bits[1] << 6);

    data[6] |= (ssid << 1);

    if (last)
    {
        data[6] |= 0b00000001; // Extension bit (bit 0), 0x01
    }

    if (cr_or_h_bit)
    {
        data[6] |= 0b10000000; // C/H-bit (bit 7), 0x80
    }

    return data;
}

std::vector<uint8_t> encode_bitstream(const packet& p, int preamble_flags, int postamble_flags)
{
    return encode_bitstream(encode_frame(p), preamble_flags, postamble_flags);
}

std::vector<uint8_t> encode_bitstream(const packet& p, uint8_t initial_nrzi_level, int preamble_flags, int postamble_flags)
{
    return encode_bitstream(encode_frame(p), initial_nrzi_level, preamble_flags, postamble_flags);
}

std::vector<uint8_t> encode_bitstream(const frame& f, int preamble_flags, int postamble_flags)
{
    return encode_bitstream(encode_frame(f), preamble_flags, postamble_flags);
}

std::vector<uint8_t> encode_bitstream(const frame& f, uint8_t initial_nrzi_level, int preamble_flags, int postamble_flags)
{
    return encode_bitstream(encode_frame(f), initial_nrzi_level, preamble_flags, postamble_flags);
}

std::vector<uint8_t> encode_bitstream(const std::vector<uint8_t>& frame, int preamble_flags, int postamble_flags)
{
    return encode_bitstream(frame.begin(), frame.end(), preamble_flags, postamble_flags);
}

std::vector<uint8_t> encode_bitstream(const std::vector<uint8_t>& frame, uint8_t initial_nrzi_level, int preamble_flags, int postamble_flags)
{
    return encode_bitstream(frame.begin(), frame.end(), initial_nrzi_level, preamble_flags, postamble_flags);
}

std::vector<uint8_t> encode_bitstream(nrz_scrambled_t, const packet& p, int preamble_flags, int postamble_flags)
{
    return encode_bitstream(nrz_scrambled, p, 0, preamble_flags, postamble_flags);
}

std::vector<uint8_t> encode_bitstream(nrz_scrambled_t, const packet& p, uint8_t initial_nrzi_level, int preamble_flags, int postamble_flags)
{
    return encode_bitstream(nrz_scrambled, encode_frame(p), initial_nrzi_level, preamble_flags, postamble_flags);
}

std::vector<uint8_t> encode_bitstream(nrz_scrambled_t, const frame& f, int preamble_flags, int postamble_flags)
{
    return encode_bitstream(nrz_scrambled, f, 0, preamble_flags, postamble_flags);
}

std::vector<uint8_t> encode_bitstream(nrz_scrambled_t, const frame& f, uint8_t initial_nrzi_level, int preamble_flags, int postamble_flags)
{
    return encode_bitstream(nrz_scrambled, encode_frame(f), initial_nrzi_level, preamble_flags, postamble_flags);
}

std::vector<uint8_t> encode_bitstream(nrz_scrambled_t, const std::vector<uint8_t>& frame, int preamble_flags, int postamble_flags)
{
    return encode_bitstream(nrz_scrambled, frame, 0, preamble_flags, postamble_flags);
}

std::vector<uint8_t> encode_bitstream(nrz_scrambled_t, const std::vector<uint8_t>& frame, uint8_t initial_nrzi_level, int preamble_flags, int postamble_flags)
{
    return encode_bitstream(nrz_scrambled, frame.begin(), frame.end(), initial_nrzi_level, preamble_flags, postamble_flags);
}

bool try_decode_bitstream(uint8_t bit, bitstream_state& state)
{
    // Process one bit at a time through the AX.25 bitstream decoding pipeline:
    //
    // 1. NRZI decode the incoming raw bit
    // 2. Add decoded bit to internal buffer
    // 3. Check for HDLC flag patterns (0x7E = 01111110)
    // 4. State machine:
    //    - searching: Looking for first preamble flag
    //    - in_preamble: Found flag(s), waiting for frame data or more flags
    //    - in_frame: Collecting frame data until postamble flag
    // 5. When postamble found, decode the accumulated frame
    //
    // Returns true when a complete packet has been decoded into 'packet'

    // After a successful decode, we preserve the state (in_preamble with the shared flag)
    // Only reset the complete flag, not the entire state - this allows shared flags to work
    // where the postamble of one packet serves as the preamble of the next
    if (state.complete)
    {
        state.complete = false;
        // Don't reset phase, bitstream, or frame_start_index
        // They were set correctly after decode to handle potential shared flags
    }

    // NRZI decode: no transition = 1, transition = 0
    uint8_t decoded_bit = nrzi_decode(bit, state.last_nrzi_level);

    state.last_nrzi_level = bit;

    // Add decoded bit to buffer
    state.bitstream.push_back(decoded_bit);

    state.global_bit_count++;

    // Check for HDLC flag pattern in the last 8 bits
    bool found_hdlc_flag = ends_with_hdlc_flag(state.bitstream);

    if (state.searching)
    {
        // Looking for the first HDLC flag
        if (found_hdlc_flag)
        {
            state.searching = false;
            state.in_preamble = true;
            state.frame_start_index = state.bitstream.size(); // Frame starts after this flag
            state.preamble_count_pending = 1;
            state.postamble_count_pending = 0;

            if (state.enable_diagnostics)
            {
                // Track where preamble started (first bit of this flag)
                state.global_preamble_start_pending = state.global_bit_count - 7;

                // Compute NRZI level before the preamble by working backwards through the 8 flag bits
                uint8_t level = state.last_nrzi_level;
                for (size_t i = 0; i < 8 && i < state.bitstream.size(); i++)
                {
                    if (state.bitstream[state.bitstream.size() - 1 - i] == 0)
                    {
                        level = level ? 0 : 1;
                    }
                }
                state.frame_nrzi_level_pending = level;
            }
        }
        else if (state.bitstream.size() > 16)
        {
            // Optimization: prevent buffer from growing indefinitely while searching
            // Keep only the last 8 bits needed for flag detection
            state.bitstream.erase(state.bitstream.begin(), state.bitstream.begin() + (state.bitstream.size() - 8));
        }
    }
    else if (state.in_preamble)
    {
        // We've seen at least one flag. Check if this is another consecutive flag
        // or if frame data has started.
        if (found_hdlc_flag)
        {
            // Another consecutive flag - update frame start position
            state.frame_start_index = state.bitstream.size();
            state.preamble_count_pending++;
        }
        else
        {
            // Check if we have at least 8 bits since the last flag
            // to confirm we're in frame data (not still at a flag boundary)
            if (state.bitstream.size() >= state.frame_start_index + 8)
            {
                state.in_preamble = false;
                state.in_frame = true;
            }
        }
    }
    else if (state.in_frame)
    {
        // Collecting frame data. Check for postamble flag.
        if (found_hdlc_flag)
        {
            state.postamble_count_pending = 1;

            // Found postamble! Extract frame bits (excluding the 8-bit postamble flag)
            size_t frame_end = state.bitstream.size() - 8;

            if (frame_end > state.frame_start_index)
            {
                // Extract frame bits
                std::vector<uint8_t> frame_bits(state.bitstream.begin() + state.frame_start_index, state.bitstream.begin() + frame_end);

                // Try to decode the packet
                bool result = try_decode_frame(frame_bits.begin(), frame_bits.end(), state.frame);

                // Set the global bit positions for the successfully found frame
                state.global_preamble_start = state.global_preamble_start_pending;
                state.global_postamble_end = state.global_bit_count;
                state.frame_nrzi_level = state.frame_nrzi_level_pending;

                // Prepare for next packet - the postamble can be the preamble of the next
                // Keep only the last 8 bits (the flag) for potential reuse as preamble
                state.bitstream.erase(state.bitstream.begin(), state.bitstream.begin() + frame_end);
                state.frame_start_index = state.bitstream.size(); // = 8 (position after the flag)
                state.in_preamble = true; // Ready for next frame
                state.in_frame = false;
                // If we found a valid frame, set complete flag regardless whether the packet was decoded successfully
                state.complete = true;
                state.preamble_count = state.preamble_count_pending;
                state.postamble_count = state.postamble_count_pending;
                state.preamble_count_pending = 1;
                state.postamble_count_pending = 0;

                state.frame_size_bits = frame_bits.size();

                if (state.enable_diagnostics)
                {
                    // Set up tracking for potential next frame using the shared flag
                    state.global_preamble_start_pending = state.global_bit_count - 7;

                    // Compute NRZI level before this shared flag
                    uint8_t level = state.last_nrzi_level;
                    for (size_t i = 0; i < 8; i++)
                    {
                        if (state.bitstream[state.bitstream.size() - 1 - i] == 0)
                        {
                            level = level ? 0 : 1;
                        }
                    }

                    state.frame_nrzi_level_pending = level;
                }

                return result;
            }
            else
            {
                // Empty frame - just consecutive flags, stay in preamble mode
                state.frame_start_index = state.bitstream.size();
                state.in_frame = false;
                state.in_preamble = true;
            }
        }

        // Continue collecting frame bits
        //
        // Safety check: prevent runaway buffer growth (max reasonable AX.25 frame)
        // AX.25 max frame is ~330 bytes = 2640 bits, with bit stuffing could be ~3200 bits
        // Add preamble flags overhead, let's say 4000 bits max

        if (state.bitstream.size() > 8000)
        {
            // Something went wrong (noise, lost sync), reset to search mode
            state.searching = true;
            state.in_frame = false;
            state.bitstream.clear();
            state.frame_start_index = 0;
            state.global_preamble_start_pending = 0;
            state.frame_nrzi_level_pending = 0;
            state.preamble_count_pending = 0;
            state.postamble_count_pending = 0;
        }
    }

    return false; // No complete packet yet
}

bool try_decode_bitstream(uint8_t bit, packet& packet, bitstream_state& state)
{
    bool result = try_decode_bitstream(bit, state);
    if (result)
    {
        packet = to_packet(state.frame);
    }
    return result;
}

bool try_decode_bitstream(const std::vector<uint8_t>& bitstream, size_t offset, packet& packet, size_t& read, bitstream_state& state)
{
    for (size_t i = offset; i < bitstream.size(); i++)
    {
        if (try_decode_bitstream(bitstream[i], packet, state))
        {
            read = i - offset + 1;
            return true;
        }
    }
    read = bitstream.size() - offset;
    return false;
}

LIBMODEM_AX25_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// FX.25                                                            //
//                                                                  //
// encode_frame, encode_bitstream                                   //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_FX25_NAMESPACE_BEGIN

std::vector<uint8_t> encode_frame(const std::vector<uint8_t>& frame_bytes, size_t min_check_bytes)
{
    return encode_frame(std::span<const uint8_t>(frame_bytes.data(), frame_bytes.size()), min_check_bytes);
}

std::vector<uint8_t> encode_frame(std::span<const uint8_t> frame_bytes, size_t min_check_bytes)
{
    // FX.25 frame encoding function
    // 
    // Encodes an AX.25 frame into an FX.25 frame by wrapping it with a
    // correlation tag and Reed-Solomon error-correction parity bytes.
    //
    // The resulting frame is fully backward-compatible: FX.25 receivers use
    // the tag and parity to correct errors, while plain AX.25 receivers
    // ignore the tag (noise), sync on the embedded AX.25 packet as usual,
    // and discard the trailing parity (noise).
    // 
    // Transmitted frame layout:
    // 
    //   +-------------------+---------------------------+------------------+
    //   | Correlation Tag   | AX.25 packet + 0x7E pad   | RS check bytes   |
    //   | (8 bytes)         | (data_size bytes)         | (parity)         |
    //   +-------------------+---------------------------+------------------+
    //   |    not encoded    |              RS-encoded                      |
    //   +-------------------+----------------------------------------------+
    //
    // References:
    //
    //   - FX.25 Development: http://ftp.tapr.org/meetings/DCC_2020/JE1WAZ/DCC-2020-PRUG-FINAL.pdf
    //   - Reed-Solomon Codes: https://en.wikipedia.org/wiki/Reed%E2%80%93Solomon_error_correction
    //   - AX.25 + FEC = FX.25: https://cloud.dvbr.net/direwolf/direwolf_git_v1.6/doc/AX25_plus_FEC_equals_FX25.pdf

    // FX.25 defines several RS code modes, each identified by a unique
    // 64-bit correlation tag. Modes differ in data capacity and error-correction strength.
    // The table is sorted by data size, then by check byte count,
    // so the first match is the smallest code that fits the frame
    // while meeting the caller's minimum parity request.
    //
    // Each entry: { correlation_tag, total_bytes, data_bytes, check_bytes }

    constexpr std::tuple<uint64_t, int, int, int> modes[] =
    {
        { 0x8F056EB4369660EEULL,  48,  32, 16 },  // Tag_04: RS(48,32)
        { 0xDBF869BD2DBB1776ULL,  64,  32, 32 },  // Tag_08: RS(64,32)
        { 0xC7DC0508F3D9B09EULL,  80,  64, 16 },  // Tag_03: RS(80,64)
        { 0x1EB7B9CDBC09C00EULL,  96,  64, 32 },  // Tag_07: RS(96,64)
        { 0x4A4ABEC4A724B796ULL, 128,  64, 64 },  // Tag_11: RS(128,64)
        { 0x26FF60A600CC8FDEULL, 144, 128, 16 },  // Tag_02: RS(144,128)
        { 0xFF94DC634F1CFF4EULL, 160, 128, 32 },  // Tag_06: RS(160,128)
        { 0xAB69DB6A543188D6ULL, 192, 128, 64 },  // Tag_10: RS(192,128)
        { 0x3ADB0C13DEAE2836ULL, 255, 191, 64 },  // Tag_09: RS(255,191)
        { 0x6E260B1AC5835FAEULL, 255, 223, 32 },  // Tag_05: RS(255,223)
        { 0xB74DB7DF8A532F3EULL, 255, 239, 16 },  // Tag_01: RS(255,239)
    };

    // Select smallest RS code that fits the frame
    uint64_t tag = 0;
    int total = 0, data_size = 0, check_size = 0;
    int mode_index = -1;
    for (int i = 0; i < std::size(modes); i++)
    {
        auto [t, tot, d, c] = modes[i];
        if (frame_bytes.size() <= d && c >= min_check_bytes)
        {
            tag = t;
            total = tot;         // Total bytes transmitted (data + check)
            data_size = d;       // Data portion size
            check_size = c;      // Number of RS check bytes
            mode_index = i + 1;
            break;
        }
    }

    (void)mode_index; // reserved for diagnostics and future use
    (void)total;      // reserved for future use

    if (tag == 0)
    {
        // Packet too large for any FX.25 format
        return {};
    }

    std::vector<uint8_t> output;

    // The 8-byte tag is transmitted LSB first (byte 0 = bits 0-7, etc.).
    // It tells FX.25 receivers that an FX.25 frame follows and which RS
    // code was used.

    for (int i = 0; i < 8; i++)
    {
        output.push_back((tag >> (i * 8)) & 0xFF);
    }

    // Prepare the data block (pre_encoded_data) for RS encoding
    // 
    // All RS modes use the parent RS(255, 255-check_size) code.
    // For shortened modes (data_size < 255-check_size), trailing zeros fill
    // the block to the full parent length; these zeros are implicit and
    // are NOT transmitted.
    // 
    // Block layout before encoding (full_data_size = 255 - check_size):
    //
    //   +-------------------------------------------------------+
    //   |                    full_data_size                     |
    //   +----------------+----------------+---------------------+
    //   |  frame         |  0x7E padding  |  0x00 (shortening)  |
    //   |  (original)    |  (idle flags)  |  (not transmitted)  |
    //   +----------------+----------+-----+---------------------+
    //   |         data_size         |                           |
    //   +---------------------------+---------------------------+
    //
    // The 0x7E padding serves double duty: it fills the data block for RS
    // encoding and appears as AX.25 idle flags to legacy receivers.
    //
    //   - frame:  complete AX.25 packet, placed UNMODIFIED for backward compatibility
    //   - 0x7E padding: fills to data_size so AX.25 receivers see idle flags
    //   - 0x00 padding: implicit zeros for RS shortening (already zero-initialized)

    constexpr size_t block_size = 255;

    int full_data_size = block_size - check_size;

    std::vector<uint8_t> pre_encoded_data(full_data_size, 0x00);

    std::copy(frame_bytes.begin(), frame_bytes.end(), pre_encoded_data.begin());

    for (size_t i = frame_bytes.size(); i < data_size; i++)
    {
        pre_encoded_data[i] = 0x7E;
    }

    // Reed-Solomon encoding
    //
    // Encode with the parent RS(255, full_data_size) code:
    // 
    //   - polynomial 0x11d (x^8 + x^4 + x^3 + x^2 + 1)
    //   - fcr = 1 (first consecutive root)
    //   - prim = 1 (primitive element)
    //
    // The encoder leaves the data bytes unchanged and appends check_size
    // parity bytes.

    std::unique_ptr<correct_reed_solomon, decltype(&correct_reed_solomon_destroy)> rs(
        correct_reed_solomon_create(correct_rs_primitive_polynomial_8_4_3_2_0, 1, 1, check_size),
        correct_reed_solomon_destroy
    );

    if (!rs)
    {
        // Failed to create RS encoder
        return {};
    }

    std::vector<uint8_t> encoded_data(block_size);

    ssize_t result = correct_reed_solomon_encode(rs.get(), pre_encoded_data.data(), full_data_size, encoded_data.data());

    if (result != block_size)
    {
        return {};
    }

    // Append the encoded block to output
    // 
    // This contains:
    // 
    //   - First 'data_size' bytes: The EXACT SAME AX.25 packet + padding
    //   - Last 'check_size' bytes: RS parity for error correction
    //
    // Encoded data layout (full RS block = 255 bytes):
    //
    // +--------------------------------------------+--------------------+
    // |               full_data_size               |     check_size     |
    // +-------------+-------------+----------------+--------------------+
    // | frame_bytes | 0x7E        | 0x00           | RS check bytes     |
    // | (original)  | (idle flags)| (RS shortening)| (parity)           |
    // +-------------+-------+-----+----------------+--------------------+
    // |      data_size      |      skipped         |     check_size     |
    // +---------------------+----------------------+--------------------+
    // |       transmitted   |                      |     transmitted    |
    // +---------------------+----------------------+--------------------+
    //
    // For shortened codes (data_size < full_data_size), the zero-padding
    // region is not transmitted.

    output.insert(output.end(), encoded_data.begin(), encoded_data.begin() + data_size);
    output.insert(output.end(), encoded_data.begin() + full_data_size, encoded_data.begin() + full_data_size + check_size);

    return output;
}

std::vector<uint8_t> encode_bitstream(const packet& p, int preamble_flags, int postamble_flags, size_t min_check_bytes)
{
    std::vector<uint8_t> ax25_frame = LIBMODEM_AX25_NAMESPACE_REFERENCE encode_frame(p);
    return LIBMODEM_FX25_NAMESPACE_REFERENCE encode_bitstream(ax25_frame.begin(), ax25_frame.end(), preamble_flags, postamble_flags, min_check_bytes);
}

std::vector<uint8_t> encode_bitstream(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, int preamble_flags, int postamble_flags, size_t min_check_bytes)
{
    std::vector<uint8_t> ax25_frame = LIBMODEM_AX25_NAMESPACE_REFERENCE encode_frame(f);
    return LIBMODEM_FX25_NAMESPACE_REFERENCE encode_bitstream(ax25_frame.begin(), ax25_frame.end(), preamble_flags, postamble_flags, min_check_bytes);
}

std::vector<uint8_t> encode_bitstream(LIBMODEM_AX25_NAMESPACE_REFERENCE nrz_scrambled_t, const packet& p, int preamble_flags, int postamble_flags, size_t min_check_bytes)
{
    std::vector<uint8_t> ax25_frame = LIBMODEM_AX25_NAMESPACE_REFERENCE encode_frame(p);
    return LIBMODEM_FX25_NAMESPACE_REFERENCE encode_bitstream(LIBMODEM_AX25_NAMESPACE_REFERENCE nrz_scrambled, ax25_frame.begin(), ax25_frame.end(), preamble_flags, postamble_flags, min_check_bytes);
}

std::vector<uint8_t> encode_bitstream(LIBMODEM_AX25_NAMESPACE_REFERENCE nrz_scrambled_t, const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, int preamble_flags, int postamble_flags, size_t min_check_bytes)
{
    std::vector<uint8_t> ax25_frame = LIBMODEM_AX25_NAMESPACE_REFERENCE encode_frame(f);
    return LIBMODEM_FX25_NAMESPACE_REFERENCE encode_bitstream(LIBMODEM_AX25_NAMESPACE_REFERENCE nrz_scrambled, ax25_frame.begin(), ax25_frame.end(), preamble_flags, postamble_flags, min_check_bytes);
}

LIBMODEM_FX25_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// IL2P                                                             //
//                                                                  //
// encode_frame, encode_bitstream                                   //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_IL2P_NAMESPACE_BEGIN

enum class header_type : uint8_t
{
    transparent = 0, // Type 0: raw AX.25 encapsulation (fallback)
    structured = 1   // Type 1: IL2P structured header
};

enum class pid_abbreviation : uint8_t
{
    s_frame = 0x0,          // supervisory frame — no AX.25 PID byte
    u_frame = 0x1,          // unnumbered frame — no PID for non-UI
    layer3 = 0x2,           // AX.25 PID 0x10-0x1F or 0x20-0x2F (AX.25 layer 3)
    iso8208 = 0x3,          // AX.25 PID 0x01 (ISO 8208/CCITT X.25 PLP)
    compressed_tcp = 0x4,   // AX.25 PID 0x06
    uncompressed_tcp = 0x5, // AX.25 PID 0x07
    segmentation = 0x6,     // AX.25 PID 0x08
    ip = 0xB,               // AX.25 PID 0xCC (ARPA IP)
    arp = 0xC,              // AX.25 PID 0xCD (ARPA ARP)
    flexnet = 0xD,          // AX.25 PID 0xCE (FlexNet)
    thenet = 0xE,           // AX.25 PID 0xCF (TheNET)
    no_layer3 = 0xF,        // AX.25 PID 0xF0 (APRS)
    unknown = 0xFF
};

struct header
{
    header_type type = header_type::structured;
    bool ui_frame = false;
    pid_abbreviation pid = pid_abbreviation::no_layer3;
    uint8_t control = 0;
    std::array<char, 6> to_address = {};
    uint8_t to_address_ssid = 0;
    std::array<char, 6> from_address = {};
    uint8_t from_address_ssid = 0;
    uint16_t payload_length = 0;
};

template<typename InputIt, typename OutputIt>
OutputIt bytes_to_bits_msb(InputIt first, InputIt last, OutputIt out);

uint8_t encode_address_char(char c) noexcept;
std::array<char, 6> encode_address_sixbit(std::string_view address);
uint8_t encode_header_byte(char address_char, uint8_t bit6, uint8_t bit7);
std::array<uint8_t, 4> encode_crc_hamming(uint16_t crc);
pid_abbreviation encode_pid(uint8_t ax25_pid);
pid_abbreviation encode_pid(LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type type, uint8_t ax25_pid);

template<typename InputIt, typename OutputIt>
OutputIt reed_solomon_encode(correct_reed_solomon* encoder, InputIt data_first, InputIt data_last, size_t parity_count, OutputIt out);

template<typename OutputIt>
void serialize_bit(int bit, uint8_t& byte, int& mask, OutputIt& out);

int scramble_bit(int& state, int in_bit);

template<typename InputIt, typename OutputIt>
OutputIt scramble_block(InputIt first, InputIt last, OutputIt out);

std::array<uint8_t, 13> encode_header(const header& h);
void scramble_and_reed_solomon_encode_header(correct_reed_solomon* encoder, std::array<uint8_t, 13>& header, std::vector<uint8_t>& output);
void scramble_and_reed_solomon_encode_payload(correct_reed_solomon* encoder, std::span<const uint8_t> data, std::vector<uint8_t>& output);
uint8_t encode_control(LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type type, uint8_t control, uint8_t command_response);
header encode_structured_header(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f);
std::array<uint8_t, 13> encode_transparent_header(uint16_t payload_length);
bool try_encode_structured_frame(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, correct_reed_solomon* header_encoder, correct_reed_solomon* payload_encoder, std::vector<uint8_t>& out);
bool try_encode_transparent_frame(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, correct_reed_solomon* header_encoder, correct_reed_solomon* payload_encoder, std::vector<uint8_t>& out);

template<typename InputIt, typename OutputIt>
OutputIt bytes_to_bits_msb(InputIt first, InputIt last, OutputIt out)
{
    // Converts bytes to individual bits (MSB-first per byte)
    //
    // Example: byte 0x7E (01111110) -> bits [0,1,1,1,1,1,1,0]

    for (auto it = first; it != last; ++it)
    {
        uint8_t byte = *it;
        for (int i = 7; i >= 0; i--)
        {
            *out++ = (byte >> i) & 1u; // Extract bits MSB-first
        }
    }

    return out;
}

uint8_t encode_address_char(char c) noexcept
{
    // Encode an ASCII character into the 6-bit DL-3ZJ / IL2P callsign alphabet.
    // Uppercases the input, then subtracts 0x20 so that space=0, '0'=16, 'A'=33, etc.

    return static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(c)) - 0x20u);
}

std::array<char, 6> encode_address_sixbit(std::string_view address)
{
    std::array<char, 6> result = {};
    for (size_t i = 0; i < 6; ++i)
    {
        result[i] = (i < address.size()) ? address[i] : ' ';
    }
    return result;
}

uint8_t encode_header_byte(char address_char, uint8_t bit6, uint8_t bit7)
{
    // +-------+-------+-------+-------+-------+-------+-------+-------+
    // | bit 7 | bit 6 | bit 5 | bit 4 | bit 3 | bit 2 | bit 1 | bit 0 |
    // +-------+-------+-------+-------+-------+-------+-------+-------+
    // | bit7  | bit6  |       6-bit encoded callsign character        |
    // +-------+-------+-----------------------------------------------+
    //
    // bits [5:0] — ASCII character converted to IL2P 6-bit encoding
    // bit  [6]   — a single bit from the UI flag, PID, or control field
    // bit  [7]   — a single bit from the frame type or payload length

    return encode_address_char(address_char) | ((bit6 & 0b1) << 6) | ((bit7 & 0b1) << 7);
}

std::array<uint8_t, 4> encode_crc_hamming(uint16_t crc)
{
    // Hamming(7,4)-encodes the AX.25 CRC for IL2P transmission.
    //
    // The 16-bit CRC is split into four 4-bit nibbles, each independently
    // encoded into a 7-bit codeword with 3 parity bits for single-bit
    // error correction. High nibble is output first.
    //
    // Codeword bit layout:
    //
    //   +-----+-----+-----+-----+-----+-----+-----+------+
    //   |  0  |  p2 |  p1 |  p0 |  d3 |  d2 |  d1 |  d0  |
    //   +-----+-----+-----+-----+-----+-----+-----+------+
    //   |     |   parity bits   |  original data nibble  |
    //   +-----+-----------------+------------------------+
    //
    //   p0 (lsb) = d0 ^ d1 ^ d2 ^ d3
    //   p1       = d0 ^ d2 ^ d3
    //   p2       = d0 ^ d1 ^ d3

    static constexpr uint8_t hamming_encode_table[16] =
    {
        0x00, 0x71, 0x62, 0x13, 0x54, 0x25, 0x36, 0x47,
        0x38, 0x49, 0x5a, 0x2b, 0x6c, 0x1d, 0x0e, 0x7f
    };

    return {{
        hamming_encode_table[(crc >> 12) & 0b1111],
        hamming_encode_table[(crc >> 8) & 0b1111],
        hamming_encode_table[(crc >> 4) & 0b1111],
        hamming_encode_table[(crc >> 0) & 0b1111],
    }};
}

pid_abbreviation encode_pid(uint8_t ax25_pid)
{
    if ((ax25_pid & 0x30) == 0x20 || (ax25_pid & 0x30) == 0x10)
    {
        return pid_abbreviation::layer3;
    }

    switch (ax25_pid)
    {
        case 0x01: return pid_abbreviation::iso8208;
        case 0x06: return pid_abbreviation::compressed_tcp;
        case 0x07: return pid_abbreviation::uncompressed_tcp;
        case 0x08: return pid_abbreviation::segmentation;
        case 0xCC: return pid_abbreviation::ip;
        case 0xCD: return pid_abbreviation::arp;
        case 0xCE: return pid_abbreviation::flexnet;
        case 0xCF: return pid_abbreviation::thenet;
        case 0xF0: return pid_abbreviation::no_layer3;
        default:   return pid_abbreviation::unknown;
    }
}

template<typename InputIt, typename OutputIt>
OutputIt reed_solomon_encode(correct_reed_solomon* encoder, InputIt data_first, InputIt data_last, size_t parity_count, OutputIt out)
{
    // Reed-Solomon encoding over GF(2^8) with a block size of 255 bytes.
    //
    // RS(255, k) where k = 255 - parity_count:
    // 
    //   Header:  RS(255, 253) → 2 parity bytes, corrects 1 symbol error
    //   Payload: RS(255, 239) → 16 parity bytes, corrects 8 symbol errors

    if (encoder == nullptr || parity_count >= 255)
    {
        return out;
    }

    constexpr size_t full_block_size = 255;
    const size_t data_size = static_cast<size_t>(std::distance(data_first, data_last));
    const size_t full_data_size = full_block_size - parity_count;

    // Build the RS input block (right-aligned with leading zeros):
    // 
    // +---------------------------+------------------+
    // |  zero padding             |   actual data    |
    // |  (right-align)            |                  |
    // +---------------------------+------------------+
    // |             full_data_size (255 - parity)    |
    // +----------------------------------------------+
    //
    //   - 0x00 padding: right-aligns data so parity is computed correctly
    //   - actual data:  headers are 13 bytes, payload blocks up to 239 bytes
    //
    // The RS codec always operates on a full 255-byte block. When the data
    // is shorter than full_data_size, it must be right-aligned with leading
    // zeros so the parity bytes are computed correctly.

    std::vector<uint8_t> pre_encoded_data(full_data_size, 0x00);

    const size_t padding = full_data_size - data_size;

    std::copy(data_first, data_last, pre_encoded_data.begin() + padding);

    // Encode using the full RS(255, full_data_size) code.
    // Output is 255 bytes: full_data_size unchanged data + parity_count parity bytes.

    std::vector<uint8_t> encoded_data(full_block_size);

    correct_reed_solomon_encode(encoder, pre_encoded_data.data(), full_data_size, encoded_data.data());

    // RS-encoded: data + parity bytes
    //
    // Encoded data layout (full RS block = 255 bytes):
    //
    // |<---------- full_data_size ---------->|<--- parity_count ---->|
    // +-------------+------------------------+-----------------------+
    // | 0x00        | actual data            | RS parity bytes       |
    // | (padding)   | (unchanged)            | (error correction)    |
    // +-------------+------------------------+-----------------------+
    //               |<---- data_size ------->|<--- parity_count ---->|
    //                    output ^^^^           ^^^^ output
    //
    // Only the actual data and parity are written to output;
    // the leading zero-padding is not included.

    out = std::copy(data_first, data_last, out);
    out = std::copy(encoded_data.begin() + full_data_size, encoded_data.end(), out);

    return out;
}

template<typename OutputIt>
void serialize_bit(int bit, uint8_t& byte, int& mask, OutputIt& out)
{
    // Pack a single bit into an output byte, MSB-first.
    // Advances the iterator and resets when a full byte is written.

    if (bit)
    {
        byte |= mask;
    }

    mask >>= 1;

    if (mask == 0)
    {
        *out++ = byte;
        byte = 0;
        mask = 0b10000000;
    }
}

int scramble_bit(int& state, int in_bit)
{
    // Clocks one bit through a 9-bit Galois LFSR with polynomial x^9 + x^4 + 1.
    //
    // Whitens the bitstream by XORing each input bit with a pseudo-random sequence,
    // breaking up long runs of identical bits that cause clock recovery failures
    // and DC bias on the radio link.
    // 
    // IL2P spec: https://tarpn.net/t/il2p/il2p-specification_draft_v0-6.pdf
    //
    //              →   →   →   →   →   →   →   →   →   →
    //            +---+---+---+---+---+---+---+---+---+---+
    // feedback → | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 | x |   state (bits)
    //     ↑      +---+---+---+---+---+---+---+---+---+---+
    //     |                          ↑   ↑           ↑
    //     |       in_bit      XOR ---+---+--XOR------+--→ output bit (bit0 ⊕ bit4)
    //     |          ↓                               ↓
    //     +----------+-----------------XOR-----------+
    //   feedback
    //   (bit0 ⊕ in_bit)

    const int bit0 = state & 1; // bit0
    const int bit4 = (state >> 4) & 1; // bit4
    const int out_bit = bit0 ^ bit4; // tap feedback, bit0 XOR bit4
    const int feedback = bit0 ^ in_bit; // feedback is bit0 XOR input bit

    state >>= 1;              // shift register
    state |= (feedback << 8); // inject feedback at MSB
    state ^= (bit0 << 3);     // Galois tap: XOR bit0 into position 3

    return out_bit;
}

template<typename InputIt, typename OutputIt>
OutputIt scramble_block(InputIt first, InputIt last, OutputIt out)
{
    // Scrambles a block of bytes through the IL2P Galois LFSR (x⁹+x⁴+1).
    //
    // The scrambler has a 5-bit pipeline delay: input bits enter the register at
    // position [8] and take 5 clocks to propagate to the output tap at [4].
    // The first 5 output bits are therefore pure register state, not yet influenced
    // by the input, and are discarded. After the last input bit, 5 zero bits are
    // clocked in to flush the remaining data out of the pipeline.
    // Total output bits == total input bits.
    //
    //  +---------+----+----+----+----+----+----+----+-----+----+-------+-------+-------+-------+-------+
    //  | clock   |  1 |  2 |  3 |  4 |  5 |  6 |  7 | ... |  N |  N+1  |  N+2  |  N+3  |  N+4  |  N+5  |
    //  +---------+----+----+----+----+----+----+----+-----+----+-------+-------+-------+-------+-------+
    //  | input   | i0 | i1 | i2 | i3 | i4 | i5 | i6 | ... | iN |   0   |   0   |   0   |   0   |   0   |
    //  +---------+----+----+----+----+----+----+----+-----+----+-------+-------+-------+-------+-------+
    //  | output  |  x |  x |  x |  x |  x | o0 | o1 | ... |oN-5| oN-4  | oN-3  | oN-2  | oN-1  |  oN   |
    //  +---------+----+----+----+----+----+----+----+-----+----+-------+-------+-------+-------+-------+
    //  | action  |   discard (5 bits)     |   collect (data)   |      collect (flush)                  |
    //  +---------+------------------------+--------------------+---------------------------------------+

    constexpr int initial_state = 0x00f;
    constexpr int pipeline_delay = 5;

    int state = initial_state;
    uint8_t out_byte = 0;
    int out_mask = 0b1000'0000;
    int warmup = pipeline_delay;

    // Feed each input bit through the LFSR, MSB first.
    // The first 'pipeline_delay' outputs are discarded
    // they contain only initial register state, not yet influenced by input data.

    for (auto it = first; it != last; it++)
    {
        for (int mask = 0b1000'0000; mask != 0; mask >>= 1)
        {
            int in_bit = (*it & mask) ? 1 : 0; // extract input bit
            int out_bit = scramble_bit(state, in_bit); // scramble the input bit

            if (warmup > 0)
            {
                // Discard the first 'pipeline_delay' output bits (warmup phase)
                warmup--;
            }
            else
            {
                serialize_bit(out_bit, out_byte, out_mask, out);
            }
        }
    }

    // Flush the pipeline by feeding 'pipeline_delay' zero bits.
    // The LFSR still holds valid scrambled data between positions [4] and [0]
    // that hasn't reached the output tap yet. Zero inputs push it out without
    // adding new information.

    for (int i = 0; i < pipeline_delay; i++)
    {
        int out_bit = scramble_bit(state, 0);
        serialize_bit(out_bit, out_byte, out_mask, out);
    }

    // Write any partially accumulated byte.

    if (out_mask != 0b1000'0000)
    {
        *out++ = out_byte;
    }

    return out;
}

std::array<uint8_t, 13> encode_header(const header& h)
{
    // Encode an IL2P header into 13 bytes.
    //
    // The control field (7 bits), PID (4 bits), and payload length (10 bits) are each
    // spread across consecutive header bytes one bit at a time, MSB first.
    //
    // +------+------------------+-----------------+---------------------+
    // | Byte | bits [5:0]       | bit [6]         | bit [7]             |
    // +------+------------------+-----------------+---------------------+
    // |  0   | to_address[0]    | UI flag         | 0                   |
    // |  1   | to_address[1]    | PID bit 3       | frame type          |
    // |  2   | to_address[2]    | PID bit 2       | payload bit 9       |
    // |  3   | to_address[3]    | PID bit 1       | payload bit 8       |
    // |  4   | to_address[4]    | PID bit 0       | payload bit 7       |
    // |  5   | to_address[5]    | control bit 6   | payload bit 6       |
    // |  6   | from_address[0]  | control bit 5   | payload bit 5       |
    // |  7   | from_address[1]  | control bit 4   | payload bit 4       |
    // |  8   | from_address[2]  | control bit 3   | payload bit 3       |
    // |  9   | from_address[3]  | control bit 2   | payload bit 2       |
    // | 10   | from_address[4]  | control bit 1   | payload bit 1       |
    // | 11   | from_address[5]  | control bit 0   | payload bit 0       |
    // +------+------------------+-----------------+---------------------+
    // | 12   | (see below)                                              |
    // +------+----------------------------------------------------------+
    // 
    // See IL2P specification: https://tarpn.net/t/il2p/il2p-specification_draft_v0-6.pdf

    std::array<uint8_t, 13> header_bytes;

    const uint8_t pid = static_cast<uint8_t>(h.pid) & 0b00001111; // 4 bits
    const uint8_t control = h.control & 0b01111111; // 7 bits
    const uint16_t payload_length = h.payload_length & 0b0000001111111111; // 10 bits

    // Destination callsign characters (bytes 0–5)
    header_bytes[0] = encode_header_byte(h.to_address[0], h.ui_frame, 0);
    header_bytes[1] = encode_header_byte(h.to_address[1], static_cast<uint8_t>(pid >> 3), static_cast<uint8_t>(h.type));
    header_bytes[2] = encode_header_byte(h.to_address[2], static_cast<uint8_t>(pid >> 2), static_cast<uint8_t>(payload_length >> 9));
    header_bytes[3] = encode_header_byte(h.to_address[3], static_cast<uint8_t>(pid >> 1), static_cast<uint8_t>(payload_length >> 8));
    header_bytes[4] = encode_header_byte(h.to_address[4], static_cast<uint8_t>(pid >> 0), static_cast<uint8_t>(payload_length >> 7));
    header_bytes[5] = encode_header_byte(h.to_address[5], static_cast<uint8_t>(control >> 6), static_cast<uint8_t>(payload_length >> 6));

    // Source callsign characters (bytes 6–11)
    header_bytes[6] = encode_header_byte(h.from_address[0], static_cast<uint8_t>(control >> 5), static_cast<uint8_t>(payload_length >> 5));
    header_bytes[7] = encode_header_byte(h.from_address[1], static_cast<uint8_t>(control >> 4), static_cast<uint8_t>(payload_length >> 4));
    header_bytes[8] = encode_header_byte(h.from_address[2], static_cast<uint8_t>(control >> 3), static_cast<uint8_t>(payload_length >> 3));
    header_bytes[9] = encode_header_byte(h.from_address[3], static_cast<uint8_t>(control >> 2), static_cast<uint8_t>(payload_length >> 2));
    header_bytes[10] = encode_header_byte(h.from_address[4], static_cast<uint8_t>(control >> 1), static_cast<uint8_t>(payload_length >> 1));
    header_bytes[11] = encode_header_byte(h.from_address[5], static_cast<uint8_t>(control >> 0), static_cast<uint8_t>(payload_length >> 0));

    // Byte 12: pack both SSIDs into a single byte
    //
    // +-------+-------+-------+-------+-------+-------+-------+-------+
    // | bit 7 | bit 6 | bit 5 | bit 4 | bit 3 | bit 2 | bit 1 | bit 0 |
    // +-------+-------+-------+-------+-------+-------+-------+-------+
    // |         to_ssid [3:0]         |        from_ssid [3:0]        |
    // +-------------------------------+-------------------------------+

    header_bytes[12] = static_cast<uint8_t>((h.to_address_ssid << 4) | (h.from_address_ssid & 0b00001111));

    return header_bytes;
}

void scramble_and_reed_solomon_encode_header(correct_reed_solomon* encoder, std::array<uint8_t, 13>& header, std::vector<uint8_t>& output)
{
    constexpr size_t header_parity = 2;
    std::array<uint8_t, 13> scrambled;
    scramble_block(header.begin(), header.end(), scrambled.begin());
    reed_solomon_encode(encoder, scrambled.begin(), scrambled.end(), header_parity, std::back_inserter(output));
}

void scramble_and_reed_solomon_encode_payload(correct_reed_solomon* encoder, std::span<const uint8_t> data, std::vector<uint8_t>& output)
{
    constexpr size_t payload_data_max = 239;
    constexpr size_t payload_parity = 16;

    const size_t N = data.size();

    if (N == 0)
    {
        return;
    }

    const size_t block_count = (N + payload_data_max - 1) / payload_data_max;
    const size_t small_block_size = N / block_count;
    const size_t large_block_size = small_block_size + 1;
    const size_t large_block_count = N - (block_count * small_block_size);
    const size_t small_block_count = block_count - large_block_count;

    size_t offset = 0;

    // Large blocks first (spec-mandated order)
    for (size_t i = 0; i < large_block_count; ++i)
    {
        auto chunk = data.subspan(offset, large_block_size);
        std::vector<uint8_t> scrambled(large_block_size);
        scramble_block(chunk.begin(), chunk.end(), scrambled.begin());
        reed_solomon_encode(encoder, scrambled.begin(), scrambled.end(), payload_parity, std::back_inserter(output));
        offset += large_block_size;
    }

    // Small blocks after
    for (size_t i = 0; i < small_block_count; ++i)
    {
        auto chunk = data.subspan(offset, small_block_size);
        std::vector<uint8_t> scrambled(small_block_size);
        scramble_block(chunk.begin(), chunk.end(), scrambled.begin());
        reed_solomon_encode(encoder, scrambled.begin(), scrambled.end(), payload_parity, std::back_inserter(output));
        offset += small_block_size;
    }
}

uint8_t encode_control(LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type type, uint8_t control, uint8_t command_response)
{
    if (type == LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type::i)
    {
        // AX.25 I-frame control byte:
        //
        // +-------+-------+-------+-------+-------+-------+-------+-------+
        // | bit 7 | bit 6 | bit 5 | bit 4 | bit 3 | bit 2 | bit 1 | bit 0 |
        // +-------+-------+-------+-------+-------+-------+-------+-------+
        // |      N(R) [2:0]       |  P/F  |      N(S) [2:0]       |   0   |
        // +-------+-------+-------+-------+-------+-------+-------+-------+
        //
        // IL2P 7-bit encoded control:
        //
        // +-------+-------+-------+-------+-------+-------+-------+
        // | bit 6 | bit 5 | bit 4 | bit 3 | bit 2 | bit 1 | bit 0 |
        // +-------+-------+-------+-------+-------+-------+-------+
        // |  P/F  |      N(R) [2:0]       |      N(S) [2:0]       |
        // +-------+-------+-------+-------+-------+-------+-------+

        const uint8_t pf = (control >> 4) & 0x01u;
        const uint8_t nr = (control >> 5) & 0x07u;
        const uint8_t ns = (control >> 1) & 0x07u;

        return static_cast<uint8_t>((pf << 6) | (nr << 3) | ns);
    }
    else if (is_s_frame_type(type))
    {
        // AX.25 S-frame control byte:
        //
        // +-------+-------+-------+-------+-------+-------+-------+-------+
        // | bit 7 | bit 6 | bit 5 | bit 4 | bit 3 | bit 2 | bit 1 | bit 0 |
        // +-------+-------+-------+-------+-------+-------+-------+-------+
        // |      N(R) [2:0]       |  P/F  |    SS [1:0]   |   0   |   1   |
        // +-------+-------+-------+-------+-------+-------+-------+-------+
        //
        // IL2P 7-bit encoded control:
        //
        // +-------+-------+-------+-------+-------+-------+-------+
        // | bit 6 | bit 5 | bit 4 | bit 3 | bit 2 | bit 1 | bit 0 |
        // +-------+-------+-------+-------+-------+-------+-------+
        // |  P/F  |      N(R) [2:0]       |   C   |    SS [1:0]   |
        // +-------+-------+-------+-------+-------+-------+-------+

        const uint8_t pf = (control >> 4) & 0x01u;
        const uint8_t nr = (control >> 5) & 0x07u;
        const uint8_t ss = (control >> 2) & 0x03u;

        return static_cast<uint8_t>((pf << 6) | (nr << 3) | (command_response << 2) | ss);
    }
    else if (is_u_frame_type(type))
    {
        // AX.25 U-frame control byte:
        //
        // +-------+-------+-------+-------+-------+-------+-------+-------+
        // | bit 7 | bit 6 | bit 5 | bit 4 | bit 3 | bit 2 | bit 1 | bit 0 |
        // +-------+-------+-------+-------+-------+-------+-------+-------+
        // |     M [2:0] (high)    |  P/F  |  M [1:0] (low)|   1   |   1   |
        // +-------+-------+-------+-------+-------+-------+-------+-------+
        //
        // IL2P 7-bit encoded control:
        //
        // +-------+-------+-------+-------+-------+-------+-------+
        // | bit 6 | bit 5 | bit 4 | bit 3 | bit 2 | bit 1 | bit 0 |
        // +-------+-------+-------+-------+-------+-------+-------+
        // |  P/F  |    OPCODE [2:0]       |   C   |   0   |   0   |
        // +-------+-------+-------+-------+-------+-------+-------+
        //
        // OPCODE mapping:
        //
        // +--------+-------+
        // | OPCODE | Type  |
        // +--------+-------+
        // |   0    | SABM  |
        // |   1    | DISC  |
        // |   2    | DM    |
        // |   3    | UA    |
        // |   4    | FRMR  |
        // |   5    | UI    |
        // |   6    | XID   |
        // |   7    | TEST  |
        // +--------+-------+

        const uint8_t pf = (control >> 4) & 0x01u;
        uint8_t opcode = 0;
        switch (type)
        {
            case LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type::sabm: opcode = 0; break;
            case LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type::disc: opcode = 1; break;
            case LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type::dm:   opcode = 2; break;
            case LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type::ua:   opcode = 3; break;
            case LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type::frmr: opcode = 4; break;
            case LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type::ui:   opcode = 5; break;
            case LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type::xid:  opcode = 6; break;
            case LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type::test: opcode = 7; break;
            default:                                                 opcode = 5; break;
        }

        return static_cast<uint8_t>((pf << 6) | (opcode << 3) | (command_response << 2));
    }

    return 0;
}

pid_abbreviation encode_pid(LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type type, uint8_t ax25_pid)
{
    // S-frames and non-UI U-frames have no AX.25 PID byte.
    // Only I-frames and UI frames carry a real PID.

    if (is_s_frame_type(type))
    {
        return pid_abbreviation::s_frame;
    }
    else if (type == LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type::i ||
        type == LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type::ui)
    {
        return encode_pid(ax25_pid);
    }
    else
    {
        return pid_abbreviation::u_frame;
    }
}

header encode_structured_header(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f)
{
    // Builds the 13-byte IL2P structured header from a frame.
    // Encodes callsigns, SSIDs, PID, control, and payload length fields.

    LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type type = LIBMODEM_AX25_NAMESPACE_REFERENCE get_frame_type(f.control[0]);

    header h;
    h.type = header_type::structured;
    h.ui_frame = (type == LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type::ui);

    const uint8_t control = f.control[0];
    const uint8_t command_response = f.to.command_response ? 1u : 0u; // CR flag set?

    h.control = encode_control(type, control, command_response);

    h.to_address = encode_address_sixbit(f.to.text);
    h.from_address = encode_address_sixbit(f.from.text);

    // Mask SSIDs to 4 bits — AX.25 SSIDs are 0–15 and IL2P packs both into a single byte.
    h.to_address_ssid = static_cast<uint8_t>(f.to.ssid & 0b00001111u);
    h.from_address_ssid = static_cast<uint8_t>(f.from.ssid & 0b00001111u);

    h.payload_length = static_cast<uint16_t>(f.data.size());

    h.pid = encode_pid(type, f.pid);

    return h;
}

std::array<uint8_t, 13> encode_transparent_header(uint16_t payload_length)
{
    // Builds the 13-byte IL2P transparent (Type 0) header.
    //
    // The payload length (up to 1023 bytes) is encoded as a 10-bit
    // value spread across the MSBs of header bytes [2..11] — one bit per byte.
    //
    //  +-------------+-------+-------+-------+-------+-------+-------+-------+-------+-------+-------+
    //  | header byte | [ 2]  | [ 3]  | [ 4]  | [ 5]  | [ 6]  | [ 7]  | [ 8]  | [ 9]  | [10]  | [11]  |
    //  +-------------+-------+-------+-------+-------+-------+-------+-------+-------+-------+-------+
    //  | length bit  |  b9   |  b8   |  b7   |  b6   |  b5   |  b4   |  b3   |  b2   |  b1   |  b0   |
    //  +-------------+-------+-------+-------+-------+-------+-------+-------+-------+-------+-------+
    //  |             | (MSB) |       |       |       |       |       |       |       |       | (LSB) |
    //  +-------------+-------+-------+-------+-------+-------+-------+-------+-------+-------+-------+
    //
    // This scatters the length field across the header so that Reed-Solomon
    // can protect each bit independently, rather than having a contiguous
    // multi-byte length field where a burst error could corrupt all bits at once.

    std::array<uint8_t, 13> header{};

    // Byte 0, bit 7: FEC level (1 = maximum FEC, 16 parity bytes per RS block)
    // Must match the parity count used in scramble_and_rs_encode_payload.
    header[0] |= 0x80u;

    for (int bit = 0; bit < 10; ++bit)
    {
        if ((payload_length >> bit) & 1u)
        {
            header[11 - bit] |= 0x80u;
        }
    }

    return header;
}

bool try_encode_structured_frame(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, correct_reed_solomon* header_encoder, correct_reed_solomon* payload_encoder, std::vector<uint8_t>& output)
{
    // Encodes a two-address AX.25 frame (no digipeater path) in IL2P
    // "structured" mode, where callsigns, SSIDs, PID, and control fields
    // are packed into a fixed 13-byte header rather than sent verbatim.
    //
    //   +-------------------------+---------------------------+---------------------+
    //   |  Header (15 B)          |  Payload (N + 16 B)       |  CRC (4 B)          |
    //   |  13 B data + 2 B parity |  N B data + 16 B parity   |  Hamming-encoded    |
    //   +-------------------------+---------------------------+---------------------+
    //
    // Steps:
    // 
    //   1. Encode the AX.25 frame to extract its FCS (CRC).
    //   2. Build the 13-byte IL2P structured header from the frame fields.
    //   3. Scramble and RS-encode the header (13 B data + 2 B parity = 15 B).
    //   4. Scramble and RS-encode the payload (N B data + 16 B parity).
    //   5. Hamming-encode the CRC and append it (4 B).

    constexpr size_t max_payload = 1023;

    if (f.data.size() > max_payload)
    {
        return false;
    }

    if (header_encoder == nullptr || payload_encoder == nullptr)
    {
        return false;
    }

    LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type type = LIBMODEM_AX25_NAMESPACE_REFERENCE get_frame_type(f.control[0]);
    if (type == LIBMODEM_AX25_NAMESPACE_REFERENCE frame_type::sabme)
    {
        return false;
    }

    if (f.is_mod128)
    {
        return false;
    }

    // Encode the full AX.25 frame to extract the AX.25 FCS
    std::vector<uint8_t> ax25_bytes = LIBMODEM_AX25_NAMESPACE_REFERENCE encode_frame(f);
    if (ax25_bytes.size() < 2)
    {
        return false;
    }

    uint16_t crc = ax25_bytes[ax25_bytes.size() - 2] | (static_cast<uint16_t>(ax25_bytes[ax25_bytes.size() - 1]) << 8);

    struct header header = encode_structured_header(f);

    if (header.pid == pid_abbreviation::unknown)
    {
        return false;
    }

    std::array<uint8_t, 13> header_bytes = encode_header(header);

    scramble_and_reed_solomon_encode_header(header_encoder, header_bytes, output);
    scramble_and_reed_solomon_encode_payload(payload_encoder, f.data, output);

    std::array<uint8_t, 4> encoded_crc = encode_crc_hamming(crc);
    output.insert(output.end(), encoded_crc.begin(), encoded_crc.end());

    return true;
}

bool try_encode_transparent_frame(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, correct_reed_solomon* rs_header, correct_reed_solomon* rs_payload, std::vector<uint8_t>& out)
{
    // Used when the frame has digipeater addresses (path is non-empty).
    // The entire AX.25 frame (minus trailing FCS) becomes the IL2P payload.
    // The header only carries hdr_type=0, FEC level, and the byte count.
    //
    // Output layout:
    //
    //   +-------------------+----------------------+-------------------------+
    //   | RS-encoded header | RS-encoded payload   | Hamming-encoded CRC     |
    //   +-------------------+----------------------+-------------------------+
    //   | 13 data + 2 RS    | Variable length,     | 2-byte AX.25 FCS        |
    //   | parity = 15 bytes | split into blocks    | → 4 bytes via (16,11)   |
    //   |                   | of 239 or 205 bytes  | Hamming coding          |
    //   | Scrambled, then   | (FEC-level dependent)|                         |
    //   | RS-encoded        | Each block scrambled | Single-bit correction   |
    //   |                   | + RS-encoded with    | per 11-bit chunk        |
    //   |                   | parity appended      |                         |
    //   +-------------------+----------------------+-------------------------+

    constexpr size_t max_payload = 1023;
    constexpr size_t min_ax25_frame_size = 17;

    // Encode the full AX.25 frame to extract the FCS and payload bytes.
    std::vector<uint8_t> ax25_bytes = LIBMODEM_AX25_NAMESPACE_REFERENCE encode_frame(f);

    if (ax25_bytes.size() < min_ax25_frame_size)
    {
        return false;
    }

    // Extract the AX.25 FCS (CRC) from the last two bytes of the encoded frame.
    uint16_t crc = ax25_bytes[ax25_bytes.size() - 2] | (static_cast<uint16_t>(ax25_bytes[ax25_bytes.size() - 1]) << 8);

    std::span<const uint8_t> payload(ax25_bytes.data(), ax25_bytes.size() - 2);
    if (payload.size() > max_payload)
    {
        return false;
    }

    std::array<uint8_t, 13> header_bytes = encode_transparent_header(static_cast<uint16_t>(payload.size()));

    scramble_and_reed_solomon_encode_header(rs_header, header_bytes, out);
    scramble_and_reed_solomon_encode_payload(rs_payload, payload, out);

    std::array<uint8_t, 4> encoded_crc = encode_crc_hamming(crc);
    out.insert(out.end(), encoded_crc.begin(), encoded_crc.end());

    return true;
}

std::vector<uint8_t> encode_frame(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f)
{
    // RS parameters for IL2P:
    //
    //  +------------+-------------------------+--------------------------------------------+
    //  | Parameter  | Value                   | Notes                                      |
    //  +------------+-------------------------+--------------------------------------------+
    //  | polynomial | 0x11D                   | x⁸+x⁴+x³+x²+1, same field as FX.25         |
    //  | fcr        | 0                       | FX.25 uses 1                               |
    //  | prim       | 1                       |                                            |
    //  +------------+-------------------------+--------------------------------------------+
    //  | Block      | N data + parity bytes   | Shortened from RS(255, 255-parity)         |
    //  +------------+-------------------------+--------------------------------------------+
    //  | Header     | 13 B data +  2 B parity | RS(255,253) shortened to RS(15,13)         |
    //  | Payload    | 1–239 B   + 16 B parity | RS(255,239), corrects up to 8 symbol errors|
    //  +------------+-------------------------+--------------------------------------------+

    constexpr uint16_t rs_polynomial = 0x11d;
    constexpr unsigned int rs_fcr = 0;
    constexpr unsigned int rs_prim = 1;
    constexpr size_t header_parity = 2;
    constexpr size_t payload_parity = 16;

    std::unique_ptr<correct_reed_solomon, decltype(&correct_reed_solomon_destroy)> rs_header(
        correct_reed_solomon_create(rs_polynomial, rs_fcr, rs_prim, header_parity),
        correct_reed_solomon_destroy
    );

    if (!rs_header)
    {
        return {};
    }

    std::unique_ptr<correct_reed_solomon, decltype(&correct_reed_solomon_destroy)> rs_payload(
        correct_reed_solomon_create(rs_polynomial, rs_fcr, rs_prim, payload_parity),
        correct_reed_solomon_destroy
    );

    if (!rs_payload)
    {
        return {};
    }

    std::vector<uint8_t> output;

    bool success = false;

    if (f.path.empty())
    {
        success = try_encode_structured_frame(f, rs_header.get(), rs_payload.get(), output);
    }

    // If structured encoding failed (SABME, mod128, unknown PID, bad callsign, etc.)
    // or if the frame has a digipeater path (non-empty), then we can't use structured mode
    // fall back to Type 0 transparent encapsulation.
    if (!success)
    {
        output.clear();
        success = try_encode_transparent_frame(f, rs_header.get(), rs_payload.get(), output);
    }

    if (!success)
    {
        return {};
    }

    return output;
}

std::vector<uint8_t> encode_bitstream(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, int preamble_flags, int postamble_flags)
{
    // Encodes an AX.25 frame as an IL2P bitstream (one byte per bit, MSB first):
    //
    //   +-----------+--------------------------------------------------+
    //   | preamble  | N × 0x55 (defaults to 1 if preamble_flags < 1)   |
    //   | sync word | 0xF1 0x5E 0x48                                   |
    //   | payload   | IL2P-encoded frame bytes                         |
    //   | postamble | N × 0x55 (defaults to 1 if postamble_flags < 1)  |
    //   +-----------+--------------------------------------------------+

    constexpr uint8_t sync_bytes[] = { 0xF1, 0x5E, 0x48 };

    std::vector<uint8_t> bits;

    for (int i = 0; i < preamble_flags; i++)
    {
        bits.insert(bits.end(), { 0, 1, 0, 1, 0, 1, 0, 1 });
    }

    bytes_to_bits_msb(std::begin(sync_bytes), std::end(sync_bytes), std::back_inserter(bits));

    std::vector<uint8_t> frame_bytes = (encode_frame)(f);

    bytes_to_bits_msb(frame_bytes.begin(), frame_bytes.end(), std::back_inserter(bits));

    for (int i = 0; i < postamble_flags; i++)
    {
        bits.insert(bits.end(), { 0, 1, 0, 1, 0, 1, 0, 1 });
    }

    return bits;
}

std::vector<uint8_t> encode_frame(const packet& p)
{
    return (encode_frame)(LIBMODEM_AX25_NAMESPACE_REFERENCE to_frame(p));
}

std::vector<uint8_t> encode_bitstream(const packet& p, int preamble_flags, int postamble_flags)
{
    return (encode_bitstream)(LIBMODEM_AX25_NAMESPACE_REFERENCE to_frame(p), preamble_flags, postamble_flags);
}

LIBMODEM_IL2P_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// il2p_bitstream_converter                                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> il2p_bitstream_converter::encode(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f) const
{
    return LIBMODEM_IL2P_NAMESPACE_REFERENCE encode_bitstream(f, 1, 1);
}

std::vector<uint8_t> il2p_bitstream_converter::encode(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, int preamble_flags, int postamble_flags) const
{
    return LIBMODEM_IL2P_NAMESPACE_REFERENCE encode_bitstream(f, preamble_flags, postamble_flags);
}

std::vector<uint8_t> il2p_bitstream_converter::encode(const packet& p) const
{
    return LIBMODEM_IL2P_NAMESPACE_REFERENCE encode_bitstream(LIBMODEM_AX25_NAMESPACE_REFERENCE to_frame(p), 1, 1);
}

std::vector<uint8_t> il2p_bitstream_converter::encode(const packet& p, int preamble_flags, int postamble_flags) const
{
    return LIBMODEM_IL2P_NAMESPACE_REFERENCE encode_bitstream(LIBMODEM_AX25_NAMESPACE_REFERENCE to_frame(p), preamble_flags, postamble_flags);
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// il2p_bitstream_converter_adapter                                 //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> il2p_bitstream_converter_adapter::encode(const LIBMODEM_AX25_NAMESPACE_REFERENCE frame& f, int preamble_flags, int postamble_flags) const
{
    return converter.encode(f, preamble_flags, postamble_flags);
}

std::vector<uint8_t> il2p_bitstream_converter_adapter::encode(const packet& p, int preamble_flags, int postamble_flags) const
{
    return converter.encode(p, preamble_flags, postamble_flags);
}

bool il2p_bitstream_converter_adapter::try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet& p, size_t& read)
{
    (void)bitstream;
    (void)offset;
    (void)p;
    (void)read;

    return false;
}

bool il2p_bitstream_converter_adapter::try_decode(uint8_t bit, packet& p)
{
    (void)bit;
    (void)p;

    return false;
}

void il2p_bitstream_converter_adapter::reset()
{
}

LIBMODEM_NAMESPACE_END