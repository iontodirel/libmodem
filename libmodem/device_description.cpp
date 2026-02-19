// **************************************************************** //
// libmodem - APRS modem                                            //
// Version 0.1.0                                                    //
// https://github.com/iontodirel/libmodem                           //
// Copyright (c) 2025 Ion Todirel                                   //
// **************************************************************** //
//
// device_description.cpp
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

#include "device_description.h"

#if WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <combaseapi.h>
#include <Ntddser.h>

#define INITGUID
#include <devpkey.h>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "Cfgmgr32.lib")

#endif // WIN32

#ifdef __linux__

#include <libudev.h>

#endif // __linux__

#include <cstring>
#include <cstdlib>

LIBMODEM_NAMESPACE_BEGIN

#if WIN32

LIBMODEM_ANONYMOUS_NAMESPACE_BEGIN

std::wstring utf8_to_utf16(const std::string& string)
{
    if (string.empty())
    {
        return {};
    }

    int len = MultiByteToWideChar(CP_UTF8, 0, string.c_str(), -1, nullptr, 0);
    if (len <= 0)
    {
        return {};
    }

    std::wstring wstring(len - 1, L'\0');

    MultiByteToWideChar(CP_UTF8, 0, string.c_str(), -1, wstring.data(), len);

    return wstring;
}

std::string utf16_to_utf8(const wchar_t* wstring)
{
    if (wstring == nullptr || wstring[0] == '\0')
    {
        return "";
    }

    int len = WideCharToMultiByte(CP_UTF8, 0, wstring, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0)
    {
        return "";
    }

    std::string string(len - 1, '\0');

    WideCharToMultiByte(CP_UTF8, 0, wstring, -1, string.data(), len, nullptr, nullptr);

    return string;
}

std::string get_device_string_property(DEVINST device, const DEVPROPKEY& key, DEVPROPTYPE expected_type)
{
    DEVPROPTYPE type = 0;
    ULONG size = 0;

    if (CM_Get_DevNode_PropertyW(device, &key, &type, nullptr, &size, 0) != CR_BUFFER_SMALL)
    {
        return {};
    }

    if (type != expected_type || size == 0)
    {
        return {};
    }

    std::vector<BYTE> buffer(size);
    if (CM_Get_DevNode_PropertyW(device, &key, &type, buffer.data(), &size, 0) != CR_SUCCESS)
    {
        return {};
    }

    return utf16_to_utf8(reinterpret_cast<const wchar_t*>(buffer.data()));
}

uint32_t get_device_uint32_property(DEVINST device, const DEVPROPKEY& key)
{
    DEVPROPTYPE type = 0;
    uint32_t value = 0;
    ULONG size = sizeof(value);

    if (CM_Get_DevNode_PropertyW(device, &key, &type, reinterpret_cast<BYTE*>(&value), &size, 0) != CR_SUCCESS)
    {
        return 0;
    }

    if (type != DEVPROP_TYPE_UINT32)
    {
        return 0;
    }

    return value;
}

std::string get_device_guid_property(DEVINST device, const DEVPROPKEY& key)
{
    DEVPROPTYPE type = 0;
    ULONG size = sizeof(GUID);
    GUID guid = {};

    if (CM_Get_DevNode_PropertyW(device, &key, &type, reinterpret_cast<BYTE*>(&guid), &size, 0) != CR_SUCCESS)
    {
        return {};
    }

    if (type != DEVPROP_TYPE_GUID)
    {
        return {};
    }

    WCHAR guid_str[39] = {};
    StringFromGUID2(guid, guid_str, 39);

    return utf16_to_utf8(guid_str);
}

std::string parse_hardware_id_field(const std::string& hardware_id, const std::string& prefix)
{
    size_t pos = hardware_id.find(prefix);
    if (pos == std::string::npos)
    {
        return {};
    }

    pos += prefix.size();

    size_t end = hardware_id.find('&', pos);
    if (end == std::string::npos)
    {
        end = hardware_id.size();
    }

    return hardware_id.substr(pos, end - pos);
}

