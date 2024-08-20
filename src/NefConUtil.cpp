// ReSharper disable CppTooWideScope
// ReSharper disable CppClangTidyBugproneNarrowingConversions
// ReSharper disable CppClangTidyHicppAvoidGoto
#include "NefConUtil.h"

#include <numeric>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

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

    std::string GetImageBasePath();

#if !defined(NEFCON_WINMAIN)
    void CustomizeEasyLoggingColoredConsole();
#endif
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

    auto cliArgs = nefarius::winapi::cli::GetCommandLineArgs();

    if (!cliArgs)
    {
        std::cout << color(red) << cliArgs.error().getErrorMessageA() << '\n';
        return EXIT_FAILURE;
    }

#if defined(NEFCON_WINMAIN)
    int argc = 0;
    auto argv = cliArgs.value().AsArgv(&argc);

    START_EASYLOGGINGPP(argc, argv.data());
    cmdl.parse(argc, argv.data());
#else
    START_EASYLOGGINGPP(argc, argv);
    CustomizeEasyLoggingColoredConsole();
    cmdl.parse(argv);
#endif

    el::Logger* logger = el::Loggers::getLogger("default");

    const auto arguments = cliArgs.value().Arguments;

#pragma region Devcon emulation

    //
    // Before testing any "regular" arguments, see if the user has used "devcon" tool compatible syntax
    // 
    if (arguments.size() > 3 && arguments[1] == "install")
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        const std::wstring infFilePath = nefarius::utilities::ConvertToWide(arguments[2]);
        const std::wstring hardwareId = nefarius::utilities::ConvertToWide(arguments[3]);

        const auto infClassResult = nefarius::devcon::GetINFClass(infFilePath);

        if (!infClassResult)
        {
            logger->error("Failed to get class information from INF file, error: %v",
                          infClassResult.error().getErrorMessageA());
            return infClassResult.error().getErrorCode();
        }

        const auto& infClass = infClassResult.value();

        if (const auto createResult = nefarius::devcon::Create(
                infClass.ClassName,
                &infClass.ClassGUID,
                nefarius::utilities::WideMultiStringArray(hardwareId));
            !createResult)
        {
            logger->error("Failed to create device node, error: %v", createResult.error().getErrorMessageA());
            return createResult.error().getErrorCode();
        }

        bool rebootRequired = false;

        if (const auto updateResult = nefarius::devcon::Update(hardwareId, infFilePath, &rebootRequired); !updateResult)
        {
            logger->error("Failed to update device node(s) with driver, error: %v",
                          updateResult.error().getErrorMessageA());
            return updateResult.error().getErrorCode();
        }

        logger->info("Device and driver installed successfully");
        return EXIT_SUCCESS;
    }

