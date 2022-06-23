#include "NefConUtil.h"

using namespace colorwin;


//
// Enable Visual Styles for message box
// 
#pragma comment(linker,"\"/manifestdependency:type='win32' \
	name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")



bool IsCurlyBracket(char c)
{
    switch(c)
    {
    case '{':
    case '}':
        return true;
    default:
        return false;
    }
}

#if defined(NEFCON_WINMAIN)
int WINAPI WinMain (
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nShowCmd
    )
#else
int main(int, char* argv[])
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
		"--bin-path"
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
		narrow.push_back(to_string(std::wstring(szArglist[i])));
	}

	argv.resize(nArgs);
	std::transform(narrow.begin(), narrow.end(), argv.begin(), [](const std::string& arg) { return arg.c_str(); });

	argv.push_back(nullptr);

	cmdl.parse(nArgs, &argv[0]);
#else
	cmdl.parse(argv);
#endif

	std::string infPath, binPath, hwId, className, classGuid, serviceName, displayName, position;

#pragma region Filter Driver actions

	if (cmdl[{"--add-class-filter"}])
	{
		if (!(cmdl({ "--position" }) >> position)) {
			std::cout << color(red) << "Position missing" << std::endl;
			return EXIT_FAILURE;
		}

		if (!(cmdl({ "--service-name" }) >> serviceName)) {
			std::cout << color(red) << "Filter Service Name missing" << std::endl;
			return EXIT_FAILURE;
		}

		if (!(cmdl({ "--class-guid" }) >> classGuid)) {
			std::cout << color(red) << "Device Class GUID missing" << std::endl;
			return EXIT_FAILURE;
		}

		classGuid.erase(std::remove_if(classGuid.begin(), classGuid.end(), &IsCurlyBracket), classGuid.end());

		GUID clID;

		if (UuidFromStringA(reinterpret_cast<RPC_CSTR>(&classGuid[0]), &clID) == RPC_S_INVALID_STRING_UUID)
		{
			std::cout << color(red) << "Device Class GUID format invalid, expected format (no brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" << std::endl;
			return EXIT_FAILURE;
		}

		devcon::DeviceClassFilterPosition::Value pos;

		if (position == "upper")
		{
			pos = devcon::DeviceClassFilterPosition::Upper;
		}
		else if (position == "lower")
		{
			pos = devcon::DeviceClassFilterPosition::Lower;
		}
		else
		{
			std::cout << color(red) << "Unsupported position received. Valid values include: upper, lower" << std::endl;
			return EXIT_FAILURE;
		}

		auto ret = devcon::add_device_class_filter(&clID, to_wstring(serviceName), pos);

		if (ret)
		{
			std::cout << color(yellow) <<
				"Filter enabled. Reconnect affected devices or reboot system to apply changes!"
				<< std::endl;

			return EXIT_SUCCESS;
		}

		std::cout << color(red) <<
			"Failed to modify filter value, error: "
			<< winapi::GetLastErrorStdStr() << std::endl;
		return GetLastError();
	}

	if (cmdl[{"--remove-class-filter"}])
	{
		if (!(cmdl({ "--position" }) >> position)) {
			std::cout << color(red) << "Position missing" << std::endl;
			return EXIT_FAILURE;
		}

		if (!(cmdl({ "--service-name" }) >> serviceName)) {
			std::cout << color(red) << "Filter Service Name missing" << std::endl;
			return EXIT_FAILURE;
		}

		if (!(cmdl({ "--class-guid" }) >> classGuid)) {
			std::cout << color(red) << "Device Class GUID missing" << std::endl;
			return EXIT_FAILURE;
		}

		GUID clID;

		if (UuidFromStringA(reinterpret_cast<RPC_CSTR>(&classGuid[0]), &clID) == RPC_S_INVALID_STRING_UUID)
		{
			std::cout << color(red) << "Device Class GUID format invalid, expected format (no brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" << std::endl;
			return EXIT_FAILURE;
		}

		devcon::DeviceClassFilterPosition::Value pos;

		if (position == "upper")
		{
			pos = devcon::DeviceClassFilterPosition::Upper;
		}
		else if (position == "lower")
		{
			pos = devcon::DeviceClassFilterPosition::Lower;
		}
		else
		{
			std::cout << color(red) << "Unsupported position received. Valid values include: upper, lower" << std::endl;
			return EXIT_FAILURE;
		}

		auto ret = devcon::remove_device_class_filter(&clID, to_wstring(serviceName), pos);

		if (ret)
		{
			std::cout << color(yellow) <<
				"Filter enabled. Reconnect affected devices or reboot system to apply changes!"
				<< std::endl;

			return EXIT_SUCCESS;
		}

		std::cout << color(red) <<
			"Failed to modify filter value, error: "
			<< winapi::GetLastErrorStdStr() << std::endl;
		return GetLastError();
	}

