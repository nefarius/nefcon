#pragma once
#include <Windows.h>
#include <functional>

// Shared by the CLI and regression tests. Each operation owns its output flag;
// no later stage can clear reboot evidence from an earlier one.
class DriverRemovalWorkflow final
{
public:
    using Operation = std::function<DWORD(bool&)>;
    static DWORD Run(bool restartNeedsReboot, const Operation& removeService, const Operation& purgePackages)
    {
        bool servicePending = false;
        const DWORD serviceError = removeService(servicePending);
        if (serviceError != ERROR_SUCCESS)
            return serviceError;
        // A package purge must not run while the service registration is still live.
        if (servicePending)
            return ERROR_SUCCESS_REBOOT_REQUIRED;
        bool purgeNeedsReboot = false;
        const DWORD purgeError = purgePackages(purgeNeedsReboot);
        if (purgeError != ERROR_SUCCESS)
            return purgeError;
        return restartNeedsReboot || purgeNeedsReboot ? ERROR_SUCCESS_REBOOT_REQUIRED : ERROR_SUCCESS;
    }
};
