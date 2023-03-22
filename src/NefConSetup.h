#pragma once

#include "easylogging++.h"
#include "framework.h"

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace winapi
{
    BOOL AdjustProcessPrivileges();

    BOOL CreateDriverService(PCSTR ServiceName, PCSTR DisplayName, PCSTR BinaryPath);

    BOOL DeleteDriverService(PCSTR ServiceName);

    std::string GetLastErrorStdStr(DWORD errorCode = ERROR_SUCCESS);

    std::string GetVersionFromFile(std::string FilePath);

    std::string GetImageBasePath();

    DWORD IsAppRunningAsAdminMode(PBOOL IsAdmin);

    BOOL GetLogonSID(HANDLE hToken, PSID *ppsid);

    BOOL TakeFileOwnership(el::Logger* logger, LPCWSTR file);

    BOOL SetPrivilege(LPCWSTR privilege, int enable);
};
