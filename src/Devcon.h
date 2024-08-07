#pragma once

#include <guiddef.h>
#include <string>
#include <expected>

#include "MultiStringArray.hpp"
#include "Win32Error.hpp"

namespace devcon
{
    /**
     * Specifies the attaching position of a class filter driver service.
     *
     * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
     * @date	07.08.2024
     */
    enum class DeviceClassFilterPosition
    {
        ///< Upper filters
        Upper,
        ///< Lower filters
        Lower
    };

    /**
     * Creates a new root-enumerated device node for a driver to load on to.
     *
     * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
     * @date	06.08.2024
     *
     * @param 	className 	Name of the device class (System, HIDClass, USB, etc.).
     * @param 	classGuid 	Unique identifier for the device class.
     * @param 	hardwareId	The Hardware ID to set.
     *
     * @returns	A std::expected&lt;void,nefarius::util::Win32Error&gt;
     */
    std::expected<void, nefarius::util::Win32Error> create(const std::wstring& className, const GUID* classGuid,
                                                           const nefarius::util::WideMultiStringArray& hardwareId);

    /**
     * Triggers a driver update on all devices matching a given hardware ID with using the provided INF.
     *
     * @author	Benjamin "Nefarius" Hoeglinger-Stelzer
     * @date	07.08.2024
     *
     * @param 		  	hardwareId	  	The Hardware ID of the devices to affect.
     * @param 		  	fullInfPath   	Full pathname to the INF file.
     * @param [in,out]	rebootRequired	If non-null, true if reboot required.
     * @param 		  	force		  	(Optional) True to force.
     *
     * @returns	A std::expected&lt;void,nefarius::util::Win32Error&gt;
     */
    std::expected<void, nefarius::util::Win32Error> update(const std::wstring& hardwareId,
                                                           const std::wstring& fullInfPath, bool* rebootRequired,
                                                           bool force = false);

    std::expected<void, nefarius::util::Win32Error> restart_bth_usb_device(int instance = 0);

    std::expected<void, nefarius::util::Win32Error> enable_disable_bth_usb_device(bool state, int instance = 0);

    std::expected<void, nefarius::util::Win32Error> install_driver(const std::wstring& fullInfPath, bool* rebootRequired);

    std::expected<void, nefarius::util::Win32Error> uninstall_driver(const std::wstring& fullInfPath, bool* rebootRequired);

    bool add_device_class_filter(const GUID* classGuid, const std::wstring& filterName,
                                 DeviceClassFilterPosition position);

    bool remove_device_class_filter(const GUID* classGuid, const std::wstring& filterName,
                                    DeviceClassFilterPosition position);

    bool uninstall_device_and_driver(const GUID* classGuid, const std::wstring& hardwareId, bool* rebootRequired);

    bool inf_default_install(const std::wstring& fullInfPath, bool* rebootRequired);

    bool inf_default_uninstall(const std::wstring& fullInfPath, bool* rebootRequired);

    bool find_by_hwid(const std::wstring& matchstring);
};
