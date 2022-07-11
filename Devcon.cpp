#include "Devcon.h"

//
// WinAPI
// 
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <SetupAPI.h>
#include <tchar.h>
#include <devguid.h>
#include <newdev.h>
#include <Shlwapi.h>
#include <strsafe.h>
#include <cfgmgr32.h>
#include <Shlobj.h>

//
// STL
// 
#include <vector>

//
// Logging
// 
#include "easylogging++.h"

//
// Dynamic module helper
// 
#include "LibraryHelper.hpp"

//
// Hooking
// 
#include <detours/detours.h>

static decltype(MessageBoxW)* real_MessageBoxW = MessageBoxW;

int DetourMessageBoxW(
	HWND    hWnd,
	LPCWSTR lpText,
	LPCWSTR lpCaption,
	UINT    uType
);

static BOOL g_MbCalled = FALSE;

static decltype(SHChangeNotify)* real_SHChangeNotify = SHChangeNotify;

void DetourSHChangeNotify(
	LONG    wEventId,
	UINT    uFlags,
	LPCVOID dwItem1,
	LPCVOID dwItem2
);

static decltype(RestartDialogEx)* real_RestartDialogEx = RestartDialogEx;

int DetourRestartDialogEx(
	HWND   hwnd,
	PCWSTR pszPrompt,
	DWORD  dwReturn,
	DWORD  dwReasonCode
);

static decltype(InitiateSystemShutdownExW)* real_InitiateSystemShutdownExW = InitiateSystemShutdownExW;

BOOL DetourInitiateSystemShutdownExW(
	LPWSTR lpMachineName,
	LPWSTR lpMessage,
	DWORD  dwTimeout,
	BOOL   bForceAppsClosed,
	BOOL   bRebootAfterShutdown,
	DWORD  dwReason
);


// Helper function to build a multi-string from a vector<wstring>
inline std::vector<wchar_t> BuildMultiString(const std::vector<std::wstring>& data)
{
	// Special case of the empty multi-string
	if (data.empty())
	{
		// Build a vector containing just two NULs
		return std::vector<wchar_t>(2, L'\0');
	}

	// Get the total length in wchar_ts of the multi-string
	size_t totalLen = 0;
	for (const auto& s : data)
	{
		// Add one to current string's length for the terminating NUL
		totalLen += (s.length() + 1);
	}

	// Add one for the last NUL terminator (making the whole structure double-NUL terminated)
	totalLen++;

	// Allocate a buffer to store the multi-string
	std::vector<wchar_t> multiString;
	multiString.reserve(totalLen);

	// Copy the single strings into the multi-string
	for (const auto& s : data)
	{
		multiString.insert(multiString.end(), s.begin(), s.end());

		// Don't forget to NUL-terminate the current string
		multiString.push_back(L'\0');
	}

	// Add the last NUL-terminator
	multiString.push_back(L'\0');

	return multiString;
}

auto devcon::create(const std::wstring& className, const GUID* classGuid, const std::wstring& hardwareId) -> bool
{
	el::Logger* logger = el::Loggers::getLogger("default");
	const auto deviceInfoSet = SetupDiCreateDeviceInfoList(classGuid, nullptr);

	if (INVALID_HANDLE_VALUE == deviceInfoSet)
	{
		logger->error("SetupDiCreateDeviceInfoList failed with error code %v", GetLastError());
		return false;
	}

	SP_DEVINFO_DATA deviceInfoData;
	deviceInfoData.cbSize = sizeof(deviceInfoData);

	const auto cdiRet = SetupDiCreateDeviceInfoW(
		deviceInfoSet,
		className.c_str(),
		classGuid,
		nullptr,
		nullptr,
		DICD_GENERATE_ID,
		&deviceInfoData
	);

	if (!cdiRet)
	{
		logger->error("SetupDiCreateDeviceInfoW failed with error code %v", GetLastError());
		SetupDiDestroyDeviceInfoList(deviceInfoSet);
		return false;
	}

	const auto sdrpRet = SetupDiSetDeviceRegistryPropertyW(
		deviceInfoSet,
		&deviceInfoData,
		SPDRP_HARDWAREID,
		(const PBYTE)hardwareId.c_str(),
		static_cast<DWORD>(hardwareId.size() * sizeof(WCHAR))
	);

	if (!sdrpRet)
	{
		logger->error("SetupDiSetDeviceRegistryPropertyW failed with error code %v", GetLastError());
		SetupDiDestroyDeviceInfoList(deviceInfoSet);
		return false;
	}

	const auto cciRet = SetupDiCallClassInstaller(
		DIF_REGISTERDEVICE,
		deviceInfoSet,
		&deviceInfoData
	);

	if (!cciRet)
	{
		logger->error("SetupDiCallClassInstaller failed with error code %v", GetLastError());
		SetupDiDestroyDeviceInfoList(deviceInfoSet);
		return false;
	}

	SetupDiDestroyDeviceInfoList(deviceInfoSet);

	return true;
}

