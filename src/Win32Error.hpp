#pragma once

#include <windows.h>
#include <string>
#include <format>

#include "UniUtil.h"

namespace nefarius::util
{
    class Win32Error
    {
    public:
        explicit Win32Error(std::string additionalMessage) : errorCode(GetLastError()),
                                                             additionalMessage(std::move(additionalMessage))
        {
        }

        explicit Win32Error(DWORD errorCode = GetLastError(), std::string additionalMessage = "") :
            errorCode(errorCode),
            additionalMessage(std::move(additionalMessage))
        {
        }

        [[nodiscard]] DWORD getErrorCode() const
        {
            return errorCode;
        }

        [[nodiscard]] std::string getErrorMessageA() const
        {
            char* messageBuffer = nullptr;
            size_t messageSize = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                errorCode,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPSTR)&messageBuffer,
                0,
                NULL
            );

            std::string message(messageBuffer, messageSize);
            LocalFree(messageBuffer);

            if (additionalMessage.empty())
                return message;

            return std::format(
                "{} failed with error: {} ({})",
                additionalMessage,
                message,
                errorCode
            );
        }

        [[nodiscard]] std::wstring getErrorMessageW() const
        {
            wchar_t* messageBuffer = nullptr;
            size_t messageSize = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                errorCode,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPWSTR)&messageBuffer,
                0,
                NULL
            );

            std::wstring message(messageBuffer, messageSize);
            LocalFree(messageBuffer);

            if (additionalMessage.empty())
                return message;

            return ConvertAnsiToWide(std::format(
                "{} failed with error: {} ({})",
                additionalMessage,
                ConvertWideToANSI(message),
                errorCode
            ));
        }

    private:
        DWORD errorCode;
        std::string additionalMessage;
    };
}
