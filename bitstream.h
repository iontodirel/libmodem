// **************************************************************** //
// modem - APRS modem                                               // 
// Version 0.1.0                                                    //
// https://github.com/iontodirel/modem                              //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// bitstream.h
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
#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <array>
#include <span>
#include <tuple>

// Typedef for the packet type and packet type customization
// A packet type has to be supplied externally, and it allows sharing of the type across various projects
// 
// The packet type has the following interface:
//
// struct packet
// {
//     std::string from;
//     std::string to;
//     std::vector<std::string> path;
//     std::string data;
// };

#ifndef LIBMODEM_PACKET_NAMESPACE_REFERENCE
#define LIBMODEM_PACKET_NAMESPACE_REFERENCE
#endif

typedef LIBMODEM_PACKET_NAMESPACE_REFERENCE packet packet_type;

#ifndef LIBMODEM_AX25_NAMESPACE_BEGIN
#define LIBMODEM_AX25_NAMESPACE_BEGIN namespace ax25 {
#endif
#ifndef LIBMODEM_AX25_NAMESPACE_END
#define LIBMODEM_AX25_NAMESPACE_END }
#endif
#ifndef LIBMODEM_AX25_USING_NAMESPACE
#define LIBMODEM_AX25_USING_NAMESPACE using namespace ax25;
#endif
#ifndef LIBMODEM_AX25_NAMESPACE_REFERENCE
#define LIBMODEM_AX25_NAMESPACE_REFERENCE ax25 :: 
#endif
#ifndef LIBMODEM_FX25_NAMESPACE_BEGIN
#define LIBMODEM_FX25_NAMESPACE_BEGIN namespace fx25 {
#endif
#ifndef LIBMODEM_FX25_NAMESPACE_END
#define LIBMODEM_FX25_NAMESPACE_END }
#endif
#ifndef LIBMODEM_FX25_USING_NAMESPACE
#define LIBMODEM_FX25_USING_NAMESPACE using namespace fx25;
#endif

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
// address                                                          //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct address
{
    std::string text;
    int n = 0;
    int N = 0;
    int ssid = 0;
    bool mark = false;
};

bool try_parse_address(std::string_view address_string, struct address& address);
std::string to_string(const struct address& address);

// **************************************************************** //
//                                                                  //
//                                                                  //
// frame                                                            //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_AX25_NAMESPACE_BEGIN

struct frame
{
    address from;
    address to;
    std::vector<address> path;
    std::vector<uint8_t> data;
    std::array<uint8_t, 2> crc;
};

packet_type to_packet(const struct frame& frame);

LIBMODEM_AX25_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// bitstream_state                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_AX25_NAMESPACE_BEGIN

struct bitstream_state
{
    void reset();

    bool searching = true;    // Searching for preamble
    bool in_preamble = false; // Currently in preamble
    bool in_frame = false;    // Currently in frame
    bool complete = false;    // Frame complete
    uint8_t last_nrzi_level = 0;
    size_t frame_start_index = 0; // Index in bitstream where current frame starts

    // Accumulated bitstream with NRZI decoded bits and without preamble/postamble
    // Partial during decoding, and invalidated when frame decode is complete, do not use directly
    std::vector<uint8_t> bitstream; 
    
    // Fully decoded frame
    struct frame frame;
    
    bool enable_diagnostics = false; // Enable diagnostic frame capture

    size_t frame_start = 0;       // Global count of bits until the current frame start, 1-based index
    size_t frame_end = 0;         // Global count of bits until the current frame end, 1-based index
    uint8_t frame_nrzi_level = 0; // Initial NRZI level at the start of the frame
    size_t frame_size_bits = 0;   // Size of the last decoded frame in bits

    size_t global_bit_count = 0;       // Total bits processed
    size_t preamble_start_bit = 0;     // Global bit position where current preamble started
    uint8_t preamble_initial_nrzi = 0; // NRZI level before current preamble	
};

LIBMODEM_AX25_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// basic_bitstream_converter                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct basic_bitstream_converter
{
    std::vector<uint8_t> encode(const packet_type& p, int preamble_flags, int postamble_flags) const;
    bool try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet_type& p, size_t& read);
    bool try_decode(uint8_t bit, packet_type& p);
    void reset();

private:
    LIBMODEM_AX25_NAMESPACE_REFERENCE bitstream_state state;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// fx25_bitstream_converter                                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct fx25_bitstream_converter
{
    std::vector<uint8_t> encode(const packet_type& p, int preamble_flags, int postamble_flags) const;
    bool try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet_type& p, size_t& read);
    void reset();
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// bitstream_converter_base                                         //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct bitstream_converter_base
{
    virtual std::vector<uint8_t> encode(const packet_type& p, int preamble_flags, int postamble_flags) const = 0;
    virtual bool try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet_type& p, size_t& read) = 0;
    virtual bool try_decode(uint8_t bit, packet_type& p) = 0;
    virtual void reset() = 0;
    virtual ~bitstream_converter_base();
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// basic_bitstream_converter_adapter                                //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct basic_bitstream_converter_adapter : public bitstream_converter_base
{
    std::vector<uint8_t> encode(const packet_type& p, int preamble_flags = 45, int postamble_flags = 5) const override;
    bool try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet_type& p, size_t& read) override;
    bool try_decode(uint8_t bit, packet_type& p) override;
    void reset() override;

