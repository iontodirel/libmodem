// **************************************************************** //
// modem - APRS modem                                               // 
// Version 0.1.0                                                    //
// https://github.com/iontodirel/modem                              //
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
    std::string_view address_text = address_string;

    address.text = address_text;
    address.mark = false;
    address.ssid = 0;
    address.n = 0;
    address.N = 0;

    // Check to see if the address is used (ending with *)
    if (!address_text.empty() && address_text.back() == '*')
    {
        address.mark = true;
        address_text.remove_suffix(1); // remove the *
        address.text = address_text; // set the text to the address without the *
    }

    auto sep_position = address_text.find('-');

    // No separator found
    if (sep_position == std::string::npos)
    {
        if (!address_text.empty() && isdigit(static_cast<unsigned char>(address_text.back())))
        {
            address.n = address_text.back() - '0'; // get the last character as a number
            address_text.remove_suffix(1); // remove the digit from the address text

            // Validate the n is in the range 1-7
            if (address.n > 0 && address.n <= 7)
            {
                address.text = address_text;
            }
            else
            {
                address.n = 0;
            }
        }

        return true;
    }

    // Separator found, check if we have exactly one digit on both sides of the separator, ex WIDE1-1
    // If the address does not match the n-N format, we will treat it as a regular address ex address with SSID
    if (sep_position != std::string::npos && sep_position > 0 &&
        std::isdigit(static_cast<unsigned char>(address_text[sep_position - 1])) &&
        (sep_position + 1) < address_text.size() && std::isdigit(static_cast<unsigned char>(address_text[sep_position + 1])) &&
        (sep_position + 2 == address_text.size()))
    {
        address.n = address_text[sep_position - 1] - '0';
        address.N = address_text[sep_position + 1] - '0';

        if (address.N >= 0 && address.N <= 7 && address.n > 0 && address.n <= 7)
        {
            address.text = address_text.substr(0, sep_position - 1); // remove the separator and both digits from the address text
        }
        else
        {
            address.n = 0;
            address.N = 0;
        }

        return true;
    }

    // Handle SSID parsing
    // Expecting the separator to be followed by a digit, ex: CALL-1
    if ((sep_position + 1) < address_text.size() && std::isdigit(static_cast<unsigned char>(address_text[sep_position + 1])))
    {
        std::string ssid_str = std::string(address_text.substr(sep_position + 1));

        // Check for a single digit or two digits, ex: CALL-1 or CALL-12
        if (ssid_str.size() == 1 || (ssid_str.size() == 2 && std::isdigit(static_cast<unsigned char>(ssid_str[1]))))
        {
            int ssid;
            try
            {
                ssid = std::stoi(ssid_str);
            }
            catch (const std::invalid_argument&)
            {
                return true;
            }
            catch (const std::out_of_range&)
            {
                return true;
            }

            if (ssid >= 0 && ssid <= 15)
            {
                address.ssid = ssid;
                address.text = address_text.substr(0, sep_position);
            }
        }
    }

    return true;
}

std::string to_string(const struct address& address)
{
    if (address.text.empty())
    {
        return "";
    }

    std::string result = address.text;

    if (address.n > 0)
    {
        result += char('0' + address.n);
    }

    if (address.N > 0)
    {
        result += '-';
        result += char('0' + address.N);
    }

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

    if (address.mark)
    {
        result += '*';
    }

    return result;
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// frame                                                            //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_AX25_NAMESPACE_BEGIN

packet_type to_packet(const struct frame& frame)
{
    packet_type p;

    p.from = to_string(frame.from);
    p.to = to_string(frame.to);

    p.path.clear();
    for (const auto& path_address : frame.path)
    {
        p.path.push_back(to_string(path_address));
    }

    p.data = std::string(frame.data.begin(), frame.data.end());

    return p;
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
    frame_start = 0;
    frame_end = 0;
}

LIBMODEM_AX25_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// basic_bitstream_converter                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> basic_bitstream_converter::encode(const packet_type& p, int preamble_flags, int postamble_flags) const
{
LIBMODEM_AX25_USING_NAMESPACE

    return encode_basic_bitstream(p, preamble_flags, postamble_flags);
}

bool basic_bitstream_converter::try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet_type& p, size_t& read)
{
LIBMODEM_AX25_USING_NAMESPACE

    return try_decode_basic_bitstream(bitstream, offset, p, read, state);
}

bool basic_bitstream_converter::try_decode(uint8_t bit, packet_type& p)
{
LIBMODEM_AX25_USING_NAMESPACE

    return try_decode_basic_bitstream(bit, p, state);
}

