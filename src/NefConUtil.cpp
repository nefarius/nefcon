// ReSharper disable CppTooWideScope
// ReSharper disable CppClangTidyBugproneNarrowingConversions
// ReSharper disable CppClangTidyHicppAvoidGoto
#include "NefConUtil.h"

#include <numeric>

using namespace colorwin;

INITIALIZE_EASYLOGGINGPP

//
// Enable Visual Styles for message box
// 
#pragma comment(linker,"\"/manifestdependency:type='win32' \
	name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace
{
    bool IsAdmin(int& errorCode);

    using GUIDFromString_t = BOOL(WINAPI*)(_In_ LPCSTR, _Out_ LPGUID);

    bool GUIDFromString(const std::string& input, GUID* guid);

    void CustomizeEasyLoggingColoredConsole();
}


#if defined(NEFCON_WINMAIN)
int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nShowCmd
)
#else
int main(int argc, char* argv[])
#endif
{
    argh::parser cmdl;
    cmdl.add_params({
        "--inf-path",
        "--hardware-id",
        "--class-name",
        "--class-guid",
        "--service-name",
        "--position",
        "--service-name",
        "--display-name",
        "--bin-path",
        "--file-path"
    });

#if defined(NEFCON_WINMAIN)
    LPWSTR* szArglist;
    int nArgs;
    int i;

    szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (nullptr == szArglist)
    {
        std::cout << color(red) << "CommandLineToArgvW failed" << std::endl;
        return EXIT_FAILURE;
    }

    std::vector<const char*> argv;
    std::vector<std::string> narrow;

    for (i = 0; i < nArgs; i++)
    {
        narrow.push_back(ConvertWideToANSI(std::wstring(szArglist[i])));
    }

    argv.resize(nArgs);
    std::ranges::transform(narrow, argv.begin(), [](const std::string& arg) { return arg.c_str(); });

    argv.push_back(nullptr);

    START_EASYLOGGINGPP(nArgs, &argv[0]);
    cmdl.parse(nArgs, &argv[0]);
#else
    START_EASYLOGGINGPP(argc, argv);
    CustomizeEasyLoggingColoredConsole();
    cmdl.parse(argv);
#endif

    el::Logger* logger = el::Loggers::getLogger("default");

    std::string infPath, binPath, hwId, className, classGuid, serviceName, displayName, position, filePath;

#pragma region Filter Driver actions

    if (cmdl[{"--add-class-filter"}])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        if (!(cmdl({"--position"}) >> position))
        {
            logger->error("Position missing");
            return EXIT_FAILURE;
        }

        if (!(cmdl({"--service-name"}) >> serviceName))
        {
            logger->error("Filter Service Name missing");
            return EXIT_FAILURE;
        }

        if (!(cmdl({"--class-guid"}) >> classGuid))
        {
            logger->error("Device Class GUID missing");
            return EXIT_FAILURE;
        }

        GUID clID;

        if (!GUIDFromString(classGuid, &clID))
        {
            logger->error(
                "Device Class GUID format invalid, expected format (with or without brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
            return EXIT_FAILURE;
        }

        nefarius::devcon::DeviceClassFilterPosition pos;

        if (position == "upper")
        {
            logger->verbose(1, "Modifying upper filters");
            pos = nefarius::devcon::DeviceClassFilterPosition::Upper;
        }
        else if (position == "lower")
        {
            logger->verbose(1, "Modifying lower filters");
            pos = nefarius::devcon::DeviceClassFilterPosition::Lower;
        }
        else
        {
            logger->error("Unsupported position received. Valid values include: upper, lower");
            return EXIT_FAILURE;
        }

        auto ret = AddDeviceClassFilter(&clID, nefarius::utilities::ConvertAnsiToWide(serviceName), pos);

        if (ret)
        {
            logger->warn("Filter enabled. Reconnect affected devices or reboot system to apply changes!");
            return EXIT_SUCCESS;
        }

        logger->error("Failed to modify filter value, error: %v", winapi::GetLastErrorStdStr());
        return GetLastError();
    }

    if (cmdl[{"--remove-class-filter"}])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        if (!(cmdl({"--position"}) >> position))
        {
            logger->error("Position missing");
            return EXIT_FAILURE;
        }

        if (!(cmdl({"--service-name"}) >> serviceName))
        {
            logger->error("Filter Service Name missing");
            return EXIT_FAILURE;
        }

        if (!(cmdl({"--class-guid"}) >> classGuid))
        {
            logger->error("Device Class GUID missing");
            return EXIT_FAILURE;
        }

        GUID clID;

        if (!GUIDFromString(classGuid, &clID))
        {
            logger->error(
                "Device Class GUID format invalid, expected format (with or without brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
            return EXIT_FAILURE;
        }

        nefarius::devcon::DeviceClassFilterPosition pos;

        if (position == "upper")
        {
            logger->verbose(1, "Modifying upper filters");
            pos = nefarius::devcon::DeviceClassFilterPosition::Upper;
        }
        else if (position == "lower")
        {
            logger->verbose(1, "Modifying lower filters");
            pos = nefarius::devcon::DeviceClassFilterPosition::Lower;
        }
        else
        {
            logger->error("Unsupported position received. Valid values include: upper, lower");
            return EXIT_FAILURE;
        }

        auto ret = RemoveDeviceClassFilter(&clID, nefarius::utilities::ConvertAnsiToWide(serviceName), pos);

        if (ret)
        {
            logger->warn("Filter enabled. Reconnect affected devices or reboot system to apply changes!");
            return EXIT_SUCCESS;
        }

        logger->error("Failed to modify filter value, error: %v", winapi::GetLastErrorStdStr());
        return GetLastError();
    }

#pragma endregion

#pragma region Generic driver installer

    if (cmdl[{"--install-driver"}])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        infPath = cmdl({"--inf-path"}).str();

        if (infPath.empty())
        {
            logger->error("INF path missing");
            return EXIT_FAILURE;
        }

        if (_access(infPath.c_str(), 0) != 0)
        {
            logger->error("The given INF file doesn't exist, is the path correct?");
            return EXIT_FAILURE;
        }

        const DWORD attribs = GetFileAttributesA(infPath.c_str());

        if (attribs & FILE_ATTRIBUTE_DIRECTORY)
        {
            logger->error("The given INF path is a directory, not a file");
            return EXIT_FAILURE;
        }

        bool rebootRequired;

        if (!devcon::install_driver(nefarius::utilities::ConvertAnsiToWide(infPath), &rebootRequired))
        {
            logger->error("Failed to install driver, error: %v", winapi::GetLastErrorStdStr());
            return GetLastError();
        }

        logger->info("Driver installed successfully");

        return (rebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
    }

    if (cmdl[{"--uninstall-driver"}])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        infPath = cmdl({"--inf-path"}).str();

        if (infPath.empty())
        {
            logger->error("INF path missing");
            return EXIT_FAILURE;
        }

        if (_access(infPath.c_str(), 0) != 0)
        {
            logger->error("The given INF file doesn't exist, is the path correct?");
            return EXIT_FAILURE;
        }

        const DWORD attribs = GetFileAttributesA(infPath.c_str());

        if (attribs & FILE_ATTRIBUTE_DIRECTORY)
        {
            logger->error("The given INF path is a directory, not a file");
            return EXIT_FAILURE;
        }

        bool rebootRequired;

        if (!devcon::uninstall_driver(nefarius::utilities::ConvertAnsiToWide(infPath), &rebootRequired))
        {
            logger->error("Failed to uninstall driver, error: %v", winapi::GetLastErrorStdStr());
            return GetLastError();
        }

        logger->info("Driver uninstalled successfully");

        return (rebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
    }

    if (cmdl[{"--create-driver-service"}])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        binPath = cmdl({"--bin-path"}).str();

        if (binPath.empty())
        {
            logger->error("Binary path missing");
            return EXIT_FAILURE;
        }

        if (_access(binPath.c_str(), 0) != 0)
        {
            logger->error("The given binary file doesn't exist, is the path correct?");
            return EXIT_FAILURE;
        }

        const DWORD attribs = GetFileAttributesA(binPath.c_str());

        if (attribs & FILE_ATTRIBUTE_DIRECTORY)
        {
            logger->error("The given binary path is a directory, not a file");
            return EXIT_FAILURE;
        }

        if (!(cmdl({"--service-name"}) >> serviceName))
        {
            logger->error("Service name missing");
            return EXIT_FAILURE;
        }

        displayName = cmdl({"--display-name"}).str();

        if (displayName.empty())
        {
            logger->error("Display name missing");
            return EXIT_FAILURE;
        }

        if (!winapi::CreateDriverService(serviceName.c_str(), displayName.c_str(), binPath.c_str()))
        {
            logger->error("Failed to create driver service, error: %v", winapi::GetLastErrorStdStr());
            return GetLastError();
        }

        logger->info("Driver service created successfully");

        return EXIT_SUCCESS;
    }

    if (cmdl[{"--remove-driver-service"}])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        if (!(cmdl({"--service-name"}) >> serviceName))
        {
            logger->error("Service name missing");
            return EXIT_FAILURE;
        }

        if (!winapi::DeleteDriverService(serviceName.c_str()))
        {
            logger->error("Failed to remove driver service, error: %v", winapi::GetLastErrorStdStr());
            return GetLastError();
        }

        logger->info("Driver service removed successfully");

        return EXIT_SUCCESS;
    }

    if (cmdl[{"--create-device-node"}])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        if (!(cmdl({"--hardware-id"}) >> hwId))
        {
            logger->error("Hardware ID missing");
            return EXIT_FAILURE;
        }

        if (!(cmdl({"--class-name"}) >> className))
        {
            logger->error("Device Class Name missing");
            return EXIT_FAILURE;
        }

        if (!(cmdl({"--class-guid"}) >> classGuid))
        {
            logger->error("Device Class GUID missing");
            return EXIT_FAILURE;
        }

        GUID clID;

        if (!GUIDFromString(classGuid, &clID))
        {
            logger->error(
                "Device Class GUID format invalid, expected format (with or without brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
            return EXIT_FAILURE;
        }

        auto ret = devcon::create(nefarius::utilities::ConvertAnsiToWide(className), &clID,
                                  nefarius::utilities::WideMultiStringArray(nefarius::utilities::ConvertAnsiToWide(hwId)));

        if (!ret)
        {
            logger->error("Failed to create device node, error: %v", winapi::GetLastErrorStdStr());
            return GetLastError();
        }

        logger->info("Device node created successfully");

        return EXIT_SUCCESS;
    }

    if (cmdl[{"--remove-device-node"}])
    {
        logger->verbose(1, "Invoked --remove-device-node");

        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        if (!(cmdl({"--hardware-id"}) >> hwId))
        {
            logger->error("Hardware ID missing");
            return EXIT_FAILURE;
        }

        if (!(cmdl({"--class-guid"}) >> classGuid))
        {
            logger->error("Device Class GUID missing");
            return EXIT_FAILURE;
        }

        GUID clID;

        if (!GUIDFromString(classGuid, &clID))
        {
            logger->error(
                "Device Class GUID format invalid, expected format (with or without brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
            return EXIT_FAILURE;
        }

        bool rebootRequired;

        auto results = devcon::uninstall_device_and_driver(&clID, nefarius::utilities::ConvertAnsiToWide(hwId), &rebootRequired);

        // TODO: finish proper error propagation!
        for (const auto& item : results)
        {
            if (!item)
            {
                logger->error("Failed to delete device node, error: %v", winapi::GetLastErrorStdStr());
                return GetLastError();
            }
        }

        logger->info("Device and driver removed successfully");

        return (rebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
    }

    if (cmdl[{"--inf-default-install"}])
    {
        logger->verbose(1, "Invoked --inf-default-install");

        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        infPath = cmdl({"--inf-path"}).str();

        if (infPath.empty())
        {
            logger->error("INF path missing");
            return EXIT_FAILURE;
        }

        if (_access(infPath.c_str(), 0) != 0)
        {
            logger->error("The given INF file doesn't exist, is the path correct?");
            return EXIT_FAILURE;
        }

        const DWORD attribs = GetFileAttributesA(infPath.c_str());

        if (attribs & FILE_ATTRIBUTE_DIRECTORY)
        {
            logger->error("The given INF path is a directory, not a file");
            return EXIT_FAILURE;
        }

        bool rebootRequired = false;

        if (!devcon::inf_default_install(nefarius::utilities::ConvertAnsiToWide(infPath), &rebootRequired))
        {
            logger->error("Failed to install INF file, error: %v", winapi::GetLastErrorStdStr());
            return GetLastError();
        }

        if (!rebootRequired)
        {
            logger->info("INF file installed successfully");
        }
        else
        {
            logger->info("INF file installed successfully, but a reboot is required");
        }

        return (rebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
    }

    if (cmdl[{"--inf-default-uninstall"}])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        infPath = cmdl({"--inf-path"}).str();

        if (infPath.empty())
        {
            logger->error("INF path missing");
            return EXIT_FAILURE;
        }

        if (_access(infPath.c_str(), 0) != 0)
        {
            logger->error("The given INF file doesn't exist, is the path correct?");
            return EXIT_FAILURE;
        }

        const DWORD attribs = GetFileAttributesA(infPath.c_str());

        if (attribs & FILE_ATTRIBUTE_DIRECTORY)
        {
            logger->error("The given INF path is a directory, not a file");
            return EXIT_FAILURE;
        }

        bool rebootRequired = false;

        if (!devcon::inf_default_uninstall(nefarius::utilities::ConvertAnsiToWide(infPath), &rebootRequired))
        {
            logger->error("Failed to uninstall INF file, error: %v", winapi::GetLastErrorStdStr());
            return GetLastError();
        }

        if (!rebootRequired)
        {
            logger->info("INF file uninstalled successfully");
        }
        else
        {
            logger->info("INF file uninstalled successfully, but a reboot is required");
        }

        return (rebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
    }

#pragma endregion

#pragma region Other Utilities

    if (cmdl[{"--delete-file-on-reboot"}])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        filePath = cmdl({"--file-path"}).str();

        if (filePath.empty())
        {
            logger->error("File path missing");
            return EXIT_FAILURE;
        }

        if (_access(filePath.c_str(), 0) != 0)
        {
            logger->error("The given file path doesn't exist, is the path correct?");
            return EXIT_FAILURE;
        }

        const DWORD attribs = GetFileAttributesA(filePath.c_str());

        if (attribs & FILE_ATTRIBUTE_DIRECTORY)
        {
            logger->error("The given file path is a directory, not a file");
            return EXIT_FAILURE;
        }

    retryRemove:
        const BOOL ret = MoveFileExA(filePath.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);

        // if this happens despite elevated permissions...
        if (!ret && GetLastError() == ERROR_ACCESS_DENIED)
        {
            // ...take ownership of protected file (e.g. within the system directories)...
            if (!winapi::TakeFileOwnership(logger, nefarius::utilities::ConvertAnsiToWide(filePath).c_str()))
            {
                logger->error("Failed to take ownership of file, error: %v", winapi::GetLastErrorStdStr());
                return GetLastError();
            }

            // ...and try again
            goto retryRemove; // NOLINT(cppcoreguidelines-avoid-goto)
        }

        if (!ret)
        {
            logger->error("Failed to register file for removal, error: %v", winapi::GetLastErrorStdStr());
            return GetLastError();
        }

        logger->info("File removal registered successfully");

        return EXIT_SUCCESS;
    }

    if (cmdl[{"--find-hwid"}])
    {
        hwId = cmdl({"--hardware-id"}).str();

        if (hwId.empty())
        {
            logger->error("Hardware ID missing");
            return EXIT_FAILURE;
        }

        const auto findResult = devcon::find_by_hwid(nefarius::utilities::ConvertAnsiToWide(hwId));

        if (!findResult)
        {
            // TODO: better error handling!
            return findResult.error().getErrorCode();
        }

        if (findResult.value().empty())
        {
            return ERROR_NOT_FOUND;
        }

        for (const auto& [HardwareIds, Name, Version] : findResult.value())
        {
            std::wstring idValue = std::accumulate(
                std::begin(HardwareIds), std::end(HardwareIds), std::wstring(),
                [](const std::wstring& ss, const std::wstring& s)
                {
                    return ss.empty() ? s : ss + L", " + s;
                });

            logger->info("Hardware IDs: %v", idValue);
            logger->info("Name: %v", Name);
            logger->info("Version: %v.%v.%v.%v",
                         std::to_wstring(Version.Major),
                         std::to_wstring(Version.Minor),
                         std::to_wstring(Version.Build),
                         std::to_wstring(Version.Private)
            );
        }

        return EXIT_SUCCESS;
    }

#pragma endregion

#pragma region Version

    if (cmdl[{"-v", "--version"}])
    {
        std::cout << "nefcon version " <<
            winapi::GetVersionFromFile(winapi::GetImageBasePath())
            << " (C) Nefarius Software Solutions e.U."
            << std::endl;
        return EXIT_SUCCESS;
    }

#pragma endregion

#pragma region Print usage

#if defined(NEFCON_WINMAIN)
    std::cout << "usage: .\\nefconw [options] [logging]" << std::endl << std::endl;
#else
    std::cout << "usage: .\\nefconc [options] [logging]" << std::endl << std::endl;
#endif
    std::cout << "  options:" << std::endl;
    std::cout << "    --install-driver           Invoke the installation of a given PNP driver" << std::endl;
    std::cout << "      --inf-path               Absolute path to the INF file to install (required)" << std::endl;
    std::cout << "    --uninstall-driver         Invoke the removal of a given PNP driver" << std::endl;
    std::cout << "      --inf-path               Absolute path to the INF file to uninstall (required)" << std::endl;
    std::cout << "    --create-device-node       Create a new ROOT enumerated virtual device" << std::endl;
    std::cout << "      --hardware-id            Hardware ID of the new device (required)" << std::endl;
    std::cout << "      --class-name             Device Class Name of the new device (required)" << std::endl;
    std::cout << "      --class-guid             Device Class GUID of the new device (required)" << std::endl;
    std::cout << "    --remove-device-node       Removes a device and its driver" << std::endl;
    std::cout << "      --hardware-id            Hardware ID of the device (required)" << std::endl;
    std::cout << "      --class-guid             Device Class GUID of the device (required)" << std::endl;
    std::cout << "    --add-class-filter         Adds a service to a device class' filter collection" << std::endl;
    std::cout << "      --position               Which filter to modify (required)" << std::endl;
    std::cout << "                                 Valid values include: upper|lower" << std::endl;
    std::cout << "      --service-name           The driver service name to insert (required)" << std::endl;
    std::cout << "      --class-guid             Device Class GUID to modify (required)" << std::endl;
    std::cout << "    --remove-class-filter      Removes a service to a device class' filter collection" << std::endl;
    std::cout << "      --position               Which filter to modify (required)" << std::endl;
    std::cout << "                                 Valid values include: upper|lower" << std::endl;
    std::cout << "      --service-name           The driver service name to insert (required)" << std::endl;
    std::cout << "      --class-guid             Device Class GUID to modify (required)" << std::endl;
    std::cout << "    --create-driver-service    Creates a new service with a kernel driver as binary" << std::endl;
    std::cout << "      --bin-path               Absolute path to the .sys file (required)" << std::endl;
    std::cout << "      --service-name           The driver service name to create (required)" << std::endl;
    std::cout << "      --display-name           The friendly name of the service (required)" << std::endl;
    std::cout << "    --remove-driver-service    Removes an existing kernel driver service" << std::endl;
    std::cout << "      --service-name           The driver service name to remove (required)" << std::endl;
    std::cout << "    --inf-default-install      Installs an INF file with a [DefaultInstall] section" << std::endl;
    std::cout << "      --inf-path               Absolute path to the INF file to install (required)" << std::endl;
    std::cout << "    --inf-default-uninstall    Uninstalls an INF file with a [DefaultUninstall] section" << std::endl;
    std::cout << "      --inf-path               Absolute path to the INF file to uninstall (required)" << std::endl;
    std::cout << "    --delete-file-on-reboot    Marks a given file to get deleted on next reboot" << std::endl;
    std::cout << "      --file-path              The absolute path of the file to remove (required)" << std::endl;
    std::cout << "    --find-hwid                Shows one or more devices matching a partial Hardware ID" << std::endl;
    std::cout << "      ---hardware-id           (Partial) Hardware ID of the device to match against (required)" <<
        std::endl;
    std::cout << "    -v, --version              Display version of this utility" << std::endl;
    std::cout << std::endl;
    std::cout << "  logging:" << std::endl;
    std::cout << "    --default-log-file=.\\log.txt       Write details of execution to a log file (optional)" <<
        std::endl;
    std::cout << "    --verbose                          Turn on verbose/diagnostic logging (optional)" << std::endl;
    std::cout << std::endl;

#pragma endregion

    return EXIT_SUCCESS;
}

namespace
{
    bool IsAdmin(int& errorCode)
    {
        el::Logger* logger = el::Loggers::getLogger("default");
        BOOL isAdmin = FALSE;

        if (winapi::IsAppRunningAsAdminMode(&isAdmin) != ERROR_SUCCESS)
        {
            logger->error("Failed to determine elevation status, error: ", winapi::GetLastErrorStdStr());
            errorCode = EXIT_FAILURE;
            return false;
        }

        if (!isAdmin)
        {
            logger->error(
                "This command requires elevated privileges. Please run as Administrator and make sure the UAC is enabled.");
            errorCode = EXIT_FAILURE;
            return false;
        }

        return true;
    }

    bool GUIDFromString(const std::string& input, GUID* guid)
    {
        // try without brackets...
        if (UuidFromStringA(RPC_CSTR(input.data()), guid) == RPC_S_INVALID_STRING_UUID)
        {
            const HMODULE shell32 = LoadLibraryA("Shell32.dll");

            if (shell32 == nullptr)
                return false;

            const auto pFnGUIDFromString = reinterpret_cast<GUIDFromString_t>(
                GetProcAddress(shell32, MAKEINTRESOURCEA(703)));

            // ...finally try with brackets
            return pFnGUIDFromString(input.c_str(), guid);
        }

        return true;
    }

    void CustomizeEasyLoggingColoredConsole()
    {
        el::Configurations conf;
        conf.setToDefault();

        // Disable STDOUT logging for all log levels
        conf.set(el::Level::Global, el::ConfigurationType::ToStandardOutput, "false");

        el::Loggers::addFlag(el::LoggingFlag::ImmediateFlush);

        // Register the custom log dispatch callback
        el::Helpers::installLogDispatchCallback<ConsoleColorLogDispatchCallback>("ConsoleColorLogDispatchCallback");

        // Enable the custom log dispatch callback
        el::Helpers::logDispatchCallback<ConsoleColorLogDispatchCallback>("ConsoleColorLogDispatchCallback")->
            setEnabled(true);

        // Apply the configuration
        el::Loggers::reconfigureLogger("default", conf);
    }
}