private:
    basic_bitstream_converter converter;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// fx25_bitstream_converter_adapter                                 //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct fx25_bitstream_converter_adapter : public bitstream_converter_base
{
    std::vector<uint8_t> encode(const packet_type& p, int preamble_flags = 45, int postamble_flags = 5) const override;
    bool try_decode(const std::vector<uint8_t>& bitstream, size_t offset, packet_type& p, size_t& read) override;
    bool try_decode(uint8_t bit, packet_type& p) override;
    void reset() override;

private:
    fx25_bitstream_converter converter;
};

// **************************************************************** //
//                                                                  //
//                                                                  //
// trim                                                             //
//                                                                  //
//                                                                  //
// **************************************************************** //

std::string_view trim(std::string_view str);

// **************************************************************** //
//                                                                  //
//                                                                  //
//                                                                  //
// bitstream routines                                               //
//                                                                  //
// bytes_to_bits, bits_to_bytes, compute_crc                        //
// bit_stuff, nrzi_encode, add_hdlc_flags                           //
// encode_basic_bitstream                                           //
//                                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_AX25_NAMESPACE_BEGIN

template<typename InputIt, typename OutputIt>
OutputIt bytes_to_bits(InputIt first, InputIt last, OutputIt out);

template<typename InputIt, typename OutputIt>
OutputIt bits_to_bytes(InputIt first, InputIt last, OutputIt out);

template<typename InputIt>
std::array<uint8_t, 2> compute_crc(InputIt first, InputIt last);

template<typename InputIt>
LIBMODEM_INLINE std::array<uint8_t, 2> compute_crc_using_lut(InputIt first, InputIt last);

template<typename InputIt, typename OutputIt>
OutputIt bit_stuff(InputIt first, InputIt last, OutputIt out);

template<typename InputIt, typename OutputIt>
OutputIt bit_unstuff(InputIt first, InputIt last, OutputIt out);

template<typename It>
void nrzi_encode(It first, It last);

template<typename It>
uint8_t nrzi_decode(It first, It last, uint8_t initial_value = 0);

uint8_t nrzi_decode(uint8_t bit, uint8_t last_nrzi_level);

template<typename OutputIt>
OutputIt add_hdlc_flags(OutputIt out, int count);

template<typename It>
It find_last_consecutive_hdlc_flag(It first, It last);

template<typename It>
It find_first_hdlc_flag(It first, It last);

bool ends_with_hdlc_flag(const std::vector<uint8_t>& bitstream);

template<typename InputIt, typename OutputIt>
LIBMODEM_INLINE OutputIt bytes_to_bits(InputIt first, InputIt last, OutputIt out)
{
    // Converts bytes to individual bits (LSB-first per byte)
    // 
    // Example: byte 0x7E (01111110) -> bits [0,1,1,1,1,1,1,0]

    for (auto it = first; it != last; ++it)
    {
        uint8_t byte = *it;
        for (int i = 0; i < 8; ++i)
        {
            *out++ = (byte >> i) & 1;  // Extract bits LSB-first
        }
    }

    return out;
}

template<typename InputIt, typename OutputIt>
LIBMODEM_INLINE OutputIt bits_to_bytes(InputIt first, InputIt last, OutputIt out)
{
    // Converts individual bits back to bytes (LSB-first per byte)
    // 
    // Example: bits [0,1,1,1,1,1,1,0] -> byte 0x7E (01111110)

    auto it = first;
    while (it != last)
    {
        uint8_t byte = 0;
        for (int i = 0; i < 8 && it != last; ++i)
        {
            if (*it++)
            {
                byte |= (1 << i);  // Set bit i if input bit is 1
            }
        }
        *out++ = byte;
    }

    return out;
}

template<typename InputIt>
LIBMODEM_INLINE std::array<uint8_t, 2> compute_crc(InputIt first, InputIt last)
{
    // Computes CRC-16-CCITT checksum for error detection in AX.25 frames
    // Uses reversed polynomial 0x8408 and processes bits LSB-first
    // 
    // Returns 2-byte CRC in little-endian format [low_byte, high_byte]

    const uint16_t poly = 0x8408; // CRC-16-CCITT reversed polynomial

    uint16_t crc = 0xFFFF;

    for (auto it = first; it != last; ++it)
    {
        uint8_t byte = *it;
        for (int i = 0; i < 8; ++i)
        {
            uint8_t bit = (byte >> i) & 1;  // LSB-first
            uint8_t xor_in = (crc ^ bit) & 0x01;
            crc >>= 1;
            if (xor_in)
            {
                crc ^= poly;
            }
        }
    }

    crc ^= 0xFFFF;
    return { static_cast<uint8_t>(crc & 0xFF), static_cast<uint8_t>((crc >> 8) & 0xFF) };
}

template<typename InputIt>
LIBMODEM_INLINE std::array<uint8_t, 2> compute_crc_using_lut(InputIt first, InputIt last)
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

    uint16_t crc = 0xFFFF;
    for (auto it = first; it != last; ++it)
    {
        uint8_t table_index = (crc ^ *it) & 0xFF;
        crc = (crc >> 8) ^ crc_table[table_index];
    }

    crc ^= 0xFFFF;
    return { static_cast<uint8_t>(crc & 0xFF), static_cast<uint8_t>((crc >> 8) & 0xFF) };
}