void basic_bitstream_converter::reset()
{
    state.reset();
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// fx25_bitstream_converter                                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> fx25_bitstream_converter::encode(const packet_type& p, int preamble_flags, int postamble_flags) const
{
LIBMODEM_FX25_USING_NAMESPACE

    return encode_fx25_bitstream(p, preamble_flags, postamble_flags);
}

bool fx25_bitstream_converter::try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet_type& p, size_t& read)
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
// basic_bitstream_converter_adapter                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::vector<uint8_t> basic_bitstream_converter_adapter::encode(const packet_type& p, int preamble_flags, int postamble_flags) const
{
    return converter.encode(p, preamble_flags, postamble_flags);
}

bool basic_bitstream_converter_adapter::try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet_type& p, size_t& read)
{
    return converter.try_decode(bitstream, offset, p, read);
}

bool basic_bitstream_converter_adapter::try_decode(uint8_t bit, packet_type& p)
{
    return converter.try_decode(bit, p);
}

void basic_bitstream_converter_adapter::reset()
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

std::vector<uint8_t> fx25_bitstream_converter_adapter::encode(const packet_type& p, int preamble_flags, int postamble_flags) const
{
    return converter.encode(p, preamble_flags, postamble_flags);
}

bool fx25_bitstream_converter_adapter::try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet_type& p, size_t& read)
{
    return converter.try_decode(bitstream, offset, p, read);
}

bool fx25_bitstream_converter_adapter::try_decode(uint8_t bit, packet_type& p)
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

bool ends_with_hdlc_flag(const std::vector<uint8_t>& bitstream)
{
    if (bitstream.size() < 8)
    {
        return false;
    }

    size_t start = bitstream.size() - 8;

    // HDLC flag pattern: 01111110 (LSB first representation of 0x7E)
    return bitstream[start + 0] == 0 &&
        bitstream[start + 1] == 1 &&
        bitstream[start + 2] == 1 &&
        bitstream[start + 3] == 1 &&
        bitstream[start + 4] == 1 &&
        bitstream[start + 5] == 1 &&
        bitstream[start + 6] == 1 &&
        bitstream[start + 7] == 0;
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

bool try_parse_address(std::string_view data, std::string& address_text, int& ssid, bool& mark)
{
    return try_parse_address(data.begin(), data.end(), address_text, ssid, mark);
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
        addresses.push_back(address);
    }
}

std::vector<uint8_t> encode_frame(const packet_type& p)
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
    return encode_frame(frame.from, frame.to, frame.path, frame.data.begin(), frame.data.end());
}

std::vector<uint8_t> encode_frame(const address& from, const address& to, const std::vector<address>& path, std::string_view data)
{
    return encode_frame(from, to, path, data.begin(), data.end());
}

bool try_decode_frame(const std::vector<uint8_t>& frame_bytes, packet_type& p)
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
    return try_decode_frame(frame_bytes, frame.from, frame.to, frame.path, frame.data, frame.crc);
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
    std::string address_text = address.text;
    int ssid = 0;

    if (address.n > 0)
    {
        address_text += std::to_string(address.n);
    }

    if (address.N > 0)
    {
        ssid = address.N;
    }

    if (address.ssid > 0)
    {
        ssid = address.ssid;
    }

    return encode_address(address_text, ssid, address.mark, last);
}

std::array<uint8_t, 7> encode_address(std::string_view address, int ssid, bool mark, bool last)
{
    assert(ssid >= 0 && ssid <= 15);

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

    data[6] = 0b01100000; // 0 1 1 0 0 0 0 0, 0x60

    data[6] |= (ssid << 1);

    if (last)
    {
        data[6] |= 0b00000001; // Extension bit (bit 0), 0x01
    }

    if (mark)
    {
        data[6] |= 0b10000000; // H-bit (bit 7), 0x80
    }

    return data;
}

std::vector<uint8_t> encode_basic_bitstream(const packet_type& p, int preamble_flags, int postamble_flags)
{
    return encode_basic_bitstream(encode_frame(p), preamble_flags, postamble_flags);
}

std::vector<uint8_t> encode_basic_bitstream(const std::vector<uint8_t>& frame, int preamble_flags, int postamble_flags)
{
    return encode_basic_bitstream(frame.begin(), frame.end(), preamble_flags, postamble_flags);
}

