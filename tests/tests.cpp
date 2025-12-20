// **************************************************************** //
// modem - APRS modem                                               // 
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// tests.cpp
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

#define APRS_ROUTER_ENABLE_PACKET_SUPPORT true

#include "external/aprsroute.hpp"

#define LIBMODEM_PACKET_NAMESPACE_REFERENCE aprs::router::

#include <bitstream.h>
#include <audio_stream.h>
#include <modem.h>
#include <modulator.h>

#include <random>
#include <fstream>
#include <sstream>
#include <fmt/core.h>
#include <fmt/format.h>
#include <filesystem>

#ifdef WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <corecrt_io.h> 
#include <fcntl.h>
#include <windows.h>

#endif

#include <io.h>
#include <boost/process.hpp>
#include <gtest/gtest.h>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "external/aprstrack.hpp"

using namespace LIBMODEM_NAMESPACE;

// **************************************************************** //
//                                                                  //
//                                                                  //
// Utilities                                                        //
//                                                                  //
//                                                                  //
// **************************************************************** //

struct fft_bin
{
    double frequency;
    double magnitude;
};

std::vector<uint8_t> generate_random_bits(size_t count);
size_t random_size(size_t max_size);
void print_hex(const std::vector<uint8_t>& data, std::size_t per_line = 16);
void print_bits(const std::vector<uint8_t>& bits, std::size_t per_line = 8);
std::string replace_non_printable(const std::string& s);
template<typename... Args>
int run_process(const std::string& exe_path, std::string& output, std::string& error, Args&&... args);
struct fft_bin;
std::vector<fft_bin> compute_fft(const std::string& wav_file);
fft_bin dominant_frequency(const std::vector<fft_bin>& bins);
std::vector<fft_bin> frequencies_above_threshold(const std::vector<fft_bin>& bins, double threshold);
std::string to_string(const std::vector<address>& path);
std::string to_hex_string(const std::vector<uint8_t>& data, size_t columns = 25);
static std::string replace_crlf(std::string_view s);
void direwolf_output_to_packets(const std::string& direwolf_output_filename, std::vector<std::string>& packets);
void direwolf_output_to_packets(const std::string& direwolf_output_filename, const std::string& packets_filename);

std::vector<uint8_t> generate_random_bits(size_t count)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 1);

    std::vector<uint8_t> bits(count);
    for (size_t i = 0; i < count; i++)
    {
        bits[i] = dis(gen);
    }
    return bits;
}

size_t random_size(size_t max_size)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dis(8, max_size);
    return dis(gen);
}

void print_hex(const std::vector<uint8_t>& data, std::size_t per_line)
{
    std::ios_base::fmtflags f = std::cout.flags(); // save flags

    std::cout << std::uppercase << std::hex << std::setfill('0');

    for (std::size_t i = 0; i < data.size(); ++i)
    {
        std::cout << std::setw(2) << static_cast<unsigned int>(data[i]);
        if (i + 1 != data.size())
        {
            std::cout << ' ';
        }
        if (per_line && ((i + 1) % per_line == 0))
        {
            std::cout << '\n';
        }
    }

    std::cout << '\n';

    std::cout.flags(f); // restore flags
}

void print_bits(const std::vector<uint8_t>& bits, std::size_t per_line)
{
    for (size_t i = 0; uint8_t bit : bits)
    {
        std::cout << static_cast<int>(bit) << ", ";

        if (per_line && ((i + 1) % per_line == 0))
        {
            std::cout << '\n';
        }

        i++;
    }
    std::cout << std::endl;
}

std::string replace_non_printable(const std::string& s)
{
    std::ostringstream oss;
    for (unsigned char c : s)
    {
        if (c < 0x20 || c > 0x7e)
        {
            oss << "<0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c) << ">";
        }
        else
        {
            oss << c;
        }
    }
    return oss.str();
}

template<typename... Args>
int run_process(const std::string& exe_path, std::string& output, std::string& error, Args&&... args)
{
namespace bp = boost::process;

    bp::ipstream stdout_pipe;
    bp::ipstream stderr_pipe;

    bp::child process(
        exe_path,
        std::forward<Args>(args)...,
        bp::std_out > stdout_pipe,
        bp::std_err > stderr_pipe
    );

    std::string line;
    std::stringstream output_stream;
    std::stringstream error_stream;

    while (std::getline(stdout_pipe, line))
    {
        output_stream << line << "\n";
    }

    while (std::getline(stderr_pipe, line))
    {
        error_stream << line << "\n";
    }

    process.wait();

    output = output_stream.str();
    error = error_stream.str();

    return process.exit_code();
}

std::vector<fft_bin> compute_fft(const std::string& wav_file)
{
    std::string output;

    // Python executable path is set by CMake at build time using find_package(Python3 COMPONENTS Interpreter)
    // The python script fft.py is located in the source directory

    std::filesystem::path script_path = std::filesystem::current_path() / "fft.py";

    if (!std::filesystem::exists(script_path))
    {
        throw std::runtime_error("FFT script not found: " + script_path.string());
    }

    std::string python_exe_path = PYTHON_EXE_PATH;

    if (python_exe_path.empty())
    {
        throw std::runtime_error("Python executable path is not set. Please set PYTHON_EXE_PATH macro.");
    }

    if (!std::filesystem::exists(python_exe_path))
    {
        throw std::runtime_error("Python executable not found: " + python_exe_path);
    }

    std::string error;

    run_process(python_exe_path, output, error, script_path.string(), wav_file);

    if (!error.empty())
    {
        printf("Error: %s\n", error.c_str());
    }

    if (output.empty())
    {
        throw std::runtime_error("FFT computation failed or produced no output");
    }

    // Output is in the format "frequency:magnitude\n"

    std::vector<fft_bin> bins;

    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line))
    {
        size_t colon_pos = line.find(',');
        if (colon_pos != std::string::npos)
        {
            fft_bin bin;
            bin.frequency = std::stod(line.substr(0, colon_pos));
            bin.magnitude = std::stod(line.substr(colon_pos + 1));
            bins.push_back(bin);
        }
    }

    return bins;
}

fft_bin dominant_frequency(const std::vector<fft_bin>& bins)
{
    auto it = std::max_element(bins.begin(), bins.end(), [](const fft_bin& a, const fft_bin& b) {
        return a.magnitude < b.magnitude;
    });
    if (it == bins.end())
    {
        return fft_bin { 0.0, 0.0 };
    }
    return *it;
}

std::vector<fft_bin> frequencies_above_threshold(const std::vector<fft_bin>& bins, double threshold)
{
    std::vector<fft_bin> frequencies;
    for (const auto& bin : bins)
    {
        if (bin.magnitude >= threshold)
        {
            frequencies.push_back(bin);
        }
    }
    return frequencies;
}

std::string to_string(const std::vector<address>& path)
{
    std::string result;
    for (size_t i = 0; i < path.size(); i++)
    {
        result += to_string(path[i]);
        if (i + 1 < path.size())
        {
            result += ",";
        }
    }
    return result;
}

std::string to_hex_string(const std::vector<uint8_t>& data, size_t columns)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; uint8_t byte : data)
    {
        oss << std::setw(2) << static_cast<int>(byte) << " ";
        if (columns && ((i + 1) % columns == 0))
        {
            oss << "\n";
        }
        i++;
    }
    return oss.str();
}

static std::string replace_crlf(std::string_view s)
{
    std::string out;
    out.reserve(s.size()); // grows as needed if many replacements
    size_t i = 0;
    while (true)
    {
        size_t j = s.find_first_of("\r\n", i);
        if (j == std::string_view::npos)
        {
            out.append(s.substr(i));
            break;
        }
        out.append(s.substr(i, j - i));
        out += (s[j] == '\r') ? "<CR>" : "<LF>";
        i = j + 1;
    }
    return out;
}

void direwolf_output_to_packets(const std::string& direwolf_output_filename, std::vector<aprs::router::packet>& packets)
{
    std::ifstream input_file(direwolf_output_filename);
    std::string line;

    while (std::getline(input_file, line))
    {
        // Find "[0.0] " or "[0.1] " pattern
        size_t pos = line.find("[0.");
        if (pos == std::string::npos)
        {
            pos = line.find("[0");
        }
        if (pos != std::string::npos)
        {
            size_t bracket_end = line.find("] ", pos);
            if (bracket_end != std::string::npos)
            {
                std::string packet_string = line.substr(bracket_end + 2);

#if 0
                // Replace hex markers with readable names
                size_t p;
                while ((p = packet_string.find("<0x0d>")) != std::string::npos)
                    packet_string.replace(p, 6, "<CR>");
                while ((p = packet_string.find("<0x0a>")) != std::string::npos)
                    packet_string.replace(p, 6, "<LF>");

                std::vector<std::pair<std::string, char>> control_chars = { {"<0x1c>", 0x1C}, {"<0x1d>", 0x1D}, {"<0x7f>", 0x7F}, {"<0x1f>", 0x1F}, {"<0x1e>", 0x1E}, {"<0x20>", 0x20} };
                for (const auto& [marker, ch] : control_chars)
                {
                    while ((p = packet_string.find(marker)) != std::string::npos)
                    {
                        packet_string.replace(p, 6, 1, ch);
                    }
                }
#endif

                if (!packet_string.empty())
                {
                    aprs::router::packet p(packet_string);
                    packets.push_back(p);
                }
            }
        }
    }
}

void direwolf_output_to_packets(const std::string& direwolf_output_filename, std::vector<std::string>& packets_string)
{
    std::vector<aprs::router::packet> packets;
    direwolf_output_to_packets(direwolf_output_filename, packets);
    for (const auto& packet : packets)
    {
        packets_string.push_back(aprs::router::to_string(packet));
    }
}

void direwolf_output_to_packets(const std::string& direwolf_output_filename, const std::string& packets_filename)
{
    std::vector<std::string> packets;
    direwolf_output_to_packets(direwolf_output_filename, packets);
    std::ofstream outfile(packets_filename);
    for (const auto& packet : packets)
    {
        outfile << packet << "\n";
    }
    outfile.close();
}

// **************************************************************** //
//                                                                  //
//                                                                  //
// Tests                                                            //
//                                                                  //
//                                                                  //
// **************************************************************** //

TEST(address, try_parse_address)
{
    {
        address s;
        EXPECT_TRUE(try_parse_address("WIDE2-1", s));
        EXPECT_TRUE(s.text == "WIDE");
        EXPECT_TRUE(s.n == 2);
        EXPECT_TRUE(s.N == 1);
        EXPECT_TRUE(s.ssid == 0);
    }

    {
        address s;
        EXPECT_TRUE(s.mark == false);
        EXPECT_TRUE(try_parse_address("WIDE2-1*", s));
        EXPECT_TRUE(s.text == "WIDE");
        EXPECT_TRUE(s.n == 2);
        EXPECT_TRUE(s.N == 1);
        EXPECT_TRUE(s.mark == true);
        EXPECT_TRUE(s.ssid == 0);
    }

    {
        address s;
        EXPECT_TRUE(try_parse_address("WIDE2*", s));
        EXPECT_TRUE(s.text == "WIDE");
        EXPECT_TRUE(s.n == 2);
        EXPECT_TRUE(s.N == 0);
        EXPECT_TRUE(s.mark == true);
        EXPECT_TRUE(s.ssid == 0);
    }

    {
        address s;
        EXPECT_TRUE(try_parse_address("WIDE*", s));
        EXPECT_TRUE(s.text == "WIDE");
        EXPECT_TRUE(s.n == 0);
        EXPECT_TRUE(s.N == 0);
        EXPECT_TRUE(s.mark == true);
        EXPECT_TRUE(s.ssid == 0);
    }

    {
        address s;
        EXPECT_TRUE(try_parse_address("N0CALL-10", s));
        EXPECT_TRUE(s.text == "N0CALL");
        EXPECT_TRUE(s.ssid == 10);
        EXPECT_TRUE(s.mark == false);
        EXPECT_TRUE(s.n == 0);
        EXPECT_TRUE(s.N == 0);
    }

    {
        address s;
        EXPECT_TRUE(try_parse_address("N0CALL-10*", s));
        EXPECT_TRUE(s.text == "N0CALL");
        EXPECT_TRUE(s.ssid == 10);
        EXPECT_TRUE(s.mark == true);
        EXPECT_TRUE(s.n == 0);
        EXPECT_TRUE(s.N == 0);
    }
}

TEST(address, to_string)
{
    address s;
    s.text = "WIDE";
    s.n = 2;
    s.N = 1;
    s.mark = false;
    EXPECT_TRUE(to_string(s) == "WIDE2-1");

    s.mark = true;
    EXPECT_TRUE(to_string(s) == "WIDE2-1*");

    s.mark = true;
    s.N = 0;
    EXPECT_TRUE(to_string(s) == "WIDE2*");

    s.N = 0;
    s.n = 0;
    EXPECT_TRUE(to_string(s) == "WIDE*");

    s = address{};
    s.text = "N0CALL";
    s.ssid = 10;
    EXPECT_TRUE(to_string(s) == "N0CALL-10");

    s = address{};
    s.text = "N0CALL";
    s.ssid = 10;
    s.mark = true;
    EXPECT_TRUE(to_string(s) == "N0CALL-10*");

    s = address{};
    s.text = "N0CALL-10";
    s.ssid = 10;
    EXPECT_TRUE(to_string(s) == "N0CALL-10-10"); // to_string preserves the text even if ssid is specified and results in an invalid address
}

TEST(ax25, encode_header)
{
LIBMODEM_AX25_USING_NAMESPACE

    {
        address from = { "N0CALL", 0, 0, 10, false };
        address to = { "APZ001", 0, 0, 0, false };
        std::vector<address> path = {
            { "WIDE1", 0, 1, 0, false },
            { "WIDE2", 0, 2, 0, false }
        };

        std::vector<uint8_t> header = encode_header(from, to, path);

        EXPECT_TRUE(header.size() == 28);

        EXPECT_TRUE(header == (std::vector<uint8_t>{
            // Destination: APZ001
            0x82, 0xA0, 0xB4, 0x60, 0x60, 0x62, 0x60,
            // Source: N0CALL-10
            0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x74,
            // Path 1: WIDE1-1
            0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
            // Path 2: WIDE2-2 (last addr, end bit set)
            0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0x65,      
        }));
    }

    {
        // Source address is set as set becuase the path is empty

        address from = { "N0CALL", 0, 0, 10, false };
        address to = { "APZ001", 0, 0, 0, false };

        std::vector<uint8_t> header = encode_header(from, to, {});

        EXPECT_TRUE(header.size() == 14);

        EXPECT_TRUE(header == (std::vector<uint8_t>{
            // Destination: APZ001
            0x82, 0xA0, 0xB4, 0x60, 0x60, 0x62, 0x60,
            // Source: N0CALL-10 (last addr, end bit set)
            0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x75,
        }));
    }
}

