#include "NefConSetup.h"

BOOL winapi::AdjustProcessPrivileges()
{
	HANDLE procToken;
	LUID luid;
	TOKEN_PRIVILEGES tp;
	BOOL bRetVal;
	DWORD err;

	bRetVal = OpenProcessToken(
		GetCurrentProcess(),
		TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
		&procToken
	);

	if (!bRetVal)
	{
		err = GetLastError();
		goto exit;
	}

	bRetVal = LookupPrivilegeValue(nullptr, SE_LOAD_DRIVER_NAME, &luid);
	if (!bRetVal)
	{
		err = GetLastError();
		goto exit1;
	}

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	//
	// AdjustTokenPrivileges can succeed even when privileges are not adjusted.
	// In such case GetLastError returns ERROR_NOT_ALL_ASSIGNED.
	//
	// Hence we check for GetLastError in both success and failure case.
	//

	(void)AdjustTokenPrivileges(
		procToken,
		FALSE,
		&tp,
		sizeof(TOKEN_PRIVILEGES),
		(PTOKEN_PRIVILEGES)nullptr,
		(PDWORD)nullptr
	);
	err = GetLastError();

	if (err != ERROR_SUCCESS)
	{
		bRetVal = FALSE;
		goto exit1;
	}

exit1:
	CloseHandle(procToken);
exit:
	return bRetVal;
}

BOOL winapi::CreateDriverService(PCSTR ServiceName, PCSTR DisplayName, PCSTR BinaryPath)
{
	SC_HANDLE hSCManager;
	SC_HANDLE hService;

	hSCManager = OpenSCManagerA(
		nullptr,
		nullptr,
		SC_MANAGER_CREATE_SERVICE
	);

	if (!hSCManager) {
		return FALSE;
	}

	hService = CreateServiceA(
		hSCManager,
		ServiceName,
		DisplayName,
		SERVICE_START | DELETE | SERVICE_STOP,
		SERVICE_KERNEL_DRIVER,
		SERVICE_DEMAND_START,
		SERVICE_ERROR_IGNORE,
		BinaryPath,
		nullptr,
		nullptr,
		nullptr,
		nullptr,
		nullptr
	);

	if (!hService)
	{
		CloseServiceHandle(hSCManager);
		return FALSE;
	}

	CloseServiceHandle(hService);
	CloseServiceHandle(hSCManager);

	return TRUE;
}

BOOL winapi::DeleteDriverService(PCSTR ServiceName)
{
	SC_HANDLE hSCManager;
	SC_HANDLE hService;
	BOOL ret;

	hSCManager = OpenSCManagerA(
		nullptr,
		nullptr,
		SC_MANAGER_CREATE_SERVICE
	);

	if (!hSCManager) {
		return FALSE;
	}

	hService = OpenServiceA(
		hSCManager,
		ServiceName,
		SERVICE_START | DELETE | SERVICE_STOP
	);

	if (!hService) {
		CloseServiceHandle(hSCManager);
		return FALSE;
	}

	ret = DeleteService(hService);

	CloseServiceHandle(hService);
	CloseServiceHandle(hSCManager);

	return ret;
}

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
			auto lpMsgStr = (LPCSTR)lpMsgBuf;
			std::string result(lpMsgStr, lpMsgStr + bufLen);

			LocalFree(lpMsgBuf);

			return result;
		}
	}
	return std::string("LEER");
}

std::string winapi::GetVersionFromFile(std::string FilePath)
{
	DWORD  verHandle = 0;
	UINT   size = 0;
	LPBYTE lpBuffer = nullptr;
	DWORD  verSize = GetFileVersionInfoSizeA(FilePath.c_str(), &verHandle);
	std::stringstream versionString;

	if (verSize != NULL)
	{
		auto verData = new char[verSize];

		if (GetFileVersionInfoA(FilePath.c_str(), verHandle, verSize, verData))
		{
			if (VerQueryValueA(verData, "\\", (VOID FAR * FAR*) & lpBuffer, &size))
			{
				if (size)
				{
					auto* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;
					if (verInfo->dwSignature == 0xfeef04bd)
					{
						versionString
							<< static_cast<ULONG>(HIWORD(verInfo->dwProductVersionMS)) << "."
							<< static_cast<ULONG>(LOWORD(verInfo->dwProductVersionMS)) << "."
							<< static_cast<ULONG>(HIWORD(verInfo->dwProductVersionLS));
					}
				}
			}
		}
		delete[] verData;
	}

	return versionString.str();
}

std::string winapi::GetImageBasePath()
{
	char myPath[MAX_PATH + 1] = { 0 };

	GetModuleFileNameA(
		reinterpret_cast<HINSTANCE>(&__ImageBase),
		myPath,
		MAX_PATH + 1
	);

	return std::string(myPath);
}