uint16_t compute_crc_using_lut_init();
uint16_t compute_crc_using_lut_update(uint8_t byte, uint16_t crc);
std::array<uint8_t, 2> compute_crc_using_lut_finalize(uint16_t crc);

template<typename InputIt, typename OutputIt>
LIBMODEM_INLINE OutputIt bit_stuff(InputIt first, InputIt last, OutputIt out)
{
    // Inserts a 0-bit after five consecutive 1-bits to prevent false flag detection
    // Prevents data from accidentally looking like the HDLC flag byte (0x7E = 01111110)
    // 
    // Example:
    // 
    //   Input:  1 1 1 1 1 1 0
    //           ~~~~~~~~~
    //   Output: 1 1 1 1 1 0 1 0  (0 stuffed after 5th and 6th 1)
    //                     ~

    int count = 0;

    for (auto it = first; it != last; ++it)
    {
        *out++ = *it;  // Output the bit

        if (*it == 1)
        {
            count += 1;
            if (count == 5)
            {
                *out++ = 0;  // Stuff a zero
                count = 0;
            }
        }
        else
        {
            count = 0;
        }
    }

    return out;
}

template<typename InputIt, typename OutputIt>
LIBMODEM_INLINE OutputIt bit_unstuff(InputIt first, InputIt last, OutputIt out)
{
    // Removes stuffed 0-bits that were inserted after five consecutive 1-bits
    // This is the inverse operation of bit_stuff
    // 
    // Example:
    // 
    //   Input:  1 1 1 1 1 0 1 0  (0 stuffed after 5th 1)
    //   Output: 1 1 1 1 1 1 0

    int count = 0;

    for (auto it = first; it != last; ++it)
    {
        if (*it == 1)
        {
            *out++ = *it;
            count += 1;
        }
        else  // *it == 0
        {
            if (count == 5)
            {
                // This is a stuffed bit, skip it
                count = 0;
            }
            else
            {
                // This is a real data bit
                *out++ = *it;
                count = 0;
            }
        }
    }

    return out;
}

template<typename InputIt>
LIBMODEM_INLINE void nrzi_encode(InputIt first, InputIt last)
{
    // Encodes bitstream in-place to ensure signal transitions for clock recovery
    // NRZI: 0-bit = toggle level, 1-bit = keep level
    // 
    // Example:
    // 
    //   Input:  1 0 1 1 0 0 1
    //   Output: 0 1 1 1 0 1 1

    uint8_t level = 0; // Start at level 0

    for (auto it = first; it != last; ++it)
    {
        if (*it == 0)
        {
            level ^= 1;
        }
        *it = level;
    }
}

template<typename It>
LIBMODEM_INLINE uint8_t nrzi_decode(It first, It last, uint8_t initial_value)
{
    if (first == last) return initial_value;

    uint8_t prev = *first;
    uint8_t curr = 0;

    *first = initial_value;  // First bit ambiguous, often set to initial_value

    for (auto it = first + 1; it != last; ++it)
    {
        curr = *it;
        *it = (curr == prev) ? 1 : 0;  // No transition=1, transition=0
        prev = curr;
    }

    return curr; // Return last level for chaining
}

template<typename OutputIt>
LIBMODEM_INLINE OutputIt add_hdlc_flags(OutputIt out, int count)
{
    constexpr uint8_t HDLC_FLAG = 0x7E;  // 01111110

    for (int j = 0; j < count; ++j)
    {
        for (int i = 0; i < 8; ++i)
        {
            *out++ = (HDLC_FLAG >> i) & 1;
        }
    }

    return out;
}

template<typename It>
LIBMODEM_INLINE It find_last_consecutive_hdlc_flag(It first, It last)
{
    // Finds the last flag in a sequence of consecutive HDLC flags
    // Returns iterator to the start of the last flag, or last if not found

    constexpr std::array<uint8_t, 8> flag_pattern = { 0, 1, 1, 1, 1, 1, 1, 0 };

    auto current_preamble_flag = std::search(first, last, flag_pattern.begin(), flag_pattern.end());

    if (current_preamble_flag == last)
    {
        return last;
    }

    auto last_preamble_flag = current_preamble_flag;

    while (true)
    {
        auto next_search_start = last_preamble_flag + 8;

        if (next_search_start >= last)
        {
            break;
        }

        auto next_flag = std::search(next_search_start, last, flag_pattern.begin(), flag_pattern.end());

        if (next_flag == next_search_start)
        {
            last_preamble_flag = next_flag;
        }
        else
        {
            // Found a gap or no more flags - frame data starts here
            break;
        }
    }

    return last_preamble_flag;
}

template<typename It>
LIBMODEM_INLINE It find_first_hdlc_flag(It first, It last)
{
    // Finds the first HDLC flag in the bitstream
    // Returns iterator to the start of the flag, or last if not found

    constexpr std::array<uint8_t, 8> flag_pattern = { 0, 1, 1, 1, 1, 1, 1, 0 };

    return std::search(first, last, flag_pattern.begin(), flag_pattern.end());
}

LIBMODEM_AX25_NAMESPACE_END

