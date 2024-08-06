// ReSharper disable CppInconsistentNaming
#pragma once

#include <newdev.h>
#include <type_traits>

class ProcPtr
{
public:
    explicit ProcPtr(FARPROC ptr) : _ptr(ptr)
    {
    }

    template <typename T, typename = std::enable_if_t<std::is_function_v<T>>>
    operator T*() const
    {
        return reinterpret_cast<T*>(_ptr);
    }

private:
    FARPROC _ptr;
};

class DllHelper
{
public:
    explicit DllHelper(LPCTSTR filename) : _module(LoadLibrary(filename))
    {
    }

    ~DllHelper() { FreeLibrary(_module); }

    ProcPtr operator[](LPCSTR proc_name) const
    {
        return ProcPtr(GetProcAddress(_module, proc_name));
    }

    static HMODULE _parent_module;

private:
    HMODULE _module;
};

class Newdev
{
private:
    DllHelper _dll{L"Newdev.dll"};

public:
    decltype(DiUninstallDriverW)* CallDiUninstallDriverW = _dll["DiUninstallDriverW"];
    decltype(DiInstallDriverW)* CallDiInstallDriverW = _dll["DiInstallDriverW"];
    decltype(DiUninstallDevice)* CallDiUninstallDevice = _dll["DiUninstallDevice"];
    decltype(UpdateDriverForPlugAndPlayDevicesW)* CallUpdateDriverForPlugAndPlayDevicesW = _dll[
        "UpdateDriverForPlugAndPlayDevicesW"];
};