bool devcon::restart_bth_usb_device()
{
	DWORD i, err;
	bool found = false, succeeded = false;

	HDEVINFO hDevInfo;
	SP_DEVINFO_DATA spDevInfoData;

	hDevInfo = SetupDiGetClassDevs(
		&GUID_DEVCLASS_BLUETOOTH,
		nullptr,
		nullptr,
		DIGCF_PRESENT
	);
	if (hDevInfo == INVALID_HANDLE_VALUE)
	{
		return succeeded;
	}

	spDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &spDevInfoData); i++)
	{
		DWORD DataT;
		LPTSTR p, buffer = nullptr;
		DWORD buffersize = 0;

		// get all devices info
		while (!SetupDiGetDeviceRegistryProperty(hDevInfo,
			&spDevInfoData,
			SPDRP_ENUMERATOR_NAME,
			&DataT,
			(PBYTE)buffer,
			buffersize,
			&buffersize))
		{
			if (GetLastError() == ERROR_INVALID_DATA)
			{
				break;
			}
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				if (buffer)
					LocalFree(buffer);
				buffer = (wchar_t*)LocalAlloc(LPTR, buffersize);
			}
			else
			{
				goto cleanup_DeviceInfo;
			}
		}

		if (GetLastError() == ERROR_INVALID_DATA)
			continue;

		//find device with enumerator name "USB"
		for (p = buffer; *p && (p < &buffer[buffersize]); p += lstrlen(p) + sizeof(TCHAR))
		{
			if (!_tcscmp(TEXT("USB"), p))
			{
				found = true;
				break;
			}
		}

		if (buffer)
			LocalFree(buffer);

		// if device found restart
		if (found)
		{
			if (!SetupDiRestartDevices(hDevInfo, &spDevInfoData))
			{
				err = GetLastError();
				break;
			}

			succeeded = true;

			break;
		}
	}

cleanup_DeviceInfo:
	err = GetLastError();
	SetupDiDestroyDeviceInfoList(hDevInfo);
	SetLastError(err);

	return succeeded;
}

bool devcon::enable_disable_bth_usb_device(bool state)
{
	DWORD i, err;
	bool found = false, succeeded = false;

	HDEVINFO hDevInfo;
	SP_DEVINFO_DATA spDevInfoData;

	hDevInfo = SetupDiGetClassDevs(
		&GUID_DEVCLASS_BLUETOOTH,
		nullptr,
		nullptr,
		DIGCF_PRESENT
	);
	if (hDevInfo == INVALID_HANDLE_VALUE)
	{
		return succeeded;
	}

	spDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &spDevInfoData); i++)
	{
		DWORD DataT;
		LPTSTR p, buffer = nullptr;
		DWORD buffersize = 0;

		// get all devices info
		while (!SetupDiGetDeviceRegistryProperty(hDevInfo,
			&spDevInfoData,
			SPDRP_ENUMERATOR_NAME,
			&DataT,
			(PBYTE)buffer,
			buffersize,
			&buffersize))
		{
			if (GetLastError() == ERROR_INVALID_DATA)
			{
				break;
			}
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				if (buffer)
					LocalFree(buffer);
				buffer = (wchar_t*)LocalAlloc(LPTR, buffersize);
			}
			else
			{
				goto cleanup_DeviceInfo;
			}
		}

		if (GetLastError() == ERROR_INVALID_DATA)
			continue;

		//find device with enumerator name "USB"
		for (p = buffer; *p && (p < &buffer[buffersize]); p += lstrlen(p) + sizeof(TCHAR))
		{
			if (!_tcscmp(TEXT("USB"), p))
			{
				found = true;
				break;
			}
		}

		if (buffer)
			LocalFree(buffer);

		// if device found change it's state
		if (found)
		{
			SP_PROPCHANGE_PARAMS params;

			params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
			params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
			params.Scope = DICS_FLAG_GLOBAL;
			params.StateChange = (state) ? DICS_ENABLE : DICS_DISABLE;

			// setup proper parameters            
			if (!SetupDiSetClassInstallParams(hDevInfo, &spDevInfoData, &params.ClassInstallHeader, sizeof(params)))
			{
				err = GetLastError();
			}
			else
			{
				// use parameters
				if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hDevInfo, &spDevInfoData))
				{
					err = GetLastError(); // error here  
				}
				else { succeeded = true; }
			}

			break;
		}
	}

