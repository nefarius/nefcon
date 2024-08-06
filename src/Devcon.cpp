// ReSharper disable CppClangTidyCppcoreguidelinesAvoidGoto
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
#include <ShlObj.h>

//
// STL
// 
#include <vector>
#include <iomanip>
#include <numeric>


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

using namespace nefarius::util;

static decltype(MessageBoxW)* real_MessageBoxW = MessageBoxW;

int DetourMessageBoxW(
    HWND hWnd,
    LPCWSTR lpText,
    LPCWSTR lpCaption,
    UINT uType
);

static BOOL g_MbCalled = FALSE;

static decltype(RestartDialogEx)* real_RestartDialogEx = RestartDialogEx;

int DetourRestartDialogEx(
    HWND hwnd,
    PCWSTR pszPrompt,
    DWORD dwReturn,
    DWORD dwReasonCode
);

static BOOL g_RestartDialogExCalled = FALSE;


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

bool devcon::create(const std::wstring& className, const GUID* classGuid, const std::wstring& hardwareId)
{
    el::Logger* logger = el::Loggers::getLogger("default");
    bool success = false;
    DWORD win32Error = ERROR_SUCCESS;
    const auto deviceInfoSet = SetupDiCreateDeviceInfoList(classGuid, nullptr);

    if (INVALID_HANDLE_VALUE == deviceInfoSet)
    {
        logger->error("SetupDiCreateDeviceInfoList failed with error code %v", GetLastError());
        goto exit;
    }

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(deviceInfoData);

    //
    // Create new device node
    // 
    if (!SetupDiCreateDeviceInfoW(
        deviceInfoSet,
        className.c_str(),
        classGuid,
        nullptr,
        nullptr,
        DICD_GENERATE_ID,
        &deviceInfoData
    ))
    {
        win32Error = GetLastError();
        logger->error("SetupDiCreateDeviceInfoW failed with error code %v", win32Error);
        goto exit;
    }

    //
    // Add the HardwareID to the Device's HardwareID property.
    //
    if (!SetupDiSetDeviceRegistryPropertyW(
        deviceInfoSet,
        &deviceInfoData,
        SPDRP_HARDWAREID,
        (const PBYTE)hardwareId.c_str(),
        static_cast<DWORD>(hardwareId.size() * sizeof(WCHAR))
    ))
    {
        win32Error = GetLastError();
        logger->error("SetupDiSetDeviceRegistryPropertyW failed with error code %v", win32Error);
        goto exit;
    }

    //
    // Transform the registry element into an actual device node in the PnP HW tree
    //
    if (!SetupDiCallClassInstaller(
        DIF_REGISTERDEVICE,
        deviceInfoSet,
        &deviceInfoData
    ))
    {
        win32Error = GetLastError();
        logger->error("SetupDiCallClassInstaller failed with error code %v", win32Error);
        goto exit;
    }

    success = true;

exit:

    if (deviceInfoSet != INVALID_HANDLE_VALUE)
    {
        SetupDiDestroyDeviceInfoList(deviceInfoSet);
    }

    SetLastError(win32Error);

    return success;
}

std::expected<bool, Win32Error> devcon::update(const std::wstring& hardwareId, const std::wstring& fullInfPath,
                                               bool* rebootRequired, bool force)
{
    Newdev newdev;
    DWORD flags = 0;
    BOOL reboot = FALSE;

    if (force)
        flags |= INSTALLFLAG_FORCE;

    switch (newdev.CallFunction(
        newdev.fpUpdateDriverForPlugAndPlayDevicesW,
        nullptr,
        hardwareId.c_str(),
        fullInfPath.c_str(),
        flags,
        &reboot
    ))
    {
    case FunctionCallResult::NotAvailable:
        return std::unexpected(Win32Error(ERROR_INVALID_FUNCTION));
    case FunctionCallResult::Failure:
        return std::unexpected(Win32Error(GetLastError()));
    case FunctionCallResult::Success:
        if (rebootRequired)
            *rebootRequired = reboot > 0;
        return true;
    }

    return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
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
                buffer = static_cast<wchar_t*>(LocalAlloc(LPTR, buffersize));
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
                buffer = static_cast<wchar_t*>(LocalAlloc(LPTR, buffersize));
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

    switch (newdev.CallFunction(
        newdev.fpDiInstallDriverW,
        nullptr,
        fullInfPath.c_str(),
        DIIRFLAG_FORCE_INF,
        &reboot
    ))
    {
    case FunctionCallResult::NotAvailable:
        logger->error("Couldn't find DiInstallDriverW export");
        SetLastError(ERROR_INVALID_FUNCTION);
        return false;
    case FunctionCallResult::Failure:
        return false;
    case FunctionCallResult::Success:
        if (rebootRequired)
        {
            *rebootRequired = reboot > 0;
        }
        return true;
    }

    return false;
}

