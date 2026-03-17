// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// device_description.h
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

#pragma once

#include <string>

#include <audio_stream.h>
#include <io.h>

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
#ifndef LIBMODEM_ANONYMOUS_NAMESPACE_BEGIN
#define LIBMODEM_ANONYMOUS_NAMESPACE_BEGIN namespace {
#endif
#ifndef LIBMODEM_ANONYMOUS_NAMESPACE_END
#define LIBMODEM_ANONYMOUS_NAMESPACE_END }
#endif

LIBMODEM_NAMESPACE_BEGIN

struct device_description
{
    std::string id;
    std::string name;
    std::string description;
    std::string vendor;
    std::string model;
    std::string serial_number;
    std::string product_id;
    std::string vendor_id;
    std::string revision;
    std::string path;
    std::string container_id;
    std::string instance_id;
    std::string hw_path;
    uint32_t bus_number = 0;
    uint32_t device_address = 0;
    uint32_t topology_depth = 0;
    int major_number = -1;
    int minor_number = -1;
    std::string enumerator;
};

device_description get_device_description(audio_device& device);
device_description get_device_description(const serial_port_info& info);
std::vector<device_description> get_sibling_audio_devices(const device_description& desc, int depth);
std::vector<audio_device> get_audio_devices(const device_description& desc);

LIBMODEM_NAMESPACE_END