cleanup_DeviceInfo:
	err = GetLastError();
	SetupDiDestroyDeviceInfoList(hDevInfo);
	SetLastError(err);

	return succeeded;
}

bool devcon::install_driver(const std::wstring& fullInfPath, bool* rebootRequired)
{
	el::Logger* logger = el::Loggers::getLogger("default");

	Newdev newdev;
	BOOL reboot;

	if (!newdev.pDiInstallDriverW)
	{
		logger->error("Couldn't find DiInstallDriverW export");
		SetLastError(ERROR_INVALID_FUNCTION);
		return false;
	}

	logger->verbose(1, "Invoking DiInstallDriverW");

	const auto ret = newdev.pDiInstallDriverW(
		nullptr,
		fullInfPath.c_str(),
		DIIRFLAG_FORCE_INF,
		&reboot
	);

	logger->verbose(1, "DiInstallDriverW returned %v, reboot required: %v", ret, reboot);

	if (rebootRequired)
		*rebootRequired = reboot > 1;

	return ret > 0;
}

bool devcon::uninstall_driver(const std::wstring& fullInfPath, bool* rebootRequired)
{
	el::Logger* logger = el::Loggers::getLogger("default");

	Newdev newdev;
	BOOL reboot;

	if (!newdev.pDiUninstallDriverW)
	{
		logger->error("Couldn't find DiUninstallDriverW export");
		SetLastError(ERROR_INVALID_FUNCTION);
		return false;
	}

	logger->verbose(1, "Invoking DiUninstallDriverW");

	const auto ret = newdev.pDiUninstallDriverW(
		nullptr,
		fullInfPath.c_str(),
		0,
		&reboot
	);

	logger->verbose(1, "DiUninstallDriverW returned %v, reboot required: %v", ret, reboot);

	if (rebootRequired)
		*rebootRequired = reboot > 1;

	return ret > 0;
}

