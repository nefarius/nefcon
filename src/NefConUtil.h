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

//
// CLI argument parser
// 
#include <argh.h>

//
// Add some colors to console
// 
#include "colorwin.hpp"

//
// Logging
// 
#include "easylogging++.h"

//
// Setup helpers
// 
#include "Devcon.h"
#include "NefConSetup.h"
#include "UniUtil.h"