// **************************************************************** //
//                                                                  //
//                                                                  //
// AX.25                                                            //
//                                                                  //
// encode_header, encode_addresses, encode_address, encode_frame    //
// try_decode_frame, try_decode_packet, encode_basic_bitstream      //
// try_parse_address, parse_addresses, try_decode_basic_bitstream   //
//                                                                  //
//                                                                  //
//                                                                  //
// **************************************************************** //

LIBMODEM_AX25_NAMESPACE_BEGIN

template <typename InputIt, typename OutputIt>
std::pair<OutputIt, bool> try_parse_address(InputIt first, InputIt last, OutputIt out, int& ssid, bool& mark);

template <typename InputIt, typename OutputIt>
std::pair<OutputIt, bool> try_parse_address(InputIt first_it, InputIt last_it, OutputIt out, int& ssid, bool& mark, bool& last);

template <typename InputIt>
bool try_parse_address(InputIt first, InputIt last, std::string& address_text, int& ssid, bool& mark);

template <typename InputIt>
bool try_parse_address(InputIt first, InputIt last, struct address& address);

template <typename InputIt, typename OutputIt>
OutputIt parse_addresses(InputIt first, InputIt last, OutputIt out);

bool try_parse_address(std::string_view data, std::string& address, int& ssid, bool& mark);
bool try_parse_address(std::string_view data, struct address& address);

void parse_addresses(std::string_view data, std::vector<address>& addresses);

std::vector<uint8_t> encode_frame(const packet_type& p);
std::vector<uint8_t> encode_frame(const struct frame& frame);

template <typename InputIt>
std::vector<uint8_t> encode_frame(const address& from, const address& to, const std::vector<address>& path, InputIt input_it_first, InputIt input_it_last);

template <typename PathInputIt, typename DataInputIt, typename BidirIt>
BidirIt encode_frame(const address& from, const address& to, PathInputIt path_first_it, PathInputIt path_last_it, DataInputIt data_it_first, DataInputIt data_it_last, BidirIt out);

bool try_decode_frame(const std::vector<uint8_t>& frame_bytes, packet_type& p);
bool try_decode_frame(const std::vector<uint8_t>& frame_bytes, struct frame& frame);
bool try_decode_frame(const std::vector<uint8_t>& frame_bytes, address& from, address& to, std::vector<address>& path, std::vector<uint8_t>& data);
bool try_decode_frame(const std::vector<uint8_t>& frame_bytes, address& from, address& to, std::vector<address>& path, std::vector<uint8_t>& data, std::array<uint8_t, 2>& crc);

template<class InputIt>
bool try_decode_packet(InputIt frame_it_first, InputIt frame_it_last, packet_type& p);

template<class InputIt>
bool try_decode_frame(InputIt frame_it_first, InputIt frame_it_last, struct frame& frame);

template<typename InputIt>
bool try_decode_frame(InputIt frame_it_first, InputIt frame_it_last, address& from, address& to, std::vector<address>& path, std::vector<uint8_t>& data, std::array<uint8_t, 2>& crc);

std::vector<uint8_t> encode_header(const packet_type& p);
std::vector<uint8_t> encode_header(const address& from, const address& to, const std::vector<address>& path);

template<typename OutputIt>
OutputIt encode_header(const address& from, const address& to, const std::vector<address>& path, OutputIt out);

template<typename InputIt, typename OutputIt>
OutputIt encode_header(const address& from, const address& to, InputIt path_first_it, InputIt path_last_it, OutputIt out);

std::vector<uint8_t> encode_addresses(const std::vector<address>& path);

template<typename OutputIt>
OutputIt encode_addresses(const std::vector<address>& path, OutputIt out);

template<typename InputIt, typename OutputIt>
OutputIt encode_addresses(InputIt path_first_it, InputIt path_last_it, OutputIt out);

std::array<uint8_t, 7> encode_address(const struct address& address, bool last);
std::array<uint8_t, 7> encode_address(std::string_view address, int ssid, bool mark, bool last);

std::vector<uint8_t> encode_basic_bitstream(const packet_type& p, int preamble_flags, int postamble_flags);
std::vector<uint8_t> encode_basic_bitstream(const std::vector<uint8_t>& frame, int preamble_flags, int postamble_flags);

template<typename InputIt>
std::vector<uint8_t> encode_basic_bitstream(InputIt frame_it_first, InputIt frame_it_last, int preamble_flags, int postamble_flags);

bool try_decode_basic_bitstream(uint8_t bit, bitstream_state& state);
bool try_decode_basic_bitstream(uint8_t bit, packet_type& packet, bitstream_state& state);
bool try_decode_basic_bitstream(const std::vector<uint8_t>& bitstream, size_t offset, packet_type& packet, size_t& read, bitstream_state& state);

template <typename InputIt, typename OutputIt>
LIBMODEM_INLINE std::pair<OutputIt, bool> try_parse_address(InputIt first_it, InputIt last_it, OutputIt out, int& ssid, bool& mark)
{
    bool last = false;
    return try_parse_address(first_it, last_it, out, ssid, mark, last);
}