bool devcon::add_device_class_filter(const GUID* classGuid, const std::wstring& filterName, DeviceClassFilterPosition::Value position)
{
	el::Logger* logger = el::Loggers::getLogger("default");
	auto key = SetupDiOpenClassRegKey(classGuid, KEY_ALL_ACCESS);

	if (INVALID_HANDLE_VALUE == key)
	{
		logger->error("SetupDiOpenClassRegKey failed with error code %v", GetLastError());
		return false;
	}

	LPCWSTR filterValue = (position == DeviceClassFilterPosition::Lower) ? L"LowerFilters" : L"UpperFilters";
	DWORD type, size;
	std::vector<std::wstring> filters;

	auto status = RegQueryValueExW(
		key,
		filterValue,
		nullptr,
		&type,
		nullptr,
		&size
	);

	//
	// Value exists already, read it with returned buffer size
	// 
	if (status == ERROR_SUCCESS)
	{
		std::vector<wchar_t> temp(size / sizeof(wchar_t));

		status = RegQueryValueExW(
			key,
			filterValue,
			nullptr,
			&type,
			reinterpret_cast<LPBYTE>(&temp[0]),
			&size
		);

		if (status != ERROR_SUCCESS)
		{
			logger->error("RegQueryValueExW failed with status %v", status);
			RegCloseKey(key);
			SetLastError(status);
			return false;
		}

		size_t index = 0;
		size_t len = wcslen(&temp[0]);
		while (len > 0)
		{
			filters.emplace_back(&temp[index]);
			index += len + 1;
			len = wcslen(&temp[index]);
		}

		//
		// Filter not there yet, add
		// 
		if (std::find(filters.begin(), filters.end(), filterName) == filters.end())
		{
			filters.emplace_back(filterName);
		}

		const std::vector<wchar_t> multiString = BuildMultiString(filters);

		const DWORD dataSize = static_cast<DWORD>(multiString.size() * sizeof(wchar_t));

		status = RegSetValueExW(
			key,
			filterValue,
			0, // reserved
			REG_MULTI_SZ,
			reinterpret_cast<const BYTE*>(&multiString[0]),
			dataSize
		);

		if (status != ERROR_SUCCESS)
		{
			logger->error("RegSetValueExW failed with status %v", status);
			RegCloseKey(key);
			SetLastError(status);
			return false;
		}

		RegCloseKey(key);
		return true;
	}
	//
	// Value doesn't exist, create and populate
	// 
	else if (status == ERROR_FILE_NOT_FOUND)
	{
		filters.emplace_back(filterName);

		const std::vector<wchar_t> multiString = BuildMultiString(filters);

		const DWORD dataSize = static_cast<DWORD>(multiString.size() * sizeof(wchar_t));

		status = RegSetValueExW(
			key,
			filterValue,
			0, // reserved
			REG_MULTI_SZ,
			reinterpret_cast<const BYTE*>(&multiString[0]),
			dataSize
		);

		if (status != ERROR_SUCCESS)
		{
			logger->error("RegSetValueExW failed with status %v", status);
			RegCloseKey(key);
			SetLastError(status);
			return false;
		}

		RegCloseKey(key);
		return true;
	}

	RegCloseKey(key);
	return false;
}

bool devcon::remove_device_class_filter(const GUID* classGuid, const std::wstring& filterName, DeviceClassFilterPosition::Value position)
{
	el::Logger* logger = el::Loggers::getLogger("default");
	auto key = SetupDiOpenClassRegKey(classGuid, KEY_ALL_ACCESS);

	if (INVALID_HANDLE_VALUE == key)
	{
		logger->error("SetupDiOpenClassRegKey failed with error code %v", GetLastError());
		return false;
	}

	LPCWSTR filterValue = (position == DeviceClassFilterPosition::Lower) ? L"LowerFilters" : L"UpperFilters";
	DWORD type, size;
	std::vector<std::wstring> filters;

	auto status = RegQueryValueExW(
		key,
		filterValue,
		nullptr,
		&type,
		nullptr,
		&size
	);

	//
	// Value exists already, read it with returned buffer size
	// 
	if (status == ERROR_SUCCESS)
	{
		std::vector<wchar_t> temp(size / sizeof(wchar_t));

		status = RegQueryValueExW(
			key,
			filterValue,
			nullptr,
			&type,
			reinterpret_cast<LPBYTE>(&temp[0]),
			&size
		);

		if (status != ERROR_SUCCESS)
		{
			logger->error("RegQueryValueExW failed with status %v", status);
			RegCloseKey(key);
			SetLastError(status);
			return false;
		}

		//
		// Remove value, if found
		//
		size_t index = 0;
		size_t len = wcslen(&temp[0]);
		while (len > 0)
		{
			if (filterName != &temp[index])
			{
				filters.emplace_back(&temp[index]);
			}
			index += len + 1;
			len = wcslen(&temp[index]);
		}

		const std::vector<wchar_t> multiString = BuildMultiString(filters);

		const DWORD dataSize = static_cast<DWORD>(multiString.size() * sizeof(wchar_t));

		status = RegSetValueExW(
			key,
			filterValue,
			0, // reserved
			REG_MULTI_SZ,
			reinterpret_cast<const BYTE*>(&multiString[0]),
			dataSize
		);

		if (status != ERROR_SUCCESS)
		{
			logger->error("RegSetValueExW failed with status %v", status);
			RegCloseKey(key);
			SetLastError(status);
			return false;
		}

		RegCloseKey(key);
		return true;
	}
	//
	// Value doesn't exist, return
	// 
	if (status == ERROR_FILE_NOT_FOUND)
	{
		RegCloseKey(key);
		return true;
	}

	RegCloseKey(key);
	return false;
}