DWORD winapi::IsAppRunningAsAdminMode(PBOOL IsAdmin)
{
	DWORD dwError = ERROR_SUCCESS;
	PSID pAdministratorsGroup = NULL;

	// Allocate and initialize a SID of the administrators group.
	SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
	if (!AllocateAndInitializeSid(
		&NtAuthority,
		2,
		SECURITY_BUILTIN_DOMAIN_RID,
		DOMAIN_ALIAS_RID_ADMINS,
		0, 0, 0, 0, 0, 0,
		&pAdministratorsGroup))
	{
		dwError = GetLastError();
		goto Cleanup;
	}

	// Determine whether the SID of administrators group is enabled in 
	// the primary access token of the process.
	if (!CheckTokenMembership(NULL, pAdministratorsGroup, IsAdmin))
	{
		dwError = GetLastError();
		goto Cleanup;
	}

Cleanup:
	// Centralized cleanup for all allocated resources.
	if (pAdministratorsGroup)
	{
		FreeSid(pAdministratorsGroup);
		pAdministratorsGroup = NULL;
	}

	return dwError;
}

BOOL winapi::GetLogonSID(HANDLE hToken, PSID* ppsid)
{
	BOOL bSuccess = FALSE;
	DWORD dwLength = 0;
	PTOKEN_GROUPS ptg = nullptr;

	// Get required buffer size and allocate the TOKEN_GROUPS buffer.
	GetTokenInformation(hToken, TokenGroups, ptg, 0, &dwLength);

	ptg = static_cast<PTOKEN_GROUPS>(HeapAlloc(
		GetProcessHeap(),
		HEAP_ZERO_MEMORY,
		dwLength
	));

	// Get the token group information from the access token.
	GetTokenInformation(hToken, TokenGroups, ptg, dwLength, &dwLength);

	// Loop through the groups to find the logon SID.
	for (DWORD dwIndex = 0; dwIndex < ptg->GroupCount; dwIndex++)
	{
		if ((ptg->Groups[dwIndex].Attributes & SE_GROUP_LOGON_ID)
			== SE_GROUP_LOGON_ID)
		{
			// Found the logon SID; make a copy of it.

			dwLength = GetLengthSid(ptg->Groups[dwIndex].Sid);
			*ppsid = HeapAlloc(GetProcessHeap(),
				HEAP_ZERO_MEMORY, dwLength);
			CopySid(dwLength, *ppsid, ptg->Groups[dwIndex].Sid);

			break;
		}
	}

	return TRUE;
}

BOOL winapi::TakeFileOwnership(el::Logger* logger, LPCWSTR file)
{
	HANDLE token;
	DWORD len;
	PSECURITY_DESCRIPTOR security = nullptr;
	PSID sid = nullptr;
	BOOL ret = FALSE;

	do
	{
		// Get the privileges you need
		if (OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &token))
		{
			if (!SetPrivilege(L"SeTakeOwnershipPrivilege", 1))
			{
				logger->error("SeTakeOwnershipPrivilege failed, error: %v",
					GetLastErrorStdStr());
				break;
			}
			if (!SetPrivilege(L"SeSecurityPrivilege", 1))
			{
				logger->error("SeSecurityPrivilege failed, error: %v",
					GetLastErrorStdStr());
				break;
			}
			if (!SetPrivilege(L"SeBackupPrivilege", 1))
			{
				logger->error("SeBackupPrivilege failed, error: %v",
					GetLastErrorStdStr());
				break;
			}
			if (!SetPrivilege(L"SeRestorePrivilege", 1))
			{
				logger->error("SeRestorePrivilege failed, error: %v",
					GetLastErrorStdStr());
				break;
			}
		}
		else
		{
			logger->error("OpenProcessToken failed, error: %v",
				GetLastErrorStdStr());
			break;
		}

		// Create the security descriptor
		GetFileSecurity(file, OWNER_SECURITY_INFORMATION, security, 0, &len);

		security = malloc(len);

		if (!InitializeSecurityDescriptor(security, SECURITY_DESCRIPTOR_REVISION))
		{
			logger->error("InitializeSecurityDescriptor failed, error: %v",
				GetLastErrorStdStr());
			break;
		}

		// Get the sid for the username
		if (!GetLogonSID(token, &sid))
		{
			logger->error("GetLogonSID failed, error: %v",
				GetLastErrorStdStr());
			break;
		}

		// Set the sid to be the new owner
		if (sid && !SetSecurityDescriptorOwner(security, sid, 0))
		{
			logger->error("SetSecurityDescriptorOwner failed, error: %v",
				GetLastErrorStdStr());
			break;
		}

		// Save the security descriptor
		if (!SetFileSecurity(file, OWNER_SECURITY_INFORMATION, security))
		{
			logger->error("SetFileSecurity failed, error: %v",
				GetLastErrorStdStr());
			break;
		}

		ret = TRUE;
	} while (FALSE);

	if (security)
		free(security);

	return ret;
}

BOOL winapi::SetPrivilege(LPCWSTR privilege, int enable)
{
	TOKEN_PRIVILEGES tp;
	LUID luid;
	HANDLE token;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token)) return FALSE;
	if (!LookupPrivilegeValue(nullptr, privilege, &luid)) return FALSE;

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : FALSE;

	// Enable the privilege or disable all privileges.
	return AdjustTokenPrivileges(token, 0, &tp, NULL, nullptr, nullptr);
}
