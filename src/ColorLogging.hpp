#pragma once

#include <chrono>
#include <map>
#include "colorwin.hpp"
#include <easylogging++.h>


class ConsoleColorLogDispatchCallback : public el::LogDispatchCallback
{
    std::unordered_map<el::Level, colorwin::CW_COLORS> logLevelToColor = {
        {el::Level::Debug, colorwin::white},
        {el::Level::Info, colorwin::green},
        {el::Level::Warning, colorwin::yellow},
        {el::Level::Error, colorwin::red},
        {el::Level::Fatal, colorwin::magenta},
        {el::Level::Verbose, colorwin::white}
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
            std::cout << timestamp << " " << colorwin::color(logLevelToColor[logMessage->level()]) << level;
        }
        std::cout << " " << message << '\n';
    }
};