inline bool uninstall_device_and_driver(HDEVINFO hDevInfo, PSP_DEVINFO_DATA spDevInfoData, bool* rebootRequired)
{
	el::Logger* logger = el::Loggers::getLogger("default");
	BOOL drvNeedsReboot = FALSE, devNeedsReboot = FALSE;
	DWORD requiredBufferSize = 0;
	DWORD err = ERROR_SUCCESS;
	bool ret = false;

	Newdev newdev;

	if (!newdev.pDiUninstallDevice || !newdev.pDiUninstallDriverW)
	{
		logger->error("Couldn't get DiUninstallDevice or DiUninstallDriverW function exports");
		SetLastError(ERROR_INVALID_FUNCTION);
		return false;
	}

	SP_DRVINFO_DATA_W drvInfoData;
	drvInfoData.cbSize = sizeof(drvInfoData);

	PSP_DRVINFO_DETAIL_DATA_W pDrvInfoDetailData = nullptr;

	do
	{
		logger->verbose(1, "Enumerating");

		//
		// Start building driver info
		// 
		if (!SetupDiBuildDriverInfoList(
			hDevInfo,
			spDevInfoData,
			SPDIT_COMPATDRIVER
		))
		{
			err = GetLastError();
			logger->error("SetupDiBuildDriverInfoList failed, error code: %v", err);
			break;
		}

		if (!SetupDiEnumDriverInfo(
			hDevInfo,
			spDevInfoData,
			SPDIT_COMPATDRIVER,
			0, // One result expected
			&drvInfoData
		))
		{
			err = GetLastError();
			logger->error("SetupDiEnumDriverInfo failed, error code: %v", err);
			break;
		}

		//
		// Details will contain the INF path to driver store copy
		// 
		SP_DRVINFO_DETAIL_DATA_W drvInfoDetailData;
		drvInfoDetailData.cbSize = sizeof(drvInfoDetailData);

		//
		// Request required buffer size
		// 
		(void)SetupDiGetDriverInfoDetail(
			hDevInfo,
			spDevInfoData,
			&drvInfoData,
			&drvInfoDetailData,
			drvInfoDetailData.cbSize,
			&requiredBufferSize
		);

		if (requiredBufferSize == 0)
		{
			err = GetLastError();
			logger->error("SetupDiGetDriverInfoDetail (size) failed, error code: %v", err);
			break;
		}

		//
		// Allocate required amount
		// 
		pDrvInfoDetailData = static_cast<PSP_DRVINFO_DETAIL_DATA_W>(malloc(requiredBufferSize));

		if (pDrvInfoDetailData == nullptr)
		{
			logger->error("Out of memory");
			err = ERROR_INSUFFICIENT_BUFFER;
			break;
		}

		pDrvInfoDetailData->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);

		//
		// Query full driver details
		// 
		if (!SetupDiGetDriverInfoDetail(
			hDevInfo,
			spDevInfoData,
			&drvInfoData,
			pDrvInfoDetailData,
			requiredBufferSize,
			nullptr
		))
		{
			err = GetLastError();
			logger->error("SetupDiGetDriverInfoDetail (payload) failed, error code: %v", err);
			break;
		}

		//
		// Remove device
		// 
		if (!newdev.pDiUninstallDevice(
			nullptr,
			hDevInfo,
			spDevInfoData,
			0,
			&devNeedsReboot
		))
		{
			err = GetLastError();
			logger->error("DiUninstallDevice failed, error code: %v", err);
			break;
		}

		//
		// Uninstall from driver store
		// 
		if (!newdev.pDiUninstallDriverW(
			nullptr,
			pDrvInfoDetailData->InfFileName,
			0,
			&drvNeedsReboot
		))
		{
			err = GetLastError();
			logger->error("DiUninstallDriverW failed, error code: %v", err);
			break;
		}

		*rebootRequired = (drvNeedsReboot > 0) || (devNeedsReboot > 0);
		ret = true;

		logger->verbose(1, "Reboot required: %v", *rebootRequired);

	} while (FALSE);

	if (pDrvInfoDetailData)
		free(pDrvInfoDetailData);

	(void)SetupDiDestroyDriverInfoList(
		hDevInfo,
		spDevInfoData,
		SPDIT_COMPATDRIVER
	);

	SetLastError(err);

	logger->verbose(1, "Freed memory, returning with %v and error code %v", ret, err);

	return ret;
}