bool devcon::uninstall_driver(const std::wstring& fullInfPath, bool* rebootRequired)
{
    el::Logger* logger = el::Loggers::getLogger("default");

    Newdev newdev;
    BOOL reboot;

    switch (newdev.CallFunction(
        newdev.fpDiUninstallDriverW,
        nullptr,
        fullInfPath.c_str(),
        0,
        &reboot
    ))
    {
    case FunctionCallResult::NotAvailable:
        logger->error("Couldn't find DiUninstallDriverW export");
        SetLastError(ERROR_INVALID_FUNCTION);
        return false;
    case FunctionCallResult::Failure:
        return false;
    case FunctionCallResult::Success:
        if (rebootRequired)
        {
            *rebootRequired = reboot > 0;
        }
        return true;
    }

    return false;
}

bool devcon::add_device_class_filter(const GUID* classGuid, const std::wstring& filterName,
                                     DeviceClassFilterPosition::Value position)
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
    if (status == ERROR_FILE_NOT_FOUND)
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

bool devcon::remove_device_class_filter(const GUID* classGuid, const std::wstring& filterName,
                                        DeviceClassFilterPosition::Value position)
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

    if (!newdev.fpDiUninstallDevice || !newdev.fpDiUninstallDriverW)
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
        if (!newdev.fpDiUninstallDevice(
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
        if (!newdev.fpDiUninstallDriverW(
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
    }
    while (FALSE);

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

static PWSTR wstristr(PCWSTR haystack, PCWSTR needle)
{
    do
    {
        PCWSTR h = haystack;
        PCWSTR n = needle;
        while (towlower(*h) == towlower(*n) && *n)
        {
            h++;
            n++;
        }
        if (*n == 0)
        {
            return (PWSTR)haystack;
        }
    }
    while (*haystack++);
    return nullptr;
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
    auto hInf = INVALID_HANDLE_VALUE;
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
            && SetupFindFirstLineW(hInf, InfSectionWithExt, nullptr,
                                   reinterpret_cast<PINFCONTEXT>(&sysInfo.lpMaximumApplicationAddress)))
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
            DetourAttach((void**)&real_RestartDialogEx, DetourRestartDialogEx);
            DetourTransactionCommit();

            g_MbCalled = FALSE;
            g_RestartDialogExCalled = FALSE;

            InstallHinfSectionW(nullptr, nullptr, pszDest, 0);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach((void**)&real_MessageBoxW, DetourMessageBoxW);
            DetourDetach((void**)&real_RestartDialogEx, DetourRestartDialogEx);
            DetourTransactionCommit();

            logger->verbose(1, "InstallHinfSectionW finished");

            //
            // If a message box call was intercepted, we encountered an error
            // 
            if (g_MbCalled)
            {
                logger->error(
                    "The installation encountered an error, make sure there's no reboot pending and try again afterwards");
                g_MbCalled = FALSE;
                errCode = ERROR_PNP_REBOOT_REQUIRED;
                break;
            }
        }

        if (!SetupFindFirstLineW(hInf, L"Manufacturer", nullptr,
                                 reinterpret_cast<PINFCONTEXT>(&sysInfo.lpMaximumApplicationAddress)))
        {
            logger->verbose(1, "No Manufacturer section found");

            if (!defaultSection)
            {
                logger->error("No DefaultInstall and no Manufacturer section, can't continue");
                errCode = ERROR_SECTION_NOT_FOUND;
                break;
            }
        }

        Newdev newdev;
        BOOL reboot = FALSE;

        switch (newdev.CallFunction(
            newdev.fpDiInstallDriverW,
            nullptr,
            fullInfPath.c_str(),
            0,
            &reboot
        ))
        {
        case FunctionCallResult::NotAvailable:
            logger->error("Couldn't find DiInstallDriverW export");
            SetLastError(ERROR_INVALID_FUNCTION);
            break;
        case FunctionCallResult::Failure:
            errCode = GetLastError();
            break;
        case FunctionCallResult::Success:
            if (rebootRequired)
            {
                *rebootRequired = reboot > 0 || g_RestartDialogExCalled;
                logger->verbose(1, "Set rebootRequired to: %v", *rebootRequired);
            }
            errCode = ERROR_SUCCESS;
            break;
        }
    }
    while (FALSE);

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
    auto hInf = INVALID_HANDLE_VALUE;
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
            && SetupFindFirstLineW(hInf, InfSectionWithExt, nullptr,
                                   reinterpret_cast<PINFCONTEXT>(&sysInfo.lpMaximumApplicationAddress)))
        {
            if (StringCchPrintfW(pszDest, 280ui64, L"DefaultUninstall 132 %ws", fullInfPath.c_str()) < 0)
            {
                errCode = GetLastError();
                logger->error("StringCchPrintfW failed with error code %v", errCode);
                break;
            }

            logger->verbose(1, "Calling InstallHinfSectionW");

            g_RestartDialogExCalled = FALSE;

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach((void**)&real_RestartDialogEx, DetourRestartDialogEx);
            DetourTransactionCommit();

            InstallHinfSectionW(nullptr, nullptr, pszDest, 0);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourDetach((void**)&real_RestartDialogEx, DetourRestartDialogEx);
            DetourTransactionCommit();

            logger->verbose(1, "InstallHinfSectionW finished");

            if (rebootRequired)
            {
                *rebootRequired = g_RestartDialogExCalled;
                logger->verbose(1, "Set rebootRequired to: %v", *rebootRequired);
            }
        }
        else
        {
            logger->error("No DefaultUninstall section found");
            errCode = ERROR_SECTION_NOT_FOUND;
        }
    }
    while (FALSE);

    if (hInf != INVALID_HANDLE_VALUE)
    {
        SetupCloseInfFile(hInf);
    }

    logger->verbose(1, "Returning with error code %v", errCode);

    SetLastError(errCode);
    return errCode == ERROR_SUCCESS;
}

