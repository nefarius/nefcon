// ReSharper disable CppTooWideScope
// ReSharper disable CppClangTidyBugproneNarrowingConversions
// ReSharper disable CppClangTidyHicppAvoidGoto
#include "NefConUtil.h"

#include <algorithm>
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

    enum class DeviceExistsResult { Found, NotFound, Error };

    DeviceExistsResult DeviceExists(const std::string& hwId, int& errorCode);

    DeviceExistsResult CheckNoDuplicates(const argh::parser& cmdl, const std::string& hwId, int& errorCode);

    struct RemoveDuplicatesResult
    {
        DWORD Removed;
        DWORD Failed;
        bool RebootRequired;
    };

    RemoveDuplicatesResult RemoveDuplicateDeviceNodes(const GUID* classGuid, const std::string& hwId);

    struct RestartAffectedDevicesResult
    {
        bool AnyRebootRequired;
    };

    //
    // Best-effort restart of every present device belonging to any of ClassGuids, using the
    // neflib strategy ladder (USB port cycle, property change, remove+re-enumerate). Never
    // blocks longer than roughly ClassGuids.size() * device-count * Timeout, since every
    // individual attempt is itself time-boxed by neflib.
    // 
    RestartAffectedDevicesResult RestartAffectedDevices(const std::vector<GUID>& classGuids,
                                                        std::chrono::milliseconds timeout);

    //
    // Parses a millisecond timeout argument, falling back to (and warning on) DefaultMs for
    // missing, non-numeric, or non-positive input, so a bad value can never turn into a
    // zero/negative std::chrono::milliseconds.
    //
    int ParseTimeoutMs(const argh::parser& cmdl, const std::string& paramName, int defaultMs);

    //
    // Shared --attempt-restart-affected handling for --inf-default-install/-uninstall: merges
    // class GUIDs discovered from the INF's class filter directives with an optional explicit
    // --class-guid (deduplicated), then restarts affected devices and folds the result into
    // rebootRequired. No-op if --attempt-restart-affected wasn't passed.
    //
    void RestartAffectedDevicesForInf(const argh::parser& cmdl, const std::string& infPath, bool& rebootRequired);

    //
    // --install-filter-driver's race-safety fix: unconditionally restarts devices affected by the
    // INF's class filter declarations (same discovery as RestartAffectedDevicesForInf, but never
    // gated behind --attempt-restart-affected since this command is meant to be atomic), then
    // waits for each declared filter service to settle into a state a caller can trust instead of
    // probing it immediately. A service is only expected to be SERVICE_RUNNING if a device of its
    // class is actually present; otherwise a legitimately demand-started, still-stopped service is
    // logged, not treated as an error. Folds any settle failure into rebootRequired rather than
    // failing the overall install, since the driver package itself installed successfully.
    //
    void SettleFilterDriverInstall(const argh::parser& cmdl, const std::string& infPath, bool& rebootRequired);

    //
    // A single device detached from the system in preparation for a driver upgrade/removal,
    // recorded so a later --reenumerate-affected invocation (possibly after the process/session
    // that detached it has exited) can bring it back.
    // 
    struct DetachedDeviceRecord
    {
        std::wstring InstanceId;
        std::wstring ParentInstanceId;
    };

    //
    // Default --state-file location for a given service name, used when --state-file isn't
    // passed explicitly, so --remove-driver-service and --reenumerate-affected agree on where to
    // find each other's data as long as the same --service-name is used for both. Returns
    // std::nullopt if the directory couldn't be created/secured, so callers don't persist or trust
    // detach state in an unprotected location.
    // 
    std::optional<std::string> GetDefaultDetachStateFilePath(const std::string& serviceName);

    std::expected<void, nefarius::utilities::Win32Error> WriteDetachStateFile(
        const std::string& stateFilePath, const std::vector<DetachedDeviceRecord>& records);

    std::expected<std::vector<DetachedDeviceRecord>, nefarius::utilities::Win32Error> ReadDetachStateFile(
        const std::string& stateFilePath);

    //
    // --attempt-detach-affected handling for --remove-driver-service: enumerates every present
    // device currently bound to serviceName, best-effort detaches each (releasing the driver's
    // file locks) and persists the successfully detached ones' parent devnodes to a state file so
    // --reenumerate-affected can bring them back later. Never blocks the caller from proceeding
    // with stopping/deleting the service; a device that couldn't be detached just means a reboot
    // may be required for the upgrade/removal to fully take effect.
    //
    void DetachAffectedDevicesForService(const argh::parser& cmdl, const std::string& serviceName);

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
        "--display-name",
        "--bin-path",
        "--file-path",
        "--service-guid",
        "--restart-timeout",
        "--stop-timeout",
        "--state-file",
        "--health-timeout",
        "--retry-timeout"
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

        int findErrorCode;
        const auto dupCheck = CheckNoDuplicates(cmdl, arguments[3], findErrorCode);

        if (dupCheck == DeviceExistsResult::Error)
            return findErrorCode;

        if (cmdl[{"--remove-duplicates"}] && !cmdl[{"--no-duplicates"}])
            logger->warn("--remove-duplicates has no effect without --no-duplicates");

        const bool deviceAlreadyExists = (dupCheck == DeviceExistsResult::Found);

        bool scrubRebootRequired = false;

        if (deviceAlreadyExists && cmdl[{"--remove-duplicates"}])
        {
            const auto scrubResult = RemoveDuplicateDeviceNodes(&infClass.ClassGUID, arguments[3]);

            if (scrubResult.Failed > 0)
            {
                logger->error("Failed to remove %v duplicate device node(s)", scrubResult.Failed);
                return EXIT_FAILURE;
            }

            if (scrubResult.Removed > 0)
                logger->info("Removed %v duplicate device node(s)", scrubResult.Removed);

            scrubRebootRequired = scrubResult.RebootRequired;
        }

        if (!deviceAlreadyExists)
        {
            const auto createResult = nefarius::devcon::Create(
                infClass.ClassName,
                &infClass.ClassGUID,
                nefarius::utilities::WideMultiStringArray(hardwareId));

            if (!createResult)
            {
                bool createdConcurrently = false;

                if (cmdl[{"--no-duplicates"}])
                {
                    int recheckErrorCode; // intentionally ignored; prefer the original Create() error on failure
                    createdConcurrently = DeviceExists(arguments[3], recheckErrorCode) == DeviceExistsResult::Found;
                    (void)recheckErrorCode;
                }

                if (createdConcurrently)
                {
                    logger->info("Device with hardware ID \"%v\" was created concurrently, skipping node creation", arguments[3]);
                }
                else
                {
                    logger->error("Failed to create device node, error: %v", createResult.error().getErrorMessageA());
                    return createResult.error().getErrorCode();
                }
            }
        }

        bool driverRebootRequired = false;

        if (const auto updateResult = nefarius::devcon::Update(hardwareId, infFilePath, &driverRebootRequired); !updateResult)
        {
            logger->error("Failed to update device node(s) with driver, error: %v",
                          updateResult.error().getErrorMessageA());
            return updateResult.error().getErrorCode();
        }

        const bool rebootRequired = driverRebootRequired || scrubRebootRequired;

        logger->info((rebootRequired)
                         ? "Device and driver installed successfully, but a reboot is required"
                         : "Device and driver installed successfully"
        );
        return (rebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
    }

    if (arguments.size() > 2 && arguments[1] == "remove")
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        const std::wstring hardwareId = nefarius::utilities::ConvertToWide(arguments[2]);

        const HDEVINFO hDevInfo = SetupDiGetClassDevs(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);

        if (hDevInfo == INVALID_HANDLE_VALUE)
        {
            logger->error("Failed to enumerate devices, error: %v",
                          nefarius::utilities::Win32Error("SetupDiGetClassDevs").getErrorMessageA());
            return EXIT_FAILURE;
        }

        nefarius::utilities::guards::HDEVINFOHandleGuard hDevInfoGuard(hDevInfo);

        SP_DEVINFO_DATA devInfoData = {};
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        DWORD deviceIndex = 0;
        DWORD removedCount = 0;
        DWORD failedCount = 0;
        bool rebootRequired = false;

        while (SetupDiEnumDeviceInfo(hDevInfo, deviceIndex++, &devInfoData))
        {
            DWORD requiredSize = 0;
            SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData, SPDRP_HARDWAREID, nullptr, nullptr, 0,
                                              &requiredSize);

            if (requiredSize == 0)
                continue;

            std::vector<BYTE> buffer(requiredSize);
            if (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData, SPDRP_HARDWAREID, nullptr, buffer.data(),
                                                   requiredSize, nullptr))
                continue;

            bool matched = false;
            for (auto pCurrent = reinterpret_cast<LPCWSTR>(buffer.data()); *pCurrent != L'\0';
                 pCurrent += wcslen(pCurrent) + 1)
            {
                if (_wcsicmp(pCurrent, hardwareId.c_str()) == 0)
                {
                    matched = true;
                    break;
                }
            }

            if (!matched)
                continue;

            WCHAR instanceId[MAX_DEVICE_ID_LEN] = {};
            SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfoData, instanceId, MAX_DEVICE_ID_LEN, nullptr);

            SP_REMOVEDEVICE_PARAMS rmdParams = {};
            rmdParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
            rmdParams.ClassInstallHeader.InstallFunction = DIF_REMOVE;
            rmdParams.Scope = DI_REMOVEDEVICE_GLOBAL;
            rmdParams.HwProfile = 0;

            if (!SetupDiSetClassInstallParams(hDevInfo, &devInfoData, &rmdParams.ClassInstallHeader,
                                              sizeof(rmdParams)) ||
                !SetupDiCallClassInstaller(DIF_REMOVE, hDevInfo, &devInfoData))
            {
                logger->error("Failed to remove device %v, error: %v",
                              std::wstring(instanceId),
                              nefarius::utilities::Win32Error("SetupDiCallClassInstaller").getErrorMessageA());
                failedCount++;
                continue;
            }

            SP_DEVINSTALL_PARAMS devParams = {};
            devParams.cbSize = sizeof(SP_DEVINSTALL_PARAMS);
            if (SetupDiGetDeviceInstallParams(hDevInfo, &devInfoData, &devParams) &&
                (devParams.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT)))
            {
                logger->info("Removed: %v (reboot required)", std::wstring(instanceId));
                rebootRequired = true;
            }
            else
            {
                logger->info("Removed: %v", std::wstring(instanceId));
            }

            removedCount++;
        }

        if (removedCount == 0 && failedCount == 0)
        {
            logger->warn("No devices found matching hardware ID \"%v\"", arguments[2]);
            return EXIT_FAILURE;
        }

        if (failedCount > 0)
        {
            logger->error("%v device(s) removed, %v failed", removedCount, failedCount);
            return EXIT_FAILURE;
        }

        logger->info("%v device(s) removed successfully%v",
                     removedCount,
                     rebootRequired ? ", reboot required" : "");
        return (rebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
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

        auto ret = AddDeviceClassFilter(&guid.value(),
                                        nefarius::utilities::ConvertAnsiToWide(serviceName), pos);

        if (ret)
        {
            if (cmdl[{"--attempt-restart-affected"}])
            {
                const int restartTimeoutMs = ParseTimeoutMs(cmdl, "--restart-timeout", 10000);

                const auto restartResult = RestartAffectedDevices(
                    {guid.value()}, std::chrono::milliseconds(restartTimeoutMs));

                return (restartResult.AnyRebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
            }

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
            if (cmdl[{"--attempt-restart-affected"}])
            {
                const int restartTimeoutMs = ParseTimeoutMs(cmdl, "--restart-timeout", 10000);

                const auto restartResult = RestartAffectedDevices(
                    {guid.value()}, std::chrono::milliseconds(restartTimeoutMs));

                return (restartResult.AnyRebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
            }

            logger->warn("Filter enabled. Reconnect affected devices or reboot system to apply changes!");
            return EXIT_SUCCESS;
        }

        logger->error("Failed to modify filter value, error: %v", ret.error().getErrorMessageA());
        return ret.error().getErrorCode();
    }

    if (cmdl[{"--install-filter-driver"}])
    {
        logger->verbose(1, "Invoked --install-filter-driver");

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

        if (attribs == INVALID_FILE_ATTRIBUTES)
        {
            logger->error("Failed to query attributes of the given INF path, error: %v", GetLastError());
            return EXIT_FAILURE;
        }

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

        logger->info("INF file installed successfully");

        //
        // Unlike --inf-default-install, restart-and-settle always runs; this command is meant to
        // be a single atomic "install and make sure it's actually usable" step.
        // 
        SettleFilterDriverInstall(cmdl, infPath, rebootRequired);

        if (rebootRequired)
        {
            logger->warn(
                "Filter driver installed, but a reboot (or reconnecting affected devices) may be required for it to become fully operational");
        }

        return (rebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
    }

    if (cmdl[{"--uninstall-filter-driver"}])
    {
        logger->verbose(1, "Invoked --uninstall-filter-driver");

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

        //
        // Step 1: remove the filter registry entry FIRST and confirm it is actually gone before
        // touching the service at all. This ordering is what makes it impossible to end up with
        // a dangling UpperFilters/LowerFilters entry pointing at a since-deleted service, which
        // can prevent the whole device class from starting (the "bricking" scenario).
        // 
        if (const auto removeResult = RemoveDeviceClassFilter(&guid.value(),
                                                               nefarius::utilities::ConvertAnsiToWide(serviceName),
                                                               pos); !removeResult)
        {
            logger->error("Failed to remove filter registry entry, error: %v",
                          removeResult.error().getErrorMessageA());
            return removeResult.error().getErrorCode();
        }

        const auto stillPresent = HasDeviceClassFilter(&guid.value(),
                                                        nefarius::utilities::ConvertAnsiToWide(serviceName), pos);

        if (!stillPresent)
        {
            logger->error(
                "Failed to verify filter registry entry removal, error: %v; aborting before touching the driver service",
                stillPresent.error().getErrorMessageA());
            return stillPresent.error().getErrorCode();
        }

        if (stillPresent.value())
        {
            logger->error(
                "Filter registry entry for service \"%v\" is still present after removal; aborting before touching the driver service to avoid leaving a dangling filter entry",
                serviceName);
            return EXIT_FAILURE;
        }

        logger->info("Filter registry entry removed successfully");

        //
        // Step 2: restart affected devices so the filter driver actually unloads and frees its
        // image, then delete the service with retry to absorb the brief race where the kernel
        // hasn't yet released the image by the time deletion is attempted.
        // 
        bool rebootRequired = false;

        const int restartTimeoutMs = ParseTimeoutMs(cmdl, "--restart-timeout", 10000);
        const auto restartResult = RestartAffectedDevices(
            {guid.value()}, std::chrono::milliseconds(restartTimeoutMs));
        rebootRequired = rebootRequired || restartResult.AnyRebootRequired;

        const int stopTimeoutMs = ParseTimeoutMs(cmdl, "--stop-timeout", 10000);
        const int retryTimeoutMs = ParseTimeoutMs(cmdl, "--retry-timeout", 5000);

        if (const auto deleteResult = nefarius::winapi::services::DeleteDriverServiceWithRetry(
            serviceName, std::chrono::milliseconds(stopTimeoutMs), std::chrono::milliseconds(retryTimeoutMs),
            &rebootRequired); !deleteResult)
        {
            logger->error("Failed to remove driver service, error: %v", deleteResult.error().getErrorMessageA());
            return deleteResult.error().getErrorCode();
        }

        logger->info("Driver service removed successfully");

        //
        // Step 3 (optional): purge the driver-store package. Opt-in only, and run last so the
        // package is deleted only after the filter entry is confirmed gone and the service has
        // been deleted (i.e. the driver image is already freed).
        // 
        if (const auto storeInfPath = cmdl({"--inf-path"}).str(); !storeInfPath.empty())
        {
            if (_access(storeInfPath.c_str(), 0) != 0)
            {
                logger->warn("The given --inf-path \"%v\" doesn't exist; skipping driver store purge",
                             storeInfPath);
            }
            else if (const auto purgeResult = nefarius::devcon::RemoveDriverStorePackage(
                nefarius::utilities::ConvertAnsiToWide(storeInfPath), &rebootRequired); !purgeResult)
            {
                logger->warn("Failed to purge driver store package, error: %v; a reboot may be required",
                             purgeResult.error().getErrorMessageA());
                rebootRequired = true;
            }
            else
            {
                logger->info("Driver store package purged successfully");
            }
        }

        if (rebootRequired)
        {
            logger->warn("Filter driver removed, but a reboot may be required to fully complete this operation");
        }

        return (rebootRequired) ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
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

        //
        // Best-effort: releases the driver's file locks by detaching any device still bound to
        // it before the service (and its .sys file) is stopped/deleted below. A device that
        // couldn't be detached only means a reboot may still be required for the removal to
        // fully take effect; it never blocks the stop/delete that follows.
        // 
        DetachAffectedDevicesForService(cmdl, serviceName);

        const int stopTimeoutMs = ParseTimeoutMs(cmdl, "--stop-timeout", 10000);
        bool rebootRequired = false;

        if (const auto result = nefarius::winapi::services::DeleteDriverService(
            serviceName, std::chrono::milliseconds(stopTimeoutMs), &rebootRequired); !result)
        {
            logger->error("Failed to remove driver service, error: %v", result.error().getErrorMessageA());
            return result.error().getErrorCode();
        }

        if (rebootRequired)
        {
            logger->warn(
                "Driver service marked for removal, but its driver never advertised support for being stopped live; a reboot is required to fully remove it");
            return ERROR_SUCCESS_REBOOT_REQUIRED;
        }

        logger->info("Driver service removed successfully");

        return EXIT_SUCCESS;
    }

    if (cmdl[{"--reenumerate-affected"}])
    {
        int errorCode;
        if (!IsAdmin(errorCode)) return errorCode;

        if (!(cmdl({"--service-name"}) >> serviceName))
        {
            logger->error("Service name missing");
            return EXIT_FAILURE;
        }

        std::string stateFilePath;

        if (!(cmdl({"--state-file"}) >> stateFilePath))
        {
            const auto defaultPath = GetDefaultDetachStateFilePath(serviceName);

            if (!defaultPath)
            {
                logger->error("Failed to determine default state file location");
                return EXIT_FAILURE;
            }

            stateFilePath = *defaultPath;
        }

        const auto records = ReadDetachStateFile(stateFilePath);

        if (!records)
        {
            if (records.error().getErrorCode() == ERROR_FILE_NOT_FOUND)
            {
                logger->warn("Detach state file \"%v\" not found, nothing to re-enumerate", stateFilePath);
                return EXIT_SUCCESS;
            }

            logger->error("Failed to read detach state file \"%v\", error: %v", stateFilePath,
                         records.error().getErrorMessageA());
            return records.error().getErrorCode();
        }

        if (records->empty())
        {
            logger->warn("Detach state file \"%v\" has no devices to re-enumerate", stateFilePath);
            return EXIT_SUCCESS;
        }

        std::vector<std::wstring> parentInstanceIds;

        for (const auto& record : records.value())
        {
            const bool alreadyPresent = std::any_of(parentInstanceIds.begin(), parentInstanceIds.end(),
                                                     [&record](const std::wstring& existing)
                                                     {
                                                         return _wcsicmp(existing.c_str(),
                                                                        record.ParentInstanceId.c_str()) == 0;
                                                     });

            if (!alreadyPresent)
            {
                parentInstanceIds.push_back(record.ParentInstanceId);
            }
        }

        const int reenumerateTimeoutMs = ParseTimeoutMs(cmdl, "--restart-timeout", 10000);
        bool anyFailed = false;

        for (const auto& parentInstanceId : parentInstanceIds)
        {
            const auto result = nefarius::devcon::ReenumerateParentDevNode(
                parentInstanceId, std::chrono::milliseconds(reenumerateTimeoutMs));

            const std::string parentInstanceIdA = nefarius::utilities::ConvertWideToANSI(parentInstanceId);

            if (result.Succeeded)
            {
                logger->info("Re-enumerated devnode \"%v\"", parentInstanceIdA);
            }
            else if (result.TimedOut)
            {
                anyFailed = true;
                logger->warn("Timed out re-enumerating devnode \"%v\", a reboot may be required",
                             parentInstanceIdA);
            }
            else
            {
                anyFailed = true;
                logger->warn("Failed to re-enumerate devnode \"%v\", a reboot may be required", parentInstanceIdA);
            }
        }

        //
        // Single-use by design: whether or not every devnode could be re-enumerated, stale
        // entries must not be replayed against a future, unrelated driver upgrade.
        // 
        std::error_code deleteEc;
        std::filesystem::remove(stateFilePath, deleteEc);

        return anyFailed ? ERROR_SUCCESS_REBOOT_REQUIRED : EXIT_SUCCESS;
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

        {
            int findErrorCode;
            const auto dupCheck = CheckNoDuplicates(cmdl, hwId, findErrorCode);

            if (dupCheck == DeviceExistsResult::Error)
                return findErrorCode;

            if (dupCheck == DeviceExistsResult::Found)
                return EXIT_SUCCESS;
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
            bool createdConcurrently = false;

            if (cmdl[{"--no-duplicates"}])
            {
                int recheckErrorCode; // intentionally ignored; prefer the original Create() error on failure
                createdConcurrently = DeviceExists(hwId, recheckErrorCode) == DeviceExistsResult::Found;
                (void)recheckErrorCode;
            }

            if (!createdConcurrently)
            {
                logger->error("Failed to create device node, error: %v", ret.error().getErrorMessageA());
                return ret.error().getErrorCode();
            }

            logger->info("Device with hardware ID \"%v\" was created concurrently, skipping creation", hwId);
            return EXIT_SUCCESS;
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

        RestartAffectedDevicesForInf(cmdl, infPath, rebootRequired);

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

        RestartAffectedDevicesForInf(cmdl, infPath, rebootRequired);

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

    constexpr PCSTR ENABLE_BLUETOOTH_SERVICE = "--enable-bluetooth-service";
    constexpr PCSTR DISABLE_BLUETOOTH_SERVICE = "--disable-bluetooth-service";

    if (cmdl[{ENABLE_BLUETOOTH_SERVICE}] || cmdl[{DISABLE_BLUETOOTH_SERVICE}])
    {
        //
        // Sanity check
        // 
        if (cmdl[{ENABLE_BLUETOOTH_SERVICE}] && cmdl[{DISABLE_BLUETOOTH_SERVICE}])
        {
            logger->error("You must either specify 'enable' or 'disable' action, not both together");
            return EXIT_FAILURE;
        }

        const bool enable = cmdl[{ENABLE_BLUETOOTH_SERVICE}];

        auto bthServiceName = cmdl({"--service-name"}).str();
        auto bthServiceGuid = cmdl({"--service-guid"}).str();

        if (bthServiceName.empty())
        {
            logger->error("Service name missing");
            return EXIT_FAILURE;
        }

        if (bthServiceGuid.empty())
        {
            logger->error("Service GUID missing");
            return EXIT_FAILURE;
        }

        const auto guid = nefarius::winapi::GUIDFromString(bthServiceGuid);

        if (!guid)
        {
            logger->error(
                "GUID format invalid, expected format (with or without brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx");
            return EXIT_FAILURE;
        }

        int errorCode;
        if (!IsAdmin(errorCode))
        {
            return errorCode;
        }

        if (auto ret = nefarius::winapi::security::AdjustProcessPrivileges(); !ret)
        {
            logger->error("Failed to modify process privileges, error: %v", ret.error().getErrorMessageA());
            return ret.error().getErrorCode();
        }

        auto serviceNameWide = nefarius::utilities::ConvertAnsiToWide(bthServiceName);

        BLUETOOTH_LOCAL_SERVICE_INFO svcInfo = {};
	    wcscpy_s(svcInfo.szName, sizeof(svcInfo.szName) / sizeof(WCHAR), serviceNameWide.c_str());

        svcInfo.Enabled = enable ? TRUE : FALSE;
	    Bthprops bth;

        if (DWORD err; ERROR_SUCCESS != (err = bth.pBluetoothSetLocalServiceInfo(
            nullptr, //callee would select the first found radio
            &guid.value(),
            0,
            &svcInfo
        )))
        {
            auto error = nefarius::utilities::Win32Error(err);
            logger->error("Failed to %v local service, error: %v",
                          enable ? "enable" : "disable",
                          error.getErrorMessageA());
            return error.getErrorCode();
        }

        logger->info("Service %v successfully", enable ? "enabled" : "disabled");

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
    std::cout << "      --inf-path               Path to the INF file to install, absolute or relative to CWD (required)" << '\n';
    std::cout << "    --uninstall-driver         Invoke the removal of a given PNP driver" << '\n';
    std::cout << "      --inf-path               Path to the INF file to uninstall, absolute or relative to CWD (required)" << '\n';
    std::cout << "    --create-device-node       Create a new ROOT enumerated virtual device" << '\n';
    std::cout << "      --hardware-id            Hardware ID of the new device (required)" << '\n';
    std::cout << "      --class-name             Device Class Name of the new device (required)" << '\n';
    std::cout << "      --class-guid             Device Class GUID of the new device (required)" << '\n';
    std::cout << "      --no-duplicates          Skip creation if a device with the same Hardware ID already exists (optional)" << '\n';
    std::cout << "    --remove-device-node       Removes a device and its driver" << '\n';
    std::cout << "      --hardware-id            Hardware ID of the device (required)" << '\n';
    std::cout << "      --class-guid             Device Class GUID of the device (required)" << '\n';
    std::cout << "    --add-class-filter         Adds a service to a device class' filter collection" << '\n';
    std::cout << "      --position               Which filter to modify (required)" << '\n';
    std::cout << "                                 Valid values include: upper|lower" << '\n';
    std::cout << "      --service-name           The driver service name to insert (required)" << '\n';
    std::cout << "      --class-guid             Device Class GUID to modify (required)" << '\n';
    std::cout << "      --attempt-restart-affected Best-effort attempt to restart present devices of this class; a reboot may still be required (optional)" << '\n';
    std::cout << "      --restart-timeout        Milliseconds to wait per device restart attempt, default 10000 (optional)" << '\n';
    std::cout << "    --remove-class-filter      Removes a service to a device class' filter collection" << '\n';
    std::cout << "      --position               Which filter to modify (required)" << '\n';
    std::cout << "                                 Valid values include: upper|lower" << '\n';
    std::cout << "      --service-name           The driver service name to insert (required)" << '\n';
    std::cout << "      --class-guid             Device Class GUID to modify (required)" << '\n';
    std::cout << "      --attempt-restart-affected Best-effort attempt to restart present devices of this class; a reboot may still be required (optional)" << '\n';
    std::cout << "      --restart-timeout        Milliseconds to wait per device restart attempt, default 10000 (optional)" << '\n';
    std::cout << "    --install-filter-driver    Atomically installs an INF-based filter driver and confirms it is settled" << '\n';
    std::cout << "      --inf-path               Path to the INF file to install, absolute or relative to CWD (required)" << '\n';
    std::cout << "      --class-guid             Additional Device Class GUID to restart/settle, on top of what the INF declares (optional)" << '\n';
    std::cout << "      --restart-timeout        Milliseconds to wait per device restart attempt, default 10000 (optional)" << '\n';
    std::cout << "      --health-timeout         Milliseconds to wait for each declared filter service to reach SERVICE_RUNNING when a device of its class is present, default 10000 (optional)" << '\n';
    std::cout << "                                 A filter service with no present device is expected to remain stopped (demand-start); that is not reported as an error" << '\n';
    std::cout << "    --uninstall-filter-driver  Atomically removes a class filter entry and its driver service without ever leaving a dangling filter entry" << '\n';
    std::cout << "      --position               Which filter to modify (required)" << '\n';
    std::cout << "                                 Valid values include: upper|lower" << '\n';
    std::cout << "      --service-name           The driver service name to remove (required)" << '\n';
    std::cout << "      --class-guid             Device Class GUID to modify (required)" << '\n';
    std::cout << "      --restart-timeout        Milliseconds to wait per device restart attempt, default 10000 (optional)" << '\n';
    std::cout << "      --stop-timeout           Milliseconds to wait for the service to stop, default 10000 (optional)" << '\n';
    std::cout << "      --retry-timeout          Milliseconds to keep retrying service deletion while the driver image is still in use, default 5000 (optional)" << '\n';
    std::cout << "      --inf-path               Path to the original INF file; if given, also purges the matching published package from the driver store, default: not purged (optional)" << '\n';
    std::cout << "    --create-driver-service    Creates a new service with a kernel driver as binary" << '\n';
    std::cout << "      --bin-path               Path to the .sys file, absolute or relative to CWD (required)" << '\n';
    std::cout << "      --service-name           The driver service name to create (required)" << '\n';
    std::cout << "      --display-name           The friendly name of the service (required)" << '\n';
    std::cout << "    --remove-driver-service    Removes an existing kernel driver service" << '\n';
    std::cout << "      --service-name           The driver service name to remove (required)" << '\n';
    std::cout << "      --stop-timeout           Milliseconds to wait for the service to stop, default 10000 (optional)" << '\n';
    std::cout << "      --attempt-detach-affected Best-effort attempt to detach present devices bound to this service before removal, so its driver file can be safely replaced/deleted; a reboot may still be required (optional)" << '\n';
    std::cout << "      --restart-timeout        Milliseconds to wait per device detach attempt, default 10000 (optional)" << '\n';
    std::cout << "      --state-file             Where to persist detached devices for a later --reenumerate-affected call, default a per-service-name file in %TEMP%\\nefconc (optional)" << '\n';
    std::cout << "    --reenumerate-affected     Re-enumerates devices previously detached via --remove-driver-service --attempt-detach-affected" << '\n';
    std::cout << "      --service-name           The driver service name whose detached devices to re-enumerate (required)" << '\n';
    std::cout << "      --restart-timeout        Milliseconds to wait per devnode re-enumeration attempt, default 10000 (optional)" << '\n';
    std::cout << "      --state-file             Where to read detached devices from, default a per-service-name file in %TEMP%\\nefconc (optional)" << '\n';
    std::cout << "    --inf-default-install      Installs an INF file with a [DefaultInstall] section" << '\n';
    std::cout << "      --inf-path               Path to the INF file to install, absolute or relative to CWD (required)" << '\n';
    std::cout << "      --attempt-restart-affected Best-effort attempt to restart devices affected by any class filter changes; a reboot may still be required (optional)" << '\n';
    std::cout << "      --class-guid             Additional Device Class GUID to restart devices for, on top of what the INF declares (optional)" << '\n';
    std::cout << "      --restart-timeout        Milliseconds to wait per device restart attempt, default 10000 (optional)" << '\n';
    std::cout << "    --inf-default-uninstall    Uninstalls an INF file with a [DefaultUninstall] section" << '\n';
    std::cout << "      --inf-path               Path to the INF file to uninstall, absolute or relative to CWD (required)" << '\n';
    std::cout << "      --attempt-restart-affected Best-effort attempt to restart devices affected by any class filter changes; a reboot may still be required (optional)" << '\n';
    std::cout << "      --class-guid             Additional Device Class GUID to restart devices for, on top of what the INF declares (optional)" << '\n';
    std::cout << "      --restart-timeout        Milliseconds to wait per device restart attempt, default 10000 (optional)" << '\n';
    std::cout << "    --delete-file-on-reboot    Marks a given file to get deleted on next reboot" << '\n';
    std::cout << "      --file-path              Path of the file to remove, absolute or relative to CWD (required)" << '\n';
    std::cout << "    --find-hwid                Shows one or more devices matching a partial Hardware ID" << '\n';
    std::cout << "      --hardware-id            (Partial) Hardware ID of the device to match against (required)" <<
        '\n';
    std::cout << "    --enable-bluetooth-service   Enables a local Bluetooth service" << '\n';
    std::cout << "      --service-name             The service name" << '\n';
    std::cout << "      --service-guid             The service GUID" << '\n';
    std::cout << "    --disable-bluetooth-service  Disables a local Bluetooth service" << '\n';
    std::cout << "      --service-name             The service name" << '\n';
    std::cout << "      --service-guid             The service GUID" << '\n';
    std::cout << "    -v, --version              Display version of this utility" << '\n';
    std::cout << '\n';
    std::cout << "  logging:" << '\n';
    std::cout << "    --default-log-file=.\\log.txt       Write details of execution to a log file (optional)" <<
        '\n';
    std::cout << "    --verbose                          Turn on verbose/diagnostic logging (optional)" << '\n';
    std::cout << '\n';
    std::cout << "  devcon:" << '\n';
    std::cout << "    install [INFFile] [HardwareID]     Creates and installs a ROOT-enumerated device and driver" <<
        '\n';
    std::cout << "      --no-duplicates                  Skip device creation if it already exists; still updates the driver (optional)" <<
        '\n';
    std::cout << "      --remove-duplicates              Remove extra device nodes with same Hardware ID, keeping one (optional, requires --no-duplicates)" <<
        '\n';
    std::cout << "    remove [HardwareID]                Removes all present devices matching the given Hardware ID" <<
        '\n';
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

    const char* ToString(nefarius::devcon::RestartStrategy strategy)
    {
        switch (strategy)
        {
        case nefarius::devcon::RestartStrategy::UsbPortCycle:
            return "USB port cycle";
        case nefarius::devcon::RestartStrategy::PropertyChange:
            return "property change";
        case nefarius::devcon::RestartStrategy::RemoveAndReenumerate:
            return "remove and re-enumerate";
        case nefarius::devcon::RestartStrategy::None:
        default:
            return "none";
        }
    }

    // Only the CM_PROB_* codes that are plausible for a device that just went through a restart
    // ladder are named; anything else is reported as its raw numeric value so nothing is hidden.
    std::string ProblemCodeToString(ULONG problemCode)
    {
        switch (problemCode)
        {
        case CM_PROB_NEED_RESTART:
            return "CM_PROB_NEED_RESTART";
        case CM_PROB_WILL_BE_REMOVED:
            return "CM_PROB_WILL_BE_REMOVED";
        case CM_PROB_MOVED:
            return "CM_PROB_MOVED";
        case CM_PROB_TOO_EARLY:
            return "CM_PROB_TOO_EARLY";
        case CM_PROB_NO_VALID_LOG_CONF:
            return "CM_PROB_NO_VALID_LOG_CONF";
        case CM_PROB_FAILED_INSTALL:
            return "CM_PROB_FAILED_INSTALL";
        case CM_PROB_HARDWARE_DISABLED:
            return "CM_PROB_HARDWARE_DISABLED";
        case CM_PROB_NOT_CONFIGURED:
            return "CM_PROB_NOT_CONFIGURED";
        case CM_PROB_FAILED_ADD:
            return "CM_PROB_FAILED_ADD";
        case CM_PROB_DISABLED_SERVICE:
            return "CM_PROB_DISABLED_SERVICE";
        case CM_PROB_DEVICE_NOT_THERE:
            return "CM_PROB_DEVICE_NOT_THERE";
        case CM_PROB_REGISTRY:
            return "CM_PROB_REGISTRY";
        case CM_PROB_PHANTOM:
            return "CM_PROB_PHANTOM";
        default:
            return std::to_string(problemCode);
        }
    }

    // Appended to failure/warning log lines for a still-present device so verbose logs can
    // distinguish one that is genuinely stuck (has a problem code) from one that simply took
    // slightly longer than a single strategy's verify window - the two used to look identical.
    std::string DescribeFinalDevNodeState(const nefarius::devcon::DeviceRestartResult& result)
    {
        if (!result.FinalStatusValid)
        {
            return " (final devnode status could not be queried, error 0x" +
                std::to_string(result.FinalStatusError) + ")";
        }

        std::string detail = " (final devnode state: started=";
        detail += result.FinalStarted ? "true" : "false";
        detail += result.FinalHasProblem
                       ? (", problem=" + ProblemCodeToString(result.FinalProblemCode))
                       : std::string(", no problem code");
        detail += ")";
        return detail;
    }

    RestartAffectedDevicesResult RestartAffectedDevices(const std::vector<GUID>& classGuids,
                                                        std::chrono::milliseconds timeout)
    {
        el::Logger* logger = el::Loggers::getLogger("default");

        RestartAffectedDevicesResult summary{false};

        if (classGuids.empty())
        {
            logger->warn(
                "No affected device classes could be identified. Reconnect affected devices or reboot system to apply changes!");
            return summary;
        }

        nefarius::devcon::DeviceRestartOptions options;
        options.PerDeviceTimeout = timeout;

        size_t deviceCount = 0;

        for (const auto& classGuid : classGuids)
        {
            const auto instances = nefarius::devcon::ListDeviceInstancesByClass(&classGuid);

            if (!instances)
            {
                WCHAR guidStr[64];
                StringFromGUID2(classGuid, guidStr, ARRAYSIZE(guidStr));

                logger->warn("Failed to enumerate devices for class %v, error: %v",
                             nefarius::utilities::ConvertWideToANSI(guidStr), instances.error().getErrorMessageA());
                continue;
            }

            for (const auto& instanceId : instances.value())
            {
                deviceCount++;

                const auto result = nefarius::devcon::RestartDeviceInstance(instanceId, options);

                const std::wstring displayName = result.FriendlyName.empty()
                                                     ? result.InstanceId
                                                     : result.FriendlyName;
                const std::string displayNameA = nefarius::utilities::ConvertWideToANSI(displayName);

                // Only meaningful for a still-present device; the !DevicePresent branch below
                // never consults it.
                const std::string finalStateDetail = DescribeFinalDevNodeState(result);

                if (result.Succeeded)
                {
                    logger->info("Restarted device \"%v\" via %v", displayNameA, ToString(result.Strategy));
                }
                else if (!result.DevicePresent)
                {
                    // The device disappeared during the restart ladder (unplugged, or a phantom
                    // node) - there is nothing left to restart, this is informational rather than
                    // a failure.
                    logger->info("Device \"%v\" is no longer present; nothing to restart", displayNameA);
                }
                else if (result.TimedOut)
                {
                    logger->warn("Timed out attempting to restart device \"%v\" (last attempted: %v)%v",
                                 displayNameA, ToString(result.LastAttempted), finalStateDetail);
                }
                else if (!result.VetoName.empty())
                {
                    logger->warn(
                        "Could not restart device \"%v\", blocked by \"%v\" (%v)%v",
                        displayNameA, nefarius::utilities::ConvertWideToANSI(result.VetoName),
                        nefarius::utilities::Win32Error(result.LastError).getErrorMessageA(), finalStateDetail);
                }
                else
                {
                    logger->warn("Could not restart device \"%v\", last attempted: %v, error: %v%v",
                                 displayNameA, ToString(result.LastAttempted),
                                 nefarius::utilities::Win32Error(result.LastError).getErrorMessageA(),
                                 finalStateDetail);
                }

                // Escalate to "a reboot may be required" only on real evidence, never on the bare
                // fact that this attempt didn't verify as Succeeded:
                //  - result.RebootRequired: Windows itself flagged DI_NEEDRESTART/DI_NEEDREBOOT
                //    for this device. This is reported independently of Succeeded (a device can
                //    come back online fine and still have this flag set by a strategy), so it is
                //    honored even when Succeeded is true.
                //  - the final authoritative re-check found the device present with a problem code
                //    that itself indicates a pending restart/reboot (e.g. CM_PROB_NEED_RESTART).
                // A device that is merely !Succeeded with no such evidence (including one that is
                // no longer present at all, or "present, not started, no problem code") is warned
                // about above with full detail but does not by itself demand a reboot.
                const bool problemIndicatesRestartNeeded = result.DevicePresent && result.FinalHasProblem &&
                    (result.FinalProblemCode == CM_PROB_NEED_RESTART ||
                        result.FinalProblemCode == CM_PROB_WILL_BE_REMOVED);

                if (result.RebootRequired || problemIndicatesRestartNeeded)
                {
                    summary.AnyRebootRequired = true;
                }
            }
        }

        if (deviceCount == 0)
        {
            logger->info("No present devices found for the affected device class(es)");
        }

        return summary;
    }

    int ParseTimeoutMs(const argh::parser& cmdl, const std::string& paramName, int defaultMs)
    {
        el::Logger* logger = el::Loggers::getLogger("default");

        const std::string raw = cmdl({paramName}, std::to_string(defaultMs)).str();

        try
        {
            size_t consumed = 0;
            const int parsed = std::stoi(raw, &consumed);

            if (consumed == raw.size() && parsed > 0)
            {
                return parsed;
            }
        }
        catch (const std::exception&)
        {
            // fall through to the warning/default below
        }

        logger->warn(
            "Invalid %v value \"%v\", expected a positive number of milliseconds; using default of %v ms",
            paramName, raw, defaultMs);
        return defaultMs;
    }

    struct DiscoveredClassFilterTargets
    {
        ///< Deduplicated union of every class GUID the INF declares plus an explicit --class-guid, if given
        std::vector<GUID> ClassGuids;
        ///< Raw (service name, class GUID) pairs the INF declares; empty if discovery failed
        std::vector<nefarius::devcon::InfClassFilterTarget> Targets;
        ///< True if the INF's class filter targets could not be determined (already logged as a warning)
        bool DiscoveryFailed = false;
    };

    //
    // Shared by RestartAffectedDevicesForInf and SettleFilterDriverInstall: figures out which
    // device class GUID(s) an INF's class filter registrations target, merged with an optional
    // explicit --class-guid. Callers decide for themselves how to react to DiscoveryFailed and
    // whether they need the raw Targets (e.g. to group by service name).
    // 
    DiscoveredClassFilterTargets DiscoverClassFilterTargets(const argh::parser& cmdl, const std::string& infPath)
    {
        el::Logger* logger = el::Loggers::getLogger("default");

        DiscoveredClassFilterTargets result;

        const auto addUniqueGuid = [&result](const GUID& candidate)
        {
            const bool alreadyPresent = std::any_of(result.ClassGuids.begin(), result.ClassGuids.end(),
                                                     [&candidate](const GUID& existing)
                                                     {
                                                         return IsEqualGUID(existing, candidate);
                                                     });

            if (!alreadyPresent)
            {
                result.ClassGuids.push_back(candidate);
            }
        };

        if (const auto targets = nefarius::devcon::GetInfClassFilterTargets(
            nefarius::utilities::ConvertAnsiToWide(infPath)); targets)
        {
            result.Targets = targets.value();

            for (const auto& target : result.Targets)
            {
                addUniqueGuid(target.ClassGuid);
            }
        }
        else
        {
            logger->warn("Failed to determine affected device class(es) from INF, error: %v",
                         targets.error().getErrorMessageA());
            result.DiscoveryFailed = true;
        }

        if (const auto explicitClassGuid = cmdl({"--class-guid"}).str(); !explicitClassGuid.empty())
        {
            if (const auto guid = nefarius::winapi::GUIDFromString(explicitClassGuid); guid)
            {
                addUniqueGuid(guid.value());
            }
            else
            {
                logger->error("Invalid --class-guid value \"%v\", error: %v", explicitClassGuid,
                             guid.error().getErrorMessageA());
            }
        }

        return result;
    }

    void RestartAffectedDevicesForInf(const argh::parser& cmdl, const std::string& infPath, bool& rebootRequired)
    {
        if (!cmdl[{"--attempt-restart-affected"}])
        {
            return;
        }

        const auto discovered = DiscoverClassFilterTargets(cmdl, infPath);

        if (discovered.DiscoveryFailed)
        {
            //
            // We were asked to restart devices affected by the INF's class filter declarations,
            // but couldn't determine what they are; surface reboot-required rather than silently
            // skipping the restart (or restarting only an explicit --class-guid, if any) without
            // any signal to the caller that the requested restart may not have actually happened.
            // 
            rebootRequired = true;
        }

        const int restartTimeoutMs = ParseTimeoutMs(cmdl, "--restart-timeout", 10000);

        const auto restartResult = RestartAffectedDevices(discovered.ClassGuids,
                                                            std::chrono::milliseconds(restartTimeoutMs));

        rebootRequired = rebootRequired || restartResult.AnyRebootRequired;
    }

    void SettleFilterDriverInstall(const argh::parser& cmdl, const std::string& infPath, bool& rebootRequired)
    {
        el::Logger* logger = el::Loggers::getLogger("default");

        const auto discovered = DiscoverClassFilterTargets(cmdl, infPath);

        if (discovered.DiscoveryFailed)
        {
            //
            // The whole point of this function is to hand the caller a trustworthy verdict about
            // the driver's post-install state. If we can't even determine which classes/services
            // to check, we cannot make that claim, so surface reboot-required instead of silently
            // falling through to EXIT_SUCCESS.
            // 
            rebootRequired = true;
        }

        struct FilterServiceEntry
        {
            std::wstring ServiceName;
            std::vector<GUID> ClassGuids;
        };

        std::vector<FilterServiceEntry> serviceEntries;

        const auto addServiceClassGuid = [&serviceEntries](const std::wstring& serviceNameWide, const GUID& classGuid)
        {
            const auto it = std::find_if(serviceEntries.begin(), serviceEntries.end(),
                                         [&serviceNameWide](const FilterServiceEntry& entry)
                                         {
                                             return _wcsicmp(entry.ServiceName.c_str(), serviceNameWide.c_str()) == 0;
                                         });

            if (it == serviceEntries.end())
            {
                serviceEntries.push_back({serviceNameWide, {classGuid}});
                return;
            }

            const bool alreadyPresent = std::any_of(it->ClassGuids.begin(), it->ClassGuids.end(),
                                                     [&classGuid](const GUID& existing)
                                                     {
                                                         return IsEqualGUID(existing, classGuid);
                                                     });

            if (!alreadyPresent)
            {
                it->ClassGuids.push_back(classGuid);
            }
        };

        for (const auto& target : discovered.Targets)
        {
            addServiceClassGuid(target.ServiceName, target.ClassGuid);
        }

        const int restartTimeoutMs = ParseTimeoutMs(cmdl, "--restart-timeout", 10000);

        const auto restartResult = RestartAffectedDevices(discovered.ClassGuids,
                                                            std::chrono::milliseconds(restartTimeoutMs));

        rebootRequired = rebootRequired || restartResult.AnyRebootRequired;

        if (serviceEntries.empty())
        {
            logger->verbose(1, "INF declares no class filter service; skipping service health check");
            return;
        }

        const int healthTimeoutMs = ParseTimeoutMs(cmdl, "--health-timeout", 10000);

        //
        // Compute device presence once per unique class GUID (after the restart above) instead of
        // re-enumerating devices for every service entry that happens to target the same class.
        // A failed enumeration is tracked separately from "queried successfully, found nothing" —
        // conflating the two would make a transient enumeration error look like "no device of
        // this class", which would incorrectly take the no-error demand-started branch below.
        // 
        enum class ClassDevicePresence
        {
            Absent,
            Present,
            QueryFailed
        };

        std::vector<std::pair<GUID, ClassDevicePresence>> classDevicePresence;
        classDevicePresence.reserve(discovered.ClassGuids.size());

        for (const auto& classGuid : discovered.ClassGuids)
        {
            const auto instances = nefarius::devcon::ListDeviceInstancesByClass(&classGuid);

            if (!instances)
            {
                classDevicePresence.emplace_back(classGuid, ClassDevicePresence::QueryFailed);
                continue;
            }

            classDevicePresence.emplace_back(
                classGuid, instances.value().empty() ? ClassDevicePresence::Absent : ClassDevicePresence::Present);
        }

        const auto getClassPresence = [&classDevicePresence](const GUID& classGuid)
        {
            const auto it = std::find_if(classDevicePresence.begin(), classDevicePresence.end(),
                                         [&classGuid](const std::pair<GUID, ClassDevicePresence>& entry)
                                         {
                                             return IsEqualGUID(entry.first, classGuid);
                                         });

            return it != classDevicePresence.end() ? it->second : ClassDevicePresence::QueryFailed;
        };

        for (const auto& entry : serviceEntries)
        {
            const std::string serviceNameA = nefarius::utilities::ConvertWideToANSI(entry.ServiceName);

            bool anyDevicePresent = false;
            bool anyQueryFailed = false;

            for (const auto& classGuid : entry.ClassGuids)
            {
                switch (getClassPresence(classGuid))
                {
                case ClassDevicePresence::Present:
                    anyDevicePresent = true;
                    break;
                case ClassDevicePresence::QueryFailed:
                    anyQueryFailed = true;
                    break;
                case ClassDevicePresence::Absent:
                default:
                    break;
                }
            }

            if (anyDevicePresent)
            {
                const auto status = nefarius::winapi::services::WaitForServiceState(
                    entry.ServiceName, SERVICE_RUNNING, std::chrono::milliseconds(healthTimeoutMs));

                if (!status)
                {
                    logger->warn("Filter service \"%v\" could not be queried after install, error: %v",
                                 serviceNameA, status.error().getErrorMessageA());
                    rebootRequired = true;
                    continue;
                }

                if (status.value().dwCurrentState == SERVICE_RUNNING)
                {
                    logger->info("Filter service \"%v\" is running", serviceNameA);
                }
                else
                {
                    logger->warn(
                        "Filter service \"%v\" did not reach the running state within %v ms; reconnecting affected devices or a reboot may be required",
                        serviceNameA, healthTimeoutMs);
                    rebootRequired = true;
                }

                continue;
            }

            if (anyQueryFailed)
            {
                //
                // Couldn't determine whether a device of this service's class is present, so we
                // can't tell whether SERVICE_RUNNING or demand-started-and-stopped is the correct
                // expectation. Don't guess either way; surface reboot-required instead.
                // 
                logger->warn(
                    "Failed to determine whether a device of filter service \"%v\"'s class is present; its post-install state could not be verified",
                    serviceNameA);
                rebootRequired = true;
                continue;
            }

            //
            // No device of this filter's class is currently present, so the service is
            // legitimately registered but demand-started (not yet running); that is expected,
            // not an error, and is exactly the false-positive a naive immediate health probe
            // would otherwise report.
            // 
            if (const auto status = nefarius::winapi::services::GetServiceStatus(entry.ServiceName); status)
            {
                logger->info(
                    "Filter service \"%v\" is registered (state: %v); no present device to start it yet",
                    serviceNameA, status.value().dwCurrentState);
            }
            else
            {
                logger->warn("Filter service \"%v\" could not be found after install, error: %v",
                             serviceNameA, status.error().getErrorMessageA());
            }
        }
    }

    //
    // Restricts a directory to Administrators and SYSTEM only (replacing, not merging with, any
    // inherited ACEs). The state directory holds data that a later, elevated --reenumerate-affected
    // invocation trusts without further validation, so it must not be writable by a non-admin user.
    // Returns false (and logs) on any failure, so the caller can refuse to persist state into an
    // unprotected location instead of silently trusting it later.
    //
    bool RestrictDirectoryToAdminsAndSystem(const std::filesystem::path& dir)
    {
        el::Logger* logger = el::Loggers::getLogger("default");

        PSECURITY_DESCRIPTOR sd = nullptr;

        // Protected DACL (no inheritance from parent) granting full control to Administrators (BA)
        // and SYSTEM (SY) only.
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:PAI(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)", SDDL_REVISION_1, &sd, nullptr))
        {
            logger->warn("Failed to build security descriptor for state directory, error: %v", GetLastError());
            return false;
        }

        PACL dacl = nullptr;
        BOOL daclPresent = FALSE, daclDefaulted = FALSE;
        bool succeeded = false;

        if (GetSecurityDescriptorDacl(sd, &daclPresent, &dacl, &daclDefaulted) && daclPresent)
        {
            const DWORD result = SetNamedSecurityInfoW(
                const_cast<LPWSTR>(dir.c_str()), SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, dacl, nullptr);

            if (result == ERROR_SUCCESS)
            {
                succeeded = true;
            }
            else
            {
                logger->warn("Failed to restrict permissions on state directory \"%v\", error: %v", dir.string(),
                             result);
            }
        }

        LocalFree(sd);

        return succeeded;
    }

    //
    // Derives a stable, per-service default location for the detach state file so
    // --remove-driver-service and --reenumerate-affected agree on where to look for each other's
    // data when --state-file isn't passed explicitly to either. Placed under %ProgramData% and
    // locked down to Administrators/SYSTEM, since this state is read back and trusted by a later
    // elevated invocation. Fails closed (returns std::nullopt) rather than falling back to a
    // location that couldn't be created or secured.
    // 
    std::optional<std::string> GetDefaultDetachStateFilePath(const std::string& serviceName)
    {
        el::Logger* logger = el::Loggers::getLogger("default");

        std::string sanitized;
        sanitized.reserve(serviceName.size());

        for (const char ch : serviceName)
        {
            sanitized.push_back(
                (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') ? ch : '_');
        }

        if (sanitized.empty())
        {
            sanitized = "_";
        }

        //
        // Sanitization is lossy (e.g. "My Driver" and "My_Driver" both collapse to "My_Driver"), so
        // append a hash of the original, unsanitized name to keep distinct service names from
        // colliding on the same state file.
        //
        const size_t nameHash = std::hash<std::string>{}(serviceName);
        char hashSuffix[2 * sizeof(size_t) + 1];
        snprintf(hashSuffix, sizeof(hashSuffix), "%016zx", nameHash);

        PWSTR programDataPath = nullptr;
        const HRESULT hr = SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programDataPath);

        if (FAILED(hr))
        {
            if (programDataPath)
            {
                CoTaskMemFree(programDataPath);
            }

            logger->error("Failed to determine %%ProgramData%% path, error: %v", hr);
            return std::nullopt;
        }

        std::filesystem::path dir(programDataPath);
        CoTaskMemFree(programDataPath);

        dir /= "nefconc";

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        if (ec)
        {
            logger->error("Failed to create state directory \"%v\", error: %v", dir.string(), ec.message());
            return std::nullopt;
        }

        if (!RestrictDirectoryToAdminsAndSystem(dir))
        {
            logger->error("Failed to secure state directory \"%v\"; refusing to persist detach state there",
                         dir.string());
            return std::nullopt;
        }

        dir /= (sanitized + "-" + hashSuffix + ".detach.state");

        return dir.string();
    }

    //
    // Instance IDs are round-tripped through the state file losslessly regardless of the active
    // ANSI codepage by (de)serializing as UTF-8 directly, instead of going through
    // ConvertWideToANSI/ConvertAnsiToWide (which use CP_ACP and can't represent every code point).
    //
    std::string ConvertWideToUTF8(const std::wstring& wide)
    {
        if (wide.empty())
        {
            return {};
        }

        const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0,
                                              nullptr, nullptr);

        if (size <= 0)
        {
            return {};
        }

        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), size, nullptr,
                             nullptr);

        return result;
    }

    std::wstring ConvertUTF8ToWide(const std::string& utf8)
    {
        if (utf8.empty())
        {
            return {};
        }

        const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);

        if (size <= 0)
        {
            return {};
        }

        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), result.data(), size);

        return result;
    }

    std::expected<void, nefarius::utilities::Win32Error> WriteDetachStateFile(
        const std::string& stateFilePath, const std::vector<DetachedDeviceRecord>& records)
    {
        std::ofstream file(stateFilePath, std::ios::out | std::ios::trunc | std::ios::binary);

        if (!file.is_open())
        {
            return std::unexpected(
                nefarius::utilities::Win32Error(ERROR_OPEN_FAILED, "Failed to open state file for writing"));
        }

        for (const auto& record : records)
        {
            file << ConvertWideToUTF8(record.InstanceId) << '\t'
                << ConvertWideToUTF8(record.ParentInstanceId) << '\n';
        }

        file.close();

        if (file.fail())
        {
            return std::unexpected(nefarius::utilities::Win32Error(ERROR_WRITE_FAULT, "Failed to write state file"));
        }

        return {};
    }

    std::expected<std::vector<DetachedDeviceRecord>, nefarius::utilities::Win32Error> ReadDetachStateFile(
        const std::string& stateFilePath)
    {
        std::ifstream file(stateFilePath, std::ios::binary);

        if (!file.is_open())
        {
            return std::unexpected(nefarius::utilities::Win32Error(ERROR_FILE_NOT_FOUND, "State file not found"));
        }

        std::vector<DetachedDeviceRecord> records;
        std::string line;

        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            if (line.empty())
            {
                continue;
            }

            const auto tab = line.find('\t');

            if (tab == std::string::npos)
            {
                continue;
            }

            DetachedDeviceRecord record;
            record.InstanceId = ConvertUTF8ToWide(line.substr(0, tab));
            record.ParentInstanceId = ConvertUTF8ToWide(line.substr(tab + 1));

            if (!record.ParentInstanceId.empty())
            {
                records.push_back(std::move(record));
            }
        }

        if (file.bad())
        {
            return std::unexpected(
                nefarius::utilities::Win32Error(ERROR_READ_FAULT, "I/O error while reading state file"));
        }

        return records;
    }

    void DetachAffectedDevicesForService(const argh::parser& cmdl, const std::string& serviceName)
    {
        if (!cmdl[{"--attempt-detach-affected"}])
        {
            return;
        }

        el::Logger* logger = el::Loggers::getLogger("default");

        const auto instances = nefarius::devcon::ListDeviceInstancesByService(
            nefarius::utilities::ConvertAnsiToWide(serviceName));

        if (!instances)
        {
            logger->warn("Failed to enumerate devices bound to service \"%v\", error: %v", serviceName,
                         instances.error().getErrorMessageA());
            return;
        }

        if (instances->empty())
        {
            logger->info("No present devices found bound to service \"%v\"", serviceName);
            return;
        }

        const int detachTimeoutMs = ParseTimeoutMs(cmdl, "--restart-timeout", 10000);
        std::vector<DetachedDeviceRecord> detached;

        for (const auto& instanceId : instances.value())
        {
            const auto result = nefarius::devcon::DetachDeviceInstance(
                instanceId, std::chrono::milliseconds(detachTimeoutMs));

            const std::wstring displayName = result.FriendlyName.empty() ? result.InstanceId : result.FriendlyName;
            const std::string displayNameA = nefarius::utilities::ConvertWideToANSI(displayName);

            if (result.Succeeded)
            {
                logger->info("Detached device \"%v\"", displayNameA);
                detached.push_back({result.InstanceId, result.ParentInstanceId});
            }
            else if (result.TimedOut)
            {
                logger->warn(
                    "Timed out attempting to detach device \"%v\", a reboot may be required to complete this operation",
                    displayNameA);
            }
            else if (!result.VetoName.empty())
            {
                logger->warn(
                    "Could not detach device \"%v\", blocked by \"%v\", a reboot may be required to complete this operation",
                    displayNameA, nefarius::utilities::ConvertWideToANSI(result.VetoName));
            }
            else
            {
                logger->warn("Could not detach device \"%v\", a reboot may be required to complete this operation",
                             displayNameA);
            }
        }

        if (detached.empty())
        {
            logger->warn("No devices could be detached; a reboot may be required to complete this operation");
            return;
        }

        std::string stateFilePath;

        if (!(cmdl({"--state-file"}) >> stateFilePath))
        {
            const auto defaultPath = GetDefaultDetachStateFilePath(serviceName);

            if (!defaultPath)
            {
                logger->error(
                    "Failed to determine default state file location; detached device(s) cannot be re-enumerated automatically");
                return;
            }

            stateFilePath = *defaultPath;
        }

        if (const auto written = WriteDetachStateFile(stateFilePath, detached); !written)
        {
            logger->error("Failed to write detach state file \"%v\", error: %v", stateFilePath,
                         written.error().getErrorMessageA());
            return;
        }

        logger->info(
            "Detached %v device(s); run --reenumerate-affected --service-name %v after replacing the driver to bring them back",
            detached.size(), serviceName);
    }

    /**
     * @brief Retrieves the filesystem path of the current executable image.
     *
     * Returns the absolute path including the executable filename for the running module.
     *
     * @return std::string Absolute path to the current executable image; an empty string if the path cannot be determined.
     */
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

    /**
     * @brief Checks whether a device with the given hardware ID exists on the system.
     *
     * Searches for devices matching the provided hardware identifier and reports if any match was found.
     *
     * @param hwId ASCII hardware identifier to search for.
     * @param[out] errorCode Receives a platform-specific error code when the search fails; unchanged on success.
     * @return DeviceExistsResult
     *         - DeviceExistsResult::Found if a matching device was found.
     *         - DeviceExistsResult::NotFound if no matching device was found.
     *         - DeviceExistsResult::Error if the search failed (in which case `errorCode` is set).
     */
    DeviceExistsResult DeviceExists(const std::string& hwId, int& errorCode)
    {
        el::Logger* logger = el::Loggers::getLogger("default");

        const auto hwIdWide = nefarius::utilities::ConvertAnsiToWide(hwId);
        const auto findResult = nefarius::devcon::FindByHwId(hwIdWide);

        if (!findResult)
        {
            logger->error("Failed to search for existing devices, error: %v", findResult.error().getErrorMessageA());
            errorCode = findResult.error().getErrorCode();
            return DeviceExistsResult::Error;
        }

        for (const auto& [HardwareIds, Name, Version] : findResult.value())
        {
            for (const auto& id : HardwareIds)
            {
                if (_wcsicmp(id.c_str(), hwIdWide.c_str()) == 0)
                    return DeviceExistsResult::Found;
            }
        }

        return DeviceExistsResult::NotFound;
    }

    /**
     * @brief Checks whether the --no-duplicates option is set and, if so, determines if a device with the given hardware ID already exists.
     *
     * When --no-duplicates is not present, the function reports that no existing device was found.
     *
     * @param cmdl Parsed command-line arguments.
     * @param hwId Hardware identifier to search for.
     * @param errorCode Receives a platform-specific error code if the existence check fails.
     * @return DeviceExistsResult `Found` if a matching device exists, `NotFound` if none was found, or `Error` if the check failed.
     */
    DeviceExistsResult CheckNoDuplicates(const argh::parser& cmdl, const std::string& hwId, int& errorCode)
    {
        if (!cmdl[{"--no-duplicates"}])
            return DeviceExistsResult::NotFound;

        const auto exists = DeviceExists(hwId, errorCode);

        if (exists == DeviceExistsResult::Found)
        {
            el::Logger* logger = el::Loggers::getLogger("default");
            logger->info("Device with hardware ID \"%v\" already exists, skipping node creation", hwId);
        }

        return exists;
    }

    /**
     * @brief Removes all but the first device node matching the given hardware ID within a specific device class.
     *
     * Enumerates present devices of the specified class, collects those with an exact (case-insensitive) hardware ID
     * match, then removes every match after the first one via DIF_REMOVE. The driver package in the store is left intact.
     *
     * @param classGuid Pointer to the device class GUID to scope the enumeration.
     * @param hwId ASCII hardware identifier to match against.
     * @return RemoveDuplicatesResult with counts of removed/failed nodes and whether a reboot is required.
     */
    RemoveDuplicatesResult RemoveDuplicateDeviceNodes(const GUID* classGuid, const std::string& hwId)
    {
        el::Logger* logger = el::Loggers::getLogger("default");

        RemoveDuplicatesResult result = {0, 0, false};

        const std::wstring hardwareId = nefarius::utilities::ConvertAnsiToWide(hwId);

        const HDEVINFO hDevInfo = SetupDiGetClassDevs(classGuid, nullptr, nullptr, DIGCF_PRESENT);

        if (hDevInfo == INVALID_HANDLE_VALUE)
        {
            logger->error("Failed to enumerate devices, error: %v",
                          nefarius::utilities::Win32Error("SetupDiGetClassDevs").getErrorMessageA());
            result.Failed = 1;
            return result;
        }

        nefarius::utilities::guards::HDEVINFOHandleGuard hDevInfoGuard(hDevInfo);

        SP_DEVINFO_DATA devInfoData = {};
        devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        struct MatchedDevice
        {
            SP_DEVINFO_DATA DevInfoData;
            std::wstring InstanceId;
        };

        std::vector<MatchedDevice> matches;
        DWORD deviceIndex = 0;

        while (SetupDiEnumDeviceInfo(hDevInfo, deviceIndex++, &devInfoData))
        {
            DWORD requiredSize = 0;
            SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData, SPDRP_HARDWAREID, nullptr, nullptr, 0,
                                              &requiredSize);

            if (requiredSize == 0)
                continue;

            std::vector<BYTE> buffer(requiredSize);
            if (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfoData, SPDRP_HARDWAREID, nullptr, buffer.data(),
                                                   requiredSize, nullptr))
                continue;

            bool matched = false;
            for (auto pCurrent = reinterpret_cast<LPCWSTR>(buffer.data()); *pCurrent != L'\0';
                 pCurrent += wcslen(pCurrent) + 1)
            {
                if (_wcsicmp(pCurrent, hardwareId.c_str()) == 0)
                {
                    matched = true;
                    break;
                }
            }

            if (!matched)
                continue;

            WCHAR instanceId[MAX_DEVICE_ID_LEN] = {};
            SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfoData, instanceId, MAX_DEVICE_ID_LEN, nullptr);

            matches.push_back({devInfoData, std::wstring(instanceId)});
        }

        if (matches.size() <= 1)
        {
            logger->verbose(1, "No duplicate device nodes to remove for hardware ID \"%v\"", hwId);
            return result;
        }

        logger->info("Found %v device node(s) matching hardware ID \"%v\", removing %v duplicate(s)",
                     static_cast<DWORD>(matches.size()), hwId, static_cast<DWORD>(matches.size() - 1));

        for (size_t i = 1; i < matches.size(); i++)
        {
            SP_REMOVEDEVICE_PARAMS rmdParams = {};
            rmdParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
            rmdParams.ClassInstallHeader.InstallFunction = DIF_REMOVE;
            rmdParams.Scope = DI_REMOVEDEVICE_GLOBAL;
            rmdParams.HwProfile = 0;

            if (!SetupDiSetClassInstallParams(hDevInfo, &matches[i].DevInfoData, &rmdParams.ClassInstallHeader,
                                              sizeof(rmdParams)) ||
                !SetupDiCallClassInstaller(DIF_REMOVE, hDevInfo, &matches[i].DevInfoData))
            {
                logger->error("Failed to remove duplicate device %v, error: %v",
                              matches[i].InstanceId,
                              nefarius::utilities::Win32Error("SetupDiCallClassInstaller").getErrorMessageA());
                result.Failed++;
                continue;
            }

            SP_DEVINSTALL_PARAMS devParams = {};
            devParams.cbSize = sizeof(SP_DEVINSTALL_PARAMS);
            if (SetupDiGetDeviceInstallParams(hDevInfo, &matches[i].DevInfoData, &devParams) &&
                (devParams.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT)))
            {
                logger->info("Removed duplicate: %v (reboot required)", matches[i].InstanceId);
                result.RebootRequired = true;
            }
            else
            {
                logger->info("Removed duplicate: %v", matches[i].InstanceId);
            }

            result.Removed++;
        }

        return result;
    }

#if !defined(NEFCON_WINMAIN)
    /**
     * @brief Configure EasyLogging++ to use a colored console output backend.
     *
     * Installs and enables a custom console log-dispatch callback that emits
     * colorized output, disables the default standard-output logging, and
     * enables immediate flush for the default logger.
     */
    void CustomizeEasyLoggingColoredConsole()
    {
        el::Configurations conf;

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
