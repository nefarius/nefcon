#pragma once

#include <guiddef.h>
#include <string>
#include <expected>
#include <vector>
#include <Windows.h>
#include <format>
#include <nefarius/neflib/Devcon.hpp>
#include <nefarius/neflib/MultiStringArray.hpp>
#include <nefarius/neflib/Win32Error.hpp>


namespace devcon
{
    std::expected<void, nefarius::utilities::Win32Error> restart_bth_usb_device(int instance = 0);

    std::expected<void, nefarius::utilities::Win32Error> enable_disable_bth_usb_device(bool state, int instance = 0);

    std::vector<std::expected<void, nefarius::utilities::Win32Error>> uninstall_device_and_driver(
        const GUID* classGuid, const std::wstring& hardwareId, bool* rebootRequired);

    std::expected<void, nefarius::utilities::Win32Error> inf_default_install(const std::wstring& fullInfPath,
                                                                             bool* rebootRequired);

    std::expected<void, nefarius::utilities::Win32Error> inf_default_uninstall(
        const std::wstring& fullInfPath, bool* rebootRequired);

    std::expected<std::vector<nefarius::devcon::FindByHwIdResult>, nefarius::utilities::Win32Error> find_by_hwid(
        const std::wstring& matchstring);
};