static PWSTR wstristr(PCWSTR haystack, PCWSTR needle) {
	do {
		PCWSTR h = haystack;
		PCWSTR n = needle;
		while (towlower(*h) == towlower(*n) && *n) {
			h++;
			n++;
		}
		if (*n == 0) {
			return (PWSTR)haystack;
		}
	} while (*haystack++);
	return NULL;
}

bool devcon::uninstall_device_and_driver(const GUID* classGuid, const std::wstring& hardwareId, bool* rebootRequired)
{
	el::Logger* logger = el::Loggers::getLogger("default");
	DWORD err = ERROR_SUCCESS;
	bool succeeded = false;

	HDEVINFO hDevInfo;
	SP_DEVINFO_DATA spDevInfoData;

	hDevInfo = SetupDiGetClassDevs(
		classGuid,
		nullptr,
		nullptr,
		DIGCF_PRESENT
	);
	if (hDevInfo == INVALID_HANDLE_VALUE)
	{
		logger->error("SetupDiGetClassDevs failed, error code: %v", GetLastError());
		return succeeded;
	}

	spDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

	for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &spDevInfoData); i++)
	{
		logger->verbose(1, "SetupDiEnumDeviceInfo with index %v", i);

		DWORD DataT;
		LPWSTR p, buffer = nullptr;
		DWORD buffersize = 0;

		// get all devices info
		while (!SetupDiGetDeviceRegistryProperty(hDevInfo,
			&spDevInfoData,
			SPDRP_HARDWAREID,
			&DataT,
			reinterpret_cast<PBYTE>(buffer),
			buffersize,
			&buffersize))
		{
			if (GetLastError() == ERROR_INVALID_DATA)
			{
				logger->verbose(1, "SetupDiGetDeviceRegistryProperty returned ERROR_INVALID_DATA");
				break;
			}
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				logger->verbose(1, "(Re-)allocating property buffer, bytes: %v", buffersize);
				if (buffer)
					LocalFree(buffer);
				buffer = static_cast<wchar_t*>(LocalAlloc(LPTR, buffersize));
			}
			else
			{
				logger->error("Unexpected error during SetupDiGetDeviceRegistryProperty: %v", GetLastError());
				goto cleanup_DeviceInfo;
			}
		}

		if (GetLastError() == ERROR_INVALID_DATA)
		{
			logger->verbose(1, "SetupDiGetDeviceRegistryProperty returned ERROR_INVALID_DATA");
			continue;
		}

		logger->verbose(1, "Got Hardware ID property, starting enumeration");

		//
		// find device matching hardware ID
		// 
		for (p = buffer; p && *p && (p < &buffer[buffersize]); p += lstrlenW(p) + sizeof(TCHAR))
		{
			logger->verbose(1, "Enumerating ID: %v", std::wstring(p));

			if (wstristr(p, hardwareId.c_str()))
			{
				logger->verbose(1, "Found match against %v, attempting removal", hardwareId);

				succeeded = ::uninstall_device_and_driver(hDevInfo, &spDevInfoData, rebootRequired);
				err = GetLastError();
				logger->verbose(1, "Removal response: %v, error code: %v", succeeded, err);
				break;
			}
		}

		logger->verbose(1, "Done enumerating Hardware IDs");

		if (buffer)
			LocalFree(buffer);
	}

cleanup_DeviceInfo:
	err = GetLastError();
	SetupDiDestroyDeviceInfoList(hDevInfo);
	SetLastError(err);

	logger->verbose(1, "Freed memory and returning with status: %v", succeeded);

	return succeeded;
}