template <typename InputIt, typename OutputIt>
LIBMODEM_INLINE std::pair<OutputIt, bool> try_parse_address(InputIt first_it, InputIt last_it, OutputIt out, int& ssid, bool& mark, bool& last)
{
    // Parse an AX.25 address
    //
    // AX.25 addresses are always exactly 7 bytes:
    // 
    //  - Bytes 0-5: Callsign (6 characters, space-padded)
    //    - Each character is left-shifted by 1 bit
    //  - Byte 6
    //    - Bits 1-4: SSID
    //    - Bit 0: Last address marker
    //    - Bit 7: H-bit (used/marked)

    char address_text[7] = { '\0' }; // addresses are 6 characters long

    for (size_t i = 0; i < 6; i++)
    {
        if (first_it == last_it)
        {
            return { out, false }; // Fewer than 6 bytes
        }
        address_text[i] = static_cast<uint8_t>(*first_it++) >> 1; // data is organized in 7 bits
    }

    if (first_it == last_it)
    {
        return { out, false }; // Missing byte 7
    }

    // The ssid is shifted left by 1 bit, bits 1-4 contain the SSID
    ssid = (static_cast<uint8_t>(*first_it) >> 1) & 0b00001111; // 0xF mask for bits 1-4

    mark = (static_cast<uint8_t>(*first_it) & 0b10000000) != 0; // 0x80 mask for the H bit in the last byte

    last = (static_cast<uint8_t>(*first_it) & 0b00000001) != 0; // 0x01 mask for the last address marker

    std::string_view address_text_trimmed = trim(address_text);

    out = std::copy(address_text_trimmed.begin(), address_text_trimmed.end(), out);

    return { out, true };
}

template <typename InputIt>
LIBMODEM_INLINE bool try_parse_address(InputIt first, InputIt last, std::string& address_text, int& ssid, bool& mark)
{
    auto [_, result] = try_parse_address(first, last, std::back_inserter(address_text), ssid, mark);
    return result;
}

template <typename InputIt>
LIBMODEM_INLINE bool try_parse_address(InputIt first, InputIt last, struct address& address)
{
    std::string text;
    int ssid = 0;
    bool mark = false;

    if (!try_parse_address(first, last, text, ssid, mark))
    {
        return false;
    }

    std::string address_string = text;

    if (ssid > 0)
    {
        address_string += "-" + std::to_string(ssid);
    }

    if (mark)
    {
        address_string += "*";
    }

    return LIBMODEM_NAMESPACE_REFERENCE try_parse_address(address_string, address);
}

template <typename InputIt, typename OutputIt>
LIBMODEM_INLINE OutputIt parse_addresses(InputIt first, InputIt last, OutputIt out)
{
    while (std::distance(first, last) >= 7)
    {
        struct address address;
        auto next = std::next(first, 7);
        LIBMODEM_AX25_NAMESPACE_REFERENCE try_parse_address(first, next, address);
        *out++ = address;
        first = next;
    }

    return out;
}

template <typename InputIt>
LIBMODEM_INLINE std::vector<uint8_t> encode_frame(const address& from, const address& to, const std::vector<address>& path, InputIt data_it_first, InputIt data_it_last)
{
    // Encodes an AX.25 frame
    //
    //  - Build header (from, to, path)
    //  - Add control and PID fields, typically 0x03 0xF0
    //  - Append payload
    //  - Compute 16 bits CRC and append at the end

    std::vector<uint8_t> frame;

    // Header
    std::vector<uint8_t> header = encode_header(from, to, path);
    frame.insert(frame.end(), header.begin(), header.end());

    // Control and PID
    frame.push_back(static_cast<uint8_t>(0x03));  // Control: UI frame
    frame.push_back(static_cast<uint8_t>(0xF0));  // PID: No layer 3 protocol

    // Append payload
    frame.insert(frame.end(), data_it_first, data_it_last);

    // Compute 16 bits CRC
    // Append CRC at the end of the frame

    std::array<uint8_t, 2> crc = compute_crc(frame.begin(), frame.end());
    frame.insert(frame.end(), crc.begin(), crc.end());

    return frame;
}

template <typename PathInputIt, typename DataInputIt, typename BidirIt>
LIBMODEM_INLINE BidirIt encode_frame(const address& from, const address& to, PathInputIt path_first_it, PathInputIt path_last_it, DataInputIt data_it_first, DataInputIt data_it_last, BidirIt out)
{
    // Encodes an AX.25 frame
    //
    //  - Build header (from, to, path)
    //  - Add control and PID fields, typically 0x03 0xF0
    //  - Append payload
    //  - Compute 16 bits CRC and append at the end

    BidirIt frame_start = out;
    BidirIt frame_end;

    // Encoding header
    out = encode_header(from, to, path_first_it, path_last_it, out);

    // Control: UI frame, PID: No layer 3 protocol
    std::array<uint8_t, 2> control_pid = { static_cast<uint8_t>(0x03), static_cast<uint8_t>(0xF0) };
    out = std::copy(control_pid.begin(), control_pid.end(), out);

    // Append payload
    out = std::copy(data_it_first, data_it_last, out);

    frame_end = out;

    // Compute 16 bits CRC
    // Append CRC at the end of the frame
    std::array<uint8_t, 2> crc = compute_crc(frame_start, frame_end);
    out = std::copy(crc.begin(), crc.end(), out);

    return out;
}