bool devcon::find_by_hwid(const std::wstring& matchstring)
{
    el::Logger* logger = el::Loggers::getLogger("default");
    bool found = FALSE;
    DWORD err, total = 0;
    SP_DEVINFO_DATA spDevInfoData;

    DWORD DataT;
    LPTSTR buffer = nullptr;
    DWORD buffersize = 0;

    const HDEVINFO hDevInfo = SetupDiGetClassDevs(
        nullptr,
        nullptr,
        nullptr,
        DIGCF_ALLCLASSES | DIGCF_PRESENT
    );
    if (hDevInfo == INVALID_HANDLE_VALUE)
    {
        return found;
    }

    spDevInfoData.cbSize = sizeof(spDevInfoData);

    for (DWORD devIndex = 0; SetupDiEnumDeviceInfo(hDevInfo, devIndex, &spDevInfoData); devIndex++)
    {
        // get all devices info
        while (!SetupDiGetDeviceRegistryProperty(hDevInfo,
                                                 &spDevInfoData,
                                                 SPDRP_HARDWAREID,
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
                buffer = static_cast<wchar_t*>(LocalAlloc(LPTR, buffersize));
            }
            else
            {
                goto cleanup_DeviceInfo;
            }
        }
        if (GetLastError() == ERROR_INVALID_DATA)
            continue;

        std::vector<std::wstring> entries;
        const TCHAR* p = buffer;

        while (*p)
        {
            entries.emplace_back(p);
            p += _tcslen(p) + 1;
        }

        bool foundMatch = FALSE;

        for (auto& i : entries)
        {
            if (i.find(matchstring) != std::wstring::npos)
            {
                foundMatch = TRUE;
                break;
            }
        }

        // If we have a match, print out the whole array
        if (foundMatch)
        {
            found = TRUE;
            total++;

            std::wstring idValue = std::accumulate(std::begin(entries), std::end(entries), std::wstring(),
                                                   [](const std::wstring& ss, const std::wstring& s)
                                                   {
                                                       return ss.empty() ? s : ss + L", " + s;
                                                   });

            logger->info("Hardware IDs: %v", idValue);

            while (!SetupDiGetDeviceRegistryProperty(hDevInfo,
                                                     &spDevInfoData,
                                                     SPDRP_DEVICEDESC,
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
                    buffer = static_cast<wchar_t*>(LocalAlloc(LPTR, buffersize));
                }
                else
                {
                    goto cleanup_DeviceInfo;
                }
            }
            if (GetLastError() == ERROR_INVALID_DATA)
            {
                // Lets try SPDRP_DEVICEDESC
                while (!SetupDiGetDeviceRegistryProperty(hDevInfo,
                                                         &spDevInfoData,
                                                         SPDRP_FRIENDLYNAME,
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
                        buffer = static_cast<wchar_t*>(LocalAlloc(LPTR, buffersize));
                    }
                    else
                    {
                        goto cleanup_DeviceInfo;
                    }
                }
                if (GetLastError() != ERROR_INVALID_DATA)
                {
                    logger->info("Name: %v", std::wstring(buffer));
                }
            }
            else
            {
                logger->info("Name: %v", std::wstring(buffer));
            }

            // Build a list of driver info items that we will retrieve below
            if (!SetupDiBuildDriverInfoList(hDevInfo, &spDevInfoData, SPDIT_COMPATDRIVER))
            {
                goto cleanup_DeviceInfo;
            }

            // Get the first info item for this driver
            SP_DRVINFO_DATA drvInfo;
            drvInfo.cbSize = sizeof(SP_DRVINFO_DATA);

            if (!SetupDiEnumDriverInfo(hDevInfo, &spDevInfoData, SPDIT_COMPATDRIVER, 0, &drvInfo))
            {
                goto cleanup_DeviceInfo; // Still fails with "no more items"
            }

            logger->info("Version: %v.%v.%v.%v", std::to_wstring((drvInfo.DriverVersion >> 48) & 0xFFFF),
                         std::to_wstring((drvInfo.DriverVersion >> 32) & 0xFFFF),
                         std::to_wstring((drvInfo.DriverVersion >> 16) & 0xFFFF),
                         std::to_wstring(drvInfo.DriverVersion & 0x0000FFFF));
        }
    }

cleanup_DeviceInfo:
    err = GetLastError();
    SetupDiDestroyDeviceInfoList(hDevInfo);
    SetLastError(err);
    logger->verbose(1, "Total Found:: %v", std::to_wstring(total));
    return found;
}


