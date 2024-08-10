#include "NefConSetup.h"


//__declspec(deprecated)
std::string winapi::GetLastErrorStdStr(DWORD errorCode)
{
    DWORD error = (errorCode == ERROR_SUCCESS) ? GetLastError() : errorCode;
    if (error)
    {
        LPVOID lpMsgBuf;
        DWORD bufLen = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&lpMsgBuf,
            0, nullptr);
        if (bufLen)
        {
            auto lpMsgStr = static_cast<LPCSTR>(lpMsgBuf);
            std::string result(lpMsgStr, lpMsgStr + bufLen);

            LocalFree(lpMsgBuf);

            return result;
        }
    }
    return std::string("LEER");
}

std::string winapi::GetImageBasePath()
{
    char myPath[MAX_PATH + 1] = {0};

    GetModuleFileNameA(
        reinterpret_cast<HINSTANCE>(&__ImageBase),
        myPath,
        MAX_PATH + 1
    );

    return std::string(myPath);
}
