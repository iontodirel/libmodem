// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// main.cpp
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

#include <config.h>
#include <pipeline.h>

#include <filesystem>
#include <cstdio>

#include <cxxopts.hpp>

using namespace libmodem;

struct args
{
    std::string config;
    bool help = false;
};

bool try_parse_args(int argc, char* argv[], args& args);
std::string get_config_file_name(const args& args);

int main(int argc, char* argv[])
{
    setup_console();

    args args;
    if (!try_parse_args(argc, argv, args))
    {
        return 1;
    }

    if (args.help)
    {
        return 0;
    }

    std::string config_file = get_config_file_name(args);

    if (!std::filesystem::exists(config_file))
    {
        printf("Config file not found: %s\n", config_file.c_str());
        return 1;
    }

    config c = read_config(config_file);

    pipeline_events_rich rich_events;

    pipeline p(c);

    p.on_events(rich_events);

    p.init();

    p.start();

    p.wait_stopped();

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }

    return 0;
}

bool try_parse_args(int argc, char* argv[], args& args)
{
    cxxopts::Options options("modem", "libmodem - APRS modem");

    options.add_options()
        ("c,config", "Path to configuration file", cxxopts::value<std::string>())
        ("h,help", "Print usage");

    cxxopts::ParseResult result;
    try
    {
        result = options.parse(argc, argv);
    }
    catch (const cxxopts::exceptions::exception& e)
    {
        printf("Error parsing options: %s\n", e.what());
        return false;
    }

    if (result.count("help"))
    {
        printf("%s\n", options.help().c_str());
        args.help = true;
        return true;
    }

    if (result.count("config"))
    {
        args.config = result["config"].as<std::string>();
    }

    return true;
}

std::string get_config_file_name(const args& args)
{
    if (!args.config.empty())
    {
        return args.config;
    }

    if (std::filesystem::exists("modem.json"))
    {
        return "modem.json";
    }

    return "config.json";
}