bool try_decode_basic_bitstream(uint8_t bit, bitstream_state& state)
{
    // Process one bit at a time through the AX.25 bitstream decoding pipeline:
    //
    // 1. NRZI decode the incoming raw bit
    // 2. Add to internal buffer
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

            if (state.enable_diagnostics)
            {
                // Track where preamble started (first bit of this flag)
                state.preamble_start_bit = state.global_bit_count - 7;

                // Compute NRZI level before the preamble by working backwards through the 8 flag bits
                uint8_t level = state.last_nrzi_level;
                for (size_t i = 0; i < 8 && i < state.bitstream.size(); i++)
                {
                    if (state.bitstream[state.bitstream.size() - 1 - i] == 0)
                    {
                        level = level ? 0 : 1;
                    }
                }
                state.preamble_initial_nrzi = level;
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
            // Found postamble! Extract frame bits (excluding the 8-bit postamble flag)
            size_t frame_end = state.bitstream.size() - 8;

            if (frame_end > state.frame_start_index)
            {
                // Extract frame bits
                std::vector<uint8_t> frame_bits(state.bitstream.begin() + state.frame_start_index, state.bitstream.begin() + frame_end);

                // Try to decode the packet
                bool result = try_decode_frame(frame_bits.begin(), frame_bits.end(), state.frame);

                // Set the global bit positions for the successfully found frame
                state.frame_start = state.preamble_start_bit;
                state.frame_end = state.global_bit_count;
                state.frame_nrzi_level = state.preamble_initial_nrzi;

                // Prepare for next packet - the postamble can be the preamble of the next
                // Keep only the last 8 bits (the flag) for potential reuse as preamble
                state.bitstream.erase(state.bitstream.begin(), state.bitstream.begin() + frame_end);
                state.frame_start_index = state.bitstream.size(); // = 8 (position after the flag)
                state.in_preamble = true; // Ready for next frame
                state.in_frame = false;
                // If we found a valid frame, set complete flag regardless whether the packet was decoded successfully
                state.complete = true;

                state.frame_size_bits = frame_bits.size();

                if (state.enable_diagnostics)
                {
                    // Set up tracking for potential next frame using the shared flag
                    state.preamble_start_bit = state.global_bit_count - 7;

                    // Compute NRZI level before this shared flag
                    uint8_t level = state.last_nrzi_level;
                    for (size_t i = 0; i < 8; i++)
                    {
                        if (state.bitstream[state.bitstream.size() - 1 - i] == 0)
                        {
                            level = level ? 0 : 1;
                        }
                    }

                    state.preamble_initial_nrzi = level;
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
            state.preamble_start_bit = 0;
            state.preamble_initial_nrzi = 0;
        }
    }

    return false; // No complete packet yet
}

bool try_decode_basic_bitstream(uint8_t bit, packet_type& packet, bitstream_state& state)
{
    bool result = try_decode_basic_bitstream(bit, state);
    if (result)
    {
        packet = to_packet(state.frame);
    }
    return result;
}

bool try_decode_basic_bitstream(const std::vector<uint8_t>& bitstream, size_t offset, packet_type& packet, size_t& read, bitstream_state& state)
{
    for (size_t i = offset; i < bitstream.size(); i++)
    {
        if (try_decode_basic_bitstream(bitstream[i], packet, state))
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
// encode_fx25_frame, encode_fx25_bitstream                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_FX25_NAMESPACE_BEGIN

std::vector<uint8_t> encode_fx25_frame(const std::vector<uint8_t>& frame_bytes, size_t min_check_bytes)
{
    return encode_fx25_frame(std::span<const uint8_t>(frame_bytes.data(), frame_bytes.size()), min_check_bytes);
}

std::vector<uint8_t> encode_fx25_frame(std::span<const uint8_t> frame_bytes, size_t min_check_bytes)
{
    // FX.25 frame encoding function
    //
    // References:
    //
    //   - FX.25 Development: http://ftp.tapr.org/meetings/DCC_2020/JE1WAZ/DCC-2020-PRUG-FINAL.pdf
    //   - Reed-Solomon Codes: https://en.wikipedia.org/wiki/Reed%E2%80%93Solomon_error_correction
    //   - AX.25 + FEC = FX.25: https://cloud.dvbr.net/direwolf/direwolf_git_v1.6/doc/AX25_plus_FEC_equals_FX25.pdf

    // FX.25 RS code modes from the specification
    // Each mode defines: correlation_tag, transmitted_size, data_size, check_bytes
    // Sorted by increasing data size
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

    // Select smallest RS code that fits
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

    // FX.25 Frame Structure (transmitted left to right):
    // 
    // +-----------------+------------------------+--------------------+
    // | Correlation Tag |    AX.25 packet        |   RS Check Bytes   |
    // |    (8 bytes)    | (unmodified) + padding |   (16/32/64 bytes) |
    // +-----------------+------------------------+--------------------+
    //
    // The correlation tag tells receivers:
    // 
    //   1. This is an FX.25 frame (not plain AX.25)
    //   2. How many data and check bytes follow
    //
    // Non-FX.25 receivers see the correlation tag as random noise and ignore it.
    // They then see the AX.25 flags and sync up normally to decode the AX.25 packet.
    // The RS check bytes at the end are also ignored as noise.

    if (tag == 0)
    {
        // Packet too large for any FX.25 format
        return {};
    }

    std::vector<uint8_t> output;

    // Add correlation tag (8 bytes, transmitted LSB first)
    // This identifies the frame as FX.25 and specifies the format

    for (int i = 0; i < 8; i++)
    {
        output.push_back((tag >> (i * 8)) & 0xFF);
    }

    // Prepare the data block for RS encoding
    // The AX.25 packet bytes are placed here UNMODIFIED
    // This preserves backward compatibility - the AX.25 portion is unchanged

    std::vector<uint8_t> rs_data_block(data_size, 0x00);

    // Copy the complete AX.25 packet(with flags, bit - stuffing, everything)
    // This is placed at the beginning of the data block exactly as-is
    // frame_bytes contains: [0x7E] [AX.25 frame with bit stuffing] [0x7E]

    std::copy(frame_bytes.begin(), frame_bytes.end(), rs_data_block.begin());

    // Pad the rest with 0x7E (HDLC flag pattern)
    // This padding allows the RS encoder to work with fixed block sizes
    // 0x7E is chosen because AX.25 receivers will see it as idle flags

    for (size_t i = frame_bytes.size(); i < data_size; i++)
    {
        rs_data_block[i] = 0x7E;
    }

    // At this point, rs_data_block contains:
    // [Complete unmodified AX.25 packet][0x7E padding to fill data_size]
    //
    // Reed - Solomon encoding
    // The RS encoder treats the data block as symbols and calculates parity
    // RS encoding does NOT modify the data portion!
    // It only ADDS check bytes for error correction
    //
    // For shortened RS codes (e.g., RS(48,32)), we must encode using the parent
    // RS(255, 255-check_size) code. The data is placed at the beginning with
    // implicit trailing zeros to fill to full length.
    //
    // Create RS encoder with:
    // 
    //   - polynomial 0x11d (x^8 + x^4 + x^3 + x^2 + 1)
    //   - fcr = 1 (first consecutive root)
    //   - prim = 1 (primitive element)

    int full_data_size = 255 - check_size;

    std::vector<uint8_t> full_block(full_data_size, 0x00);

    std::copy(rs_data_block.begin(), rs_data_block.end(), full_block.begin());

    correct_reed_solomon* rs = correct_reed_solomon_create(correct_rs_primitive_polynomial_8_4_3_2_0, 1, 1, check_size);

    if (rs == nullptr)
    {
        // Failed to create RS encoder
        return {};
    }

    std::vector<uint8_t> full_encoded(255);

    // Encode: creates data + check bytes
    // The first 'full_data_size' bytes are our data (unchanged)
    // The last 'check_size' bytes are the calculated RS parity

    ssize_t result = correct_reed_solomon_encode(rs, full_block.data(), full_data_size, full_encoded.data());

    correct_reed_solomon_destroy(rs);

    if (result != 255)
    {
        return {};
    }

    // Append the encoded block to output (only the transmitted portion)
    // 
    // This contains:
    // 
    //   - First 'data_size' bytes: The EXACT SAME AX.25 packet + padding   
    //   - Last 'check_size' bytes: RS parity for error correction

    for (int i = 0; i < data_size; i++)
    {
        output.push_back(full_encoded[i]);
    }

    for (int i = 0; i < check_size; i++)
    {
        output.push_back(full_encoded[full_data_size + i]);
    }

    // Final transmitted frame structure:
    // [8-byte correlation tag][Unmodified AX.25][0x7E padding][RS check bytes]
    //
    // The AX.25 packet remains completely unaltered, allowing:
    // 1. FX.25 receivers to apply error correction then extract AX.25
    // 2. Regular AX.25 receivers to ignore FX.25 overhead and decode normally

    return output;
}

std::vector<uint8_t> encode_fx25_bitstream(const packet_type& p, int preamble_flags, int postamble_flags, size_t min_check_bytes)
{
LIBMODEM_AX25_USING_NAMESPACE

    std::vector<uint8_t> ax25_frame = encode_frame(p);
    return encode_fx25_bitstream(ax25_frame.begin(), ax25_frame.end(), preamble_flags, postamble_flags, min_check_bytes);
}

LIBMODEM_FX25_NAMESPACE_END

LIBMODEM_NAMESPACE_END