// ReSharper disable CppClangTidyCppcoreguidelinesAvoidGoto
// ReSharper disable CppClangTidyModernizeUseEmplace
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
// Packages
// 
#include <detours/detours.h>
#include <scope_guard.hpp>
#include <wil/resource.h>

#include "MultiStringArray.hpp"
#include "ScopeGuards.hpp"

using namespace nefarius::util;

#pragma region Hooking

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

#pragma endregion


// Helper function to build a multi-string from a vector<wstring>
inline std::vector<wchar_t> BuildMultiString(const std::vector<std::wstring>& data)
{
    // Special case of the empty multi-string
    if (data.empty())
    {
        // Build a vector containing just two NULs
        return std::vector(2, L'\0');
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

static std::expected<wil::unique_hlocal_ptr<uint8_t[]>, Win32Error> GetDeviceRegistryProperty(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ DWORD Property,
    _Out_opt_ PDWORD PropertyRegDataType,
    _Out_opt_ PDWORD BufferSize
)
{
    DWORD sizeRequired = 0;

    //
    // Query required size
    // 
    (void)SetupDiGetDeviceRegistryProperty(DeviceInfoSet,
                                           DeviceInfoData,
                                           Property,
                                           PropertyRegDataType,
                                           NULL,
                                           0,
                                           &sizeRequired);

    DWORD win32Error = GetLastError();

    //
    // Property doesn't exist
    // 
    if (win32Error == ERROR_INVALID_DATA)
    {
        return std::unexpected(Win32Error(ERROR_NOT_FOUND, "SetupDiGetDeviceRegistryProperty"));
    }

    //
    // Unexpected status other than required size
    // 
    if (win32Error != ERROR_INSUFFICIENT_BUFFER)
    {
        return std::unexpected(Win32Error(win32Error, "SetupDiGetDeviceRegistryProperty"));
    }

    auto buffer = wil::make_unique_hlocal_nothrow<uint8_t[]>(sizeRequired);

    //
    // Query property value
    // 
    if (!SetupDiGetDeviceRegistryProperty(DeviceInfoSet,
                                          DeviceInfoData,
                                          Property,
                                          PropertyRegDataType,
                                          buffer.get(),
                                          sizeRequired,
                                          &sizeRequired))
    {
        win32Error = GetLastError();
        buffer.release();
        return std::unexpected(Win32Error(win32Error, "SetupDiGetDeviceRegistryProperty"));
    }

    if (BufferSize)
        *BufferSize = sizeRequired;

    return buffer;
}

std::expected<void, Win32Error> devcon::create(const std::wstring& className, const GUID* classGuid,
                                               const WideMultiStringArray& hardwareId)
{
    HDEVINFOHandleGuard hDevInfo(SetupDiCreateDeviceInfoList(classGuid, nullptr));

    if (hDevInfo.is_invalid())
    {
        return std::unexpected(Win32Error(GetLastError(), "SetupDiCreateDeviceInfoList"));
    }

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(deviceInfoData);

    //
    // Create new device node
    // 
    if (!SetupDiCreateDeviceInfoW(
        hDevInfo.get(),
        className.c_str(),
        classGuid,
        nullptr,
        nullptr,
        DICD_GENERATE_ID,
        &deviceInfoData
    ))
    {
        return std::unexpected(Win32Error(GetLastError(), "SetupDiCreateDeviceInfoW"));
    }

    //
    // Add the HardwareID to the Device's HardwareID property.
    //
    if (!SetupDiSetDeviceRegistryPropertyW(
        hDevInfo.get(),
        &deviceInfoData,
        SPDRP_HARDWAREID,
        hardwareId.data(),
        static_cast<DWORD>(hardwareId.size())
    ))
    {
        return std::unexpected(Win32Error(GetLastError(), "SetupDiSetDeviceRegistryPropertyW"));
    }

    //
    // Transform the registry element into an actual device node in the PnP HW tree
    //
    if (!SetupDiCallClassInstaller(
        DIF_REGISTERDEVICE,
        hDevInfo.get(),
        &deviceInfoData
    ))
    {
        return std::unexpected(Win32Error(GetLastError(), "SetupDiCallClassInstaller"));
    }

    return {};
}

std::expected<void, Win32Error> devcon::update(const std::wstring& hardwareId, const std::wstring& fullInfPath,
                                               bool* rebootRequired, bool force)
{
    Newdev newdev;
    DWORD flags = 0;
    BOOL reboot = FALSE;
    WCHAR normalisedInfPath[MAX_PATH] = {};

    const auto ret = GetFullPathNameW(fullInfPath.c_str(), MAX_PATH, normalisedInfPath, NULL);

    if ((ret >= MAX_PATH) || (ret == FALSE))
    {
        return std::unexpected(Win32Error(ERROR_BAD_PATHNAME));
    }

    if (force)
        flags |= INSTALLFLAG_FORCE;

    switch (newdev.CallFunction(
        newdev.fpUpdateDriverForPlugAndPlayDevicesW,
        nullptr,
        hardwareId.c_str(),
        normalisedInfPath,
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
        return {};
    }

    return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

std::expected<void, Win32Error> devcon::restart_bth_usb_device(int instance)
{
    bool found = false;
    SP_DEVINFO_DATA spDevInfoData;

    HDEVINFOHandleGuard hDevInfo(SetupDiGetClassDevs(
        &GUID_DEVCLASS_BLUETOOTH,
        nullptr,
        nullptr,
        DIGCF_PRESENT
    ));

    if (hDevInfo.is_invalid())
    {
        return std::unexpected(Win32Error(GetLastError(), "SetupDiGetClassDevs"));
    }

    spDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    if (!SetupDiEnumDeviceInfo(hDevInfo.get(), instance, &spDevInfoData))
    {
        std::unexpected(Win32Error(GetLastError(), "SetupDiEnumDeviceInfo"));
    }

    DWORD bufferSize = 0;
    const auto enumeratorProperty = GetDeviceRegistryProperty(
        hDevInfo.get(),
        &spDevInfoData,
        SPDRP_ENUMERATOR_NAME,
        NULL,
        &bufferSize
    );

    if (!enumeratorProperty)
    {
        return std::unexpected(enumeratorProperty.error());
    }

    const LPTSTR buffer = (LPTSTR)enumeratorProperty.value().get();

    // find device with enumerator name "USB"
    for (LPTSTR p = buffer; p && *p && (p < &buffer[bufferSize]); p += lstrlen(p) + sizeof(TCHAR))
    {
        if (!_tcscmp(TEXT("USB"), p))
        {
            found = true;
            break;
        }
    }

    // if device found restart
    if (found)
    {
        if (!SetupDiRestartDevices(hDevInfo.get(), &spDevInfoData))
        {
            std::unexpected(Win32Error(GetLastError(), "SetupDiRestartDevices"));
        }

        return {};
    }

    return std::unexpected(Win32Error(ERROR_NOT_FOUND));
}

std::expected<void, Win32Error> devcon::enable_disable_bth_usb_device(bool state, int instance)
{
    bool found = false;
    SP_DEVINFO_DATA spDevInfoData;

    HDEVINFOHandleGuard hDevInfo(SetupDiGetClassDevs(
        &GUID_DEVCLASS_BLUETOOTH,
        nullptr,
        nullptr,
        DIGCF_PRESENT
    ));

    if (hDevInfo.is_invalid())
    {
        return std::unexpected(Win32Error(GetLastError(), "SetupDiGetClassDevs"));
    }

    if (!SetupDiEnumDeviceInfo(hDevInfo.get(), instance, &spDevInfoData))
    {
        return std::unexpected(Win32Error(GetLastError(), "SetupDiEnumDeviceInfo"));
    }

    DWORD bufferSize = 0;
    const auto enumeratorProperty = GetDeviceRegistryProperty(
        hDevInfo.get(),
        &spDevInfoData,
        SPDRP_ENUMERATOR_NAME,
        NULL,
        &bufferSize
    );

    if (!enumeratorProperty)
    {
        return std::unexpected(enumeratorProperty.error());
    }

    const LPTSTR buffer = (LPTSTR)enumeratorProperty.value().get();

    // find device with enumerator name "USB"
    for (LPTSTR p = buffer; p && *p && (p < &buffer[bufferSize]); p += lstrlen(p) + sizeof(TCHAR))
    {
        if (!_tcscmp(TEXT("USB"), p))
        {
            found = true;
            break;
        }
    }

    // if device found change it's state
    if (found)
    {
        SP_PROPCHANGE_PARAMS params;

        params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
        // ReSharper disable once CppAssignedValueIsNeverUsed
        params.Scope = DICS_FLAG_GLOBAL;
        // ReSharper disable once CppAssignedValueIsNeverUsed
        params.StateChange = (state) ? DICS_ENABLE : DICS_DISABLE;

        // setup proper parameters            
        if (!SetupDiSetClassInstallParams(hDevInfo.get(), &spDevInfoData, &params.ClassInstallHeader, sizeof(params)))
        {
            return std::unexpected(Win32Error(GetLastError(), "SetupDiSetClassInstallParams"));
        }

        // use parameters
        if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hDevInfo.get(), &spDevInfoData))
        {
            return std::unexpected(Win32Error(GetLastError(), "SetupDiCallClassInstaller"));
        }

        return {};
    }

    return std::unexpected(Win32Error(ERROR_NOT_FOUND));
}

std::expected<void, Win32Error> devcon::install_driver(const std::wstring& fullInfPath,
                                                       bool* rebootRequired)
{
    Newdev newdev;
    BOOL reboot;
    WCHAR normalisedInfPath[MAX_PATH] = {};

    const auto ret = GetFullPathNameW(fullInfPath.c_str(), MAX_PATH, normalisedInfPath, NULL);

    if ((ret >= MAX_PATH) || (ret == FALSE))
    {
        return std::unexpected(Win32Error(ERROR_BAD_PATHNAME));
    }

    switch (newdev.CallFunction(
        newdev.fpDiInstallDriverW,
        nullptr,
        normalisedInfPath,
        DIIRFLAG_FORCE_INF,
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
        return {};
    }

    return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

std::expected<void, Win32Error> devcon::uninstall_driver(const std::wstring& fullInfPath, bool* rebootRequired)
{
    Newdev newdev;
    BOOL reboot;
    WCHAR normalisedInfPath[MAX_PATH] = {};

    const auto ret = GetFullPathNameW(fullInfPath.c_str(), MAX_PATH, normalisedInfPath, NULL);

    if ((ret >= MAX_PATH) || (ret == FALSE))
    {
        return std::unexpected(Win32Error(ERROR_BAD_PATHNAME));
    }

    switch (newdev.CallFunction(
        newdev.fpDiUninstallDriverW,
        nullptr,
        normalisedInfPath,
        0,
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
        return {};
    }

    return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

std::expected<void, Win32Error> devcon::add_device_class_filter(const GUID* classGuid, const std::wstring& filterName,
                                                                DeviceClassFilterPosition position)
{
    HKEYHandleGuard key(SetupDiOpenClassRegKey(classGuid, KEY_ALL_ACCESS));

    if (key.is_invalid())
    {
        return std::unexpected(Win32Error("SetupDiOpenClassRegKey"));
    }

    LPCWSTR filterValue = (position == DeviceClassFilterPosition::Lower) ? L"LowerFilters" : L"UpperFilters";
    DWORD type, size;
    std::vector<std::wstring> filters;

    auto status = RegQueryValueExW(
        key.get(),
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
            key.get(),
            filterValue,
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(&temp[0]),
            &size
        );

        if (status != ERROR_SUCCESS)
        {
            return std::unexpected(Win32Error(status, "RegQueryValueExW"));
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
        if (std::ranges::find(filters, filterName) == filters.end())
        {
            filters.emplace_back(filterName);
        }

        const std::vector<wchar_t> multiString = BuildMultiString(filters);

        const DWORD dataSize = static_cast<DWORD>(multiString.size() * sizeof(wchar_t));

        status = RegSetValueExW(
            key.get(),
            filterValue,
            0, // reserved
            REG_MULTI_SZ,
            reinterpret_cast<const BYTE*>(&multiString[0]),
            dataSize
        );

        if (status != ERROR_SUCCESS)
        {
            return std::unexpected(Win32Error(status, "RegSetValueExW"));
        }

        return {};
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
            key.get(),
            filterValue,
            0, // reserved
            REG_MULTI_SZ,
            reinterpret_cast<const BYTE*>(&multiString[0]),
            dataSize
        );

        if (status != ERROR_SUCCESS)
        {
            return std::unexpected(Win32Error(status, "RegSetValueExW"));
        }

        return {};
    }

    return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

std::expected<void, nefarius::util::Win32Error> devcon::remove_device_class_filter(const GUID* classGuid, const std::wstring& filterName,
                                        DeviceClassFilterPosition position)
{
    HKEYHandleGuard key(SetupDiOpenClassRegKey(classGuid, KEY_ALL_ACCESS));

    if (key.is_invalid())
    {
        return std::unexpected(Win32Error("SetupDiOpenClassRegKey"));
    }

    LPCWSTR filterValue = (position == DeviceClassFilterPosition::Lower) ? L"LowerFilters" : L"UpperFilters";
    DWORD type, size;
    std::vector<std::wstring> filters;

    auto status = RegQueryValueExW(
        key.get(),
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
            key.get(),
            filterValue,
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(&temp[0]),
            &size
        );

        if (status != ERROR_SUCCESS)
        {
            return std::unexpected(Win32Error(status, "RegQueryValueExW"));
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
            key.get(),
            filterValue,
            0, // reserved
            REG_MULTI_SZ,
            reinterpret_cast<const BYTE*>(&multiString[0]),
            dataSize
        );

        if (status != ERROR_SUCCESS)
        {
            return std::unexpected(Win32Error(status, "RegSetValueExW"));
        }

        return {};
    }
    //
    // Value doesn't exist, return
    // 
    if (status == ERROR_FILE_NOT_FOUND)
    {
        return {};
    }

    return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

inline std::expected<void, Win32Error> uninstall_device_and_driver(
    HDEVINFO hDevInfo, PSP_DEVINFO_DATA spDevInfoData, bool* rebootRequired)
{
    BOOL drvNeedsReboot = FALSE, devNeedsReboot = FALSE;
    DWORD requiredBufferSize = 0;
    Newdev newdev;

    if (!newdev.fpDiUninstallDevice || !newdev.fpDiUninstallDriverW)
    {
        return std::unexpected(Win32Error(ERROR_INVALID_FUNCTION));
    }

    SP_DRVINFO_DATA_W drvInfoData;
    drvInfoData.cbSize = sizeof(drvInfoData);

    //
    // Start building driver info
    // 
    if (!SetupDiBuildDriverInfoList(
        hDevInfo,
        spDevInfoData,
        SPDIT_COMPATDRIVER
    ))
    {
        return std::unexpected(Win32Error("SetupDiBuildDriverInfoList"));
    }

    if (!SetupDiEnumDriverInfo(
        hDevInfo,
        spDevInfoData,
        SPDIT_COMPATDRIVER,
        0, // One result expected
        &drvInfoData
    ))
    {
        return std::unexpected(Win32Error("SetupDiEnumDriverInfo"));
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
        return std::unexpected(Win32Error("SetupDiGetDriverInfoDetail"));
    }

    //
    // Allocate required amount
    // 
    PSP_DRVINFO_DETAIL_DATA_W pDrvInfoDetailData = static_cast<PSP_DRVINFO_DETAIL_DATA_W>(malloc(requiredBufferSize));

    const auto dataGuard = sg::make_scope_guard([pDrvInfoDetailData]() noexcept
    {
        if (pDrvInfoDetailData != nullptr)
        {
            free(pDrvInfoDetailData);
        }
    });

    if (pDrvInfoDetailData == nullptr)
    {
        return std::unexpected(Win32Error(ERROR_INSUFFICIENT_BUFFER));
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
        return std::unexpected(Win32Error("SetupDiGetDriverInfoDetail"));
    }

    const auto driverGuard = sg::make_scope_guard([hDevInfo, spDevInfoData]() noexcept
    {
        //
        // SetupDiGetDriverInfoDetail allocated memory we need to explicitly free again
        // 
        SetupDiDestroyDriverInfoList(
            hDevInfo,
            spDevInfoData,
            SPDIT_COMPATDRIVER
        );
    });

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
        return std::unexpected(Win32Error("DiUninstallDevice"));
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
        return std::unexpected(Win32Error("DiUninstallDriverW"));
    }

    if (rebootRequired)
        *rebootRequired = (drvNeedsReboot > 0) || (devNeedsReboot > 0);

    return {};
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

std::vector<std::expected<void, Win32Error>> devcon::uninstall_device_and_driver(
    const GUID* classGuid, const std::wstring& hardwareId, bool* rebootRequired)
{
    std::vector<std::expected<void, Win32Error>> results;

    SP_DEVINFO_DATA spDevInfoData;

    HDEVINFOHandleGuard hDevInfo(SetupDiGetClassDevs(
        classGuid,
        nullptr,
        nullptr,
        DIGCF_PRESENT
    ));

    if (hDevInfo.is_invalid())
    {
        results.push_back(std::unexpected(Win32Error("SetupDiGetClassDevs")));
        return results;
    }

    spDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo.get(), i, &spDevInfoData); i++)
    {
        DWORD bufferSize = 0;
        const auto hwIdBuffer = GetDeviceRegistryProperty(
            hDevInfo.get(),
            &spDevInfoData,
            SPDRP_HARDWAREID,
            NULL,
            &bufferSize
        );

        if (!hwIdBuffer)
        {
            results.push_back(std::unexpected(hwIdBuffer.error()));
            continue;
        }

        LPWSTR buffer = (LPWSTR)hwIdBuffer.value().get();

        //
        // find device matching hardware ID
        // 
        for (LPWSTR p = buffer; p && *p && (p < &buffer[bufferSize]); p += lstrlenW(p) + sizeof(TCHAR))
        {
            if (wstristr(p, hardwareId.c_str()))
            {
                results.push_back(::uninstall_device_and_driver(
                    hDevInfo.get(),
                    &spDevInfoData,
                    rebootRequired
                ));
                break;
            }
        }

        LocalFree(buffer);
    }

    return results;
}

std::expected<void, Win32Error> devcon::inf_default_install(
    const std::wstring& fullInfPath, bool* rebootRequired)
{
    SYSTEM_INFO sysInfo;
    WCHAR InfSectionWithExt[LINE_LEN] = {};
    constexpr int maxCmdLine = 280;
    WCHAR pszDest[maxCmdLine] = {};
    BOOLEAN hasDefaultSection = FALSE;

    GetNativeSystemInfo(&sysInfo);

    WCHAR normalisedInfPath[MAX_PATH] = {};

    const auto ret = GetFullPathNameW(fullInfPath.c_str(), MAX_PATH, normalisedInfPath, NULL);

    if ((ret >= MAX_PATH) || (ret == FALSE))
    {
        return std::unexpected(Win32Error(ERROR_BAD_PATHNAME));
    }

    INFHandleGuard hInf(SetupOpenInfFileW(normalisedInfPath, nullptr, INF_STYLE_WIN4, nullptr));

    if (hInf.is_invalid())
    {
        return std::unexpected(Win32Error());
    }

    //
    // Try default section first, which is common to class filter driver, filesystem drivers and alike
    // 
    if (SetupDiGetActualSectionToInstallW(
            hInf.get(),
            L"DefaultInstall",
            InfSectionWithExt,
            LINE_LEN,
            reinterpret_cast<PDWORD>(&sysInfo.lpMinimumApplicationAddress),
            nullptr)
        && SetupFindFirstLineW(
            hInf.get(),
            InfSectionWithExt,
            nullptr,
            reinterpret_cast<PINFCONTEXT>(&sysInfo.lpMaximumApplicationAddress)
        ))
    {
        hasDefaultSection = TRUE;

        if (StringCchPrintfW(pszDest, maxCmdLine, L"DefaultInstall 132 %ws", normalisedInfPath) < 0)
        {
            return std::unexpected(Win32Error("StringCchPrintfW"));
        }

        //
        // Some implementations are bugged and do not respect the non-interactive flags,
        // so we catch the use of common dialog APIs and nullify their impact :)
        // 

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

        //
        // If a message box call was intercepted, we encountered an error
        // 
        if (g_MbCalled)
        {
            g_MbCalled = FALSE;
            return std::unexpected(Win32Error(ERROR_PNP_REBOOT_REQUIRED, "InstallHinfSectionW"));
        }
    }

    //
    // If we have no Default, but a Manufacturer section we can attempt classic installation
    // 
    if (!SetupFindFirstLineW(
        hInf.get(),
        L"Manufacturer",
        nullptr,
        reinterpret_cast<PINFCONTEXT>(&sysInfo.lpMaximumApplicationAddress)
    ))
    {
        //
        // We need either one or the other, this INF appears to not be compatible with this install method
        // 
        if (!hasDefaultSection)
        {
            return std::unexpected(Win32Error(ERROR_SECTION_NOT_FOUND, "SetupFindFirstLineW"));
        }
    }

    Newdev newdev;
    BOOL reboot = FALSE;

    switch (newdev.CallFunction(
        newdev.fpDiInstallDriverW,
        nullptr,
        normalisedInfPath,
        0,
        &reboot
    ))
    {
    case FunctionCallResult::NotAvailable:
        return std::unexpected(Win32Error(ERROR_INVALID_FUNCTION));
    case FunctionCallResult::Failure:
        return std::unexpected(Win32Error());
    case FunctionCallResult::Success:
        if (rebootRequired)
        {
            *rebootRequired = reboot > FALSE || g_RestartDialogExCalled;
        }

        return {};
    }

    return std::unexpected(Win32Error(ERROR_INTERNAL_ERROR));
}

std::expected<void, Win32Error> devcon::inf_default_uninstall(const std::wstring& fullInfPath, bool* rebootRequired)
{
    SYSTEM_INFO sysInfo;
    WCHAR InfSectionWithExt[LINE_LEN] = {};
    constexpr int maxCmdLine = 280;
    WCHAR pszDest[maxCmdLine] = {};

    GetNativeSystemInfo(&sysInfo);

    WCHAR normalisedInfPath[MAX_PATH] = {};

    const auto ret = GetFullPathNameW(fullInfPath.c_str(), MAX_PATH, normalisedInfPath, NULL);

    if ((ret >= MAX_PATH) || (ret == FALSE))
    {
        return std::unexpected(Win32Error(ERROR_BAD_PATHNAME));
    }

    INFHandleGuard hInf(SetupOpenInfFileW(normalisedInfPath, nullptr, INF_STYLE_WIN4, nullptr));

    if (hInf.is_invalid())
    {
        return std::unexpected(Win32Error());
    }

    if (SetupDiGetActualSectionToInstallW(
            hInf.get(),
            L"DefaultUninstall",
            InfSectionWithExt,
            LINE_LEN,
            reinterpret_cast<PDWORD>(&sysInfo.lpMinimumApplicationAddress),
            nullptr)
        && SetupFindFirstLineW(
            hInf.get(),
            InfSectionWithExt,
            nullptr,
            reinterpret_cast<PINFCONTEXT>(&sysInfo.lpMaximumApplicationAddress)
        ))
    {
        if (StringCchPrintfW(pszDest, maxCmdLine, L"DefaultUninstall 132 %ws", normalisedInfPath) < 0)
        {
            return std::unexpected(Win32Error("StringCchPrintfW"));
        }

        g_RestartDialogExCalled = FALSE;

        //
        // Some implementations are bugged and do not respect the non-interactive flags,
        // so we catch the use of common dialog APIs and nullify their impact :)
        // 

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach((void**)&real_RestartDialogEx, DetourRestartDialogEx);
        DetourTransactionCommit();

        InstallHinfSectionW(nullptr, nullptr, pszDest, 0);

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach((void**)&real_RestartDialogEx, DetourRestartDialogEx);
        DetourTransactionCommit();

        if (rebootRequired)
        {
            *rebootRequired = g_RestartDialogExCalled;
        }

        return {};
    }

    return std::unexpected(Win32Error(ERROR_SECTION_NOT_FOUND));
}

std::expected<std::vector<devcon::FindByHwIdResult>, Win32Error> devcon::find_by_hwid(const std::wstring& matchstring)
{
    bool found = FALSE;
    DWORD total = 0;
    SP_DEVINFO_DATA spDevInfoData;

    std::vector<FindByHwIdResult> results;

    HDEVINFOHandleGuard hDevInfo(SetupDiGetClassDevs(
        nullptr,
        nullptr,
        nullptr,
        DIGCF_ALLCLASSES | DIGCF_PRESENT
    ));

    if (hDevInfo.is_invalid())
    {
        return std::unexpected(Win32Error("SetupDiGetClassDevs"));
    }

    spDevInfoData.cbSize = sizeof(spDevInfoData);

    for (DWORD devIndex = 0; SetupDiEnumDeviceInfo(hDevInfo.get(), devIndex, &spDevInfoData); devIndex++)
    {
        DWORD bufferSize = 0;
        const auto hwIdProperty = GetDeviceRegistryProperty(
            hDevInfo.get(),
            &spDevInfoData,
            SPDRP_HARDWAREID,
            NULL,
            &bufferSize
        );

        if (!hwIdProperty)
        {
            continue;
        }

        LPTSTR hwIdsBuffer = (LPTSTR)hwIdProperty.value().get();

        std::vector<std::wstring> entries;
        const TCHAR* p = hwIdsBuffer;

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

            FindByHwIdResult result{entries};

            const auto descProperty = GetDeviceRegistryProperty(
                hDevInfo.get(),
                &spDevInfoData,
                SPDRP_DEVICEDESC,
                NULL,
                &bufferSize
            );

            LPTSTR nameBuffer = NULL;

            //
            // Try Device Description...
            // 
            if (!descProperty)
            {
                //
                // ...then Friendly Name
                // 
                const auto nameProperty = GetDeviceRegistryProperty(
                    hDevInfo.get(),
                    &spDevInfoData,
                    SPDRP_FRIENDLYNAME,
                    NULL,
                    &bufferSize
                );

                if (!nameProperty)
                {
                    continue;
                }

                nameBuffer = (LPTSTR)nameProperty.value().get();
            }
            else
            {
                nameBuffer = (LPTSTR)descProperty.value().get();
            }

            result.Name = std::wstring(nameBuffer);

            // Build a list of driver info items that we will retrieve below
            if (!SetupDiBuildDriverInfoList(hDevInfo.get(), &spDevInfoData, SPDIT_COMPATDRIVER))
            {
                continue;
            }

            const auto driverGuard = sg::make_scope_guard([&hDevInfo, &spDevInfoData]() noexcept
            {
                SetupDiDestroyDriverInfoList(hDevInfo.get(), &spDevInfoData, SPDIT_COMPATDRIVER);
            });

            // Get the first info item for this driver
            SP_DRVINFO_DATA drvInfo = {};
            drvInfo.cbSize = sizeof(SP_DRVINFO_DATA);

            if (!SetupDiEnumDriverInfo(hDevInfo.get(), &spDevInfoData, SPDIT_COMPATDRIVER, 0, &drvInfo))
            {
                continue;
            }

            result.Version.Major = (drvInfo.DriverVersion >> 48) & 0xFFFF;
            result.Version.Minor = (drvInfo.DriverVersion >> 32) & 0xFFFF;
            result.Version.Build = (drvInfo.DriverVersion >> 16) & 0xFFFF;
            result.Version.Private = drvInfo.DriverVersion & 0x0000FFFF;

            results.push_back(result);
        }
    }

    return results;
}


#pragma region Hooks trampoline functions

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

#pragma endregion