template<class InputIt>
LIBMODEM_INLINE bool try_decode_packet(InputIt frame_it_first, InputIt frame_it_last, packet_type& p)
{
    // Decode an APRS packet from an NRZI bitstream
    // The frame inside the bitstream is set between frame_it_first and frame_it_last
    // There should be no HDLC flags in the frame bitstream
    // The bitstream is assumed to be NRZI decoded already

    std::vector<uint8_t> unstuffed_bits;

    bit_unstuff(frame_it_first, frame_it_last, std::back_inserter(unstuffed_bits));

    std::vector<uint8_t> frame_bytes;

    bits_to_bytes(unstuffed_bits.begin(), unstuffed_bits.end(), std::back_inserter(frame_bytes));

    return try_decode_frame(frame_bytes, p);
}

template<class InputIt>
LIBMODEM_INLINE bool try_decode_frame(InputIt frame_it_first, InputIt frame_it_last, struct frame& frame)
{
    // Decode an AX.25 frame from an NRZI bitstream
    // The frame inside the bitstream is set between frame_it_first and frame_it_last
    // There should be no HDLC flags in the frame bitstream
    // The bitstream is assumed to be NRZI decoded already

    std::vector<uint8_t> unstuffed_bits;

    bit_unstuff(frame_it_first, frame_it_last, std::back_inserter(unstuffed_bits));

    std::vector<uint8_t> frame_bytes;

    bits_to_bytes(unstuffed_bits.begin(), unstuffed_bits.end(), std::back_inserter(frame_bytes));

    return try_decode_frame(frame_bytes, frame);
}

template<typename InputIt>
LIBMODEM_INLINE bool try_decode_frame(InputIt frame_it_first, InputIt frame_it_last, address& from, address& to, std::vector<address>& path, std::vector<uint8_t>& data, std::array<uint8_t, 2>& crc)
{
    size_t frame_size = std::distance(frame_it_first, frame_it_last);

    if (frame_size < 18)
    {
        return false;
    }

    std::array<uint8_t, 2> computed_crc = compute_crc_using_lut(frame_it_first, frame_it_last - 2);
    std::array<uint8_t, 2> received_crc = { *(frame_it_last - 2), *(frame_it_last - 1) };

    crc = received_crc;

    // Check CRC validity
    if (computed_crc != received_crc)
    {
        return false;
    }

    LIBMODEM_AX25_NAMESPACE_REFERENCE try_parse_address({ reinterpret_cast<const char*>(&(*frame_it_first)), 7 }, to);
    LIBMODEM_AX25_NAMESPACE_REFERENCE try_parse_address({ reinterpret_cast<const char*>(&(*(frame_it_first + 7))), 7 }, from);

    // C-bit in source/destination has different meaning than H-bit in digipeaters; ignore it
    to.mark = false;
    from.mark = false;

    size_t addresses_start = 14;
    size_t addresses_end_position = addresses_start;

    bool found_last_address = false;

    // Check if there are no path addresses (i.e., the source address is the last address)
    if (*(frame_it_first + 13) & 0x01)
    {
        found_last_address = true;
    }

    // Parse path addresses until we find the last address
    // Each address is 7 bytes long
    // The last address is indicated by the extension bit (bit 0 of byte 6) being set
    // or by the Control Field indicating a U-frame or S-frame
    for (size_t i = addresses_start; !found_last_address && i + 7 <= frame_size - 2; i += 7)
    {
        // First check if this looks like a Control Field (U-frame or S-frame)
        if ((*(frame_it_first + i) & 0x03) == 0x03 || (*(frame_it_first + i) & 0x03) == 0x01)
        {
            addresses_end_position = i;
            found_last_address = true;
        }
        // Otherwise check if the extension bit (bit 0 of byte 6) is set
        else if (*(frame_it_first + i + 6) & 0x01)
        {
            addresses_end_position = i + 7;
            found_last_address = true;
        }
    }

    if (!found_last_address)
    {
        return false;
    }

    size_t addresses_length = addresses_end_position - addresses_start;

    // Ensure that the addresses length is a multiple of 7
    if (addresses_length % 7 != 0)
    {
        return false;
    }

    if (addresses_length > 0)
    {
        parse_addresses({ reinterpret_cast<const char*>(&(*(frame_it_first + addresses_start))), addresses_length }, path);
    }
    else
    {
        path.clear();
    }

    size_t info_field_start = addresses_end_position + 2; // skip the Control Field byte and the Protocol ID byte

    // Check bounds before calculating length so that we do not underflow
    if (info_field_start > frame_size - 2)
    {
        return false;
    }

    size_t info_field_length = (frame_size - 2) - info_field_start; // subtract CRC bytes

    // Ensure that the info field does not exceed frame bounds
    if ((info_field_start + info_field_length) > (frame_size - 2))
    {
        return false;
    }

    if (info_field_length > 0)
    {
        data = std::vector<uint8_t>(frame_it_first + info_field_start, frame_it_first + info_field_start + info_field_length);
    }
    else
    {
        data.clear();
    }

    return true;
}