TEST(ax25, encode_frame)
{
LIBMODEM_AX25_USING_NAMESPACE

    {
        // N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!
        aprs::router::packet p = { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS!" };

        std::vector<uint8_t> frame = encode_frame(p);

        EXPECT_TRUE(frame.size() == 44);

        EXPECT_TRUE(frame == (std::vector<uint8_t>{
            // Destination: APZ001
            0x82, 0xA0, 0xB4, 0x60, 0x60, 0x62, 0x60,
            // Source: N0CALL-10
            0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x74,
            // Path 1: WIDE1-1
            0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
            // Path 2: WIDE2-2 (last addr, end bit set)
            0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0x65,
            // Control, PID
            0x03, 0xF0,
            // Payload: "Hello, APRS!"
            0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21,
            // CRC (FCS), little-endian
            0x50, 0x7B
        }));
    }

    {
        // N0CALL-10>APZ001,WIDE1-1,WIDE2-2*:Hello, APRS!
        aprs::router::packet p = { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2*" }, "Hello, APRS!" };

        std::vector<uint8_t> frame = encode_frame(p);

        EXPECT_TRUE(frame.size() == 44);

        EXPECT_TRUE(frame == (std::vector<uint8_t>{
            // Destination: APZ001
            0x82, 0xA0, 0xB4, 0x60, 0x60, 0x62, 0x60,
            // Source: N0CALL-10
            0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x74,
            // Path 1: WIDE1-1
            0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
            // Path 2: WIDE2-2* (last addr, end bit set)
            0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0xE5,
            // Control, PID
            0x03, 0xF0,
            // Payload: "Hello, APRS!"
            0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21,
            // CRC (FCS), little-endian
            0x25, 0x44
        }));
    }

    {
        // N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!
        struct frame frame {
            { "N0CALL", 0, 0, 10, false },
            { "APZ001", 0, 0, 0, false },
            {
                { "WIDE1", 0, 1, 0, false },
                { "WIDE2", 0, 2, 0, false }
            },
            { 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21 }
        };

        std::vector<uint8_t> frame_bytes = encode_frame(frame);

        EXPECT_TRUE(frame_bytes.size() == 44);

        EXPECT_TRUE(frame_bytes == (std::vector<uint8_t>{
            // Destination: APZ001
            0x82, 0xA0, 0xB4, 0x60, 0x60, 0x62, 0x60,
            // Source: N0CALL-10
            0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x74,
            // Path 1: WIDE1-1
            0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
            // Path 2: WIDE2-2 (last addr, end bit set)
            0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0x65,
            // Control, PID
            0x03, 0xF0,
            // Payload: "Hello, APRS!"
            0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21,
            // CRC (FCS), little-endian
            0x50, 0x7B
        }));
    }

    {
        // N0CALL-10>APZ001:Hello, APRS!
        aprs::router::packet p = { "N0CALL-10", "APZ001", {}, "Hello, APRS!" };

        std::vector<uint8_t> frame_bytes = encode_frame(p);

        EXPECT_TRUE(frame_bytes.size() == 30);

        EXPECT_TRUE(frame_bytes == (std::vector<uint8_t>{
            // Destination: APZ001
            0x82, 0xA0, 0xB4, 0x60, 0x60, 0x62, 0x60,
                // Source: N0CALL-10 (last addr, end bit set)
            0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x75,
            // Control, PID
            0x03, 0xF0,
            // Payload: "Hello, APRS!"
            0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21,
            // CRC (FCS), little-endian
            0xAE, 0xE6
        }));
    }
}

TEST(ax25, encode_frame_output_iterator)
{
LIBMODEM_AX25_USING_NAMESPACE

    // N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!
    struct frame frame
    {
        { "N0CALL", 0, 0, 10, false },
        { "APZ001", 0, 0, 0, false },
            {
                { "WIDE1", 0, 1, 0, false },
                { "WIDE2", 0, 2, 0, false }
            },
        { 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21 }
    };

    std::vector<uint8_t> frame_bytes(100);

    auto end_it = encode_frame(frame.from, frame.to, frame.path.begin(), frame.path.end(), frame.data.begin(), frame.data.end(), frame_bytes.begin());

    size_t frame_size = std::distance(frame_bytes.begin(), end_it);

    frame_bytes.resize(frame_size);

    EXPECT_TRUE(frame_bytes.size() == 44);

    EXPECT_TRUE(frame_bytes == (std::vector<uint8_t>{
        // Destination: APZ001
        0x82, 0xA0, 0xB4, 0x60, 0x60, 0x62, 0x60,
        // Source: N0CALL-10
        0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x74,
        // Path 1: WIDE1-1
        0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
        // Path 2: WIDE2-2 (last addr, end bit set)
        0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0x65,
        // Control, PID
        0x03, 0xF0,
        // Payload: "Hello, APRS!"
        0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21,
        // CRC (FCS), little-endian
        0x50, 0x7B
    }));
}

TEST(ax25, encode_frame_output_iterator_stack)
{
LIBMODEM_AX25_USING_NAMESPACE

    // N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!
    struct frame frame
    {
        { "N0CALL", 0, 0, 10, false },
        { "APZ001", 0, 0, 0, false },
            {
                { "WIDE1", 0, 1, 0, false },
                { "WIDE2", 0, 2, 0, false }
            },
        { 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21 }
    };

    uint8_t frame_bytes[100] = { 0 };

    auto end_it = encode_frame(frame.from, frame.to, frame.path.begin(), frame.path.end(), frame.data.begin(), frame.data.end(), std::begin(frame_bytes));

    size_t frame_size = std::distance(std::begin(frame_bytes), end_it);

    EXPECT_TRUE(frame_size == 44);

    EXPECT_TRUE(std::vector<uint8_t>(frame_bytes, frame_bytes + frame_size) == (std::vector<uint8_t>{
        // Destination: APZ001
        0x82, 0xA0, 0xB4, 0x60, 0x60, 0x62, 0x60,
        // Source: N0CALL-10
        0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x74,
        // Path 1: WIDE1-1
        0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
        // Path 2: WIDE2-2 (last addr, end bit set)
        0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0x65,
        // Control, PID
        0x03, 0xF0,
        // Payload: "Hello, APRS!"
        0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21,
        // CRC (FCS), little-endian
        0x50, 0x7B
    }));
}

TEST(ax25, to_packet)
{
LIBMODEM_AX25_USING_NAMESPACE

    // N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!
    struct frame frame
    {
        { "N0CALL", 0, 0, 10, false },
        { "APZ001", 0, 0, 0, false },
            {
                { "WIDE1", 0, 1, 0, false },
                { "WIDE2", 0, 2, 0, false }
            },
        { 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21 }
    };

    EXPECT_TRUE(to_string(to_packet(frame)) == "N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!");
}

TEST(ax25, encode_address)
{
LIBMODEM_AX25_USING_NAMESPACE

    {
        std::array<uint8_t, 7> address = encode_address("N0CALL", 10, false, false);
        EXPECT_TRUE(address == (std::array<uint8_t, 7>{ 0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x74 }));
    }

    {
        std::array<uint8_t, 7> address = encode_address("WIDE2", 2, true, false);
        EXPECT_TRUE(address == (std::array<uint8_t, 7>{ 0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0xE4 }));
    }

    {
        std::array<uint8_t, 7> address = encode_address("APZ001", 0, false, true);
        EXPECT_TRUE(address == (std::array<uint8_t, 7>{ 0x82, 0xA0, 0xB4, 0x60, 0x60, 0x62, 0x61 }));
    }

    {
        std::array<uint8_t, 7> address = encode_address("WIDE1", 1, false, true);
        EXPECT_TRUE(address == (std::array<uint8_t, 7>{ 0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x63 }));
    }

    {
        std::array<uint8_t, 7> address = encode_address("WIDE2", 2, true, true);
        EXPECT_TRUE(address == (std::array<uint8_t, 7>{ 0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0xE5 }));
    }
}

TEST(ax25, encode_address_ssid_0_15)
{
LIBMODEM_AX25_USING_NAMESPACE

    std::vector<uint8_t> ssids = std::vector<uint8_t>({
        0b01100000, // 0 - or not set
        0b01100010, // 1
        0b01100100, // 2
        0b01100110, // 3
        0b01101000, // 4
        0b01101010, // 5
        0b01101100, // 6
        0b01101110, // 7
        0b01110000, // 8
        0b01110010, // 9
        0b01110100, // 10
        0b01110110, // 11
        0b01111000, // 12
        0b01111010, // 13
        0b01111100, // 14
        0b01111110, // 15
    });

    // Encode the address "T7SVVQ" with ssid values from 0 to 15
    // Compare the encoded address bytes with expected values
    // The comparsion is made against an address created with known byte values for "T7SVVQ" and varying ssid byte
    // The ssid byte is taken from the ssids vector

    for (int i = 0; i <= 15; i++)
    {
        std::array<uint8_t, 7> address = encode_address("T7SVVQ", i, false, false);
        EXPECT_TRUE(address == (std::array<uint8_t, 7>{ 0xA8, 0x6E, 0xA6, 0xAC, 0xAC, 0xA2, ssids[i] }));
    }
}

TEST(ax25, try_parse_address_string_view)
{
LIBMODEM_AX25_USING_NAMESPACE

    {
        std::string address;
        int ssid;
        bool mark;
        try_parse_address(std::string_view("\x9C\x60\x86\x82\x98\x98\x74", 7), address, ssid, mark);
        EXPECT_EQ(address, "N0CALL");
        EXPECT_EQ(ssid, 10);
        EXPECT_FALSE(mark);
    }

    {
        std::string address;
        int ssid;
        bool mark;
        try_parse_address(std::string_view("\xAE\x92\x88\x8A\x64\x40\xE4", 7), address, ssid, mark);
        EXPECT_EQ(address, "WIDE2");
        EXPECT_EQ(ssid, 2);
        EXPECT_TRUE(mark);
    }

    {
        std::string address;
        int ssid;
        bool mark;
        try_parse_address(std::string_view("\x82\xA0\xB4\x60\x60\x62\x61", 7), address, ssid, mark);
        EXPECT_EQ(address, "APZ001");
        EXPECT_EQ(ssid, 0);
        EXPECT_FALSE(mark);
    }

    {
        std::string address;
        int ssid;
        bool mark;
        try_parse_address(std::string_view("\xAE\x92\x88\x8A\x62\x40\x63", 7), address, ssid, mark);
        EXPECT_EQ(address, "WIDE1");
        EXPECT_EQ(ssid, 1);
        EXPECT_FALSE(mark);
    }

    {
        std::string address;
        int ssid;
        bool mark;
        // Intentially using an invalid byte 6 to represent non alphanumeric character
        // This address is not valid
        try_parse_address(std::string_view("\xAE\x92\x88\x8A\x64\x5A\xE5", 7), address, ssid, mark);
        EXPECT_EQ(address, "WIDE2-");
        EXPECT_EQ(ssid, 2);
        EXPECT_TRUE(mark);
    }
}

TEST(ax25, try_parse_address_iterator)
{
LIBMODEM_AX25_USING_NAMESPACE

    {
        std::string address;
        int ssid;
        bool mark;
        std::vector<uint8_t> address_bytes = { 0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x74 };
        try_parse_address(address_bytes.begin(), address_bytes.end(), address, ssid, mark);
        EXPECT_EQ(address, "N0CALL");
        EXPECT_EQ(ssid, 10);
        EXPECT_FALSE(mark);
    }

    {
        std::string address;
        int ssid;
        bool mark;
        std::array<uint8_t, 7> address_bytes = { 0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x76 };
        try_parse_address(address_bytes.begin(), address_bytes.end(), address, ssid, mark);
        EXPECT_EQ(address, "N0CALL");
        EXPECT_EQ(ssid, 11);
        EXPECT_FALSE(mark);
    }
}

TEST(ax25, try_parse_address_output_iterator)
{
    LIBMODEM_AX25_USING_NAMESPACE

    {
        std::string address;
        int ssid;
        bool mark;
        std::vector<uint8_t> address_bytes = { 0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x74 };
        try_parse_address(address_bytes.begin(), address_bytes.end(), std::back_inserter(address), ssid, mark);
        EXPECT_EQ(address, "N0CALL");
        EXPECT_EQ(ssid, 10);
        EXPECT_FALSE(mark);
    }

    {
        std::string address;
        int ssid;
        bool mark;
        std::array<uint8_t, 7> address_bytes = { 0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x76 };
        try_parse_address(address_bytes.begin(), address_bytes.end(), std::back_inserter(address), ssid, mark);
        EXPECT_EQ(address, "N0CALL");
        EXPECT_EQ(ssid, 11);
        EXPECT_FALSE(mark);
    }
}

TEST(ax25, parse_address_ssid_0_15)
{
LIBMODEM_AX25_USING_NAMESPACE

    std::vector<uint8_t> ssids = std::vector<uint8_t>({
        0b01100000, // 0 - or not set
        0b01100010, // 1
        0b01100100, // 2
        0b01100110, // 3
        0b01101000, // 4
        0b01101010, // 5
        0b01101100, // 6
        0b01101110, // 7
        0b01110000, // 8
        0b01110010, // 9
        0b01110100, // 10
        0b01110110, // 11
        0b01111000, // 12
        0b01111010, // 13
        0b01111100, // 14
        0b01111110, // 15
    });

    // Create address bytes by substituting the ssid byte 
    // Pick the ssid byte 0 to 15 from the ssid byte array
    // Parse the address from the address bytes
    // Convert the parsed address back to string and verify correctness

    for (int i = 0; i <= 15; i++)
    {
        std::array<uint8_t, 7> address_bytes = { 0xA8, 0x6E, 0xA6, 0xAC, 0xAC, 0xA2, ssids[i] };
        struct address address;
        LIBMODEM_AX25_NAMESPACE_REFERENCE try_parse_address(std::string_view(reinterpret_cast<const char*>(address_bytes.data()), address_bytes.size()), address);
        
        if (i == 0)
        {
            EXPECT_TRUE(to_string(address) == "T7SVVQ");
        }
        else
        {
            EXPECT_TRUE(to_string(address) == "T7SVVQ-" + std::to_string(i));
        }
    }
}

TEST(ax25, try_decode_frame)
{
LIBMODEM_AX25_USING_NAMESPACE

    // N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!
    std::vector<uint8_t> frame = {
        // Destination: APZ001
        0x82, 0xA0, 0xB4, 0x60, 0x60, 0x62, 0x60,
        // Source: N0CALL-10
        0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x74,
        // Path 1: WIDE1-1
        0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
        // Path 2: WIDE2-2 (last addr, end bit set)
        0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0x65,
        // Control, PID
        0x03, 0xF0,
        // Payload: "Hello, APRS!"
        0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21,
        // CRC (FCS), little-endian
        0x50, 0x7B
    };

    {
        aprs::router::packet p;

        EXPECT_TRUE(try_decode_frame(frame, p));

        EXPECT_TRUE(to_string(p) == "N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!");
    }

    {
        address from;
        address to;
        std::vector<address> path;
        std::vector<uint8_t> data;
        EXPECT_TRUE(try_decode_frame(frame, from, to, path, data));
        EXPECT_TRUE(to_string(from) == "N0CALL-10");
        EXPECT_TRUE(to_string(to) == "APZ001");
        EXPECT_TRUE(path.size() == 2);
        EXPECT_TRUE(to_string(path[0]) == "WIDE1-1");
        EXPECT_TRUE(to_string(path[1]) == "WIDE2-2");
        EXPECT_TRUE(data == std::vector<uint8_t>({ 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21 }));
    }

    {
        // N6XQY-12>GPSLJ,RELAY,WIDE2-2:$GPRMC,013641.06,A,3348.1607,N,11807.4631,W,34.0,090.5,231105,13.,E*73<0x0d>

        std::vector<uint8_t> frame = { 0x8e, 0xa0, 0xa6, 0x98, 0x94, 0x40, 0x60, 0x9c, 0x6c, 0xb0, 0xa2, 0xb2, 0x40, 0xf8, 0xa4, 0x8a, 0x98, 0x82, 0xb2, 0x40, 0x60, 0xae, 0x92, 0x88, 0x8a, 0x64, 0x40, 0x65, 0x03, 0xf0, 0x24, 0x47, 0x50, 0x52, 0x4d, 0x43, 0x2c, 0x30, 0x31, 0x33, 0x36, 0x34, 0x31, 0x2e, 0x30, 0x36, 0x2c, 0x41, 0x2c, 0x33, 0x33, 0x34, 0x38, 0x2e, 0x31, 0x36, 0x30, 0x37, 0x2c, 0x4e, 0x2c, 0x31, 0x31, 0x38, 0x30, 0x37, 0x2e, 0x34, 0x36, 0x33, 0x31, 0x2c, 0x57, 0x2c, 0x33, 0x34, 0x2e, 0x30, 0x2c, 0x30, 0x39, 0x30, 0x2e, 0x35, 0x2c, 0x32, 0x33, 0x31, 0x31, 0x30, 0x35, 0x2c, 0x31, 0x33, 0x2e, 0x2c, 0x45, 0x2a, 0x37, 0x33, 0x0d, 0xc9, 0x42 };

        aprs::router::packet p;

        EXPECT_TRUE(try_decode_frame(frame, p));

        EXPECT_TRUE(to_string(p) == "N6XQY-12>GPSLJ,RELAY,WIDE2-2:$GPRMC,013641.06,A,3348.1607,N,11807.4631,W,34.0,090.5,231105,13.,E*73\r");
    }

    {
        // WA6YLB>APRX46,WA6YLB-7*,W6SCE-10*:>081839z wa6ylb@theworks.com<0x0d>

        std::vector<uint8_t> frame = { 0x82, 0xa0, 0xa4, 0xb0, 0x68, 0x6c, 0x60, 0xae, 0x82, 0x6c, 0xb2, 0x98, 0x84, 0x60, 0xae, 0x82, 0x6c, 0xb2, 0x98, 0x84, 0xee, 0xae, 0x6c, 0xa6, 0x86, 0x8a, 0x40, 0xf5, 0x03, 0xf0, 0x3e, 0x30, 0x38, 0x31, 0x38, 0x33, 0x39, 0x7a, 0x20, 0x77, 0x61, 0x36, 0x79, 0x6c, 0x62, 0x40, 0x74, 0x68, 0x65, 0x77, 0x6f, 0x72, 0x6b, 0x73, 0x2e, 0x63, 0x6f, 0x6d, 0x0d, 0x0c, 0x66 };

        aprs::router::packet p;

        EXPECT_TRUE(try_decode_frame(frame, p));

        EXPECT_TRUE(to_string(p) == "WA6YLB>APRX46,WA6YLB-7*,W6SCE-10*:>081839z wa6ylb@theworks.com\r");
    }

    {
        // N0CALL-10>APZ001:Hello, APRS!
        std::vector<uint8_t> frame = {
            // Destination: APZ001
            0x82, 0xA0, 0xB4, 0x60, 0x60, 0x62, 0x60,
            // Source: N0CALL-10 (last addr, end bit set)
            0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x75,
            // Control, PID
            0x03, 0xF0,
            // Payload: "Hello, APRS!"
            0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21,
            // CRC (FCS), little-endian
            0xAE, 0xE6
        };

        aprs::router::packet p;

        EXPECT_TRUE(try_decode_frame(frame, p));

        EXPECT_TRUE(to_string(p) == "N0CALL-10>APZ001:Hello, APRS!");
    }

    {
        // KD7FNO-5>S5RTQP,W6PVG-3,WB6JAR-10,WIDE2*:'/3hl"Ku/]"4t}     

        std::vector<uint8_t> frame = { 0xa6, 0x6a, 0xa4, 0xa8, 0xa2, 0xa0, 0x60, 0x96, 0x88, 0x6e, 0x8c, 0x9c, 0x9e, 0xea, 0xae, 0x6c, 0xa0, 0xac, 0x8e, 0x40, 0xe6, 0xae, 0x84, 0x6c, 0x94, 0x82, 0xa4, 0xf4, 0xae, 0x92, 0x88, 0x8a, 0x64, 0x40, 0xe1, 0x03, 0xf0, 0x27, 0x2f, 0x33, 0x68, 0x6c, 0x22, 0x4b, 0x75, 0x2f, 0x5d, 0x22, 0x34, 0x74, 0x7d, 0x0d, 0x20, 0xef };
       
        aprs::router::packet p;

        EXPECT_TRUE(try_decode_frame(frame, p));

        EXPECT_TRUE(to_string(p) == "KD7FNO-5>S5RTQP,W6PVG-3*,WB6JAR-10*,WIDE2*:'/3hl\"Ku/]\"4t}\r");
    }

    {
        // N0CALL-10>APZ001:Hello, APRS!
        // The Source address does not have the last address bit set
        // And there are no path addresses

        std::vector<uint8_t> frame = {
            // Destination: APZ001
            0x82, 0xA0, 0xB4, 0x60, 0x60, 0x62, 0x60,
            // Source: N0CALL-10 (last addr, end bit set NOT SET)
            0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x74,
            // Control, PID
            0x03, 0xF0,
            // Payload: "Hello, APRS!"
            0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21,
            // CRC (FCS), little-endian
            0x84, 0xAE
        };

        aprs::router::packet p;

        EXPECT_TRUE(try_decode_frame(frame, p));

        EXPECT_TRUE(to_string(p) == "N0CALL-10>APZ001:Hello, APRS!");
    }
}

TEST(fx25, encode_fx25_frame_iterator)
{
LIBMODEM_AX25_USING_NAMESPACE
LIBMODEM_FX25_USING_NAMESPACE

    aprs::router::packet p = { "W7ION-5", "T7SVVQ", { "WIDE1-1", "WIDE2-1" }, R"(`2(al"|[/>"3u}hello world^)" };

    // Using encode_fx25_frame with iterators

    std::vector<uint8_t> frame_bytes = encode_frame(p);

    std::vector<uint8_t> fx25_frame_bytes = encode_fx25_frame(frame_bytes.begin(), frame_bytes.end(), 0);

    EXPECT_TRUE(fx25_frame_bytes == std::vector<uint8_t>({
        // FX.25 Correlation Tag
        // Tag_03: RS(80,64)
        // { 0xC7DC0508F3D9B09EULL,  80,  64, 16 }
        0x9E, 0xB0, 0xD9, 0xF3, 0x08, 0x05, 0xDC, 0xC7,
        // AX.25 frame start
        // Destination: T7SVVQ
        0xA8, 0x6E, 0xA6, 0xAC, 0xAC, 0xA2, 0x60,
        // Source: W7ION-5
        0xAE, 0x6E, 0x92, 0x9E, 0x9C, 0x40, 0x6A,
        // Path 1: WIDE1-1
        0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
        // Path 2: WIDE2-1 (last addr)
        0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0x63,
        // Control, PID
        0x03, 0xF0,
        // Payload: `2(al"|[/>"3u}hello world^)
        0x60, 0x32, 0x28, 0x61, 0x6C, 0x22, 0x7C, 0x5B, 0x2F, 0x3E, 0x22, 0x33, 0x75, 0x7D, 0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64, 0x5E,
        // FCS
        0x99, 0x3C,
        // AX.25 frame end
        // Padding with 6 bytes to fit within 64 bytes block
        // Frame size is 58 bytes
        0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E,
        // RS Check Bytes, 16 bytes
        0x02, 0xFC, 0xED, 0x9F, 0x4B, 0x8E, 0x6A, 0x33,
        0xA6, 0x03, 0x4B, 0x67, 0x45, 0x3B, 0xAB, 0x7E
    }));
}

TEST(fx25, encode_fx25_frame_container)
{
LIBMODEM_AX25_USING_NAMESPACE
LIBMODEM_FX25_USING_NAMESPACE

    // aprs::router::packet p = { "W7ION-5", "T7SVVQ", { "WIDE1-1", "WIDE2-1" }, R"(`2(al"|[/>"3u}hello world^)" };

    // Using encode_fx25_frame with a container

    std::vector<uint8_t> frame_bytes = {
        // Destination: T7SVVQ
        0xA8, 0x6E, 0xA6, 0xAC, 0xAC, 0xA2, 0x60,
        // Source: W7ION-5
        0xAE, 0x6E, 0x92, 0x9E, 0x9C, 0x40, 0x6A,
        // Path 1: WIDE1-1
        0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
        // Path 2: WIDE2-1 (last addr)
        0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0x63,
        // Control, PID
        0x03, 0xF0,
        // Payload: `2(al"|[/>"3u}hello world^)
        0x60, 0x32, 0x28, 0x61, 0x6C, 0x22, 0x7C, 0x5B, 0x2F, 0x3E, 0x22, 0x33, 0x75, 0x7D, 0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64, 0x5E,
        // FCS
        0x99, 0x3C,
    };

    std::vector<uint8_t> fx25_frame_bytes = encode_fx25_frame(frame_bytes, 0);

    EXPECT_TRUE(fx25_frame_bytes == std::vector<uint8_t>({
        // FX.25 Correlation Tag
        // Tag_03: RS(80,64)
        // { 0xC7DC0508F3D9B09EULL,  80,  64, 16 }
        0x9E, 0xB0, 0xD9, 0xF3, 0x08, 0x05, 0xDC, 0xC7,
        // AX.25 frame start
        // Destination: T7SVVQ
        0xA8, 0x6E, 0xA6, 0xAC, 0xAC, 0xA2, 0x60,
        // Source: W7ION-5
        0xAE, 0x6E, 0x92, 0x9E, 0x9C, 0x40, 0x6A,
        // Path 1: WIDE1-1
        0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
        // Path 2: WIDE2-1 (last addr)
        0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0x63,
        // Control, PID
        0x03, 0xF0,
        // Payload: `2(al"|[/>"3u}hello world^)
        0x60, 0x32, 0x28, 0x61, 0x6C, 0x22, 0x7C, 0x5B, 0x2F, 0x3E, 0x22, 0x33, 0x75, 0x7D, 0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64, 0x5E,
        // FCS
        0x99, 0x3C,
        // AX.25 frame end
        // Padding with 6 bytes to fit within 64 bytes block
        // Frame size is 58 bytes
        0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E,
        // RS Check Bytes, 16 bytes
        0x02, 0xFC, 0xED, 0x9F, 0x4B, 0x8E, 0x6A, 0x33,
        0xA6, 0x03, 0x4B, 0x67, 0x45, 0x3B, 0xAB, 0x7E
    }));
}

TEST(fx25, encode_fx25_frame_span)
{
LIBMODEM_AX25_USING_NAMESPACE
LIBMODEM_FX25_USING_NAMESPACE

    // aprs::router::packet p = { "W7ION-5", "T7SVVQ", { "WIDE1-1", "WIDE2-1" }, R"(`2(al"|[/>"3u}hello world^)" };

    // Using encode_fx25_frame with a span

    uint8_t frame_bytes[] = {
        // Destination: T7SVVQ
        0xA8, 0x6E, 0xA6, 0xAC, 0xAC, 0xA2, 0x60,
        // Source: W7ION-5
        0xAE, 0x6E, 0x92, 0x9E, 0x9C, 0x40, 0x6A,
        // Path 1: WIDE1-1
        0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
        // Path 2: WIDE2-1 (last addr)
        0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0x63,
        // Control, PID
        0x03, 0xF0,
        // Payload: `2(al"|[/>"3u}hello world^)
        0x60, 0x32, 0x28, 0x61, 0x6C, 0x22, 0x7C, 0x5B, 0x2F, 0x3E, 0x22, 0x33, 0x75, 0x7D, 0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64, 0x5E,
        // FCS
        0x99, 0x3C,
    };

    std::vector<uint8_t> fx25_frame_bytes = encode_fx25_frame(frame_bytes, 0);

    EXPECT_TRUE(fx25_frame_bytes == std::vector<uint8_t>({
        // FX.25 Correlation Tag
        // Tag_03: RS(80,64)
        // { 0xC7DC0508F3D9B09EULL,  80,  64, 16 }
        0x9E, 0xB0, 0xD9, 0xF3, 0x08, 0x05, 0xDC, 0xC7,
        // AX.25 frame start
        // Destination: T7SVVQ
        0xA8, 0x6E, 0xA6, 0xAC, 0xAC, 0xA2, 0x60,
        // Source: W7ION-5
        0xAE, 0x6E, 0x92, 0x9E, 0x9C, 0x40, 0x6A,
        // Path 1: WIDE1-1
        0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
        // Path 2: WIDE2-1 (last addr)
        0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0x63,
        // Control, PID
        0x03, 0xF0,
        // Payload: `2(al"|[/>"3u}hello world^)
        0x60, 0x32, 0x28, 0x61, 0x6C, 0x22, 0x7C, 0x5B, 0x2F, 0x3E, 0x22, 0x33, 0x75, 0x7D, 0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64, 0x5E,
        // FCS
        0x99, 0x3C,
        // AX.25 frame end
        // Padding with 6 bytes to fit within 64 bytes block
        // Frame size is 58 bytes
        0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E,
        // RS Check Bytes, 16 bytes
        0x02, 0xFC, 0xED, 0x9F, 0x4B, 0x8E, 0x6A, 0x33,
        0xA6, 0x03, 0x4B, 0x67, 0x45, 0x3B, 0xAB, 0x7E
    }));
}

TEST(bitstream, nrzi_encode)
{
LIBMODEM_AX25_USING_NAMESPACE

    {
        std::vector<uint8_t> bits = { 1,0,1,1,0,0,1 };
        nrzi_encode(bits.begin(), bits.end());
        EXPECT_TRUE(bits == (std::vector<uint8_t>{ 0, 1, 1, 1, 0, 1, 1 }));
    }

    {
        std::vector<uint8_t> bits = { 1,1,1,1,1,1,1 };
        nrzi_encode(bits.begin(), bits.end());
        EXPECT_TRUE(bits == (std::vector<uint8_t>{ 0, 0, 0, 0, 0, 0, 0 }));
    }

    {
        std::vector<uint8_t> bits = { 0,0,0,0,0,0,0 };
        nrzi_encode(bits.begin(), bits.end());
        EXPECT_TRUE(bits == (std::vector<uint8_t>{ 1, 0, 1, 0, 1, 0, 1 }));
    }
}

TEST(bitstream, nrzi_decode)
{
LIBMODEM_AX25_USING_NAMESPACE

    {
        std::vector<uint8_t> bits = { 0,1,1,1,0,1,1 };
        nrzi_decode(bits.begin(), bits.end());
        EXPECT_TRUE(bits == (std::vector<uint8_t>{ 0, 0, 1, 1, 0, 0, 1 }));
    }

    {
        std::vector<uint8_t> bits = { 0,0,0,0,0,0,0 };
        nrzi_decode(bits.begin(), bits.end());
        EXPECT_TRUE(bits == (std::vector<uint8_t>{ 0, 1, 1, 1, 1, 1, 1 }));
    }

    {
        std::vector<uint8_t> bits = { 1,0,1,0,1,0,1 };
        nrzi_decode(bits.begin(), bits.end());
        EXPECT_TRUE(bits == (std::vector<uint8_t>{ 0, 0, 0, 0, 0, 0, 0 }));
    }
}

TEST(bitstream, compute_crc)
{
LIBMODEM_AX25_USING_NAMESPACE

    std::vector<uint8_t> frame = {
        // Destination: APZ001
        0x82, 0xA0, 0xB4, 0x60, 0x60, 0x62, 0x60,
        // Source: N0CALL-10
        0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x74,
        // Path 1: WIDE1-1
        0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
        // Path 2: WIDE2-2* (last addr, end bit set)
        0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0x65,
        // Control, PID
        0x03, 0xF0,
        // Payload: "Hello, APRS!"
        0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21
    };

    std::array<uint8_t, 2> crc = compute_crc(frame.begin(), frame.end());

    EXPECT_TRUE(crc == (std::array<uint8_t, 2>{ 0x50, 0x7B }));
}

TEST(bitstream, compute_crc_using_lut)
{
LIBMODEM_AX25_USING_NAMESPACE

    std::vector<uint8_t> frame = {
        // Destination: APZ001
        0x82, 0xA0, 0xB4, 0x60, 0x60, 0x62, 0x60,
        // Source: N0CALL-10
        0x9C, 0x60, 0x86, 0x82, 0x98, 0x98, 0x74,
        // Path 1: WIDE1-1
        0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
        // Path 2: WIDE2-2* (last addr, end bit set)
        0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0x65,
        // Control, PID
        0x03, 0xF0,
        // Payload: "Hello, APRS!"
        0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x41, 0x50, 0x52, 0x53, 0x21
    };

    std::array<uint8_t, 2> crc = compute_crc_using_lut(frame.begin(), frame.end());

    EXPECT_TRUE(crc == (std::array<uint8_t, 2>{ 0x50, 0x7B }));
}

TEST(bitstream, bytes_to_bits)
{
LIBMODEM_AX25_USING_NAMESPACE

    {
        std::vector<uint8_t> bytes = { 0xA5 }; // 10100101
        std::vector<uint8_t> bits;
        bytes_to_bits(bytes.begin(), bytes.end(), std::back_inserter(bits));
        EXPECT_TRUE(bits == (std::vector<uint8_t>{ 1, 0, 1, 0, 0, 1, 0, 1 }));
    }

    {
        std::vector<uint8_t> bytes = { 0xFF, 0x00, 0x55 }; // 11111111 00000000 01010101
        std::vector<uint8_t> bits;
        bytes_to_bits(bytes.begin(), bytes.end(), std::back_inserter(bits));
        EXPECT_TRUE(bits == (std::vector<uint8_t> {
            1, 1, 1, 1, 1, 1, 1, 1,  // 0xFF LSB-first
            0, 0, 0, 0, 0, 0, 0, 0,  // 0x00 LSB-first
            1, 0, 1, 0, 1, 0, 1, 0   // 0x55 LSB-first (bit 0, bit 1, bit 2, ...)
        }));
    }
}

TEST(bitstream, bits_to_bytes)
{
LIBMODEM_AX25_USING_NAMESPACE

    {
        std::vector<uint8_t> bits = { 1, 0, 1, 0, 0, 1, 0, 1 }; // 0xA5
        std::vector<uint8_t> bytes;
        bits_to_bytes(bits.begin(), bits.end(), std::back_inserter(bytes));
        EXPECT_TRUE(bytes == (std::vector<uint8_t>{ 0xA5 }));
    }

    {
        std::vector<uint8_t> bits = {
            1, 1, 1, 1, 1, 1, 1, 1,  // 0xFF LSB-first
            0, 0, 0, 0, 0, 0, 0, 0,  // 0x00 LSB-first
            1, 0, 1, 0, 1, 0, 1, 0   // 0x55 LSB-first (bit 0, bit 1, bit 2, ...)
        };
        std::vector<uint8_t> bytes;
        bits_to_bytes(bits.begin(), bits.end(), std::back_inserter(bytes));
        EXPECT_TRUE(bytes == (std::vector<uint8_t>{ 0xFF, 0x00, 0x55 }));
    }
}

TEST(bitstream, add_hdlc_flags)
{
LIBMODEM_AX25_USING_NAMESPACE

    std::vector<uint8_t> buffer(20, 0);
    add_hdlc_flags(buffer.begin(), 2);
    EXPECT_TRUE(buffer == std::vector<uint8_t>({ 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 }));
}

TEST(bitstream, find_first_hdlc_flag)
{
LIBMODEM_AX25_USING_NAMESPACE

    {
        std::vector<uint8_t> bits = { 0,0,0,1,1,1,1,1,1,0,0,0 };
        auto it = find_first_hdlc_flag(bits.begin(), bits.end());
        EXPECT_TRUE(it == bits.begin() + 2);
    }

    {
        std::vector<uint8_t> bits = { 0,1,1,1,1,1,1,0,0,1,1,1,1,1,1,0 };
        auto it = find_first_hdlc_flag(bits.begin(), bits.end());
        EXPECT_TRUE(it == bits.begin());
    }

    {
        std::vector<uint8_t> bits = { 0,0,0,0,0 };
        auto it = find_first_hdlc_flag(bits.begin(), bits.end());
        EXPECT_TRUE(it == bits.end());
    }

    {
        std::vector<uint8_t> bits = { 1,1,1,1,1,1,0 };
        auto it = find_first_hdlc_flag(bits.begin(), bits.end());
        EXPECT_TRUE(it == bits.end());
    }
}

TEST(bitstream, find_last_consecutive_hdlc_flag)
{
LIBMODEM_AX25_USING_NAMESPACE

    {
        std::vector<uint8_t> bits = { 0,0,0,1,1,1,1,1,1,0,0,0 };
        auto it = find_last_consecutive_hdlc_flag(bits.begin(), bits.end());
        EXPECT_TRUE(it == bits.begin() + 2);
    }

    {
        std::vector<uint8_t> bits = { 0,1,1,1,1,1,1,0,0,1,1,1,1,1,1,0 };
        auto it = find_last_consecutive_hdlc_flag(bits.begin(), bits.end());
        EXPECT_TRUE(it == bits.begin() + 8);
    }

    {
        std::vector<uint8_t> bits = { 0,0,0,0,0 };
        auto it = find_last_consecutive_hdlc_flag(bits.begin(), bits.end());
        EXPECT_TRUE(it == bits.end());
    }

    {
        std::vector<uint8_t> bits = { 1,1,1,1,1,1,0 };
        auto it = find_last_consecutive_hdlc_flag(bits.begin(), bits.end());
        EXPECT_TRUE(it == bits.end());
    }
}

TEST(bitstream, ends_with_hdlc_flag)
{
LIBMODEM_AX25_USING_NAMESPACE

    {
        std::vector<uint8_t> bits = { 0,0,0,1,1,1,1,1,1,0,0,0 };
        EXPECT_FALSE(ends_with_hdlc_flag(bits));
    }

    {
        std::vector<uint8_t> bits = { 0,1,1,1,1,1,1,0,0,1,1,1,1,1,1,0 };
        EXPECT_TRUE(ends_with_hdlc_flag(bits));
    }

    {
        std::vector<uint8_t> bits = { 0,0,0,0,0 };
        EXPECT_FALSE(ends_with_hdlc_flag(bits));
    }

    {
        std::vector<uint8_t> bits = { 1,1,1,1,1,1,0 };
        EXPECT_FALSE(ends_with_hdlc_flag(bits));
    }
}

TEST(bitstream, bit_stuff)
{
LIBMODEM_AX25_USING_NAMESPACE

    {
        std::vector<uint8_t> bits = { 1,1,1,1,1,1,0,0,0 }; // 5 consecutive 1s
        std::vector<uint8_t> stuffed;
        bit_stuff(bits.begin(), bits.end(), std::back_inserter(stuffed));
        EXPECT_TRUE(stuffed == (std::vector<uint8_t>{ 1, 1, 1, 1, 1, 0, 1, 0, 0, 0 })); // Stuff a 0 after 5 consecutive 1s
    }

    {
        std::vector<uint8_t> bits = { 1,0,1,1,1,1,1,1,0 }; // 5 consecutive 1s
        std::vector<uint8_t> stuffed;
        bit_stuff(bits.begin(), bits.end(), std::back_inserter(stuffed));
        EXPECT_TRUE(stuffed == (std::vector<uint8_t>{ 1, 0, 1, 1, 1, 1, 1, 0, 1, 0 })); // Stuff a 0 after 5 consecutive 1s
    }

    {
        std::vector<uint8_t> bits = { 0,0,0,0 }; // No 1s
        std::vector<uint8_t> stuffed;
        bit_stuff(bits.begin(), bits.end(), std::back_inserter(stuffed));
        EXPECT_TRUE(stuffed == (std::vector<uint8_t>{ 0, 0, 0, 0 })); // No stuffing needed
    }

    {
        std::vector<uint8_t> bits = { 1,1,1,1,1 }; // 5 consecutive 1s
        std::vector<uint8_t> stuffed;
        bit_stuff(bits.begin(), bits.end(), std::back_inserter(stuffed));
        EXPECT_TRUE(stuffed == (std::vector<uint8_t>{ 1, 1, 1, 1, 1, 0 }));
    }
}

TEST(bitstream, bit_unstuff)
{
LIBMODEM_AX25_USING_NAMESPACE

    {
        std::vector<uint8_t> bits = { 1, 1, 1, 1, 1, 0, 1, 0, 0, 0 };
        std::vector<uint8_t> unstuffed;
        bit_unstuff(bits.begin(), bits.end(), std::back_inserter(unstuffed));
        EXPECT_TRUE(unstuffed == (std::vector<uint8_t>{ 1, 1, 1, 1, 1, 1, 0, 0, 0 }));
    }

    {
        std::vector<uint8_t> bits = { 1, 0, 1, 1, 1, 1, 1, 0, 1, 0 };
        std::vector<uint8_t> unstuffed;
        bit_unstuff(bits.begin(), bits.end(), std::back_inserter(unstuffed));
        EXPECT_TRUE(unstuffed == (std::vector<uint8_t>{ 1, 0, 1, 1, 1, 1, 1, 1, 0 }));
    }

    {
        std::vector<uint8_t> bits = { 0, 0, 0, 0 };
        std::vector<uint8_t> unstuffed;
        bit_unstuff(bits.begin(), bits.end(), std::back_inserter(unstuffed));
        EXPECT_TRUE(unstuffed == (std::vector<uint8_t>{ 0, 0, 0, 0 }));
    }
}

TEST(bitstream, encode_basic_bitstream)
{
LIBMODEM_AX25_USING_NAMESPACE

    // N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!
    aprs::router::packet p = { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS!" };

    std::vector<uint8_t> bitstream = encode_basic_bitstream(p, 1, 1);

    EXPECT_TRUE(bitstream.size() == 368);

    EXPECT_TRUE(bitstream == (std::vector<uint8_t>{
        // Preamble HDLC flag (0x7E)
        1, 1, 1, 1, 1, 1, 1, 0,
        // Destination: APZ001
        1, 1, 0, 1, 0, 1, 0, 0,
        1, 0, 1, 0, 1, 1, 0, 0,
        1, 0, 0, 1, 1, 1, 0, 0,
        1, 0, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 1, 1, 0,
        1, 1, 0, 1, 0, 0, 0, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        // Source: N0CALL-10
        0, 1, 1, 1, 1, 0, 1, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        0, 0, 0, 1, 0, 1, 0, 0,
        1, 1, 0, 1, 0, 1, 0, 0,
        1, 0, 1, 1, 1, 0, 1, 1,
        0, 1, 0, 0, 0, 1, 0, 0,
        1, 0, 0, 1, 1, 1, 1, 0,
        // Path 1: WIDE1-1
        1, 1, 1, 1, 0, 0, 1, 1,
        0, 0, 1, 0, 0, 1, 0, 0,
        1, 0, 1, 1, 0, 1, 0, 0,
        1, 1, 0, 0, 1, 0, 1, 1,
        0, 0, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 0, 0, 1,
        0, 0, 1, 0, 1, 1, 1, 0,
        // Path 2: WIDE2-2
        1, 1, 1, 1, 0, 0, 1, 1,
        0, 0, 1, 0, 0, 1, 0, 0,
        1, 0, 1, 1, 0, 1, 0, 0, 
        1, 1, 0, 0, 1, 0, 1, 1,
        0, 1, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 0, 0, 1,
        1, 0, 0, 1, 0, 0, 0, 1, 
        // Control, PID
        1, 1, 0, 1, 0, 1, 0, 1,        
        0, 1, 0, 1, 1, 1, 1, 1, 
        // Data: "Hello, APRS!"
        0, 1, 0, 0, 1, 0, 0, 1,        
        1, 0, 0, 1, 0, 0, 0, 1, 
        0, 1, 1, 1, 0, 0, 0, 1,
        0, 1, 1, 1, 0, 0, 0, 1, 
        1, 1, 1, 1, 0, 0, 0, 1,
        0, 1, 1, 1, 0, 0, 1, 0,
        1, 0, 1, 0, 1, 1, 0, 1,
        1, 0, 1, 0, 1, 0, 0, 1,
        0, 1, 0, 1, 1, 0, 0, 1,
        0, 0, 1, 0, 0, 1, 1, 0, 
        0, 0, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 1, 0, 0, 1, 0,  
        // CRC (FCS), little-endian
        1, 0, 1, 0, 0, 1, 1, 0,
        0, 0, 1, 1, 1, 1, 1, 0,
        // Postamble HDLC flag (0x7E)
        1, 1, 1, 1, 1, 1, 1, 0,
    }));
}

TEST(bitstream, encode_fx25_bitstream)
{
LIBMODEM_FX25_USING_NAMESPACE

    // N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!
    aprs::router::packet p = { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS!" };

    std::vector<uint8_t> bitstream = encode_fx25_bitstream(p, 1, 1);

    EXPECT_TRUE(bitstream.size() == 720);

    EXPECT_TRUE(bitstream == (std::vector<uint8_t>{
        // Preamble HDLC flag (0x7E)
        1, 1, 1, 1, 1, 1, 1, 0,
        // Correlation Tag Tag_03: RS(80,64) 0xC7DC0508F3D9B09E -- 64 bytes with 16 bytes parity
        1, 1, 1, 1, 1, 0, 1, 1,
        0, 1, 0, 1, 1, 1, 0, 0,
        0, 1, 0, 0, 0, 1, 1, 1,
        1, 1, 0, 1, 1, 1, 1, 1,
        0, 1, 0, 0, 1, 0, 1, 0,
        0, 1, 1, 0, 1, 0, 1, 0,
        1, 0, 0, 0, 0, 1, 1, 1,
        1, 1, 1, 0, 1, 0, 0, 0,
        // Begin AX.25 Frame (46 bytes)
        // Preamble HDLC flag (0x7E)
        1, 1, 1, 1, 1, 1, 1, 0,
        // Destination: APZ001
        1, 1, 0, 1, 0, 1, 0, 0,
        1, 0, 1, 0, 1, 1, 0, 0,
        1, 0, 0, 1, 1, 1, 0, 0,
        1, 0, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 1, 1, 0,
        1, 1, 0, 1, 0, 0, 0, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        // Source: N0CALL-10
        0, 1, 1, 1, 1, 0, 1, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        0, 0, 0, 1, 0, 1, 0, 0,
        1, 1, 0, 1, 0, 1, 0, 0,
        1, 0, 1, 1, 1, 0, 1, 1,
        0, 1, 0, 0, 0, 1, 0, 0,
        1, 0, 0, 1, 1, 1, 1, 0,
        // Path 1: WIDE1-1
        1, 1, 1, 1, 0, 0, 1, 1,
        0, 0, 1, 0, 0, 1, 0, 0,
        1, 0, 1, 1, 0, 1, 0, 0,
        1, 1, 0, 0, 1, 0, 1, 1,
        0, 0, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 0, 0, 1,
        0, 0, 1, 0, 1, 1, 1, 0,
        // Path 2: WIDE2-2
        1, 1, 1, 1, 0, 0, 1, 1,
        0, 0, 1, 0, 0, 1, 0, 0,
        1, 0, 1, 1, 0, 1, 0, 0, 
        1, 1, 0, 0, 1, 0, 1, 1,
        0, 1, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 0, 0, 1,
        1, 0, 0, 1, 0, 0, 0, 1, 
        // Control, PID
        1, 1, 0, 1, 0, 1, 0, 1,        
        0, 1, 0, 1, 1, 1, 1, 1, 
        // Data: "Hello, APRS!"
        0, 1, 0, 0, 1, 0, 0, 1,        
        1, 0, 0, 1, 0, 0, 0, 1, 
        0, 1, 1, 1, 0, 0, 0, 1,
        0, 1, 1, 1, 0, 0, 0, 1, 
        1, 1, 1, 1, 0, 0, 0, 1,
        0, 1, 1, 1, 0, 0, 1, 0,
        1, 0, 1, 0, 1, 1, 0, 1,
        1, 0, 1, 0, 1, 0, 0, 1,
        0, 1, 0, 1, 1, 0, 0, 1,
        0, 0, 1, 0, 0, 1, 1, 0, 
        0, 0, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 1, 0, 0, 1, 0,  
        // CRC (FCS), little-endian
        1, 0, 1, 0, 0, 1, 1, 0,
        0, 0, 1, 1, 1, 1, 1, 0,
        // Postamble HDLC flag (0x7E)
        1, 1, 1, 1, 1, 1, 1, 0,
        // End of AX.25 Frame
        // Padding (18 bytes)
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 1, 1, 0,
        // RS check bytes (16 bytes)
        0, 0, 1, 0, 1, 1, 1, 0,
        0, 1, 1, 1, 0, 1, 1, 0,
        1, 1, 1, 0, 0, 0, 1, 1,
        1, 1, 1, 0, 0, 1, 1, 0,
        1, 0, 1, 0, 0, 0, 0, 0,
        0, 1, 1, 0, 1, 0, 0, 1,
        1, 1, 0, 1, 1, 0, 1, 0,
        0, 0, 0, 1, 0, 1, 0, 1,
        1, 1, 0, 0, 1, 1, 0, 1,
        0, 0, 1, 1, 1, 0, 0, 1,
        0, 0, 1, 1, 1, 0, 0, 1,
        1, 0, 1, 0, 0, 0, 1, 0,
        0, 1, 0, 1, 1, 1, 1, 1,
        0, 0, 0, 1, 0, 0, 0, 1,
        0, 0, 0, 1, 0, 1, 1, 1,
        1, 1, 1, 0, 1, 0, 1, 1,
        // Postamble HDLC flag (0x7E)
        0, 0, 0, 0, 0, 0, 0, 1,
    }));
}

TEST(bitstream, encode_end_to_end_demo)
{
LIBMODEM_AX25_USING_NAMESPACE

    aprs::router::packet p = { "W7ION-5", "T7SVVQ", { "WIDE1-1", "WIDE2-1" }, R"(`2(al"|[/>"3u}hello world^)" };

    std::vector<uint8_t> frame = encode_frame(p);

    EXPECT_TRUE(frame == std::vector<uint8_t>({
        // Destination: T7SVVQ
        0xA8, 0x6E, 0xA6, 0xAC, 0xAC, 0xA2, 0x60,
        // Source: W7ION-5
        0xAE, 0x6E, 0x92, 0x9E, 0x9C, 0x40, 0x6A,
        // Path 1: WIDE1-1
        0xAE, 0x92, 0x88, 0x8A, 0x62, 0x40, 0x62,
        // Path 2: WIDE2-1 (last addr)
        0xAE, 0x92, 0x88, 0x8A, 0x64, 0x40, 0x63,
        // Control, PID
        0x03, 0xF0,
        // Payload: `2(al"|[/>"3u}hello world^)
        0x60, 0x32, 0x28, 0x61, 0x6C, 0x22, 0x7C, 0x5B, 0x2F, 0x3E, 0x22, 0x33, 0x75, 0x7D, 0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64, 0x5E,
        // FCS
        0x99, 0x3C
    })); 

    std::vector<uint8_t> frame_bits_lsb;

    bytes_to_bits(frame.begin(), frame.end(), std::back_inserter(frame_bits_lsb));

    EXPECT_TRUE(frame_bits_lsb == std::vector<uint8_t>({
        // Destination: T7SVVQ
        0, 0, 0, 1, 0, 1, 0, 1,
        0, 1, 1, 1, 0, 1, 1, 0,
        0, 1, 1, 0, 0, 1, 0, 1,
        0, 0, 1, 1, 0, 1, 0, 1,
        0, 0, 1, 1, 0, 1, 0, 1,
        0, 1, 0, 0, 0, 1, 0, 1,
        0, 0, 0, 0, 0, 1, 1, 0,
        // Source: W7ION-5
        0, 1, 1, 1, 0, 1, 0, 1,
        0, 1, 1, 1, 0, 1, 1, 0,
        0, 1, 0, 0, 1, 0, 0, 1,
        0, 1, 1, 1, 1, 0, 0, 1,
        0, 0, 1, 1, 1, 0, 0, 1,
        0, 0, 0, 0, 0, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 1, 0,
        // Path 1: WIDE1-1
        0, 1, 1, 1, 0, 1, 0, 1,
        0, 1, 0, 0, 1, 0, 0, 1,
        0, 0, 0, 1, 0, 0, 0, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        0, 1, 0, 0, 0, 1, 1, 0,
        0, 0, 0, 0, 0, 0, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0,
        // Path 2: WIDE2-1
        0, 1, 1, 1, 0, 1, 0, 1,
        0, 1, 0, 0, 1, 0, 0, 1,
        0, 0, 0, 1, 0, 0, 0, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        0, 0, 1, 0, 0, 1, 1, 0,
        0, 0, 0, 0, 0, 0, 1, 0,
        1, 1, 0, 0, 0, 1, 1, 0,
        // Control, PID
        1, 1, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 1, 1, 1, 1,
        // Data: `2(al"|[/>"3u}hello world^)
        0, 0, 0, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 1, 1, 0, 0,
        0, 0, 0, 1, 0, 1, 0, 0,
        1, 0, 0, 0, 0, 1, 1, 0,
        0, 0, 1, 1, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 0, 0,
        0, 0, 1, 1, 1, 1, 1, 0,
        1, 1, 0, 1, 1, 0, 1, 0,
        1, 1, 1, 1, 0, 1, 0, 0,
        0, 1, 1, 1, 1, 1, 0, 0,
        0, 1, 0, 0, 0, 1, 0, 0,
        1, 1, 0, 0, 1, 1, 0, 0,
        1, 0, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 1, 1, 1, 1, 0,
        0, 0, 0, 1, 0, 1, 1, 0,
        1, 0, 1, 0, 0, 1, 1, 0,
        0, 0, 1, 1, 0, 1, 1, 0,
        0, 0, 1, 1, 0, 1, 1, 0,
        1, 1, 1, 1, 0, 1, 1, 0,
        0, 0, 0, 0, 0, 1, 0, 0,
        1, 1, 1, 0, 1, 1, 1, 0,
        1, 1, 1, 1, 0, 1, 1, 0,
        0, 1, 0, 0, 1, 1, 1, 0,
        0, 0, 1, 1, 0, 1, 1, 0,
        0, 0, 1, 0, 0, 1, 1, 0,
        0, 1, 1, 1, 1, 0, 1, 0,
        // CRC (FCS), little-endian
        1, 0, 0, 1, 1, 0, 0, 1,
        0, 0, 1, 1, 1, 1, 0, 0,
    }));

    std::vector<uint8_t> frame_bits_bit_stuffed;

    bit_stuff(frame_bits_lsb.begin(), frame_bits_lsb.end(), std::back_inserter(frame_bits_bit_stuffed));

    EXPECT_TRUE(frame_bits_bit_stuffed == std::vector<uint8_t>({
        // Destination: T7SVVQ
        0, 0, 0, 1, 0, 1, 0, 1,
        0, 1, 1, 1, 0, 1, 1, 0,
        0, 1, 1, 0, 0, 1, 0, 1,
        0, 0, 1, 1, 0, 1, 0, 1,
        0, 0, 1, 1, 0, 1, 0, 1,
        0, 1, 0, 0, 0, 1, 0, 1,
        0, 0, 0, 0, 0, 1, 1, 0,
        // Source: W7ION-5
        0, 1, 1, 1, 0, 1, 0, 1,
        0, 1, 1, 1, 0, 1, 1, 0,
        0, 1, 0, 0, 1, 0, 0, 1,
        0, 1, 1, 1, 1, 0, 0, 1,
        0, 0, 1, 1, 1, 0, 0, 1,
        0, 0, 0, 0, 0, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 1, 0,
        // Path 1: WIDE1-1
        0, 1, 1, 1, 0, 1, 0, 1,
        0, 1, 0, 0, 1, 0, 0, 1,
        0, 0, 0, 1, 0, 0, 0, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        0, 1, 0, 0, 0, 1, 1, 0,
        0, 0, 0, 0, 0, 0, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0,
        // Path 2: WIDE2-1
        0, 1, 1, 1, 0, 1, 0, 1,
        0, 1, 0, 0, 1, 0, 0, 1,
        0, 0, 0, 1, 0, 0, 0, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        0, 0, 1, 0, 0, 1, 1, 0,
        0, 0, 0, 0, 0, 0, 1, 0,
        1, 1, 0, 0, 0, 1, 1, 0,
        // Control, PID
        1, 1, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 1, 1, 1, 1,
        // Data: `2(al"|[/>"3u}hello world^)
        0, 0, 0, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 1, 1, 0, 0,
        0, 0, 0, 1, 0, 1, 0, 0,
        1, 0, 0, 0, 0, 1, 1, 0,
        0, 0, 1, 1, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 0, 0,
        0, 0, 1, 1, 1, 1, 1, 0, 0,
        //    ~~~~~~~~~~~~~~~~
        //    bit stuffed
        1, 1, 0, 1, 1, 0, 1, 0,
        1, 1, 1, 1, 0, 1, 0, 0,
        0, 1, 1, 1, 1, 1, 0, 0, 0,
        // ~~~~~~~~~~~~~~~~
        // bit stuffed
        0, 1, 0, 0, 0, 1, 0, 0,
        1, 1, 0, 0, 1, 1, 0, 0,
        1, 0, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 1, 1, 1, 1, 0, 0,
        //    ~~~~~~~~~~~~~~~~
        //     bit stuffed
        0, 0, 0, 1, 0, 1, 1, 0,
        1, 0, 1, 0, 0, 1, 1, 0,
        0, 0, 1, 1, 0, 1, 1, 0,
        0, 0, 1, 1, 0, 1, 1, 0,
        1, 1, 1, 1, 0, 1, 1, 0,
        0, 0, 0, 0, 0, 1, 0, 0,
        1, 1, 1, 0, 1, 1, 1, 0,
        1, 1, 1, 1, 0, 1, 1, 0,
        0, 1, 0, 0, 1, 1, 1, 0,
        0, 0, 1, 1, 0, 1, 1, 0,
        0, 0, 1, 0, 0, 1, 1, 0,
        0, 1, 1, 1, 1, 0, 1, 0,
        // CRC (FCS), little-endian
        1, 0, 0, 1, 1, 0, 0, 1,
        0, 0, 1, 1, 1, 1, 0, 0,
    }));

    std::vector<uint8_t> frame_bits_bit_stuffed_with_hdlc;

    add_hdlc_flags(std::back_inserter(frame_bits_bit_stuffed_with_hdlc), 1);

    frame_bits_bit_stuffed_with_hdlc.insert(frame_bits_bit_stuffed_with_hdlc.end(), frame_bits_bit_stuffed.begin(), frame_bits_bit_stuffed.end());

    add_hdlc_flags(std::back_inserter(frame_bits_bit_stuffed_with_hdlc), 1);

    EXPECT_TRUE(frame_bits_bit_stuffed_with_hdlc == std::vector<uint8_t>({
        // HDLC flag
        0, 1, 1, 1, 1, 1, 1, 0,
        // Destination: T7SVVQ
        0, 0, 0, 1, 0, 1, 0, 1,
        0, 1, 1, 1, 0, 1, 1, 0,
        0, 1, 1, 0, 0, 1, 0, 1,
        0, 0, 1, 1, 0, 1, 0, 1,
        0, 0, 1, 1, 0, 1, 0, 1,
        0, 1, 0, 0, 0, 1, 0, 1,
        0, 0, 0, 0, 0, 1, 1, 0,
        // Source: W7ION-5
        0, 1, 1, 1, 0, 1, 0, 1,
        0, 1, 1, 1, 0, 1, 1, 0,
        0, 1, 0, 0, 1, 0, 0, 1,
        0, 1, 1, 1, 1, 0, 0, 1,
        0, 0, 1, 1, 1, 0, 0, 1,
        0, 0, 0, 0, 0, 0, 1, 0,
        0, 1, 0, 1, 0, 1, 1, 0,
        // Path 1: WIDE1-1
        0, 1, 1, 1, 0, 1, 0, 1,
        0, 1, 0, 0, 1, 0, 0, 1,
        0, 0, 0, 1, 0, 0, 0, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        0, 1, 0, 0, 0, 1, 1, 0,
        0, 0, 0, 0, 0, 0, 1, 0,
        0, 1, 0, 0, 0, 1, 1, 0,
        // Path 2: WIDE2-1
        0, 1, 1, 1, 0, 1, 0, 1,
        0, 1, 0, 0, 1, 0, 0, 1,
        0, 0, 0, 1, 0, 0, 0, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        0, 0, 1, 0, 0, 1, 1, 0,
        0, 0, 0, 0, 0, 0, 1, 0,
        1, 1, 0, 0, 0, 1, 1, 0,
        // Control, PID
        1, 1, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 1, 1, 1, 1,
        // Data: `2(al"|[/>"3u}hello world^)
        0, 0, 0, 0, 0, 1, 1, 0,
        0, 1, 0, 0, 1, 1, 0, 0,
        0, 0, 0, 1, 0, 1, 0, 0,
        1, 0, 0, 0, 0, 1, 1, 0,
        0, 0, 1, 1, 0, 1, 1, 0,
        0, 1, 0, 0, 0, 1, 0, 0,
        0, 0, 1, 1, 1, 1, 1, 0, 0,
        //    ~~~~~~~~~~~~~~~~
        //    bit stuffed
        1, 1, 0, 1, 1, 0, 1, 0,
        1, 1, 1, 1, 0, 1, 0, 0,
        0, 1, 1, 1, 1, 1, 0, 0, 0,
        // ~~~~~~~~~~~~~~~~
        // bit stuffed
        0, 1, 0, 0, 0, 1, 0, 0,
        1, 1, 0, 0, 1, 1, 0, 0,
        1, 0, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 1, 1, 1, 1, 0, 0,
        //    ~~~~~~~~~~~~~~~~
        //     bit stuffed
        0, 0, 0, 1, 0, 1, 1, 0,
        1, 0, 1, 0, 0, 1, 1, 0,
        0, 0, 1, 1, 0, 1, 1, 0,
        0, 0, 1, 1, 0, 1, 1, 0,
        1, 1, 1, 1, 0, 1, 1, 0,
        0, 0, 0, 0, 0, 1, 0, 0,
        1, 1, 1, 0, 1, 1, 1, 0,
        1, 1, 1, 1, 0, 1, 1, 0,
        0, 1, 0, 0, 1, 1, 1, 0,
        0, 0, 1, 1, 0, 1, 1, 0,
        0, 0, 1, 0, 0, 1, 1, 0,
        0, 1, 1, 1, 1, 0, 1, 0,
        // CRC (FCS), little-endian
        1, 0, 0, 1, 1, 0, 0, 1,
        0, 0, 1, 1, 1, 1, 0, 0,
        // HDLC flag
        0, 1, 1, 1, 1, 1, 1, 0
    }));

    std::vector<uint8_t> bitstream = frame_bits_bit_stuffed_with_hdlc;

    nrzi_encode(bitstream.begin(), bitstream.end());

    EXPECT_TRUE(bitstream == std::vector<uint8_t>({
        // HDLC flag
        1, 1, 1, 1, 1, 1, 1, 0,
        // Destination: T7SVVQ
        1, 0, 1, 1, 0, 0, 1, 1,
        0, 0, 0, 0, 1, 1, 1, 0,
        1, 1, 1, 0, 1, 1, 0, 0,
        1, 0, 0, 0, 1, 1, 0, 0,
        1, 0, 0, 0, 1, 1, 0, 0,
        1, 1, 0, 1, 0, 0, 1, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        // Source: W7ION-5
        0, 0, 0, 0, 1, 1, 0, 0,
        1, 1, 1, 1, 0, 0, 0, 1,
        0, 0, 1, 0, 0, 1, 0, 0,
        1, 1, 1, 1, 1, 0, 1, 1,
        0, 1, 1, 1, 1, 0, 1, 1,
        0, 1, 0, 1, 0, 1, 1, 0,
        1, 1, 0, 0, 1, 1, 1, 0,
        // Path 1: WIDE1-1
        1, 1, 1, 1, 0, 0, 1, 1,
        0, 0, 1, 0, 0, 1, 0, 0,
        1, 0, 1, 1, 0, 1, 0, 0,
        1, 1, 0, 0, 1, 0, 1, 1,
        0, 0, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 0, 0, 1,
        0, 0, 1, 0, 1, 1, 1, 0,
        // Path 2: WIDE2-1
        1, 1, 1, 1, 0, 0, 1, 1,
        0, 0, 1, 0, 0, 1, 0, 0,
        1, 0, 1, 1, 0, 1, 0, 0,
        1, 1, 0, 0, 1, 0, 1, 1,
        0, 1, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 0, 0, 1,
        1, 1, 0, 1, 0, 0, 0, 1,
        // Control, PID
        1, 1, 0, 1, 0, 1, 0, 1,
        0, 1, 0, 1, 1, 1, 1, 1,
        // Data: `2(al"|[/>"3u}hello world^)
        0, 1, 0, 1, 0, 0, 0, 1,
        0, 0, 1, 0, 0, 0, 1, 0,
        1, 0, 1, 1, 0, 0, 1, 0,
        0, 1, 0, 1, 0, 0, 0, 1,
        0, 1, 1, 1, 0, 0, 0, 1,
        0, 0, 1, 0, 1, 1, 0, 1,
        0, 1, 1, 1, 1, 1, 1, 0, 1,
        //    ~~~~~~~~~~~~~~~~
        //    bit stuffed
        1, 1, 0, 0, 0, 1, 1, 0,
        0, 0, 0, 0, 1, 1, 0, 1,
        0, 0, 0, 0, 0, 0, 1, 0, 1,
        // ~~~~~~~~~~~~~~~~
        // bit stuffed
        0, 0, 1, 0, 1, 1, 0, 1,
        1, 1, 0, 1, 1, 1, 0, 1,
        1, 0, 0, 1, 1, 1, 1, 0,
        0, 1, 1, 1, 1, 1, 1, 0, 1,
        //    ~~~~~~~~~~~~~~~~
        //     bit stuffed
        0, 1, 0, 0, 1, 1, 1, 0,
        0, 1, 1, 0, 1, 1, 1, 0,
        1, 0, 0, 0, 1, 1, 1, 0,
        1, 0, 0, 0, 1, 1, 1, 0,
        0, 0, 0, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 1, 0, 1,
        1, 1, 1, 0, 0, 0, 0, 1,
        1, 1, 1, 1, 0, 0, 0, 1,
        0, 0, 1, 0, 0, 0, 0, 1,
        0, 1, 1, 1, 0, 0, 0, 1,
        0, 1, 1, 0, 1, 1, 1, 0,
        1, 1, 1, 1, 1, 0, 0, 1,
        // CRC (FCS), little-endian
        1, 0, 1, 1, 1, 0, 1, 1,
        0, 1, 1, 1, 1, 1, 0, 1,
        // HDLC flag
        0, 0, 0, 0, 0, 0, 0, 1
    }));
}

TEST(bitstream, try_decode_basic_bitstream)
{
LIBMODEM_AX25_USING_NAMESPACE

    std::vector<uint8_t> bitstream = {
        // Preamble HDLC flag (0x7E)
        1, 1, 1, 1, 1, 1, 1, 0,
        // Destination: APZ001
        1, 1, 0, 1, 0, 1, 0, 0,
        1, 0, 1, 0, 1, 1, 0, 0,
        1, 0, 0, 1, 1, 1, 0, 0,
        1, 0, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 1, 1, 0,
        1, 1, 0, 1, 0, 0, 0, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        // Source: N0CALL-10
        0, 1, 1, 1, 1, 0, 1, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        0, 0, 0, 1, 0, 1, 0, 0,
        1, 1, 0, 1, 0, 1, 0, 0,
        1, 0, 1, 1, 1, 0, 1, 1,
        0, 1, 0, 0, 0, 1, 0, 0,
        1, 0, 0, 1, 1, 1, 1, 0,
        // Path 1: WIDE1-1
        1, 1, 1, 1, 0, 0, 1, 1,
        0, 0, 1, 0, 0, 1, 0, 0,
        1, 0, 1, 1, 0, 1, 0, 0,
        1, 1, 0, 0, 1, 0, 1, 1,
        0, 0, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 0, 0, 1,
        0, 0, 1, 0, 1, 1, 1, 0,
        // Path 2: WIDE2-2
        1, 1, 1, 1, 0, 0, 1, 1,
        0, 0, 1, 0, 0, 1, 0, 0,
        1, 0, 1, 1, 0, 1, 0, 0,
        1, 1, 0, 0, 1, 0, 1, 1,
        0, 1, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 0, 0, 1,
        1, 0, 0, 1, 0, 0, 0, 1,
        // Control, PID
        1, 1, 0, 1, 0, 1, 0, 1,
        0, 1, 0, 1, 1, 1, 1, 1,
        // Data: "Hello, APRS!"
        0, 1, 0, 0, 1, 0, 0, 1,
        1, 0, 0, 1, 0, 0, 0, 1,
        0, 1, 1, 1, 0, 0, 0, 1,
        0, 1, 1, 1, 0, 0, 0, 1,
        1, 1, 1, 1, 0, 0, 0, 1,
        0, 1, 1, 1, 0, 0, 1, 0,
        1, 0, 1, 0, 1, 1, 0, 1,
        1, 0, 1, 0, 1, 0, 0, 1,
        0, 1, 0, 1, 1, 0, 0, 1,
        0, 0, 1, 0, 0, 1, 1, 0,
        0, 0, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 1, 0, 0, 1, 0,
        // CRC (FCS), little-endian
        1, 0, 1, 0, 0, 1, 1, 0,
        0, 0, 1, 1, 1, 1, 1, 0,
        // Postamble HDLC flag (0x7E)
        1, 1, 1, 1, 1, 1, 1, 0,
    };

    aprs::router::packet p;
    bitstream_state state;

    for (uint8_t bit : bitstream)
    {
        if (try_decode_basic_bitstream(bit, p, state))
        {
            break;
        }
    }

    EXPECT_TRUE(to_string(p) == "N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!");
}

TEST(bitstream, try_decode_basic_bitstream_with_offset)
{
LIBMODEM_AX25_USING_NAMESPACE

    std::vector<uint8_t> bitstream = {
        // Preamble HDLC flag (0x7E)
        1, 1, 1, 1, 1, 1, 1, 0,
        // Destination: APZ001
        1, 1, 0, 1, 0, 1, 0, 0,
        1, 0, 1, 0, 1, 1, 0, 0,
        1, 0, 0, 1, 1, 1, 0, 0,
        1, 0, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 1, 1, 0,
        1, 1, 0, 1, 0, 0, 0, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        // Source: N0CALL-10
        0, 1, 1, 1, 1, 0, 1, 1,
        0, 1, 0, 1, 0, 0, 0, 1,
        0, 0, 0, 1, 0, 1, 0, 0,
        1, 1, 0, 1, 0, 1, 0, 0,
        1, 0, 1, 1, 1, 0, 1, 1,
        0, 1, 0, 0, 0, 1, 0, 0,
        1, 0, 0, 1, 1, 1, 1, 0,
        // Path 1: WIDE1-1
        1, 1, 1, 1, 0, 0, 1, 1,
        0, 0, 1, 0, 0, 1, 0, 0,
        1, 0, 1, 1, 0, 1, 0, 0,
        1, 1, 0, 0, 1, 0, 1, 1,
        0, 0, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 0, 0, 1,
        0, 0, 1, 0, 1, 1, 1, 0,
        // Path 2: WIDE2-2
        1, 1, 1, 1, 0, 0, 1, 1,
        0, 0, 1, 0, 0, 1, 0, 0,
        1, 0, 1, 1, 0, 1, 0, 0,
        1, 1, 0, 0, 1, 0, 1, 1,
        0, 1, 1, 0, 1, 1, 1, 0,
        1, 0, 1, 0, 1, 0, 0, 1,
        1, 0, 0, 1, 0, 0, 0, 1,
        // Control, PID
        1, 1, 0, 1, 0, 1, 0, 1,
        0, 1, 0, 1, 1, 1, 1, 1,
        // Data: "Hello, APRS!"
        0, 1, 0, 0, 1, 0, 0, 1,
        1, 0, 0, 1, 0, 0, 0, 1,
        0, 1, 1, 1, 0, 0, 0, 1,
        0, 1, 1, 1, 0, 0, 0, 1,
        1, 1, 1, 1, 0, 0, 0, 1,
        0, 1, 1, 1, 0, 0, 1, 0,
        1, 0, 1, 0, 1, 1, 0, 1,
        1, 0, 1, 0, 1, 0, 0, 1,
        0, 1, 0, 1, 1, 0, 0, 1,
        0, 0, 1, 0, 0, 1, 1, 0,
        0, 0, 1, 0, 0, 1, 1, 0,
        0, 1, 0, 1, 0, 0, 1, 0,
        // CRC (FCS), little-endian
        1, 0, 1, 0, 0, 1, 1, 0,
        0, 0, 1, 1, 1, 1, 1, 0,
        // Postamble HDLC flag (0x7E)
        1, 1, 1, 1, 1, 1, 1, 0,
    };

    aprs::router::packet p;

    size_t read = 0;
    bitstream_state state;
    EXPECT_TRUE(try_decode_basic_bitstream(bitstream, 0, p, read, state));

    // The entire bitstream should be consumed
    EXPECT_TRUE(read == bitstream.size());

    EXPECT_TRUE(to_string(p) == "N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!");
}

TEST(bitstream, try_decode_basic_bitstream_1005_random_feed)
{
LIBMODEM_AX25_USING_NAMESPACE

    std::ifstream file("bitstream_1.txt");
    if (!file.is_open())
    {
        FAIL() << "Failed to open bitstream.txt";
    }

    std::vector<uint8_t> bitstream;
    char c;
    while (file.get(c))
    {
        if (c == '0') bitstream.push_back(0);
        else if (c == '1') bitstream.push_back(1);
    }

    std::vector<aprs::router::packet> packets;

    std::vector<uint8_t> buffer;
    bitstream_state state;

    size_t buffer_offset = 0;

    while (buffer_offset < bitstream.size())
    {
        size_t size = random_size(512);
        buffer.assign(bitstream.begin() + buffer_offset, bitstream.begin() + (std::min)(buffer_offset + size, bitstream.size()));
        buffer_offset += buffer.size();

        size_t offset = 0;
        while (offset < buffer.size())  // Inner loop to get ALL packets from this chunk
        {
            size_t read = 0;
            aprs::router::packet packet;
            if (try_decode_basic_bitstream(buffer, offset, packet, read, state))
            {
                packets.push_back(packet);
            }

            if (read == 0)
            {
                break;
            }

            offset += read;
        }
    }

    EXPECT_EQ(packets.size(), 1005);
}

TEST(bitstream, try_decode_basic_bitstream_1005)
{
LIBMODEM_AX25_USING_NAMESPACE

    std::ifstream file("bitstream_1.txt");
    if (!file.is_open())
    {
        FAIL() << "Failed to open bitstream_1.txt";
    }

    std::vector<uint8_t> bitstream;
    char c;
    while (file.get(c))
    {
        if (c == '0') bitstream.push_back(0);
        else if (c == '1') bitstream.push_back(1);
    }

    {

        // Decode the entire bitstream into packets and expected 1005 packets

        std::vector<aprs::router::packet> packets;

        bitstream_state state;

        for (uint8_t bit : bitstream)
        {
            aprs::router::packet packet;
            if (try_decode_basic_bitstream(bit, packet, state))
            {
                EXPECT_TRUE(state.complete == true);

                packets.push_back(packet);
            }
        }

        EXPECT_TRUE(packets.size() == 1005);
    }

    {
        // Decoding frames and converting to packets explicitly

        std::vector<aprs::router::packet> packets;

        bitstream_state state;

        for (uint8_t bit : bitstream)
        {
            aprs::router::packet packet;
            if (try_decode_basic_bitstream(bit, state))
            {
                packet = to_packet(state.frame);
                packets.push_back(packet);
            }
        }

        EXPECT_TRUE(packets.size() == 1005);
    }

    {
        // Decoding and comparing to expected packets from file

        std::ifstream file("packets_1d.txt");
        if (!file.is_open())
        {
            FAIL() << "Failed to open packets_1d.txt";
        }

        std::vector<std::string> actual_packets;
        std::vector<std::string> expected_packets;

        std::string line;
        while (std::getline(file, line))
        {
            expected_packets.push_back(line);
        }

        bitstream_state state;

        for (uint8_t bit : bitstream)
        {
            aprs::router::packet packet;
            if (try_decode_basic_bitstream(bit, state))
            {
                packet = to_packet(state.frame);
                std::string packet_str = to_string(packet); // packet to string
                packet_str = replace_crlf(packet_str); // replace newlines for printing
                actual_packets.push_back(packet_str);
            }
        }

        EXPECT_TRUE(actual_packets.size() == expected_packets.size());

        for (size_t i = 0; i < (std::min)(actual_packets.size(), expected_packets.size()); i++)
        {
            if (actual_packets[i] != expected_packets[i])
            {
                fmt::println("Packet {} does not match:", i);
                fmt::println("Actual:   {}", actual_packets[i]);
                fmt::println("Expected: {}", expected_packets[i]);
            }
        }

        EXPECT_TRUE(actual_packets == expected_packets);
    }
}

TEST(bitstream, save_to_file_demo)
{
LIBMODEM_AX25_USING_NAMESPACE

    std::ifstream file("bitstream_1.txt");
    if (!file.is_open())
    {
        FAIL() << "Failed to open bitstream_1.txt";
    }

    std::vector<uint8_t> bitstream;
    char c;
    while (file.get(c))
    {
        if (c == '0') bitstream.push_back(0);
        else if (c == '1') bitstream.push_back(1);
    }

    std::vector<std::string> packets;

    bitstream_state state;

    for (uint8_t bit : bitstream)
    {
        if (try_decode_basic_bitstream(bit, state))
        {
            aprs::router::packet packet = to_packet(state.frame);
            std::string packet_str = to_string(packet); // packet to string
            packet_str = replace_crlf(packet_str); // replace newlines for printing
            packets.push_back(packet_str);
        }
    }

    std::ofstream outfile("packets_1d.txt");
    if (!outfile.is_open())
    {
        FAIL() << "Failed to open packets_1d.txt for writing";
    }

    for (const auto& packet_str : packets)
    {
        outfile.write(packet_str.data(), packet_str.size());
        outfile.put('\n');
    }
}

TEST(bitstream, try_decode_basic_bitstream_demo)
{
LIBMODEM_AX25_USING_NAMESPACE

    // Read bitstream from file generated by Direwolf using DEBUG5
    std::ifstream file("bitstream_1.txt");
    if (!file.is_open())
    {
        FAIL() << "Failed to open bitstream.txt";
    }

    std::vector<uint8_t> bitstream;
    char c;
    while (file.get(c))
    {
        if (c == '0') bitstream.push_back(0);
        else if (c == '1') bitstream.push_back(1);
    }

    bitstream_state state;

    for (uint8_t bit : bitstream)
    {
        if (try_decode_basic_bitstream(bit, state)) // AX.25 only
        {
            const frame& f = state.frame; // decoded frame

            fmt::println("from: {}\nto: {}\npath: {}\n{}\ncrc: {}",
                to_string(f.from), // from address to string
                to_string(f.to),   // from address to string
                to_string(f.path), // from vector<address> to string
                to_hex_string(f.data),  // from vector<uint8_t> to hex string
                std::bit_cast<uint16_t>(f.crc));

            // Convert to packet
            aprs::router::packet p = to_packet(f); // frame to packet
            std::string packet_str = to_string(p); // packet to string
            packet_str = replace_crlf(packet_str); // replace newlines for printing
            
            fmt::print("packet: ");
            std::cout.write(packet_str.data(), packet_str.size());
            fmt::print("\n\n");
        }
    }
}

TEST(bitstream, try_decode_basic_bitstream_1005_enable_diagnostics)
{
LIBMODEM_AX25_USING_NAMESPACE

    std::ifstream file("bitstream_1.txt");
    if (!file.is_open())
    {
        FAIL() << "Failed to open bitstream.txt";
    }

    std::vector<uint8_t> bitstream;
    char c;
    while (file.get(c))
    {
        if (c == '0') bitstream.push_back(0);
        else if (c == '1') bitstream.push_back(1);
    }

    std::vector<aprs::router::packet> packets;

    bitstream_state state;

    state.enable_diagnostics = true;

    std::vector<std::vector<uint8_t>> packet_bitstreams;
    std::vector<uint8_t> packet_bitstream_nrzi_levels;

    for (uint8_t bit : bitstream)
    {
        aprs::router::packet packet;
        if (try_decode_basic_bitstream(bit, packet, state))
        {
            std::vector<uint8_t> packet_bitstream;
            // state.start and state.end are 1-based, convert to 0-based for array indexing
            // state.start is first bit of preamble, state.end is last bit of postamble
            packet_bitstream.insert(packet_bitstream.end(),
                bitstream.begin() + (state.frame_start - 1),  // 1-based to 0-based
                bitstream.begin() + state.frame_end);          // end is exclusive in C++
            packet_bitstreams.push_back(packet_bitstream);
            packet_bitstream_nrzi_levels.push_back(state.frame_nrzi_level);  // Use output field

            packets.push_back(packet);
        }
    }

    EXPECT_TRUE(packets.size() == 1005);

    std::vector<aprs::router::packet> diag_packets;

    for (int j = 0; const auto& packet_bitstream : packet_bitstreams)
    {
        bitstream_state packet_state;
        packet_state.last_nrzi_level = packet_bitstream_nrzi_levels[j];
        for (uint8_t bit : packet_bitstream)
        {
            aprs::router::packet packet;
            if (try_decode_basic_bitstream(bit, packet, packet_state))
            {
                diag_packets.push_back(packet);
            }
        }
        j++;
    }

    EXPECT_TRUE(diag_packets.size() == 1005);

    EXPECT_TRUE(packets == diag_packets);
}

TEST(bitstream, try_decode_basic_bitstream_1005_enable_diagnostics_batch)
{
LIBMODEM_AX25_USING_NAMESPACE

    std::ifstream file("bitstream_1.txt");
    if (!file.is_open())
    {
        FAIL() << "Failed to open bitstream.txt";
    }

    std::vector<uint8_t> bitstream;
    char c;
    while (file.get(c))
    {
        if (c == '0') bitstream.push_back(0);
        else if (c == '1') bitstream.push_back(1);
    }

    std::vector<aprs::router::packet> packets;

    bitstream_state state;

    state.enable_diagnostics = true;

    std::vector<std::vector<uint8_t>> packet_bitstreams;
    std::vector<uint8_t> packet_bitstream_nrzi_levels;

    std::vector<uint8_t> buffer;

    for (uint8_t bit : bitstream)
    {
        buffer.push_back(bit);

        aprs::router::packet packet;
        if (try_decode_basic_bitstream(bit, packet, state))
        {
            packets.push_back(packet);

            size_t frame_total_bits = state.frame_end - state.frame_start + 1;

            std::vector<uint8_t> packet_bitstream(buffer.end() - frame_total_bits, buffer.end());            
            packet_bitstreams.push_back(packet_bitstream);

            packet_bitstream_nrzi_levels.push_back(state.frame_nrzi_level);

            buffer.erase(buffer.begin(), buffer.end() - 8);
        }
    }

    EXPECT_TRUE(packets.size() == 1005);

    std::vector<aprs::router::packet> diag_packets;

    for (int j = 0; const auto& packet_bitstream : packet_bitstreams)
    {
        bitstream_state packet_state;
        packet_state.last_nrzi_level = packet_bitstream_nrzi_levels[j];
        for (uint8_t bit : packet_bitstream)
        {
            aprs::router::packet packet;
            if (try_decode_basic_bitstream(bit, packet, packet_state))
            {
                diag_packets.push_back(packet);
            }
        }
        j++;
    }

    EXPECT_TRUE(diag_packets.size() == 1005);

    EXPECT_TRUE(packets == diag_packets);
}

TEST(bitstream, try_decode_basic_bitstream_shared_preamble_postamble)
{
LIBMODEM_AX25_USING_NAMESPACE

    aprs::router::packet p1 = { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS!" };
    aprs::router::packet p2 = { "N0CALL-11", "APZ002", { "WIDE1-1", "WIDE2-2" }, "Another test!" };
    aprs::router::packet p3 = { "N0CALL-12", "APZ003", { "WIDE1-1", "WIDE2-2" }, "Yet another packet." };
    aprs::router::packet p4 = { "N0CALL-13", "APZ004", { "WIDE1-1", "WIDE2-2" }, "Packet." };
    aprs::router::packet p5 = { "N0CALL-14", "APZ005", { "WIDE1-1", "WIDE2-2" }, "A packet." };
    aprs::router::packet p6 = { "N0CALL-15", "APZ006", { "WIDE1-1", "WIDE2-2" }, "0 packet!" };
    aprs::router::packet p7 = { "N0CALL-15", "APZ007", { "WIDE1-1", "WIDE2-2" }, "Final packet." };

    std::vector<uint8_t> frame1 = encode_frame(p1);
    std::vector<uint8_t> frame2 = encode_frame(p2);
    std::vector<uint8_t> frame3 = encode_frame(p3);
    std::vector<uint8_t> frame4 = encode_frame(p4);
    std::vector<uint8_t> frame5 = encode_frame(p5);
    std::vector<uint8_t> frame6 = encode_frame(p6);
    std::vector<uint8_t> frame7 = encode_frame(p7);

    std::vector<uint8_t> frame1_bits_lsb;
    std::vector<uint8_t> frame2_bits_lsb;
    std::vector<uint8_t> frame3_bits_lsb;
    std::vector<uint8_t> frame4_bits_lsb;
    std::vector<uint8_t> frame5_bits_lsb;
    std::vector<uint8_t> frame6_bits_lsb;
    std::vector<uint8_t> frame7_bits_lsb;

    bytes_to_bits(frame1.begin(), frame1.end(), std::back_inserter(frame1_bits_lsb));
    bytes_to_bits(frame2.begin(), frame2.end(), std::back_inserter(frame2_bits_lsb));
    bytes_to_bits(frame3.begin(), frame3.end(), std::back_inserter(frame3_bits_lsb));
    bytes_to_bits(frame4.begin(), frame4.end(), std::back_inserter(frame4_bits_lsb));
    bytes_to_bits(frame5.begin(), frame5.end(), std::back_inserter(frame5_bits_lsb));
    bytes_to_bits(frame6.begin(), frame6.end(), std::back_inserter(frame6_bits_lsb));
    bytes_to_bits(frame7.begin(), frame7.end(), std::back_inserter(frame7_bits_lsb));

    std::vector<uint8_t> frame1_bits_bit_stuffed;
    std::vector<uint8_t> frame2_bits_bit_stuffed;
    std::vector<uint8_t> frame3_bits_bit_stuffed;
    std::vector<uint8_t> frame4_bits_bit_stuffed;
    std::vector<uint8_t> frame5_bits_bit_stuffed;
    std::vector<uint8_t> frame6_bits_bit_stuffed;
    std::vector<uint8_t> frame7_bits_bit_stuffed;

    bit_stuff(frame1_bits_lsb.begin(), frame1_bits_lsb.end(), std::back_inserter(frame1_bits_bit_stuffed));
    bit_stuff(frame2_bits_lsb.begin(), frame2_bits_lsb.end(), std::back_inserter(frame2_bits_bit_stuffed));
    bit_stuff(frame3_bits_lsb.begin(), frame3_bits_lsb.end(), std::back_inserter(frame3_bits_bit_stuffed));
    bit_stuff(frame4_bits_lsb.begin(), frame4_bits_lsb.end(), std::back_inserter(frame4_bits_bit_stuffed));
    bit_stuff(frame5_bits_lsb.begin(), frame5_bits_lsb.end(), std::back_inserter(frame5_bits_bit_stuffed));
    bit_stuff(frame6_bits_lsb.begin(), frame6_bits_lsb.end(), std::back_inserter(frame6_bits_bit_stuffed));
    bit_stuff(frame7_bits_lsb.begin(), frame7_bits_lsb.end(), std::back_inserter(frame7_bits_bit_stuffed));

    std::vector<uint8_t> combined_bitstream;

    add_hdlc_flags(std::back_inserter(combined_bitstream), 1);

    combined_bitstream.insert(combined_bitstream.end(), frame1_bits_bit_stuffed.begin(), frame1_bits_bit_stuffed.end());

    add_hdlc_flags(std::back_inserter(combined_bitstream), 1);

    combined_bitstream.insert(combined_bitstream.end(), frame2_bits_bit_stuffed.begin(), frame2_bits_bit_stuffed.end());

    add_hdlc_flags(std::back_inserter(combined_bitstream), 1);

    combined_bitstream.insert(combined_bitstream.end(), frame3_bits_bit_stuffed.begin(), frame3_bits_bit_stuffed.end());

    add_hdlc_flags(std::back_inserter(combined_bitstream), 1);

    combined_bitstream.insert(combined_bitstream.end(), frame4_bits_bit_stuffed.begin(), frame4_bits_bit_stuffed.end());

    add_hdlc_flags(std::back_inserter(combined_bitstream), 1);

    combined_bitstream.insert(combined_bitstream.end(), frame5_bits_bit_stuffed.begin(), frame5_bits_bit_stuffed.end());

    add_hdlc_flags(std::back_inserter(combined_bitstream), 45);

    combined_bitstream.insert(combined_bitstream.end(), frame6_bits_bit_stuffed.begin(), frame6_bits_bit_stuffed.end());

    add_hdlc_flags(std::back_inserter(combined_bitstream), 30);

    combined_bitstream.insert(combined_bitstream.end(), frame7_bits_bit_stuffed.begin(), frame7_bits_bit_stuffed.end());

    add_hdlc_flags(std::back_inserter(combined_bitstream), 1);

    nrzi_encode(combined_bitstream.begin(), combined_bitstream.end());

    std::vector<aprs::router::packet> packets;

    bitstream_state state;

    for (uint8_t bit : combined_bitstream)
    {
        aprs::router::packet p;
        if (try_decode_basic_bitstream(bit, p, state))
        {
            packets.push_back(p);
        }
    }

    EXPECT_TRUE(packets.size() == 7);

    EXPECT_TRUE(to_string(packets[0]) == to_string(p1));

    EXPECT_TRUE(to_string(packets[1]) == to_string(p2));

    EXPECT_TRUE(to_string(packets[2]) == to_string(p3));

    EXPECT_TRUE(to_string(packets[3]) == to_string(p4));

    EXPECT_TRUE(to_string(packets[4]) == to_string(p5));

    EXPECT_TRUE(to_string(packets[5]) == to_string(p6));

    EXPECT_TRUE(to_string(packets[6]) == to_string(p7));
}

TEST(bitstream, try_decode_basic_bitstream_heavy_bit_stuffing)
{
LIBMODEM_AX25_USING_NAMESPACE

    aprs::router::packet p1 = { "N0CALL", "APZ001", {}, "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF" };
    aprs::router::packet p2 = { "N0CALL", "APZ001", {}, "\x7E\x7E\x7E\x7E" };

    std::vector<uint8_t> frame1 = encode_frame(p1);
    std::vector<uint8_t> frame2 = encode_frame(p2);

    std::vector<uint8_t> bits1, bits2;
    bytes_to_bits(frame1.begin(), frame1.end(), std::back_inserter(bits1));
    bytes_to_bits(frame2.begin(), frame2.end(), std::back_inserter(bits2));

    std::vector<uint8_t> stuffed1, stuffed2;
    bit_stuff(bits1.begin(), bits1.end(), std::back_inserter(stuffed1));
    bit_stuff(bits2.begin(), bits2.end(), std::back_inserter(stuffed2));

    std::vector<uint8_t> bitstream;
    add_hdlc_flags(std::back_inserter(bitstream), 5);
    bitstream.insert(bitstream.end(), stuffed1.begin(), stuffed1.end());
    add_hdlc_flags(std::back_inserter(bitstream), 1);
    bitstream.insert(bitstream.end(), stuffed2.begin(), stuffed2.end());
    add_hdlc_flags(std::back_inserter(bitstream), 1);

    nrzi_encode(bitstream.begin(), bitstream.end());

    std::vector<aprs::router::packet> packets;
    bitstream_state state;

    for (uint8_t bit : bitstream)
    {
        aprs::router::packet p;
        if (try_decode_basic_bitstream(bit, p, state))
        {
            packets.push_back(p);
        }
    }

    ASSERT_EQ(packets.size(), 2);
    EXPECT_EQ(to_string(packets[0]), to_string(p1));
    EXPECT_EQ(to_string(packets[1]), to_string(p2));
}

TEST(bitstream, encode_fx25_frame_larger_size)
{
LIBMODEM_FX25_USING_NAMESPACE

    {
        aprs::router::packet p = { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS! ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" }; // 239

        std::vector<uint8_t> bitstream = encode_fx25_bitstream(p, 1, 1);

        EXPECT_TRUE(bitstream.size() == 2120);
    }

    {
        aprs::router::packet p = { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS! ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890" }; // 241

        std::vector<uint8_t> bitstream = encode_fx25_bitstream(p, 1, 1);

        EXPECT_TRUE(bitstream.empty());
    }
}

TEST(modem, modulate_afsk_1200_ax25_packet)
{
    aprs::router::packet p = { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS!" };

    {
        dds_afsk_modulator_double_adapter modulator(1200.0, 2200.0, 1200, 48000);
        basic_bitstream_converter_adapter bitstream_converter;
        wav_audio_output_stream wav_stream("test.wav", 48000);        

        modem m;
        m.baud_rate(1200);
        m.tx_delay(300);
        m.tx_tail(45);
        m.gain(0.3);
        m.initialize(wav_stream, modulator, bitstream_converter);

        m.transmit(p);

        wav_stream.close();
    }

    std::string output;
    std::string error;

    // Run Direwolf's ATEST with -B 1200
    run_process(ATEST_EXE_PATH, output, error, "-B 1200", "test.wav");

    // Expect [0] N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!
    EXPECT_TRUE(output.find("[0] " + to_string(p)) != std::string::npos);
}

TEST(modem, modulate_afsk_1200_ax25_packet_sample_rates)
{
    aprs::router::packet p = { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS!" };

    std::vector<int> sample_rates = { 8000, 9600, 44100, 96000, 192000 };

    for (const auto& rate : sample_rates)
    {
        {
            dds_afsk_modulator_double_adapter modulator(1200.0, 2200.0, 1200, rate);
            basic_bitstream_converter_adapter bitstream_converter;
            wav_audio_output_stream wav_stream("test.wav", rate);

            modem m;
            m.baud_rate(1200);
            m.tx_delay(300);
            m.tx_tail(45);
            m.gain(0.3);
            m.initialize(wav_stream, modulator, bitstream_converter);

            m.transmit(p);

            wav_stream.close();
        }

        std::string output;
        std::string error;

        // Run Direwolf's ATEST with -B 1200
        run_process(ATEST_EXE_PATH, output, error, "-B 1200", "test.wav");

        // Expect [0] N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!
        EXPECT_TRUE(output.find("[0] " + to_string(p)) != std::string::npos);
    }
}

TEST(modem, modulate_afsk_1200_fx25_packet_data_lengths)
{
LIBMODEM_FX25_USING_NAMESPACE

    std::vector<aprs::router::packet> packets = {
        { "N0CALL-10", "APZ001", { "WIDE1-1" }, "Hello" }, // 32
        { "N0CALL-10", "APZ001", { "WIDE1-1" }, "Hello" }, // 32
        { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS!" }, //64
        { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS!" }, //64
        { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS!" }, //64
        { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS! ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" }, // 128
        { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS! ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" }, // 128
        { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS! ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" }, // 128
        { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS! ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" }, // 191
        { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS! ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" }, //223
        { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS! ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789" } // 239
    };

    // Corresponding data and check lengths
    // Include all combinations of data and check lengths supported by FX.25
    std::vector<int> data_lengths =  { 32, 32, 64, 64, 64, 128, 128, 128, 191, 223, 239 };
    std::vector<int> check_lengths = { 16, 32, 16, 32, 64, 16,  32,  64,  64,  32,  16  };

    for (int i = 0; const auto& p : packets)
    {
        {
            dds_afsk_modulator_double_adapter modulator(1200.0, 2200.0, 1200, 48000);
            wav_audio_output_stream wav_stream("test.wav", 48000);

            modem m;
            m.baud_rate(1200);
            m.gain(0.3);
            m.initialize(wav_stream, modulator);

            std::vector<uint8_t> bitstream = encode_fx25_bitstream(p, 45, 30, check_lengths[i]);

            m.transmit(bitstream);

            wav_stream.close();
        }

        std::string output;
        std::string error;

        // Run Direwolf's ATEST with -B 1200 -d x
        run_process(ATEST_EXE_PATH, output, error, "-B 1200", "-d x", "test.wav");

        // Expect [0] N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!
        EXPECT_TRUE(output.find("[0] " + to_string(p)) != std::string::npos);
        // Expect FX.25  0 (indicating successful FX.25 decoding)
        EXPECT_TRUE(output.find("FX.25  0") != std::string::npos);
        // Expect "Expecting 239 data & 16 check bytes"
        std::string expected_string = fmt::format("Expecting {} data & {} check bytes", data_lengths[i], check_lengths[i]);
        EXPECT_TRUE(output.find(expected_string) != std::string::npos);

        i++;
    }
}

TEST(modem, modulate_afsk_1200_fx25_packet_with_bit_errors)
{
    aprs::router::packet p = { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS!" };

    {
        std::vector<double> audio_buffer;
     
        fx25_bitstream_converter bitstream_converter;

        std::vector<uint8_t> bitstream = bitstream_converter.encode(p, 45, 30);

        // Introduce some bit errors into the FX.25 bitstream
        for (size_t i = 400; i <= 500; i += 10)
        {
            bitstream[i] ^= 1;
        }
        
        dds_afsk_modulator_double modulator(1200.0, 2200.0, 1200, 48000, 1.0); // Coherent 1200 baud AFSK
        
        for (uint8_t bit : bitstream)
        {
            for (int i = 0; i < modulator.next_samples_per_bit(); ++i)
            {
                audio_buffer.push_back(modulator.modulate(bit));
            }
        }

        apply_gain(audio_buffer.begin(), audio_buffer.end(), 0.3);

        wav_audio_output_stream wav_stream("test.wav", 48000);

        wav_stream.write(audio_buffer.data(), audio_buffer.size());

        wav_stream.close();
    }

    std::string output;
    std::string error;

    // Run Direwolf's ATEST with -B 1200 -d x
    run_process(ATEST_EXE_PATH, output, error, "-B 1200", "-d x", "test.wav");

    // Expect [0] N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!
    EXPECT_TRUE(output.find("[0] " + to_string(p)) != std::string::npos);
    // Expect FX.25  8 (indicating bit errors during FX.25 decoding)
    EXPECT_TRUE(output.find("FX.25  8") != std::string::npos);
    // Expect FEC complete, fixed  8 errors in byte positions: 0 2 3 4 5 7 8 9
    EXPECT_TRUE(output.find("FEC complete, fixed  8 errors in byte positions: 0 2 3 4 5 7 8 9") != std::string::npos);
}

TEST(modem, transmit_demo)
{
APRS_TRACK_NAMESPACE_USE
APRS_TRACK_DETAIL_NAMESPACE_USE

    // N0CALL>T9QPVP,WIDE1-1:`3T{m\\\x1f[/\"4F}
    std::string packet_string = encode_mic_e_packet_no_message("N0CALL", "WIDE1-1", 49.176666666667, -123.94916666667, mic_e_status::in_service, 3, 15.999, '/', '[', 0, 154.2);
    
    EXPECT_TRUE(packet_string == "N0CALL>T9QPVP,WIDE1-1:`3T{m\\\x1f[/\"4F}");

    aprs::router::packet packet = packet_string;

    wav_audio_output_stream wav_stream("test.wav", 48000);
    dds_afsk_modulator_double_adapter modulator(1200.0, 2200.0, 1200, wav_stream.sample_rate());
    basic_bitstream_converter_adapter bitstream_converter;

    modem m;
    m.baud_rate(1200);
    m.tx_delay(300);
    m.tx_tail(45);
    m.start_silence(0.1);
    m.end_silence(0.1);
    m.gain(0.3);
    m.initialize(wav_stream, modulator, bitstream_converter);

    m.transmit(packet);

    wav_stream.close();

    std::string output;
    std::string error;

    // Run Direwolf's ATEST with -B 1200 -d x
    run_process(ATEST_EXE_PATH, output, error, "-B 1200", "-d x", "test.wav");

    // Expect [0] N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!
    EXPECT_TRUE(output.find("[0] " + replace_non_printable(to_string(packet))) != std::string::npos);
}

TEST(dds_afsk_modulator, samples_per_bit)
{
    aprs::router::packet p = { "N0CALL-10", "APZ001", { "WIDE1-1", "WIDE2-2" }, "Hello, APRS!" };

    {
        // The next_samples_per_bit function should be constant when the sample rate is an integer multiple of the baud rate

        basic_bitstream_converter bitstream_converter;

        std::vector<uint8_t> bitstream = bitstream_converter.encode(p, 1, 1);

        int total_samples = 0;

        int sample_rate = 48000;
        int baud_rate = 1200;

        dds_afsk_modulator_double modulator(1200.0, 2200.0, baud_rate, sample_rate, 1.0);

        int samples_per_bit = sample_rate / baud_rate;

        for (uint8_t bit : bitstream)
        {
            EXPECT_TRUE(modulator.next_samples_per_bit() == samples_per_bit);

            for (int i = 0; i < modulator.next_samples_per_bit(); ++i)
            {
                EXPECT_TRUE(modulator.next_samples_per_bit() == samples_per_bit);

                modulator.modulate(bit);

                total_samples++;
            }
        }

        EXPECT_TRUE(total_samples == bitstream.size() * samples_per_bit);
    }

    {
        std::vector<double> audio_buffer;

        basic_bitstream_converter bitstream_converter;

        std::vector<uint8_t> bitstream = bitstream_converter.encode(p, 45, 30);

        int sample_rate = 44100;
        int baud_rate = 1200;

        dds_afsk_modulator_double modulator(1200.0, 2200.0, baud_rate, sample_rate, 1.0);

        double samples_per_bit_fixed = static_cast<double>(sample_rate) / baud_rate;
        std::vector<int> samples_per_bit_values;

        for (uint8_t bit : bitstream)
        {
            int samples_per_bit = modulator.next_samples_per_bit();

            EXPECT_TRUE(samples_per_bit == 36 || samples_per_bit == 37);

            samples_per_bit_values.push_back(samples_per_bit);

            for (int i = 0; i < samples_per_bit; ++i)
            {
                audio_buffer.push_back(modulator.modulate(bit));
            }
        }

        int min_samples_per_bit = *std::min_element(samples_per_bit_values.begin(), samples_per_bit_values.end());
        int max_samples_per_bit = *std::max_element(samples_per_bit_values.begin(), samples_per_bit_values.end());

        EXPECT_TRUE(min_samples_per_bit == 36);
        EXPECT_TRUE(max_samples_per_bit == 37);

        size_t total_samples = audio_buffer.size();
        size_t total_bits = bitstream.size();

        double effective_baud = static_cast<double>(total_bits) * static_cast<double>(sample_rate) / static_cast<double>(total_samples);

        EXPECT_NEAR(effective_baud, static_cast<double>(baud_rate), 1e-6);

        apply_gain(audio_buffer.begin(), audio_buffer.end(), 0.3);

        wav_audio_output_stream wav_stream("test.wav", 48000);

        wav_stream.write(audio_buffer.data(), audio_buffer.size());

        wav_stream.close();
    }
}

TEST(dds_afsk_modulator, afsk_1200_frequency_accuracy)
{
    const std::vector<std::pair<int, double>> test_cases = {
       {1, 1200.0},  // mark
       {0, 2200.0}   // space
    };

    for (const auto& [bit, expected_freq] : test_cases)
    {
        std::vector<double> audio_buffer;

        std::vector<uint8_t> bitstream = std::vector<uint8_t>(10000, bit);

        dds_afsk_modulator_double modulator(1200.0, 2200.0, 1200, 48000, 1.0);

        for (uint8_t bit : bitstream)
        {
            for (int i = 0; i < modulator.next_samples_per_bit(); ++i)
            {
                audio_buffer.push_back(modulator.modulate(bit));
            }
        }

        apply_gain(audio_buffer.begin(), audio_buffer.end(), 0.3);

        wav_audio_output_stream wav_stream("test.wav", 48000);

        wav_stream.write(audio_buffer.data(), audio_buffer.size());

        wav_stream.close();

        std::vector<fft_bin> fft_bins = compute_fft("test.wav");

        fft_bin dominant_frequency_bin = dominant_frequency(fft_bins);

        EXPECT_NEAR(dominant_frequency_bin.frequency, expected_freq, 0.1f);

        std::vector<fft_bin> significant_frequencies = frequencies_above_threshold(fft_bins, dominant_frequency_bin.magnitude * 0.01); // 1% of dominant frequency as threshold

        // If there is only one significant frequency, it should be the dominant frequency
        // If there are multiple significant frequencies, they should form a sinc pattern around the dominant frequency

        if (significant_frequencies.size() == 1)
        {
            EXPECT_TRUE(significant_frequencies.size() == 1);
            EXPECT_TRUE(significant_frequencies[0].frequency == dominant_frequency_bin.frequency);
        }
        else
        {
            for (const auto& bin : significant_frequencies)
            {
                // All significant energy should be within ±100 Hz of expected frequency
                EXPECT_NEAR(bin.frequency, expected_freq, 100.0);

                double distance = std::abs(bin.frequency - expected_freq);
                if (distance > 0.5)  // skip the peak itself at around ±0.5 Hz
                {
                    
                    double ratio = bin.magnitude / dominant_frequency_bin.magnitude; // 0.0 to 1.0, or 0% to 100% normalized
                    double ratio_percent = ratio * 100.0;  // now 0-100%

                    // Just verify it's decreasing - bins further away should be smaller
                    // The sinc pattern means ~1/distance relationship approximately
                    // The magnitude ratio should be less than 1/distance plus a 10% margin
                    EXPECT_LT(ratio_percent, 100.0 / distance + 10.0);  // rough upper bound
                }
            }
        }
    }
}

TEST(dds_afsk_modulator, afsk_1200_phase_continuity)
{
    dds_afsk_modulator_double modulator(1200.0, 2200.0, 1200, 48000, 1.0);

    std::vector<double> audio_buffer;

    // Generate alternating bits to force frequency transitions
    std::vector<uint8_t> bitstream = generate_random_bits(10000);

    for (uint8_t bit : bitstream)
    {
        for (int i = 0; i < modulator.next_samples_per_bit(); ++i)
        {
            audio_buffer.push_back(modulator.modulate(bit));
        }
    }

    // Check max sample-to-sample difference
    // For a continuous sine wave at 2200 Hz and 48000 sample rate,
    // max change per sample is approximately 2 * pi * 2200 / 48000 ≈ 0.288
    // With some margin for the transition, should still be well under 0.5
    double max_delta = 0.0;
    for (size_t i = 1; i < audio_buffer.size(); ++i)
    {
        double delta = std::abs(audio_buffer[i] - audio_buffer[i - 1]);
        max_delta = (std::max)(max_delta, delta);
    }

    // A discontinuity would show as a delta close to 2.0 (jumping from +1 to -1)
    // Continuous phase should never exceed ~0.35 for these frequencies
    EXPECT_LT(max_delta, 0.4);
}

TEST(dds_afsk_modulator, afsk_1200_dc_offset)
{
    dds_afsk_modulator_double modulator(1200.0, 2200.0, 1200, 48000, 1.0);

    std::vector<double> audio_buffer;

    std::vector<uint8_t> bitstream = { 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 };

    for (uint8_t bit : bitstream)
    {
        for (int i = 0; i < modulator.next_samples_per_bit(); ++i)
        {
            audio_buffer.push_back(modulator.modulate(bit));
        }
    }

    double sum = std::accumulate(audio_buffer.begin(), audio_buffer.end(), 0.0);
    double mean = sum / audio_buffer.size();

    EXPECT_NEAR(mean, 0.0, 0.01);  // DC offset should be negligible
}

TEST(dds_afsk_modulator, afsk_1200_constant_envelope)
{
    dds_afsk_modulator_double modulator(1200.0, 2200.0, 1200, 48000, 1.0);

    std::vector<double> audio_buffer;

    std::vector<uint8_t> bitstream = generate_random_bits(10000);

    double max_sample = 0.0;
    double min_sample = 0.0;

    for (uint8_t bit : bitstream)
    {
        for (int i = 0; i < modulator.next_samples_per_bit(); ++i)
        {
            double sample = modulator.modulate(bit);
            max_sample = (std::max)(max_sample, sample);
            min_sample = (std::min)(min_sample, sample);
        }
    }

    // With gain 1.0, samples should reach close to ±1.0
    EXPECT_NEAR(max_sample, 1.0, 0.01);
    EXPECT_NEAR(min_sample, -1.0, 0.01);
}

#ifdef ENABLE_HARDWARE_TESTS_1

TEST(modem, transmit_hardware_demo)
{
    // Note: This test requires a Digirig device connected to the system
    // Note: Windows: port COM16 is used and the audio device is named "Speakers (2- USB Audio Device)".
    // Note: Linux: the audio device is named "USB Audio" and is connected to a serial port ex: /dev/ttyUSB0
    // Note: the port and audio device name will vary, update accordingly
    // The Digirig should be connected to a radio configured for 1200 baud AFSK APRS transmission.
    // The radio will be set to transmit when the RTS line is asserted on the Digirig's serial port.
    // The test will transmit an APRS packet over the air, which can be verified by receiving it with another APRS receiver.
    // Ensure that the Digirig device is connected and the correct audio device name and serial port is used.
    // This test is disabled by default.
    // To enable, define ENABLE_HARDWARE_IN_THE_LOOP_TESTS during compilation.

    // Get the Digirig render audio device
    audio_device device;
#if WIN32
    if (!try_get_audio_device_by_description("Speakers (2- USB Audio Device)", device, audio_device_type::render, audio_device_state::active))
    {
        return;
    }
#endif // WIN32
#if __linux__
    if (!try_get_audio_device_by_name("USB Audio", device, audio_device_type::render, audio_device_state::active))
    {
        return;
    }
#endif // __linux__

    aprs::router::packet p = { "W7ION-5", "T7SVVQ", { "WIDE1-1", "WIDE2-1" }, R"(`2(al"|[/>"3u}hello world^)" };

    // Connecting to a Digirig serial port, which uses the RTS line for the PTT
    serial_port port;
#if WIN32
    if (!port.open("COM16", 9600))
    {
        return;
    }
#endif // WIN32
#if __linux__
    if (!port.open("/dev/ttyUSB0", 9600))
    {
        return;
    }
    // Turns out opening the port asserts the RTS line to on
    // Disable it, if we do it fast it will never be asserted high
    port.rts(false);
#endif // __linux__

    audio_stream stream = device.stream();
    dds_afsk_modulator_f64_adapter modulator(1200.0, 2200.0, 1200, stream.sample_rate());
    basic_bitstream_converter_adapter bitstream_converter;

    modem m;
    m.baud_rate(1200);
    m.tx_delay(300);
    m.tx_tail(45);
    m.start_silence(0.1);
    m.end_silence(0.1);
    m.gain(0.3);
    m.initialize(stream, modulator, bitstream_converter);

    // Turn the transmitter on
    port.rts(true);

    // Set audio stream volume to 50%
    stream.volume(50);

    // Send the modulated packet to the audio device
    m.transmit(p);

    // Turn the transmitter off
    port.rts(false);
}

#endif // ENABLE_HARDWARE_TESTS_1

#ifdef ENABLE_HARDWARE_TESTS_2

#if WIN32

TEST(audio_device, try_get_default_audio_device)
{
    for (int i = 0; i < 100; ++i)
    {
        audio_device render_device;
        EXPECT_TRUE(try_get_default_audio_device(render_device, audio_device_type::render));

        EXPECT_TRUE(!render_device.id.empty());
        EXPECT_TRUE(!render_device.name.empty());
        EXPECT_TRUE(!render_device.description.empty());
        EXPECT_TRUE(!render_device.container_id.empty());
        EXPECT_TRUE(render_device.type == audio_device_type::render);
        EXPECT_TRUE(render_device.state == audio_device_state::active);

        audio_device capture_device;
        EXPECT_TRUE(try_get_default_audio_device(capture_device, audio_device_type::capture));

        EXPECT_TRUE(!capture_device.id.empty());
        EXPECT_TRUE(!capture_device.name.empty());
        EXPECT_TRUE(!capture_device.description.empty());
        EXPECT_TRUE(!capture_device.container_id.empty());
        EXPECT_TRUE(capture_device.type == audio_device_type::capture);
        EXPECT_TRUE(capture_device.state == audio_device_state::active);
    }
}

#endif // WIN32

#ifdef __linux__

TEST(audio_device, audio_device)
{
    for (int i = 0; i < 100; ++i)
    {
        audio_device render_device;

        std::vector<audio_device> render_devices = get_audio_devices(audio_device_type::render, audio_device_state::active);

        EXPECT_TRUE(render_devices.size() > 0);

        render_device = std::move(render_devices[0]);

        EXPECT_TRUE(!render_device.id.empty());
        EXPECT_TRUE(!render_device.name.empty());
        EXPECT_TRUE(!render_device.description.empty());
        EXPECT_TRUE(render_device.card_id >= 0);
        EXPECT_TRUE(render_device.device_id >= 0);
        EXPECT_TRUE(render_device.type == audio_device_type::render);
        EXPECT_TRUE(render_device.state == audio_device_state::active);

        audio_device capture_device;

        std::vector<audio_device> capture_devices = get_audio_devices(audio_device_type::capture, audio_device_state::active);

        EXPECT_TRUE(capture_devices.size() > 0);

        capture_device = std::move(capture_devices[0]);

        EXPECT_TRUE(!capture_device.id.empty());
        EXPECT_TRUE(!capture_device.name.empty());
        EXPECT_TRUE(!capture_device.description.empty());
        EXPECT_TRUE(capture_device.card_id >= 0);
        EXPECT_TRUE(capture_device.device_id >= 0);
        EXPECT_TRUE(capture_device.type == audio_device_type::capture);
        EXPECT_TRUE(capture_device.state == audio_device_state::active);
    }
}

#endif // __linux__

#if WIN32

TEST(audio_stream, wasapi_audio_input_stream)
{
    audio_device device;
    EXPECT_TRUE(try_get_default_audio_device(device, audio_device_type::capture));

    std::unique_ptr<audio_stream_base> stream = device.stream();

    wasapi_audio_input_stream* wasapi_stream = dynamic_cast<wasapi_audio_input_stream*>(stream.get());

    EXPECT_TRUE(wasapi_stream != nullptr);

    wasapi_stream->stop();

    wasapi_stream->start();

    wasapi_stream->mute(true);

    EXPECT_TRUE(wasapi_stream->mute() == true);

    wasapi_stream->mute(false);

    EXPECT_TRUE(wasapi_stream->mute() == false);
}

TEST(audio_stream, wasapi_audio_output_stream)
{
    audio_device device;
    EXPECT_TRUE(try_get_default_audio_device(device));

    std::unique_ptr<audio_stream_base> stream = device.stream();

    wasapi_audio_output_stream* wasapi_stream = dynamic_cast<wasapi_audio_output_stream*>(stream.get());

    EXPECT_TRUE(wasapi_stream != nullptr);

    wasapi_stream->stop();

    wasapi_stream->start();

    wasapi_stream->mute(true);

    EXPECT_TRUE(wasapi_stream->mute() == true);

    wasapi_stream->mute(false);

    EXPECT_TRUE(wasapi_stream->mute() == false);
}

#endif // WIN32

#endif // ENABLE_HARDWARE_TESTS_2

#ifdef ENABLE_HARDWARE_TESTS_3

#if WIN32

TEST(audio_stream, render_10s_stream)
{
    // Windows audio hardware render test
    // As you are running this test you could use a sound capture app on your phone to test the render
    // We will write a 17kHz tone to the default render device for 10 seconds

    audio_device device;
    EXPECT_TRUE(try_get_default_audio_device(device, audio_device_type::render));

    audio_stream stream = device.stream();

    stream.volume(25);

    // Write a 17kHz tone for 10 seconds, in chunks
    // For audio testing purposes, we render the samples to a WAV file as well

    wav_audio_output_stream wav_stream("test.wav", stream.sample_rate());

    constexpr double frequency = 17000.0; // 17kHz tone, should be inaudible to most people
    constexpr double amplitude = 0.3;
    constexpr int duration_seconds = 10;
    constexpr double pi = 3.14159265358979323846;

    const double sample_rate = static_cast<double>(stream.sample_rate());
    const int total_samples = static_cast<int>(sample_rate * duration_seconds);

    size_t chunk_size = static_cast<size_t>(sample_rate / 500);  // 5ms

    std::vector<double> chunk(chunk_size);
    int n = 0;

    // After 5-8 seconds on playback, you might hear small increments in volume
    // This is likely due to the audio device's internal volume leveling or AGC kicking in
    // There is no control over this behavior in WASAPI, on in Windows

    while (n < total_samples)
    {
        size_t samples_to_write = (std::min)(chunk_size, static_cast<size_t>(total_samples - n));

        for (size_t i = 0; i < samples_to_write; ++i)
        {
            chunk[i] = amplitude * std::sin(2.0 * pi * frequency * (n + i) / sample_rate);
        }

        stream.write(chunk.data(), samples_to_write);

        wav_stream.write(chunk.data(), samples_to_write);

        n += static_cast<int>(samples_to_write);
    }

    stream.wait_write_completed(-1);

    wav_stream.close();
}

TEST(audio_stream, modem_transmit_1200)
{
    // Test similar to the modem-transmit_demo test
    // But instead of transmitting to a radio, it renders the packet to the default audio device
    // If you use the aprs.fi app on iOS you can decode this packet over the air using the iPhone's microphone
    // Select the aprs.fi Software Modem (1200 bps)
    // I've put the bottom of my iPhone directly on the grid of the speaker of my Razer Blade

    audio_device device;
    EXPECT_TRUE(try_get_default_audio_device(device));

    aprs::router::packet p = { "W7ION-5", "T7SVVQ", { "WIDE1-1", "WIDE2-1" }, R"(`2(al"|[/>"3u}hello world^)" };

    audio_stream stream = device.stream();
    dds_afsk_modulator_double_adapter modulator(1200.0, 2200.0, 1200, stream.sample_rate());
    basic_bitstream_converter_adapter bitstream_converter;

    modem m;
    m.baud_rate(1200);
    m.tx_delay(300);
    m.tx_tail(45);
    m.start_silence(0.1);
    m.end_silence(0.1);
    m.gain(0.3);
    m.initialize(stream, modulator, bitstream_converter);

    // Set audio stream volume to 30%
    stream.volume(30);

    // Send the modulated packet to the audio device
    m.transmit(p);
}

TEST(audio_stream, wasapi_audio_output_stream_loopback_modem_transmit_1200) // generic for linux too!
{
    std::mutex capture_mutex;
    std::condition_variable capture_cv;
    bool stop_capture = false;
    bool capture_started = false;
    bool capture_stopped = false;

    // Capture thread to read from the default capture device
    std::jthread capture_thread([&]()
    {

        std::vector<audio_device> devices = get_audio_devices(audio_device_type::capture, audio_device_state::active);

        audio_device capture_device = std::move(devices[1]);

        audio_stream capture_stream = capture_device.stream();

        wav_audio_output_stream wav_stream("loopback_capture.wav", capture_stream.sample_rate());

        std::vector<double> buffer(1024);

        {
            std::lock_guard<std::mutex> lock(capture_mutex);
            capture_started = true;
        }
        capture_cv.notify_one();

        while (true)
        {
            {
                std::unique_lock<std::mutex> lock(capture_mutex);
                if (capture_cv.wait_for(lock, std::chrono::milliseconds(10), [&] { return stop_capture; }))
                {
                    break;
                }
            }

            size_t n = capture_stream.read(buffer.data(), buffer.size());
            if (n > 0)
            {
                wav_stream.write(buffer.data(), n);
            }
        }

        wav_stream.close();

        {
            std::lock_guard<std::mutex> lock(capture_mutex);
            capture_stopped = true;
        }
        capture_cv.notify_one();
    });

    // Wait for capture thread to start
    {
        std::unique_lock<std::mutex> lock(capture_mutex);
        capture_cv.wait(lock, [&] { return capture_started; });
    }

    // Send a packet to the default render device

    audio_device device;
    EXPECT_TRUE(try_get_default_audio_device(device));

    aprs::router::packet p = { "W7ION-5", "T7SVVQ", { "WIDE1-1", "WIDE2-1" }, R"(`2(al"|[/>"3u}hello world^)" };

    audio_stream stream = device.stream();
    dds_afsk_modulator_double_adapter modulator(1200.0, 2200.0, 1200, stream.sample_rate());
    basic_bitstream_converter_adapter bitstream_converter;

    modem m;
    m.baud_rate(1200);
    m.tx_delay(300);
    m.tx_tail(45);
    m.start_silence(0.1);
    m.end_silence(0.1);
    m.gain(0.5);
    m.initialize(stream, modulator, bitstream_converter);

    // Set audio stream volume to 30%
    stream.volume(50);

    // Send the modulated packet to the audio device
    m.transmit(p);

    // Stop capture thread
    {
        std::lock_guard<std::mutex> lock(capture_mutex);
        stop_capture = true;
    }
    capture_cv.notify_one();

    // Wait for capture thread to stop
    {
        std::unique_lock<std::mutex> lock(capture_mutex);
        capture_cv.wait(lock, [&] { return capture_stopped; });
    }
}

TEST(audio_stream, capture_5s_stream)
{
    // Read from the default capture device for 5 seconds and write to a WAV file

    audio_device device;
    try_get_default_audio_device(device, audio_device_type::capture);

    audio_stream stream = device.stream();

    EXPECT_TRUE(stream.sample_rate() > 0);

    wav_audio_output_stream wav_stream("test.wav", stream.sample_rate());

    constexpr int duration_seconds = 5;

    const size_t chunk_size = stream.sample_rate() / 10;  // 100ms
    const size_t total_samples = stream.sample_rate() * duration_seconds;

    std::vector<double> buffer(chunk_size);

    size_t samples_read = 0;

    while (samples_read < total_samples)
    {
        size_t n = stream.read(buffer.data(), chunk_size);
        if (n > 0)
        {
            wav_stream.write(buffer.data(), n);
            samples_read += n;
        }
    }

    wav_stream.close();
}

TEST(audio_device, stream)
{
    audio_device render_device;
    EXPECT_TRUE(try_get_default_audio_device(render_device, audio_device_type::render));

    for (int i = 0, volume = 0; i < 10; i++, volume += 10)
    {
        audio_stream stream = render_device.stream();
        stream.volume(volume);
        EXPECT_TRUE(stream.volume() == volume);
        EXPECT_TRUE(!stream.name().empty());
        EXPECT_TRUE(stream.channels() > 0);
        EXPECT_TRUE(stream.sample_rate() > 0);
        stream.close();
    }

    audio_device capture_device;
    EXPECT_TRUE(try_get_default_audio_device(capture_device, audio_device_type::capture));

    for (int i = 0, volume = 0; i < 10; i++, volume += 10)
    {
        audio_stream stream = capture_device.stream();
        stream.volume(volume);
        EXPECT_TRUE(stream.volume() == volume);
        EXPECT_TRUE(!stream.name().empty());
        EXPECT_TRUE(stream.channels() > 0);
        EXPECT_TRUE(stream.sample_rate() > 0);
        stream.close();
    }
}

#endif // WIN32

#endif // ENABLE_HARDWARE_TESTS_3

TEST(audio_stream, wav_audio_input_stream_end_to_end)
{
    // Modulate a packet to a wav file
    // Open it using a wav_audio_input_stream
    // Write back to a wav file and demodulate with Direwolf

APRS_TRACK_NAMESPACE_USE
APRS_TRACK_DETAIL_NAMESPACE_USE

    // N0CALL>T9QPVP,WIDE1-1:`3T{m\\\x1f[/\"4F}
    std::string packet_string = encode_mic_e_packet_no_message("N0CALL", "WIDE1-1", 49.176666666667, -123.94916666667, mic_e_status::in_service, 3, 15.999, '/', '[', 0, 154.2);

    EXPECT_TRUE(packet_string == "N0CALL>T9QPVP,WIDE1-1:`3T{m\\\x1f[/\"4F}");

    aprs::router::packet packet = packet_string;

    {
        wav_audio_output_stream wav_stream("test.wav", 48000);
        dds_afsk_modulator_double_adapter modulator(1200.0, 2200.0, 1200, wav_stream.sample_rate());
        basic_bitstream_converter_adapter bitstream_converter;

        modem m;
        m.baud_rate(1200);
        m.tx_delay(300);
        m.tx_tail(45);
        m.start_silence(0.1);
        m.end_silence(0.1);
        m.gain(0.3);
        m.initialize(wav_stream, modulator, bitstream_converter);

        m.transmit(packet);

        wav_stream.close();

    }

    {
        wav_audio_input_stream wav_input_stream("test.wav");
        wav_audio_output_stream wav_output_stream("test2.wav", 48000);

        EXPECT_TRUE(wav_input_stream.sample_rate() == 48000);
        EXPECT_TRUE(wav_input_stream.channels() == 1);

        std::vector<double> audio_buffer;
        while (true)
        {
            std::vector<double> buffer(1024);
            size_t n = wav_input_stream.read(buffer.data(), buffer.size());
            if (n == 0)
            {
                break;
            }
            audio_buffer.insert(audio_buffer.end(), buffer.begin(), buffer.begin() + n);
        }

        size_t offset = 0;
        size_t chunk_size = 1024; // Deliberately use a smaller chunk size for testing purposes
        while (offset < audio_buffer.size())
        {
            size_t size = (std::min)(chunk_size, audio_buffer.size() - offset);
            size_t n = wav_output_stream.write(audio_buffer.data() + offset, size);
            if (n == 0)
            {
                break;
            }
            offset += n;
        }

        wav_input_stream.close();
        wav_output_stream.close();
    }

    std::string output;
    std::string error;

    // Run Direwolf's ATEST with -B 1200 -d x
    run_process(ATEST_EXE_PATH, output, error, "-B 1200", "-d x", "test2.wav");

    // Expect [0] N0CALL-10>APZ001,WIDE1-1,WIDE2-2:Hello, APRS!
    EXPECT_TRUE(output.find("[0] " + replace_non_printable(to_string(packet))) != std::string::npos);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