bool devcon::inf_default_install(const std::wstring& fullInfPath, bool* rebootRequired)
{
	el::Logger* logger = el::Loggers::getLogger("default");
	uint32_t errCode = ERROR_SUCCESS;
	SYSTEM_INFO sysInfo;
	HINF hInf = INVALID_HANDLE_VALUE;
	WCHAR InfSectionWithExt[255];
	WCHAR pszDest[280];
	BOOLEAN defaultSection = FALSE;

	GetNativeSystemInfo(&sysInfo);

	hInf = SetupOpenInfFileW(fullInfPath.c_str(), nullptr, INF_STYLE_WIN4, nullptr);

	do
	{
		if (hInf == INVALID_HANDLE_VALUE)
		{
			errCode = GetLastError();
			logger->error("SetupOpenInfFileW failed with error code %v", errCode);
			break;
		}

		if (SetupDiGetActualSectionToInstallW(
			hInf,
			L"DefaultInstall",
			InfSectionWithExt,
			0xFFu,
			reinterpret_cast<PDWORD>(&sysInfo.lpMinimumApplicationAddress),
			nullptr)
			&& SetupFindFirstLineW(hInf, InfSectionWithExt, nullptr, reinterpret_cast<PINFCONTEXT>(&sysInfo.lpMaximumApplicationAddress)))
		{
			logger->verbose(1, "DefaultInstall section found");
			defaultSection = TRUE;

			if (StringCchPrintfW(pszDest, 280ui64, L"DefaultInstall 132 %ws", fullInfPath.c_str()) < 0)
			{
				errCode = GetLastError();
				logger->error("StringCchPrintfW failed with error code %v", errCode);
				break;
			}

			logger->verbose(1, "Calling InstallHinfSectionW");

			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());
			DetourAttach((void**)&real_MessageBoxW, DetourMessageBoxW);
			DetourAttach((void**)&real_SHChangeNotify, DetourSHChangeNotify);
			DetourAttach((void**)&real_RestartDialogEx, DetourRestartDialogEx);
			DetourAttach((void**)&real_InitiateSystemShutdownExW, DetourInitiateSystemShutdownExW);
			DetourTransactionCommit();

			g_MbCalled = FALSE;

			InstallHinfSectionW(nullptr, nullptr, pszDest, 0);

			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());
			DetourDetach((void**)&real_MessageBoxW, DetourMessageBoxW);
			DetourDetach((void**)&real_SHChangeNotify, DetourSHChangeNotify);
			DetourDetach((void**)&real_RestartDialogEx, DetourRestartDialogEx);
			DetourDetach((void**)&real_InitiateSystemShutdownExW, DetourInitiateSystemShutdownExW);
			DetourTransactionCommit();

			logger->verbose(1, "InstallHinfSectionW finished");

			//
			// If a message box call was intercepted, we encountered an error
			// 
			if (g_MbCalled)
			{
				logger->error("The installation encountered an error, make sure there's no reboot pending and try again afterwards");
				g_MbCalled = FALSE;
				errCode = ERROR_PNP_REBOOT_REQUIRED;
				break;
			}
		}

		if (!SetupFindFirstLineW(hInf, L"Manufacturer", nullptr, reinterpret_cast<PINFCONTEXT>(&sysInfo.lpMaximumApplicationAddress)))
		{
			logger->verbose(1, "No Manufacturer section found");

			if (!defaultSection)
			{
				logger->error("No DefaultInstall and no Manufacturer section, can't continue");
				errCode = ERROR_SECTION_NOT_FOUND;
			}
			break;
		}

		Newdev newdev;
		BOOL reboot;

		if (!newdev.pDiInstallDriverW)
		{
			logger->error("Couldn't find DiInstallDriverW export");
			SetLastError(ERROR_INVALID_FUNCTION);
			return false;
		}

		logger->verbose(1, "Invoking DiInstallDriverW");

		const auto ret = newdev.pDiInstallDriverW(
			nullptr,
			fullInfPath.c_str(),
			0,
			&reboot
		);

		logger->verbose(1, "DiInstallDriverW returned with %v, reboot required: %v", ret, reboot);

		if (rebootRequired)
			*rebootRequired = reboot > 1;

	} while (FALSE);

	if (hInf != INVALID_HANDLE_VALUE)
	{
		SetupCloseInfFile(hInf);
	}

	logger->verbose(1, "Returning with error code %v", errCode);

	SetLastError(errCode);
	return errCode == ERROR_SUCCESS;
}