template<typename InputIt, typename PathOutputIt, typename DataOutputIt>
LIBMODEM_INLINE std::tuple<PathOutputIt, DataOutputIt, bool> try_decode_frame(InputIt frame_it_first, InputIt frame_it_last, address& from, address& to, PathOutputIt path, DataOutputIt data, std::array<uint8_t, 2>& crc)
{
    size_t frame_size = std::distance(frame_it_first, frame_it_last);

    if (frame_size < 18)
    {
        return { path, data, false };
    }

    std::array<uint8_t, 2> computed_crc = compute_crc_using_lut(frame_it_first, frame_it_last - 2);
    std::array<uint8_t, 2> received_crc = { *(frame_it_last - 2), *(frame_it_last - 1) };

    crc = received_crc;

    // Check CRC validity
    if (computed_crc != received_crc)
    {
        return { path, data, false };
    }

    LIBMODEM_AX25_NAMESPACE_REFERENCE try_parse_address({ reinterpret_cast<const char*>(&(*frame_it_first)), 7 }, to);
    LIBMODEM_AX25_NAMESPACE_REFERENCE try_parse_address({ reinterpret_cast<const char*>(&(*(frame_it_first + 7))), 7 }, from);

    // C-bit in source/destination has different meaning than H-bit in digipeaters; ignore it
    to.mark = false;
    from.mark = false;

    size_t addresses_start = 14;
    size_t addresses_end_position = addresses_start;

    bool found_last_address = false;

    // Check if there are no path addresses (i.e., the source address is the last address)
    if (*(frame_it_first + 13) & 0x01)
    {
        found_last_address = true;
    }

    // Parse path addresses until we find the last address
    // Each address is 7 bytes long
    // The last address is indicated by the extension bit (bit 0 of byte 6) being set
    // or by the Control Field indicating a U-frame or S-frame
    for (size_t i = addresses_start; !found_last_address && i + 7 <= frame_size - 2; i += 7)
    {
        // First check if this looks like a Control Field (U-frame or S-frame)
        if ((*(frame_it_first + i) & 0x03) == 0x03 || (*(frame_it_first + i) & 0x03) == 0x01)
        {
            addresses_end_position = i;
            found_last_address = true;
        }
        // Otherwise check if the extension bit (bit 0 of byte 6) is set
        else if (*(frame_it_first + i + 6) & 0x01)
        {
            addresses_end_position = i + 7;
            found_last_address = true;
        }
    }

    if (!found_last_address)
    {
        return { path, data, false };
    }

    size_t addresses_length = addresses_end_position - addresses_start;

    // Ensure that the addresses length is a multiple of 7
    if (addresses_length % 7 != 0)
    {
        return { path, data, false };
    }

    if (addresses_length > 0)
    {
        path = parse_addresses(frame_it_first + addresses_start, frame_it_first + addresses_end_position, path);
    }

    size_t info_field_start = addresses_end_position + 2; // skip the Control Field byte and the Protocol ID byte

    // Check bounds before calculating length so that we do not underflow
    if (info_field_start > frame_size - 2)
    {
        return { path, data, false };
    }

    size_t info_field_length = (frame_size - 2) - info_field_start; // subtract CRC bytes

    // Ensure that the info field does not exceed frame bounds
    if ((info_field_start + info_field_length) > (frame_size - 2))
    {
        return { path, data, false };
    }

    if (info_field_length > 0)
    {
        data = std::copy(frame_it_first + info_field_start, frame_it_first + info_field_start + info_field_length, data);
    }

    return { path, data, true };
}

template<typename OutputIt>
LIBMODEM_INLINE OutputIt encode_header(const address& from, const address& to, const std::vector<address>& path, OutputIt out)
{
    return encode_header(from, to, path.begin(), path.end(), out);
}

template<typename InputIt, typename OutputIt>
LIBMODEM_INLINE OutputIt encode_header(const address& from, const address& to, InputIt path_first_it, InputIt path_last_it, OutputIt out)
{
    auto to_bytes = encode_address(to, false);

    out = std::copy(to_bytes.begin(), to_bytes.end(), out);

    size_t path_size = std::distance(path_first_it, path_last_it);

    // If there is no path, the from address is the last address
    // and should be marked as such
    auto from_bytes = encode_address(from, (path_size == 0));

    out = std::copy(from_bytes.begin(), from_bytes.end(), out);

    return encode_addresses(path_first_it, path_last_it, out);
}

template<typename OutputIt>
LIBMODEM_INLINE OutputIt encode_addresses(const std::vector<address>& path, OutputIt out)
{
    for (size_t i = 0; i < path.size(); i++)
    {
        bool last = (i == path.size() - 1);
        std::array<uint8_t, 7> address_bytes = encode_address(path[i], last);
        out = std::copy(address_bytes.begin(), address_bytes.end(), out);
    }

    return out;
}

template<typename InputIt, typename OutputIt>
LIBMODEM_INLINE OutputIt encode_addresses(InputIt path_first_it, InputIt path_last_it, OutputIt out)
{
    if (path_first_it == path_last_it)
    {
        return out;
    }

    for (auto it = path_first_it; it != path_last_it; ++it)
    {
        auto next = std::next(it);
        bool last = (next == path_last_it);
        std::array<uint8_t, 7> address_bytes = encode_address(*it, last);
        out = std::copy(address_bytes.begin(), address_bytes.end(), out);
    }

    return out;
}

