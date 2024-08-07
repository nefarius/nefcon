#pragma once

#include <guiddef.h>
#include <string>
#include <expected>

#include "MultiStringArray.hpp"
#include "Win32Error.hpp"

namespace devcon
{
	struct DeviceClassFilterPosition
	{
		enum Value
		{
			Upper,
			Lower
		};
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
	 * @returns	True if it succeeds, false if it fails.
	 */
	std::expected<void, nefarius::util::Win32Error> create(const std::wstring& className, const GUID* classGuid, const nefarius::util::WideMultiStringArray& hardwareId);

    std::expected<void, nefarius::util::Win32Error> update(const std::wstring& hardwareId, const std::wstring& fullInfPath, bool* rebootRequired, bool force = false);

	bool restart_bth_usb_device();

	bool enable_disable_bth_usb_device(bool state);

	bool install_driver(const std::wstring& fullInfPath, bool* rebootRequired);

	bool uninstall_driver(const std::wstring& fullInfPath, bool* rebootRequired);

	bool add_device_class_filter(const GUID* classGuid, const std::wstring& filterName, DeviceClassFilterPosition::Value position);

	bool remove_device_class_filter(const GUID* classGuid, const std::wstring& filterName, DeviceClassFilterPosition::Value position);

	bool uninstall_device_and_driver(const GUID* classGuid, const std::wstring& hardwareId, bool* rebootRequired);

	bool inf_default_install(const std::wstring& fullInfPath, bool* rebootRequired);

	bool inf_default_uninstall(const std::wstring& fullInfPath, bool* rebootRequired);

    bool find_by_hwid(const std::wstring& matchstring);
};