bool devcon::inf_default_uninstall(const std::wstring& fullInfPath, bool* rebootRequired)
{
	el::Logger* logger = el::Loggers::getLogger("default");
	uint32_t errCode = ERROR_SUCCESS;
	SYSTEM_INFO sysInfo;
	HINF hInf = INVALID_HANDLE_VALUE;
	WCHAR InfSectionWithExt[255];
	WCHAR pszDest[280];

	GetNativeSystemInfo(&sysInfo);

	hInf = SetupOpenInfFileW(fullInfPath.c_str(), nullptr, INF_STYLE_WIN4, nullptr);

	do
	{
		if (hInf == INVALID_HANDLE_VALUE)
		{
			errCode = GetLastError();
			logger->error("SetupOpenInfFileW failed with error code %v", errCode);
			break;
		}

		if (SetupDiGetActualSectionToInstallW(
			hInf,
			L"DefaultUninstall",
			InfSectionWithExt,
			0xFFu,
			reinterpret_cast<PDWORD>(&sysInfo.lpMinimumApplicationAddress),
			nullptr)
			&& SetupFindFirstLineW(hInf, InfSectionWithExt, nullptr, reinterpret_cast<PINFCONTEXT>(&sysInfo.lpMaximumApplicationAddress)))
		{
			if (StringCchPrintfW(pszDest, 280ui64, L"DefaultUninstall 132 %ws", fullInfPath.c_str()) < 0)
			{
				errCode = GetLastError();
				logger->error("StringCchPrintfW failed with error code %v", errCode);
				break;
			}

			logger->verbose(1, "Calling InstallHinfSectionW");

			InstallHinfSectionW(nullptr, nullptr, pszDest, 0);

			logger->verbose(1, "InstallHinfSectionW finished");
		}
		else
		{
			logger->error("No DefaultUninstall section found");
			errCode = ERROR_SECTION_NOT_FOUND;
		}

	} while (FALSE);

	if (hInf != INVALID_HANDLE_VALUE)
	{
		SetupCloseInfFile(hInf);
	}

	logger->verbose(1, "Returning with error code %v", errCode);

	SetLastError(errCode);
	return errCode == ERROR_SUCCESS;
}

int DetourMessageBoxW(
	HWND    hWnd,
	LPCWSTR lpText,
	LPCWSTR lpCaption,
	UINT    uType
)
{
	el::Logger* logger = el::Loggers::getLogger("default");

	hWnd; lpCaption; uType;

	logger->verbose(1, "DetourMessageBoxW called with message: %v", std::wstring(lpText));
	logger->verbose(1, "GetLastError: %v", GetLastError());

	g_MbCalled = TRUE;

	return IDOK;
}

void DetourSHChangeNotify(
	LONG    wEventId,
	UINT    uFlags,
	LPCVOID dwItem1,
	LPCVOID dwItem2
)
{
	el::Logger* logger = el::Loggers::getLogger("default");

	logger->verbose(1, "DetourSHChangeNotify called");
	logger->verbose(1, "wEventId: %v, uFlags: %v", wEventId, uFlags);

	return real_SHChangeNotify(wEventId, uFlags, dwItem1, dwItem2);
}

int DetourRestartDialogEx(
	HWND   hwnd,
	PCWSTR pszPrompt,
	DWORD  dwReturn,
	DWORD  dwReasonCode
)
{
	el::Logger* logger = el::Loggers::getLogger("default");

	logger->verbose(1, "DetourRestartDialogEx called");
	logger->verbose(1, "pszPrompt: %v, dwReturn: %v, dwReasonCode: %v", std::wstring(pszPrompt), dwReturn, dwReasonCode);

	return real_RestartDialogEx(hwnd, pszPrompt, dwReturn, dwReasonCode);
}

BOOL DetourInitiateSystemShutdownExW(
	LPWSTR lpMachineName,
	LPWSTR lpMessage,
	DWORD  dwTimeout,
	BOOL   bForceAppsClosed,
	BOOL   bRebootAfterShutdown,
	DWORD  dwReason
)
{
	el::Logger* logger = el::Loggers::getLogger("default");

	logger->verbose(1, "DetourInitiateSystemShutdownExA called");
	logger->verbose(1, "lpMessage: %v, dwTimeout: %v, bForceAppsClosed: %v, bRebootAfterShutdown: %v, dwReason: %v",
		std::wstring(lpMessage), dwTimeout, bForceAppsClosed, bRebootAfterShutdown, dwReason);

	return real_InitiateSystemShutdownExW(lpMachineName, lpMessage, dwTimeout, bForceAppsClosed, bRebootAfterShutdown, dwReason);
}
