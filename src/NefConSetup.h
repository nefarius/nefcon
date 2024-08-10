#pragma once

#include "easylogging++.h"
#include "framework.h"

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace winapi
{
    std::string GetLastErrorStdStr(DWORD errorCode = ERROR_SUCCESS);

    std::string GetImageBasePath();
};
