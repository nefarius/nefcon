#pragma once

//
// Windows
// 
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shlwapi.h>
#include <SetupAPI.h>
#include <newdev.h>
#include <winioctl.h>
#include <rpc.h>
#include <shellapi.h>
#include <io.h>

//
// Device class interfaces
// 
#include <initguid.h>
#include <devguid.h>

//
// STL
// 
#include <iostream>
#include <algorithm>
#include <vector>
#include <string>
#include <optional>

//
// Add some colors to console
// 
#include "colorwin.hpp"

//
// Vcpkg Packages
// 
#include <argh.h>
#include <easylogging++.h>

//
// Internal
// 
#include "Devcon.h"
#include "NefConSetup.h"
#include "ColorLogging.hpp"

#include <nefarius/neflib/UniUtil.hpp>
#include <nefarius/neflib/MultiStringArray.hpp>
#include <nefarius/neflib/ClassFilter.hpp>
#include <nefarius/neflib/Win32Error.hpp>
#include <nefarius/neflib/HDEVINFOHandleGuard.hpp>
#include <nefarius/neflib/HKEYHandleGuard.hpp>
#include <nefarius/neflib/INFHandleGuard.hpp>
#include <nefarius/neflib/LibraryHelper.hpp>
#include <nefarius/neflib/Devcon.hpp>
