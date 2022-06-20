#pragma once

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
};
