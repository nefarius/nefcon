// ReSharper disable CppInconsistentNaming
#pragma once

#include <newdev.h>
#include <type_traits>

namespace nefarius::util
{
    // Enum to represent the result of the function call
    enum class FunctionCallResult
    {
        NotAvailable,
        Failure,
        Success
    };

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
        decltype(DiUninstallDriverW)* fpDiUninstallDriverW = _dll["DiUninstallDriverW"];
        decltype(DiInstallDriverW)* fpDiInstallDriverW = _dll["DiInstallDriverW"];
        decltype(DiUninstallDevice)* fpDiUninstallDevice = _dll["DiUninstallDevice"];
        decltype(UpdateDriverForPlugAndPlayDevicesW)* fpUpdateDriverForPlugAndPlayDevicesW = _dll[
            "UpdateDriverForPlugAndPlayDevicesW"];

        // Wrapper function to handle the function call and return the result
        template <typename Func, typename... Args>
        FunctionCallResult CallFunction(Func func, Args... args)
        {
            if (!func)
            {
                return FunctionCallResult::NotAvailable;
            }

            const auto ret = func(args...);
            return ret ? FunctionCallResult::Success : FunctionCallResult::Failure;
        }
    };
}