//
// Hooks MessageBoxW which is called if an error occurred, even when instructed to suppress any UI interaction
// 
int DetourMessageBoxW(
    HWND hWnd,
    LPCWSTR lpText,
    LPCWSTR lpCaption,
    UINT uType
)
{
    el::Logger* logger = el::Loggers::getLogger("default");

    UNREFERENCED_PARAMETER(hWnd);
    UNREFERENCED_PARAMETER(lpCaption);
    UNREFERENCED_PARAMETER(uType);

    logger->verbose(1, "DetourMessageBoxW called with message: %v", std::wstring(lpText));
    logger->verbose(1, "GetLastError: %v", GetLastError());

    g_MbCalled = TRUE;

    return IDOK;
}

//
// Hooks RestartDialogEx which is called if a reboot is required, even when instructed to suppress any UI interaction
// 
int DetourRestartDialogEx(
    HWND hwnd,
    PCWSTR pszPrompt,
    DWORD dwReturn,
    DWORD dwReasonCode
)
{
    el::Logger* logger = el::Loggers::getLogger("default");

    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(pszPrompt);

    logger->verbose(1, "DetourRestartDialogEx called");
    logger->verbose(1, "GetLastError: %v", GetLastError());
    logger->verbose(1, "DetourRestartDialogEx - dwReturn: %v", dwReturn);
    logger->verbose(1, "DetourRestartDialogEx - dwReasonCode: %v", dwReasonCode);

    /* for debugging

    const int result = real_RestartDialogEx(hwnd, pszPrompt, dwReturn, dwReasonCode);

    logger->verbose(1, "RestartDialogEx returned: %v", result);
    */

    g_RestartDialogExCalled = TRUE;

    return IDCANCEL; // equivalent to the user clicking "Restart Later"
}