#pragma endregion

#pragma region Generic driver installer

	if (cmdl[{ "--install-driver" }])
	{
		infPath = cmdl({ "--inf-path" }).str();

		if (infPath.empty()) {
			std::cout << color(red) << "INF path missing" << std::endl;
			return EXIT_FAILURE;
		}

		bool rebootRequired;

		if (!devcon::install_driver(to_wstring(infPath), &rebootRequired))
		{
			std::cout << color(red) <<
				"Failed to install driver, error: "
				<< winapi::GetLastErrorStdStr() << std::endl;
			return GetLastError();
		}

		std::cout << color(green) << "Driver installed successfully" << std::endl;

		return (rebootRequired) ? ERROR_RESTART_APPLICATION : EXIT_SUCCESS;
	}

	if (cmdl[{ "--uninstall-driver" }])
	{
		infPath = cmdl({ "--inf-path" }).str();

		if (infPath.empty()) {
			std::cout << color(red) << "INF path missing" << std::endl;
			return EXIT_FAILURE;
		}

		bool rebootRequired;

		if (!devcon::uninstall_driver(to_wstring(infPath), &rebootRequired))
		{
			std::cout << color(red) <<
				"Failed to uninstall driver, error: "
				<< winapi::GetLastErrorStdStr() << std::endl;
			return GetLastError();
		}

		std::cout << color(green) << "Driver uninstalled successfully" << std::endl;

		return (rebootRequired) ? ERROR_RESTART_APPLICATION : EXIT_SUCCESS;
	}

	if (cmdl[{ "--create-driver-service" }])
	{
		binPath = cmdl({ "--bin-path" }).str();

		if (binPath.empty()) {
			std::cout << color(red) << "Binary path missing" << std::endl;
			return EXIT_FAILURE;
		}

		if (!(cmdl({ "--service-name" }) >> serviceName)) {
			std::cout << color(red) << "Service name missing" << std::endl;
			return EXIT_FAILURE;
		}

		displayName = cmdl({ "--display-name" }).str();

		if (displayName.empty()) {
			std::cout << color(red) << "Display name missing" << std::endl;
			return EXIT_FAILURE;
		}

		if (!winapi::CreateDriverService(serviceName.c_str(), displayName.c_str(), binPath.c_str()))
		{
			std::cout << color(red) <<
				"Failed to create driver service, error: "
				<< winapi::GetLastErrorStdStr() << std::endl;
			return GetLastError();
		}

		std::cout << color(green) << "Driver service created successfully" << std::endl;

		return EXIT_SUCCESS;
	}

	if (cmdl[{ "--remove-driver-service" }])
	{		
		if (!(cmdl({ "--service-name" }) >> serviceName)) {
			std::cout << color(red) << "Service name missing" << std::endl;
			return EXIT_FAILURE;
		}
		
		if (!winapi::DeleteDriverService(serviceName.c_str()))
		{
			std::cout << color(red) <<
				"Failed to remove driver service, error: "
				<< winapi::GetLastErrorStdStr() << std::endl;
			return GetLastError();
		}

		std::cout << color(green) << "Driver service created successfully" << std::endl;

		return EXIT_SUCCESS;
	}

	if (cmdl[{ "--create-device-node" }])
	{
		if (!(cmdl({ "--hardware-id" }) >> hwId)) {
			std::cout << color(red) << "Hardware ID missing" << std::endl;
			return EXIT_FAILURE;
		}

		if (!(cmdl({ "--class-name" }) >> className)) {
			std::cout << color(red) << "Device Class Name missing" << std::endl;
			return EXIT_FAILURE;
		}

		if (!(cmdl({ "--class-guid" }) >> classGuid)) {
			std::cout << color(red) << "Device Class GUID missing" << std::endl;
			return EXIT_FAILURE;
		}

		classGuid.erase(std::remove_if(classGuid.begin(), classGuid.end(), &IsCurlyBracket), classGuid.end());

		GUID clID;

		if (UuidFromStringA(reinterpret_cast<RPC_CSTR>(&classGuid[0]), &clID) == RPC_S_INVALID_STRING_UUID)
		{
			std::cout << color(red) << "Device Class GUID format invalid, expected format (no brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" << std::endl;
			return EXIT_FAILURE;
		}

		auto ret = devcon::create(to_wstring(className), &clID, to_wstring(hwId));

		if (!ret)
		{
			std::cout << color(red) <<
				"Failed to create device node, error: "
				<< winapi::GetLastErrorStdStr() << std::endl;
			return GetLastError();
		}

		std::cout << color(green) << "Device node created successfully" << std::endl;

		return EXIT_SUCCESS;
	}

	if (cmdl[{ "--remove-device-node" }])
	{
		if (!(cmdl({ "--hardware-id" }) >> hwId)) {
			std::cout << color(red) << "Hardware ID missing" << std::endl;
			return EXIT_FAILURE;
		}

		if (!(cmdl({ "--class-guid" }) >> classGuid)) {
			std::cout << color(red) << "Device Class GUID missing" << std::endl;
			return EXIT_FAILURE;
		}

		classGuid.erase(std::remove_if(classGuid.begin(), classGuid.end(), &IsCurlyBracket), classGuid.end());

		GUID clID;

		if (UuidFromStringA(reinterpret_cast<RPC_CSTR>(&classGuid[0]), &clID) == RPC_S_INVALID_STRING_UUID)
		{
			std::cout << color(red) << "Device Class GUID format invalid, expected format (no brackets): xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" << std::endl;
			return EXIT_FAILURE;
		}

		bool rebootRequired;

		auto ret = devcon::uninstall_device_and_driver(&clID, to_wstring(hwId), &rebootRequired);

		if (!ret)
		{
			std::cout << color(red) <<
				"Failed to delete device node, error: "
				<< winapi::GetLastErrorStdStr() << std::endl;
			return GetLastError();
		}

		std::cout << color(green) << "Device and driver removed successfully" << std::endl;

		return (rebootRequired) ? ERROR_RESTART_APPLICATION : EXIT_SUCCESS;
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

	std::cout << "usage: .\\nefcon [options]" << std::endl << std::endl;
	std::cout << "  options:" << std::endl;
	std::cout << "    --install-driver           Invoke the installation of a given PNP driver" << std::endl;
	std::cout << "      --inf-path               Path to the INF file to install (required)" << std::endl;
	std::cout << "    --uninstall-driver         Invoke the removal of a given PNP driver" << std::endl;
	std::cout << "      --inf-path               Path to the INF file to uninstall (required)" << std::endl;
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
	std::cout << "    -v, --version              Display version of this utility" << std::endl;
	std::cout << std::endl;

#pragma endregion

	return EXIT_FAILURE;
}