template<typename InputIt>
LIBMODEM_INLINE std::vector<uint8_t> encode_basic_bitstream(InputIt frame_it_first, InputIt frame_it_last, int preamble_flags, int postamble_flags)
{
    // Encode an AX.25 frame into a complete bitstream ready for modulation
    //
    // Steps:
    // 
    //  - Convert frame bytes to bits LSB-first
    //  - Bit-stuff the bits
    //  - Add HDLC flags (0x7E) at start
    //  - Add the stuffed bits
    //  - Add HDLC flags (0x7E) at end
    //  - NRZI encode the entire bitstream

    std::vector<uint8_t> frame_bits;

    frame_bits.reserve(std::distance(frame_it_first, frame_it_last) * 8);

    bytes_to_bits(frame_it_first, frame_it_last, std::back_inserter(frame_bits));

    // Bit stuffing

    std::vector<uint8_t> stuffed_bits;

    bit_stuff(frame_bits.begin(), frame_bits.end(), std::back_inserter(stuffed_bits));

    // Build complete bitstream: preamble + data + postamble

    std::vector<uint8_t> bitstream;

    add_hdlc_flags(std::back_inserter(bitstream), preamble_flags);
    bitstream.insert(bitstream.end(), stuffed_bits.begin(), stuffed_bits.end());
    add_hdlc_flags(std::back_inserter(bitstream), postamble_flags);

    // NRZI encoding of the bitstream

    nrzi_encode(bitstream.begin(), bitstream.end());

    return bitstream;
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

std::vector<uint8_t> encode_fx25_frame(const std::vector<uint8_t>& frame_bytes, size_t min_check_bytes = 0);

std::vector<uint8_t> encode_fx25_frame(std::span<const uint8_t> frame_bytes, size_t min_check_bytes);

template<typename InputIt>
std::vector<uint8_t> encode_fx25_frame(InputIt frame_it_first, InputIt frame_it_last, size_t min_check_bytes);

template<typename InputIt>
std::vector<uint8_t> encode_fx25_bitstream(InputIt frame_it_first, InputIt frame_it_last, int preamble_flags, int postamble_flags, size_t min_check_bytes = 0);

std::vector<uint8_t> encode_fx25_bitstream(const packet_type& p, int preamble_flags, int postamble_flags, size_t min_check_bytes = 0);

template<typename InputIt>
LIBMODEM_INLINE std::vector<uint8_t> encode_fx25_frame(InputIt frame_it_first, InputIt frame_it_last, size_t min_check_bytes)
{
    return encode_fx25_frame(std::span<const uint8_t>{ frame_it_first, frame_it_last }, min_check_bytes);
}

template<typename InputIt>
LIBMODEM_INLINE std::vector<uint8_t> encode_fx25_bitstream(InputIt frame_it_first, InputIt frame_it_last, int preamble_flags, int postamble_flags, size_t min_check_bytes)
{
LIBMODEM_AX25_USING_NAMESPACE

    // Encode FX.25 frame
    // 
    //  - Convert AX.25 frame to bits LSB-first
    //  - Bit-stuff the bits
    //  - Add HDLC flags (0x7E) at start
    //  - Add the stuffed bits
    //  - Add HDLC flags (0x7E) at end
    //  - Create FX.25 frame from stuffed bits containing the HDLC flags
    //  - Convert FX.25 frame to bits LSB-first
    //  - Add HDLC flags (0x7E) at start
    //  - Add the FX.25 frame bits
    //  - Add HDLC flags (0x7E) at end
    //  - NRZI encode the entire bitstream
        
    std::vector<uint8_t> frame_bits;

    bytes_to_bits(frame_it_first, frame_it_last, std::back_inserter(frame_bits));

    std::vector<uint8_t> stuffed_bits;

    bit_stuff(frame_bits.begin(), frame_bits.end(), std::back_inserter(stuffed_bits));

    // Build complete AX.25 frame bits: preamble + stuffed bits + postamble

    std::vector<uint8_t> ax25_bits;

    add_hdlc_flags(std::back_inserter(ax25_bits), 1);
    ax25_bits.insert(ax25_bits.end(), stuffed_bits.begin(), stuffed_bits.end());
    add_hdlc_flags(std::back_inserter(ax25_bits), 1);

    // Create FX.25 frame

    std::vector<uint8_t> ax25_packet_bytes;

    bits_to_bytes(ax25_bits.begin(), ax25_bits.end(), std::back_inserter(ax25_packet_bytes));

    std::vector<uint8_t> fx25_frame = encode_fx25_frame(ax25_packet_bytes, min_check_bytes);

    if (fx25_frame.empty()) 
    {
        return {};
    }

    // Build complete bitstream: preamble + data + postamble

    std::vector<uint8_t> bitstream;

    add_hdlc_flags(std::back_inserter(bitstream), preamble_flags);
    bytes_to_bits(fx25_frame.begin(), fx25_frame.end(), std::back_inserter(bitstream));
    add_hdlc_flags(std::back_inserter(bitstream), postamble_flags);

    // NRZI encoding of the bitstream

    nrzi_encode(bitstream.begin(), bitstream.end());

    return bitstream;
}

std::vector<uint8_t> encode_fx25_bitstream(const packet_type& p, int preamble_flags, int postamble_flags, size_t min_check_bytes);

LIBMODEM_FX25_NAMESPACE_END

LIBMODEM_NAMESPACE_END