#pragma endregion

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

        const auto guid = nefarius::winapi::GUIDFromString(classGuid);

        if (!guid)
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

        auto ret = nefarius::devcon::AddDeviceClassFilter(&guid.value(),
                                                          nefarius::utilities::ConvertAnsiToWide(serviceName), pos);

        if (ret)
        {
            logger->warn("Filter enabled. Reconnect affected devices or reboot system to apply changes!");
            return EXIT_SUCCESS;
        }

        logger->error("Failed to modify filter value, error: %v", ret.error().getErrorMessageA());
        return ret.error().getErrorCode();
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

        const auto guid = nefarius::winapi::GUIDFromString(classGuid);

        if (!guid)
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

        auto ret = RemoveDeviceClassFilter(&guid.value(), nefarius::utilities::ConvertAnsiToWide(serviceName), pos);

        if (ret)
        {
            logger->warn("Filter enabled. Reconnect affected devices or reboot system to apply changes!");
            return EXIT_SUCCESS;
        }

        logger->error("Failed to modify filter value, error: %v", ret.error().getErrorMessageA());
        return ret.error().getErrorCode();
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

        if (const auto result = nefarius::devcon::InstallDriver(nefarius::utilities::ConvertAnsiToWide(infPath),
                                                                &rebootRequired); !result)
        {
            logger->error("Failed to install driver, error: %v", result.error().getErrorMessageA());
            return result.error().getErrorCode();
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

        if (const auto result = nefarius::devcon::UninstallDriver(nefarius::utilities::ConvertAnsiToWide(infPath),
                                                                  &rebootRequired); !result)
        {
            logger->error("Failed to uninstall driver, error: %v", result.error().getErrorMessageA());
            return result.error().getErrorCode();
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

        if (const auto result = nefarius::winapi::services::CreateDriverService(serviceName, displayName, binPath); !
            result)
        {
            logger->error("Failed to create driver service, error: %v", result.error().getErrorMessageA());
            return result.error().getErrorCode();
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

        if (const auto result = nefarius::winapi::services::DeleteDriverService(serviceName); !result)
        {
            logger->error("Failed to remove driver service, error: %v", result.error().getErrorMessageA());
            return result.error().getErrorCode();
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

        const auto guid = nefarius::winapi::GUIDFromString(classGuid);

        if (!guid)
        {
            logger->error(
                "Device Class GUID format invalid, expected format (with or without brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
            return EXIT_FAILURE;
        }

        auto ret = nefarius::devcon::Create(nefarius::utilities::ConvertAnsiToWide(className), &guid.value(),
                                            nefarius::utilities::WideMultiStringArray(
                                                nefarius::utilities::ConvertAnsiToWide(hwId)));

        if (!ret)
        {
            logger->error("Failed to create device node, error: %v", ret.error().getErrorMessageA());
            return ret.error().getErrorCode();
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

        const auto guid = nefarius::winapi::GUIDFromString(classGuid);

        if (!guid)
        {
            logger->error(
                "Device Class GUID format invalid, expected format (with or without brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
            return EXIT_FAILURE;
        }

        bool rebootRequired;

        auto results = nefarius::devcon::UninstallDeviceAndDriver(&guid.value(),
                                                                  nefarius::utilities::ConvertAnsiToWide(hwId),
                                                                  &rebootRequired);

        for (const auto& item : results)
        {
            if (!item)
            {
                logger->error("Failed to delete device node, error: %v", item.error().getErrorMessageA());
                return item.error().getErrorCode();
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

        if (const auto result = nefarius::devcon::InfDefaultInstall(nefarius::utilities::ConvertAnsiToWide(infPath),
                                                                    &rebootRequired); !result)
        {
            logger->error("Failed to install INF file, error: %v", result.error().getErrorMessageA());
            return result.error().getErrorCode();
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

        if (const auto result = nefarius::devcon::InfDefaultUninstall(nefarius::utilities::ConvertAnsiToWide(infPath),
                                                                      &rebootRequired); !result)
        {
            logger->error("Failed to uninstall INF file, error: %v", result.error().getErrorMessageA());
            return result.error().getErrorCode();
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
            if (const auto result = nefarius::winapi::fs::TakeFileOwnership(filePath); !result)
            {
                logger->error("Failed to take ownership of file, error: %v", result.error().getErrorMessageA());
                return result.error().getErrorCode();
            }

            // ...and try again
            goto retryRemove; // NOLINT(cppcoreguidelines-avoid-goto)
        }

        if (!ret)
        {
            logger->error("Failed to register file for removal, error: %v",
                          nefarius::utilities::Win32Error("MoveFileExA").getErrorMessageA());
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

        const auto findResult = nefarius::devcon::FindByHwId(nefarius::utilities::ConvertAnsiToWide(hwId));

        if (!findResult)
        {
            logger->error("Failed to register search for devices, error: %v", findResult.error().getErrorMessageA());
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
            to_string(nefarius::winapi::fs::GetProductVersionFromFile(GetImageBasePath()).value())
            << " (C) Nefarius Software Solutions e.U."
            << '\n';
        return EXIT_SUCCESS;
    }

#pragma endregion

#pragma region Print usage

#if defined(NEFCON_WINMAIN)
    std::cout << "usage: .\\nefconw [options] [logging]" << std::endl << std::endl;
#else
    std::cout << "usage: .\\nefconc [options] [logging]" << '\n' << '\n';
#endif
    std::cout << "  options:" << '\n';
    std::cout << "    --install-driver           Invoke the installation of a given PNP driver" << '\n';
    std::cout << "      --inf-path               Absolute path to the INF file to install (required)" << '\n';
    std::cout << "    --uninstall-driver         Invoke the removal of a given PNP driver" << '\n';
    std::cout << "      --inf-path               Absolute path to the INF file to uninstall (required)" << '\n';
    std::cout << "    --create-device-node       Create a new ROOT enumerated virtual device" << '\n';
    std::cout << "      --hardware-id            Hardware ID of the new device (required)" << '\n';
    std::cout << "      --class-name             Device Class Name of the new device (required)" << '\n';
    std::cout << "      --class-guid             Device Class GUID of the new device (required)" << '\n';
    std::cout << "    --remove-device-node       Removes a device and its driver" << '\n';
    std::cout << "      --hardware-id            Hardware ID of the device (required)" << '\n';
    std::cout << "      --class-guid             Device Class GUID of the device (required)" << '\n';
    std::cout << "    --add-class-filter         Adds a service to a device class' filter collection" << '\n';
    std::cout << "      --position               Which filter to modify (required)" << '\n';
    std::cout << "                                 Valid values include: upper|lower" << '\n';
    std::cout << "      --service-name           The driver service name to insert (required)" << '\n';
    std::cout << "      --class-guid             Device Class GUID to modify (required)" << '\n';
    std::cout << "    --remove-class-filter      Removes a service to a device class' filter collection" << '\n';
    std::cout << "      --position               Which filter to modify (required)" << '\n';
    std::cout << "                                 Valid values include: upper|lower" << '\n';
    std::cout << "      --service-name           The driver service name to insert (required)" << '\n';
    std::cout << "      --class-guid             Device Class GUID to modify (required)" << '\n';
    std::cout << "    --create-driver-service    Creates a new service with a kernel driver as binary" << '\n';
    std::cout << "      --bin-path               Absolute path to the .sys file (required)" << '\n';
    std::cout << "      --service-name           The driver service name to create (required)" << '\n';
    std::cout << "      --display-name           The friendly name of the service (required)" << '\n';
    std::cout << "    --remove-driver-service    Removes an existing kernel driver service" << '\n';
    std::cout << "      --service-name           The driver service name to remove (required)" << '\n';
    std::cout << "    --inf-default-install      Installs an INF file with a [DefaultInstall] section" << '\n';
    std::cout << "      --inf-path               Absolute path to the INF file to install (required)" << '\n';
    std::cout << "    --inf-default-uninstall    Uninstalls an INF file with a [DefaultUninstall] section" << '\n';
    std::cout << "      --inf-path               Absolute path to the INF file to uninstall (required)" << '\n';
    std::cout << "    --delete-file-on-reboot    Marks a given file to get deleted on next reboot" << '\n';
    std::cout << "      --file-path              The absolute path of the file to remove (required)" << '\n';
    std::cout << "    --find-hwid                Shows one or more devices matching a partial Hardware ID" << '\n';
    std::cout << "      ---hardware-id           (Partial) Hardware ID of the device to match against (required)" <<
        '\n';
    std::cout << "    -v, --version              Display version of this utility" << '\n';
    std::cout << '\n';
    std::cout << "  logging:" << '\n';
    std::cout << "    --default-log-file=.\\log.txt       Write details of execution to a log file (optional)" <<
        '\n';
    std::cout << "    --verbose                          Turn on verbose/diagnostic logging (optional)" << '\n';
    std::cout << '\n';

#pragma endregion

    return EXIT_SUCCESS;
}

namespace
{
    bool IsAdmin(int& errorCode)
    {
        el::Logger* logger = el::Loggers::getLogger("default");

        const auto isAdmin = nefarius::winapi::security::IsAppRunningAsAdminMode();

        if (!isAdmin)
        {
            logger->error("Failed to determine elevation status, error: ", isAdmin.error().getErrorMessageA());
            errorCode = EXIT_FAILURE;
            return false;
        }

        if (!isAdmin.value())
        {
            logger->error(
                "This command requires elevated privileges. Please run as Administrator and make sure the UAC is enabled.");
            errorCode = EXIT_FAILURE;
            return false;
        }

        return true;
    }

    std::string GetImageBasePath()
    {
        char myPath[MAX_PATH + 1] = {};

        GetModuleFileNameA(
            reinterpret_cast<HINSTANCE>(&__ImageBase),
            myPath,
            MAX_PATH + 1
        );

        return {myPath};
    }

#if !defined(NEFCON_WINMAIN)
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
#endif
}
