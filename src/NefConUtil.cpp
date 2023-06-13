// ReSharper disable CppTooWideScope
// ReSharper disable CppClangTidyBugproneNarrowingConversions
// ReSharper disable CppClangTidyHicppAvoidGoto
#include "NefConUtil.h"

using namespace colorwin;

INITIALIZE_EASYLOGGINGPP

//
// Enable Visual Styles for message box
// 
#pragma comment(linker,"\"/manifestdependency:type='win32' \
	name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")


bool IsCurlyBracket(char c)
{
    switch (c)
    {
    case '{':
    case '}':
        return true;
    default:
        return false;
    }
}

bool IsAdmin(int& errorCode)
{
    el::Logger* logger = el::Loggers::getLogger("default");
    BOOL isAdmin = FALSE;

    if (winapi::IsAppRunningAsAdminMode(&isAdmin) != ERROR_SUCCESS)
    {
        logger->error("Failed to determine elevation status, error: ",
            winapi::GetLastErrorStdStr());
        std::cout << color(red) <<
            "Failed to determine elevation status, error: "
            << winapi::GetLastErrorStdStr() << std::endl;
        errorCode = EXIT_FAILURE;
        return false;
    }

    if (!isAdmin)
    {
        logger->error("This command requires elevated privileges. Please run as Administrator and make sure the UAC is enabled.");
        std::cout << color(red) << "This command requires elevated privileges. Please run as Administrator and make sure the UAC is enabled." << std::endl;
        errorCode = EXIT_FAILURE;
        return false;
    }

    return true;
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
    std::transform(narrow.begin(), narrow.end(), argv.begin(), [](const std::string& arg) { return arg.c_str(); });

    argv.push_back(nullptr);

    START_EASYLOGGINGPP(nArgs, &argv[0]);
    cmdl.parse(nArgs, &argv[0]);
#else
    START_EASYLOGGINGPP(argc, argv);
    cmdl.parse(argv);
#endif

    el::Logger* logger = el::Loggers::getLogger("default");

    std::string infPath, binPath, hwId, className, classGuid, serviceName, displayName, position, filePath;

#pragma region Filter Driver actions

    if (cmdl[{"--add-class-filter"}])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        if (!(cmdl({ "--position" }) >> position)) {
            logger->error("Position missing");
            std::cout << color(red) << "Position missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (!(cmdl({ "--service-name" }) >> serviceName)) {
            logger->error("Filter Service Name missing");
            std::cout << color(red) << "Filter Service Name missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (!(cmdl({ "--class-guid" }) >> classGuid)) {
            logger->error("Device Class GUID missing");
            std::cout << color(red) << "Device Class GUID missing" << std::endl;
            return EXIT_FAILURE;
        }

        classGuid.erase(std::remove_if(classGuid.begin(), classGuid.end(), &IsCurlyBracket), classGuid.end());

        GUID clID;

        if (UuidFromStringA(reinterpret_cast<RPC_CSTR>(&classGuid[0]), &clID) == RPC_S_INVALID_STRING_UUID)
        {
            logger->error("Device Class GUID format invalid, expected format (no brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
            std::cout << color(red) << "Device Class GUID format invalid, expected format (no brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" << std::endl;
            return EXIT_FAILURE;
        }

        devcon::DeviceClassFilterPosition::Value pos;

        if (position == "upper")
        {
            logger->verbose(1, "Modifying upper filters");
            pos = devcon::DeviceClassFilterPosition::Upper;
        }
        else if (position == "lower")
        {
            logger->verbose(1, "Modifying lower filters");
            pos = devcon::DeviceClassFilterPosition::Lower;
        }
        else
        {
            logger->error("Unsupported position received. Valid values include: upper, lower");
            std::cout << color(red) << "Unsupported position received. Valid values include: upper, lower" << std::endl;
            return EXIT_FAILURE;
        }

        auto ret = devcon::add_device_class_filter(&clID, ConvertAnsiToWide(serviceName), pos);

        if (ret)
        {
            logger->warn("Filter enabled. Reconnect affected devices or reboot system to apply changes!");
            std::cout << color(yellow) <<
                "Filter enabled. Reconnect affected devices or reboot system to apply changes!"
                << std::endl;

            return EXIT_SUCCESS;
        }

        logger->error("Failed to modify filter value, error: %v",
            winapi::GetLastErrorStdStr());
        std::cout << color(red) <<
            "Failed to modify filter value, error: "
            << winapi::GetLastErrorStdStr() << std::endl;
        return GetLastError();
    }

    if (cmdl[{"--remove-class-filter"}])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        if (!(cmdl({ "--position" }) >> position)) {
            logger->error("Position missing");
            std::cout << color(red) << "Position missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (!(cmdl({ "--service-name" }) >> serviceName)) {
            logger->error("Filter Service Name missing");
            std::cout << color(red) << "Filter Service Name missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (!(cmdl({ "--class-guid" }) >> classGuid)) {
            logger->error("Device Class GUID missing");
            std::cout << color(red) << "Device Class GUID missing" << std::endl;
            return EXIT_FAILURE;
        }

        GUID clID;

        if (UuidFromStringA(reinterpret_cast<RPC_CSTR>(&classGuid[0]), &clID) == RPC_S_INVALID_STRING_UUID)
        {
            logger->error("Device Class GUID format invalid, expected format (no brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
            std::cout << color(red) << "Device Class GUID format invalid, expected format (no brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" << std::endl;
            return EXIT_FAILURE;
        }

        devcon::DeviceClassFilterPosition::Value pos;

        if (position == "upper")
        {
            logger->verbose(1, "Modifying upper filters");
            pos = devcon::DeviceClassFilterPosition::Upper;
        }
        else if (position == "lower")
        {
            logger->verbose(1, "Modifying lower filters");
            pos = devcon::DeviceClassFilterPosition::Lower;
        }
        else
        {
            logger->error("Unsupported position received. Valid values include: upper, lower");
            std::cout << color(red) << "Unsupported position received. Valid values include: upper, lower" << std::endl;
            return EXIT_FAILURE;
        }

        auto ret = devcon::remove_device_class_filter(&clID, ConvertAnsiToWide(serviceName), pos);

        if (ret)
        {
            logger->warn("Filter enabled. Reconnect affected devices or reboot system to apply changes!");
            std::cout << color(yellow) <<
                "Filter enabled. Reconnect affected devices or reboot system to apply changes!"
                << std::endl;

            return EXIT_SUCCESS;
        }

        logger->error("Failed to modify filter value, error: %v",
            winapi::GetLastErrorStdStr());
        std::cout << color(red) <<
            "Failed to modify filter value, error: "
            << winapi::GetLastErrorStdStr() << std::endl;
        return GetLastError();
    }

#pragma endregion

#pragma region Generic driver installer

    if (cmdl[{ "--install-driver" }])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        infPath = cmdl({ "--inf-path" }).str();

        if (infPath.empty()) {
            logger->error("INF path missing");
            std::cout << color(red) << "INF path missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (_access(infPath.c_str(), 0) != 0)
        {
            logger->error("The given INF file doesn't exist, is the path correct?");
            std::cout << color(red) << "The given INF file doesn't exist, is the path correct?" << std::endl;
            return EXIT_FAILURE;
        }

        const DWORD attribs = GetFileAttributesA(infPath.c_str());

        if (attribs & FILE_ATTRIBUTE_DIRECTORY)
        {
            logger->error("The given INF path is a directory, not a file");
            std::cout << color(red) << "The given INF path is a directory, not a file" << std::endl;
            return EXIT_FAILURE;
        }

        bool rebootRequired;

        if (!devcon::install_driver(ConvertAnsiToWide(infPath), &rebootRequired))
        {
            logger->error("Failed to install driver, error: %v",
                winapi::GetLastErrorStdStr());
            std::cout << color(red) <<
                "Failed to install driver, error: "
                << winapi::GetLastErrorStdStr() << std::endl;
            return GetLastError();
        }

        logger->info("Driver installed successfully");
        std::cout << color(green) << "Driver installed successfully" << std::endl;

        return (rebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
    }

    if (cmdl[{ "--uninstall-driver" }])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        infPath = cmdl({ "--inf-path" }).str();

        if (infPath.empty()) {
            logger->error("INF path missing");
            std::cout << color(red) << "INF path missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (_access(infPath.c_str(), 0) != 0)
        {
            logger->error("The given INF file doesn't exist, is the path correct?");
            std::cout << color(red) << "The given INF file doesn't exist, is the path correct?" << std::endl;
            return EXIT_FAILURE;
        }

        const DWORD attribs = GetFileAttributesA(infPath.c_str());

        if (attribs & FILE_ATTRIBUTE_DIRECTORY)
        {
            logger->error("The given INF path is a directory, not a file");
            std::cout << color(red) << "The given INF path is a directory, not a file" << std::endl;
            return EXIT_FAILURE;
        }

        bool rebootRequired;

        if (!devcon::uninstall_driver(ConvertAnsiToWide(infPath), &rebootRequired))
        {
            logger->error("Failed to uninstall driver, error: %v",
                winapi::GetLastErrorStdStr());
            std::cout << color(red) <<
                "Failed to uninstall driver, error: "
                << winapi::GetLastErrorStdStr() << std::endl;
            return GetLastError();
        }

        logger->info("Driver uninstalled successfully");
        std::cout << color(green) << "Driver uninstalled successfully" << std::endl;

        return (rebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
    }

    if (cmdl[{ "--create-driver-service" }])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        binPath = cmdl({ "--bin-path" }).str();

        if (binPath.empty()) {
            logger->error("Binary path missing");
            std::cout << color(red) << "Binary path missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (_access(binPath.c_str(), 0) != 0)
        {
            logger->error("The given binary file doesn't exist, is the path correct?");
            std::cout << color(red) << "The given binary file doesn't exist, is the path correct?" << std::endl;
            return EXIT_FAILURE;
        }

        const DWORD attribs = GetFileAttributesA(binPath.c_str());

        if (attribs & FILE_ATTRIBUTE_DIRECTORY)
        {
            logger->error("The given binary path is a directory, not a file");
            std::cout << color(red) << "The given binary path is a directory, not a file" << std::endl;
            return EXIT_FAILURE;
        }

        if (!(cmdl({ "--service-name" }) >> serviceName)) {
            logger->error("Service name missing");
            std::cout << color(red) << "Service name missing" << std::endl;
            return EXIT_FAILURE;
        }

        displayName = cmdl({ "--display-name" }).str();

        if (displayName.empty()) {
            logger->error("Display name missing");
            std::cout << color(red) << "Display name missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (!winapi::CreateDriverService(serviceName.c_str(), displayName.c_str(), binPath.c_str()))
        {
            logger->error("Failed to create driver service, error: %v",
                winapi::GetLastErrorStdStr());
            std::cout << color(red) <<
                "Failed to create driver service, error: "
                << winapi::GetLastErrorStdStr() << std::endl;
            return GetLastError();
        }

        logger->info("Driver service created successfully");
        std::cout << color(green) << "Driver service created successfully" << std::endl;

        return EXIT_SUCCESS;
    }

    if (cmdl[{ "--remove-driver-service" }])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        if (!(cmdl({ "--service-name" }) >> serviceName)) {
            logger->error("Service name missing");
            std::cout << color(red) << "Service name missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (!winapi::DeleteDriverService(serviceName.c_str()))
        {
            logger->error("Failed to remove driver service, error: %v",
                winapi::GetLastErrorStdStr());
            std::cout << color(red) <<
                "Failed to remove driver service, error: "
                << winapi::GetLastErrorStdStr() << std::endl;
            return GetLastError();
        }

        logger->info("Driver service removed successfully");
        std::cout << color(green) << "Driver service removed successfully" << std::endl;

        return EXIT_SUCCESS;
    }

    if (cmdl[{ "--create-device-node" }])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        if (!(cmdl({ "--hardware-id" }) >> hwId)) {
            logger->error("Hardware ID missing");
            std::cout << color(red) << "Hardware ID missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (!(cmdl({ "--class-name" }) >> className)) {
            logger->error("Device Class Name missing");
            std::cout << color(red) << "Device Class Name missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (!(cmdl({ "--class-guid" }) >> classGuid)) {
            logger->error("Device Class GUID missing");
            std::cout << color(red) << "Device Class GUID missing" << std::endl;
            return EXIT_FAILURE;
        }

        classGuid.erase(std::remove_if(classGuid.begin(), classGuid.end(), &IsCurlyBracket), classGuid.end());

        GUID clID;

        if (UuidFromStringA(reinterpret_cast<RPC_CSTR>(&classGuid[0]), &clID) == RPC_S_INVALID_STRING_UUID)
        {
            logger->error("Device Class GUID format invalid, expected format (no brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
            std::cout << color(red) << "Device Class GUID format invalid, expected format (no brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" << std::endl;
            return EXIT_FAILURE;
        }

        auto ret = devcon::create(ConvertAnsiToWide(className), &clID, ConvertAnsiToWide(hwId));

        if (!ret)
        {
            logger->error("Failed to create device node, error: %v",
                winapi::GetLastErrorStdStr());
            std::cout << color(red) <<
                "Failed to create device node, error: "
                << winapi::GetLastErrorStdStr() << std::endl;
            return GetLastError();
        }

        logger->info("Device node created successfully");
        std::cout << color(green) << "Device node created successfully" << std::endl;

        return EXIT_SUCCESS;
    }

    if (cmdl[{ "--remove-device-node" }])
    {
        logger->verbose(1, "Invoked --remove-device-node");

        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        if (!(cmdl({ "--hardware-id" }) >> hwId)) {
            logger->error("Hardware ID missing");
            std::cout << color(red) << "Hardware ID missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (!(cmdl({ "--class-guid" }) >> classGuid)) {
            logger->error("Device Class GUID missing");
            std::cout << color(red) << "Device Class GUID missing" << std::endl;
            return EXIT_FAILURE;
        }

        classGuid.erase(std::remove_if(classGuid.begin(), classGuid.end(), &IsCurlyBracket), classGuid.end());

        GUID clID;

        if (UuidFromStringA(reinterpret_cast<RPC_CSTR>(&classGuid[0]), &clID) == RPC_S_INVALID_STRING_UUID)
        {
            logger->error("Device Class GUID format invalid, expected format (no brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
            std::cout << color(red) << "Device Class GUID format invalid, expected format (no brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" << std::endl;
            return EXIT_FAILURE;
        }

        bool rebootRequired;

        auto ret = devcon::uninstall_device_and_driver(&clID, ConvertAnsiToWide(hwId), &rebootRequired);

        if (!ret)
        {
            logger->error("Failed to delete device node, error: %v",
                winapi::GetLastErrorStdStr());
            std::cout << color(red) <<
                "Failed to delete device node, error: "
                << winapi::GetLastErrorStdStr() << std::endl;
            return GetLastError();
        }

        logger->info("Device and driver removed successfully");
        std::cout << color(green) << "Device and driver removed successfully" << std::endl;

        return (rebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
    }

    if (cmdl[{ "--inf-default-install" }])
    {
        logger->verbose(1, "Invoked --inf-default-install");

        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        infPath = cmdl({ "--inf-path" }).str();

        if (infPath.empty()) {
            logger->error("INF path missing");
            std::cout << color(red) << "INF path missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (_access(infPath.c_str(), 0) != 0)
        {
            logger->error("The given INF file doesn't exist, is the path correct?");
            std::cout << color(red) << "The given INF file doesn't exist, is the path correct?" << std::endl;
            return EXIT_FAILURE;
        }

        const DWORD attribs = GetFileAttributesA(infPath.c_str());

        if (attribs & FILE_ATTRIBUTE_DIRECTORY)
        {
            logger->error("The given INF path is a directory, not a file");
            std::cout << color(red) << "The given INF path is a directory, not a file" << std::endl;
            return EXIT_FAILURE;
        }

        bool rebootRequired = false;

        if (!devcon::inf_default_install(ConvertAnsiToWide(infPath), &rebootRequired))
        {
            logger->error("Failed to install INF file, error: %v",
                winapi::GetLastErrorStdStr());
            std::cout << color(red) <<
                "Failed to install INF file, error: "
                << winapi::GetLastErrorStdStr() << std::endl;
            return GetLastError();
        }

        if (!rebootRequired)
        {
            logger->info("INF file installed successfully");
            std::cout << color(green) << "INF file installed successfully" << std::endl;
        }
        else
        {
            logger->info("INF file installed successfully, but a reboot is required");
            std::cout << color(yellow) << "INF file installed successfully, but a reboot is required" << std::endl;
        }

        return (rebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
    }

    if (cmdl[{ "--inf-default-uninstall" }])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        infPath = cmdl({ "--inf-path" }).str();

        if (infPath.empty()) {
            logger->error("INF path missing");
            std::cout << color(red) << "INF path missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (_access(infPath.c_str(), 0) != 0)
        {
            logger->error("The given INF file doesn't exist, is the path correct?");
            std::cout << color(red) << "The given INF file doesn't exist, is the path correct?" << std::endl;
            return EXIT_FAILURE;
        }

        const DWORD attribs = GetFileAttributesA(infPath.c_str());

        if (attribs & FILE_ATTRIBUTE_DIRECTORY)
        {
            logger->error("The given INF path is a directory, not a file");
            std::cout << color(red) << "The given INF path is a directory, not a file" << std::endl;
            return EXIT_FAILURE;
        }

        bool rebootRequired = false;

        if (!devcon::inf_default_uninstall(ConvertAnsiToWide(infPath), &rebootRequired))
        {
            logger->error("Failed to uninstall INF file, error: %v",
                winapi::GetLastErrorStdStr());
            std::cout << color(red) <<
                "Failed to uninstall INF file, error: "
                << winapi::GetLastErrorStdStr() << std::endl;
            return GetLastError();
        }

        if (!rebootRequired)
        {
            logger->info("INF file uninstalled successfully");
            std::cout << color(green) << "INF file uninstalled successfully" << std::endl;
        }
        else
        {
            logger->info("INF file uninstalled successfully, but a reboot is required");
            std::cout << color(yellow) << "INF file uninstalled successfully, but a reboot is required" << std::endl;
        }

        return (rebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
    }

#pragma endregion

#pragma region Other Utilities

    if (cmdl[{ "--delete-file-on-reboot" }])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        filePath = cmdl({ "--file-path" }).str();

        if (filePath.empty()) {
            logger->error("File path missing");
            std::cout << color(red) << "File path missing" << std::endl;
            return EXIT_FAILURE;
        }

        if (_access(filePath.c_str(), 0) != 0)
        {
            logger->error("The given file path doesn't exist, is the path correct?");
            std::cout << color(red) << "The given file path doesn't exist, is the path correct?" << std::endl;
            return EXIT_FAILURE;
        }

        const DWORD attribs = GetFileAttributesA(filePath.c_str());

        if (attribs & FILE_ATTRIBUTE_DIRECTORY)
        {
            logger->error("The given file path is a directory, not a file");
            std::cout << color(red) << "The given file path is a directory, not a file" << std::endl;
            return EXIT_FAILURE;
        }

    retryRemove:
        const BOOL ret = MoveFileExA(filePath.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);

        // if this happens despite elevated permissions...
        if (!ret && GetLastError() == ERROR_ACCESS_DENIED)
        {
            // ...take ownership of protected file (e.g. within the system directories)...
            if (!winapi::TakeFileOwnership(logger, ConvertAnsiToWide(filePath).c_str()))
            {
                logger->error("Failed to take ownership of file, error: %v",
                    winapi::GetLastErrorStdStr());
                std::cout << color(red) <<
                    "Failed to take ownership of file, error: "
                    << winapi::GetLastErrorStdStr() << std::endl;
                return GetLastError();
            }

            // ...and try again
            goto retryRemove;  // NOLINT(cppcoreguidelines-avoid-goto)
        }

        if (!ret)
        {
            logger->error("Failed to register file for removal, error: %v",
                winapi::GetLastErrorStdStr());
            std::cout << color(red) <<
                "Failed to register file for removal, error: "
                << winapi::GetLastErrorStdStr() << std::endl;
            return GetLastError();
        }

        logger->info("File removal registered successfully");
        std::cout << color(green) << "File removal registered successfully" << std::endl;

        return EXIT_SUCCESS;
    }

    if (cmdl[{ "--find-hwid" }])
    {
        int errorCode;
      //  if (!IsAdmin(errorCode)) return errorCode;

        hwId = cmdl({ "--hardware-id" }).str();

        if (hwId.empty()) {
            logger->error("Hardware ID missing");
            std::cout << color(red) << "Hardware ID missing" << std::endl;
            return EXIT_FAILURE;
        }
        devcon::find_hwid(ConvertAnsiToWide(hwId));
        return EXIT_SUCCESS;
    }

#pragma endregion

    if (cmdl[{ "-v", "--version" }])
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
    std::cout << "    --find-hwid                Shows the devices matching the wildcard hwid" << std::endl;
    std::cout << "      ---hardware-id           Hardware ID (or partial) of the device for matching (required)" << std::endl;
    std::cout << "    -v, --version              Display version of this utility" << std::endl;
    std::cout << std::endl;
    std::cout << "  logging:" << std::endl;
    std::cout << "    --default-log-file=.\\log.txt       Write details of execution to a log file (optional)" << std::endl;
    std::cout << "    --verbose                          Turn on verbose/diagnostic logging (optional)" << std::endl;
    std::cout << std::endl;

#pragma endregion

    return EXIT_SUCCESS;
    }
