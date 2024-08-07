#pragma once

#include <chrono>
#include <map>
#include "colorwin.hpp"
#include <easylogging++.h>

using namespace colorwin;

class ConsoleColorLogDispatchCallback : public el::LogDispatchCallback
{
    std::unordered_map<el::Level, CW_COLORS> logLevelToColor = {
        {el::Level::Debug, white},
        {el::Level::Info, green},
        {el::Level::Warning, yellow},
        {el::Level::Error, red},
        {el::Level::Fatal, magenta},
        {el::Level::Verbose, white}
    };

protected:
    void handle(const el::LogDispatchData* data) noexcept override
    {
        // Extract log message details
        const el::LogMessage* logMessage = data->logMessage();
        const auto now = std::chrono::system_clock::now();
        std::string timestamp = std::format("{:%FT%TZ}", std::chrono::time_point_cast<std::chrono::seconds>(now));
        std::string level = logMessage->level() == el::Level::Debug
                                ? "DEBUG"
                                : logMessage->level() == el::Level::Info
                                ? "INFO"
                                : logMessage->level() == el::Level::Warning
                                ? "WARNING"
                                : logMessage->level() == el::Level::Error
                                ? "ERROR"
                                : logMessage->level() == el::Level::Fatal
                                ? "FATAL"
                                : "VERBOSE";
        std::string message = logMessage->message();

        // scoped color
        {
            std::cout << timestamp << " " << color(logLevelToColor[logMessage->level()]) << level;
        }
        std::cout << " " << message << '\n';
    }
};