int get_device_topology_depth(DEVINST device)
{
    int depth = 0;

    DEVINST current_device = device;
    while (CM_Get_Parent(&current_device, current_device, 0) == CR_SUCCESS)
    {
        depth++;
    }

    return depth;
}

bool try_find_physical_device_ancestor(DEVINST device, DEVINST& result)
{
    DEVINST current_device = device;

    for (int depth = 0; depth < 16; depth++)
    {
        std::string enumerator = get_device_string_property(current_device, DEVPKEY_Device_EnumeratorName, DEVPROP_TYPE_STRING);
        if (!enumerator.empty() && enumerator != "SWD")
        {
            result = current_device;
            return true;
        }

        DEVINST parent_device = 0;
        if (CM_Get_Parent(&parent_device, current_device, 0) != CR_SUCCESS)
        {
            break;
        }

        current_device = parent_device;
    }

    return false;
}

std::string parse_serial_number(const std::string& instance_id)
{
    size_t first = instance_id.find('\\');
    if (first == std::string::npos)
    {
        return {};
    }

    size_t second = instance_id.find('\\', first + 1);
    if (second == std::string::npos)
    {
        return {};
    }

    std::string third = instance_id.substr(second + 1);
    if (third.empty() || third.find('&') != std::string::npos)
    {
        return {};
    }

    return third;
}

std::string get_device_path(DEVINST device)
{
    DEVINST current_device = device;

    for (int depth = 0; depth < 16; depth++)
    {
        std::string path = get_device_string_property(current_device, DEVPKEY_Device_LocationPaths, DEVPROP_TYPE_STRING_LIST);
        if (!path.empty())
        {
            return path;
        }

        DEVINST parent_device = 0;
        if (CM_Get_Parent(&parent_device, current_device, 0) != CR_SUCCESS)
        {
            break;
        }

        current_device = parent_device;
    }

    return "";
}

device_description get_device_description(DEVINST device)
{
    device_description desc;

    desc.enumerator = get_device_string_property(device, DEVPKEY_Device_EnumeratorName, DEVPROP_TYPE_STRING);
    desc.path = get_device_path(device);
    desc.hw_path = desc.path;
    desc.vendor = get_device_string_property(device, DEVPKEY_Device_Manufacturer, DEVPROP_TYPE_STRING);
    desc.model = get_device_string_property(device, DEVPKEY_Device_FriendlyName, DEVPROP_TYPE_STRING);

    if (desc.model.empty())
    {
        desc.model = get_device_string_property(device, DEVPKEY_Device_DeviceDesc, DEVPROP_TYPE_STRING);
    }

    // VID/PID/serial from hardware IDs rather than parsing instance ID
    // DEVPKEY_Device_HardwareIds returns e.g. "USB\VID_0D8C&PID_013C&REV_0100"
    std::string hardware_ids = get_device_string_property(device, DEVPKEY_Device_HardwareIds, DEVPROP_TYPE_STRING_LIST);
    desc.vendor_id = parse_hardware_id_field(hardware_ids, "VID_");
    desc.product_id = parse_hardware_id_field(hardware_ids, "PID_");
    desc.revision = parse_hardware_id_field(hardware_ids, "REV_");

    desc.instance_id = get_device_string_property(device, DEVPKEY_Device_InstanceId, DEVPROP_TYPE_STRING);
    desc.container_id = get_device_guid_property(device, DEVPKEY_Device_ContainerId);
    desc.bus_number = get_device_uint32_property(device, DEVPKEY_Device_BusNumber);
    desc.device_address = get_device_uint32_property(device, DEVPKEY_Device_Address);
    desc.topology_depth = get_device_topology_depth(device);

    return desc;
}

bool try_find_serial_port_device(const std::string& port_name, DEVINST& result)
{
    HDEVINFO serial_port_devices = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_COMPORT, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (serial_port_devices == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    SP_DEVINFO_DATA serial_port_device_info = { sizeof(SP_DEVINFO_DATA) };
    bool found = false;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(serial_port_devices, i, &serial_port_device_info); i++)
    {
        HKEY key = SetupDiOpenDevRegKey(serial_port_devices, &serial_port_device_info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        char name[64] = {};
        DWORD name_size = sizeof(name);
        DWORD registry_type = 0;

        if (RegQueryValueExA(key, "PortName", nullptr, &registry_type, reinterpret_cast<BYTE*>(name), &name_size) == ERROR_SUCCESS && registry_type == REG_SZ && port_name == name)
        {
            result = serial_port_device_info.DevInst;
            found = true;
        }

        RegCloseKey(key);

        if (found)
        {
            break;
        }
    }

    SetupDiDestroyDeviceInfoList(serial_port_devices);

    return found;
}

bool try_find_parent_hub(DEVINST device, DEVINST& result)
{
    DEVINST current_device = device;

    for (int depth = 0; depth < 16; depth++)
    {
        DEVINST parent_device = 0;
        if (CM_Get_Parent(&parent_device, current_device, 0) != CR_SUCCESS)
        {
            break;
        }

        std::string service = get_device_string_property(parent_device, DEVPKEY_Device_Service, DEVPROP_TYPE_STRING);

        if (service == "usbhub" || service == "usbhub3" || service == "USBHUB" || service == "USBHUB3")
        {
            result = parent_device;
            return true;
        }

        current_device = parent_device;
    }

    return false;
}

LIBMODEM_ANONYMOUS_NAMESPACE_END

#endif // WIN32

#ifdef __linux__

LIBMODEM_ANONYMOUS_NAMESPACE_BEGIN

std::string get_device_property(udev_device* device, const char* attr)
{
    const char* value = udev_device_get_sysattr_value(device, attr);
    return value ? value : "";
}

bool try_parse_int(const std::string& string, int& result)
{
    if (string.empty())
    {
        return false;
    }

    char* end = nullptr;
    long value = std::strtol(string.c_str(), &end, 10);
    if (end == string.c_str() || *end != '\0')
    {
        return false;
    }

    result = static_cast<int>(value);

    return true;
}

int get_device_topology_depth(udev_device* device)
{
    int depth = 0;

    udev_device* current_device = device;

    while (true)
    {
        current_device = udev_device_get_parent(current_device);
        if (current_device == nullptr)
        {
            break;
        }

        const char* subsystem = udev_device_get_subsystem(current_device);
        if (subsystem == nullptr || std::strlen(subsystem) == 0)
        {
            break;
        }

        depth++;
    }

    return depth;
}

bool try_find_usb_device_ancestor(udev_device* device, udev_device*& usb_device)
{
    usb_device = udev_device_get_parent_with_subsystem_devtype(device, "usb", "usb_device");
    return usb_device != nullptr;
}

device_description get_device_description(udev_device* device, udev_device* usb_device)
{
    device_description desc;

    const char* syspath = udev_device_get_syspath(device);
    const char* usb_syspath = udev_device_get_syspath(usb_device);

    const char* dev = udev_device_get_sysattr_value(device, "dev");
    if (dev != nullptr)
    {
        std::string major_minor_str(dev);
        size_t colon_pos = major_minor_str.find(':');
        if (colon_pos != std::string::npos)
        {
            try_parse_int(major_minor_str.substr(0, colon_pos), desc.major_number);
            try_parse_int(major_minor_str.substr(colon_pos + 1), desc.minor_number);
        }
    }

    desc.vendor_id = get_device_property(usb_device, "idVendor");
    desc.product_id = get_device_property(usb_device, "idProduct");
    desc.vendor = get_device_property(usb_device, "manufacturer");
    desc.model = get_device_property(usb_device, "product");
    desc.serial_number = get_device_property(usb_device, "serial");

    std::string busnum = get_device_property(usb_device, "busnum");
    std::string devnum = get_device_property(usb_device, "devnum");

    int value = 0;

    if (try_parse_int(busnum, value))
    {
        desc.bus_number = static_cast<uint32_t>(value);
    }

    if (try_parse_int(devnum, value))
    {
        desc.device_address = static_cast<uint32_t>(value);
    }

    if (syspath != nullptr)
    {
        desc.path = syspath;
    }

    if (usb_syspath != nullptr)
    {
        desc.hw_path = usb_syspath;
    }

    desc.topology_depth = get_device_topology_depth(usb_device);

    const char* subsystem = udev_device_get_subsystem(usb_device);
    if (subsystem != nullptr)
    {
        desc.enumerator = subsystem;
    }

    return desc;
}

LIBMODEM_ANONYMOUS_NAMESPACE_END

#endif // __linux__

device_description get_device_description(audio_device& audio_device)
{
    device_description desc;

#if WIN32
    std::wstring device_pnp_id = L"SWD\\MMDEVAPI\\" + utf8_to_utf16(audio_device.id);

    DEVINST device = 0;
    if (CM_Locate_DevNodeW(&device, const_cast<DEVINSTID_W>(device_pnp_id.c_str()), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
    {
        return desc;
    }

    DEVINST physical_device = 0;
    if (!try_find_physical_device_ancestor(device, physical_device))
    {
        return desc;
    }

    desc = get_device_description(physical_device);
    desc.id = audio_device.id;
    desc.name = audio_device.name;
    desc.description = audio_device.description;
#endif

#ifdef __linux__
    struct udev* udev = udev_new();
    if (udev == nullptr)
    {
        return desc;
    }

    udev_enumerate* enumerate = udev_enumerate_new(udev);
    if (enumerate == nullptr)
    {
        udev_unref(udev);
        return desc;
    }

    udev_enumerate_add_match_subsystem(enumerate, "sound");
    udev_enumerate_add_match_sysname(enumerate, ("card" + std::to_string(audio_device.card_id)).c_str());
    udev_enumerate_scan_devices(enumerate);

    udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
    if (devices == nullptr)
    {
        udev_enumerate_unref(enumerate);
        udev_unref(udev);
        return desc;
    }

    udev_device* device = udev_device_new_from_syspath(udev, udev_list_entry_get_name(devices));
    if (device == nullptr)
    {
        udev_enumerate_unref(enumerate);
        udev_unref(udev);
        return desc;
    }

    udev_device* usb_device = nullptr;
    if (try_find_usb_device_ancestor(device, usb_device))
    {
        desc = get_device_description(device, usb_device);
    }

    desc.id = audio_device.id;
    desc.name = audio_device.name;
    desc.description = audio_device.description;

    udev_device_unref(device);
    udev_enumerate_unref(enumerate);
    udev_unref(udev);
#endif

    return desc;
}

device_description get_device_description(const serial_port_info& info)
{
    device_description desc;

#if WIN32
    DEVINST device = 0;
    if (!try_find_serial_port_device(info.port_name, device))
    {
        return desc;
    }

    DEVINST physical_device = 0;
    if (!try_find_physical_device_ancestor(device, physical_device))
    {
        return desc;
    }

    desc = get_device_description(physical_device);
    desc.id = info.port_name;
    desc.name = info.port_name;
    desc.serial_number = parse_serial_number(desc.instance_id);
#endif

#ifdef __linux__
    struct udev* udev = udev_new();
    if (udev == nullptr)
    {
        return desc;
    }

    udev_enumerate* enumerate = udev_enumerate_new(udev);
    if (enumerate == nullptr)
    {
        udev_unref(udev);
        return desc;
    }

    udev_enumerate_add_match_subsystem(enumerate, "tty");
    udev_enumerate_add_match_property(enumerate, "DEVNAME", info.port_name.c_str());
    udev_enumerate_scan_devices(enumerate);

    udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
    if (devices == nullptr)
    {
        udev_enumerate_unref(enumerate);
        udev_unref(udev);
        return desc;
    }

    udev_device* device = udev_device_new_from_syspath(udev, udev_list_entry_get_name(devices));
    if (device == nullptr)
    {
        udev_enumerate_unref(enumerate);
        udev_unref(udev);
        return desc;
    }

    udev_device* usb_device = nullptr;
    if (try_find_usb_device_ancestor(device, usb_device))
    {
        desc = get_device_description(device, usb_device);
    }

    desc.id = info.port_name;
    desc.name = info.port_name;

    udev_device_unref(device);
    udev_enumerate_unref(enumerate);
    udev_unref(udev);
#endif

    return desc;
}

std::vector<device_description> get_sibling_audio_devices(const device_description& desc, int depth)
{
    std::vector<device_description> siblings;

#if WIN32
    if (desc.instance_id.empty())
    {
        return siblings;
    }

    std::wstring id_wstr = utf8_to_utf16(desc.instance_id);
    DEVINST source_device = 0;
    if (CM_Locate_DevNodeW(&source_device, const_cast<DEVINSTID_W>(id_wstr.c_str()), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
    {
        return siblings;
    }

    DEVINST ancestor_hub_device = 0;
    {
        DEVINST current_device = source_device;
        for (int i = 0; i < depth; ++i)
        {
            if (!try_find_parent_hub(current_device, ancestor_hub_device))
            {
                return siblings;
            }
            current_device = ancestor_hub_device;
        }
    }

    ULONG devices_count = 0;
    if (CM_Get_Device_ID_List_SizeW(&devices_count, L"SWD\\MMDEVAPI", CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS)
    {
        return siblings;
    }

    std::vector<wchar_t> device_ids(devices_count);
    if (CM_Get_Device_ID_ListW(L"SWD\\MMDEVAPI", device_ids.data(), devices_count, CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT) != CR_SUCCESS)
    {
        return siblings;
    }

    for (const wchar_t* current_device_id = device_ids.data(); *current_device_id != L'\0'; current_device_id += wcslen(current_device_id) + 1)
    {
        DEVINST current_device = 0;
        if (CM_Locate_DevNodeW(&current_device, const_cast<DEVINSTID_W>(current_device_id), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
        {
            continue;
        }

        DEVINST current_physical_device = 0;
        if (!try_find_physical_device_ancestor(current_device, current_physical_device))
        {
            continue;
        }

        if (current_physical_device == source_device)
        {
            continue;
        }

        bool is_sibling = false;
        {
            DEVINST candidate_device = current_physical_device;
            for (int i = 0; i < depth; ++i)
            {
                DEVINST candidate_hub_device = 0;
                if (!try_find_parent_hub(candidate_device, candidate_hub_device))
                {
                    break;
                }

                if (candidate_hub_device == ancestor_hub_device)
                {
                    is_sibling = true;
                    break;
                }

                candidate_device = candidate_hub_device;
            }
        }

        if (!is_sibling)
        {
            continue;
        }

        device_description sibling = get_device_description(current_physical_device);

        if (wcslen(current_device_id) > 13)
        {
            sibling.id = utf16_to_utf8(current_device_id + 13);
        }

        sibling.name = get_device_string_property(current_device, DEVPKEY_Device_FriendlyName, DEVPROP_TYPE_STRING);

        if (sibling.name.empty())
        {
            sibling.name = get_device_string_property(current_device, DEVPKEY_Device_DeviceDesc, DEVPROP_TYPE_STRING);
        }

        sibling.description = sibling.name;

        siblings.push_back(sibling);
    }
#endif

#ifdef __linux__
    if (desc.path.empty())
    {
        return siblings;
    }

    struct udev* udev = udev_new();
    if (udev == nullptr)
    {
        return siblings;
    }

    udev_device* source_device = udev_device_new_from_syspath(udev, desc.path.c_str());
    if (source_device == nullptr)
    {
        udev_unref(udev);
        return siblings;
    }

    // Find the USB device ancestor of the source
    udev_device* source_usb_device = nullptr;
    if (!try_find_usb_device_ancestor(source_device, source_usb_device))
    {
        udev_device_unref(source_device);
        udev_unref(udev);
        return siblings;
    }

    // Walk up N parent USB devices to find the common ancestor
    udev_device* ancestor_device = source_usb_device;
    for (int i = 0; i < depth; ++i)
    {
        udev_device* parent_device = udev_device_get_parent_with_subsystem_devtype(ancestor_device, "usb", "usb_device");
        if (parent_device == nullptr)
        {
            udev_device_unref(source_device);
            udev_unref(udev);
            return siblings;
        }
        ancestor_device = parent_device;
    }

    // Enumerate sound cards under the ancestor
    udev_enumerate* enumerate = udev_enumerate_new(udev);
    if (enumerate == nullptr)
    {
        udev_device_unref(source_device);
        udev_unref(udev);
        return siblings;
    }

    udev_enumerate_add_match_parent(enumerate, ancestor_device);
    udev_enumerate_add_match_subsystem(enumerate, "sound");
    udev_enumerate_add_match_sysname(enumerate, "card*");
    udev_enumerate_scan_devices(enumerate);

    const char* source_usb_syspath = udev_device_get_syspath(source_usb_device);

    udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
    udev_list_entry* entry = nullptr;

    udev_list_entry_foreach(entry, devices)
    {
        const char* sibling_path = udev_list_entry_get_name(entry);
        udev_device* sibling_device = udev_device_new_from_syspath(udev, sibling_path);
        if (sibling_device == nullptr)
        {
            continue;
        }

        udev_device* sibling_usb_device = nullptr;
        if (!try_find_usb_device_ancestor(sibling_device, sibling_usb_device))
        {
            udev_device_unref(sibling_device);
            continue;
        }

        // Skip if this is the same USB device as the source
        const char* sibling_usb_syspath = udev_device_get_syspath(sibling_usb_device);
        if (source_usb_syspath != nullptr && sibling_usb_syspath != nullptr && std::strcmp(sibling_usb_syspath, source_usb_syspath) == 0)
        {
            udev_device_unref(sibling_device);
            continue;
        }

        device_description sibling = get_device_description(sibling_device, sibling_usb_device);

        const char* card_number = udev_device_get_sysattr_value(sibling_device, "number");
        if (card_number != nullptr)
        {
            sibling.id = std::string("hw:") + card_number;
        }

        siblings.push_back(sibling);

        udev_device_unref(sibling_device);
    }

    udev_enumerate_unref(enumerate);
    udev_device_unref(source_device);
    udev_unref(udev);
#endif

    return siblings;
}

std::vector<audio_device> get_audio_devices(const device_description& desc)
{
#if WIN32
    std::vector<audio_device> devices = get_audio_devices();
    std::erase_if(devices, [&](const auto& d) { return d.id != desc.id; });
    return devices;
#endif

#ifdef __linux__
    std::vector<audio_device> devices;

    if (desc.path.empty())
    {
        return devices;
    }

    struct udev* udev = udev_new();
    if (udev == nullptr)
    {
        return devices;
    }

    udev_device* device = udev_device_new_from_syspath(udev, desc.path.c_str());
    if (device == nullptr)
    {
        udev_unref(udev);
        return devices;
    }

    int card_id = -1;
    const char* card_id_str = udev_device_get_sysattr_value(device, "number");
    if (card_id_str != nullptr && try_parse_int(card_id_str, card_id))
    {
        std::vector<audio_device> all_devices = get_audio_devices();
        for (auto& d : all_devices)
        {
            if (d.card_id == card_id)
            {
                devices.push_back(std::move(d));
            }
        }
    }

    udev_device_unref(device);
    udev_unref(udev);

    return devices;
#endif

#if !WIN32 && !defined(__linux__)
    (void)desc;
    return {};
#endif
}

LIBMODEM_NAMESPACE_END