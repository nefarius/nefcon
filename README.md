# <img src="assets/NSS-128x128.png" align="left" />nefcon

[![Build status](https://ci.appveyor.com/api/projects/status/11y1mbdw4fk8ir9c/branch/master?svg=true)](https://ci.appveyor.com/project/nefarius/nefcon/branch/master)
[![GitHub All Releases](https://img.shields.io/github/downloads/nefarius/nefcon/total)](https://somsubhra.github.io/github-release-stats/?username=nefarius&repository=nefcon)
[![Discord](https://img.shields.io/discord/346756263763378176.svg)](https://discord.nefarius.at)
[![GitHub followers](https://img.shields.io/github/followers/nefarius.svg?style=social&label=Follow)](https://github.com/nefarius)
[![Mastodon Follow](https://img.shields.io/mastodon/follow/109321120351128938?domain=https%3A%2F%2Ffosstodon.org%2F&style=social)](https://fosstodon.org/@Nefarius)

Windows device driver installation and management tool.

## About

This little self-contained, no-dependency tool can be built either as a console application or a Windows application which has no visible window (ideal to use in combination with setup makers). It offers a command-line-based driver (un-)installer and allows for simple manipulation of class filter entries. Run `nefconc.exe --help` to see all the options offered.

## Motivation

Windows Device Driver management is and always has been hard. The APIs involved are old, moody and come with pitfalls. Historically the [`devcon`](https://github.com/microsoft/Windows-driver-samples/tree/b3af8c8f9bd508f54075da2f2516b31d05cd52c8/setup/devcon) tool or nowadays `pnputil` have been used to offload these tedious tasks, but unintuitive and sparsely documented command line arguments and error propagation make them poor candidates for automation in e.g. setup engines. Having grown tired of these limitations I made this "devcon clone" available under a permissive license which offers the following highlighted features and more:

- Allows for true window-less execution
- Actively suppresses and works around user interaction inconsistencies ("reboot required" dialogs and OS-included bugs)
- Offers optional logging to `stdout` or file
- *Sane* command line arguments 😁
- Manipulation of [class filter](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/filter-drivers) entries
- Supports installation of [primitive drivers](https://learn.microsoft.com/en-us/windows-hardware/drivers/develop/creating-a-primitive-driver)

## How to build

### Prerequisites

- Visual Studio 2022 (Community Edition is free)
- Windows SDK

### Fresh clone / first build

Dependencies must be installed before the first build from Visual Studio:

1. Clone the repository and initialize submodules: `git submodule update --init --recursive` (ensures the vcpkg submodule is populated)
2. Open **Developer Command Prompt for VS 2022** (or x64 Native Tools for x64/ARM64, x86 Native Tools for Win32)
3. Run `prepare-deps.bat` from the repo root (installs all platforms) or `prepare-deps.bat x64` for x64 only
4. Build the solution in Visual Studio

Dependencies (argh, detours, easyloggingpp, neflib, etc.) are declared in `vcpkg.json` and installed via vcpkg (included as a submodule). The build will use existing `vcpkg_installed` if present.

### Local development against neflib

The core device/driver management logic lives in [neflib](https://github.com/nefarius/neflib), consumed as a normal vcpkg package pinned in `vcpkg-configuration.json` via the [nefarius-vcpkg-registry](https://github.com/nefarius/nefarius-vcpkg-registry). For convenience, neflib is also checked out as a git submodule at `neflib/`, so both repos can be edited from a single checkout.

To build against your local neflib checkout instead of the published package:

1. `git submodule update --init neflib`
2. `set NEFCON_LOCAL_NEFLIB=1` before running `prepare-deps.bat` or building from Visual Studio (this activates the `ports/neflib` overlay port, which builds `neflib/src/neflib.vcxproj` in place)
3. After editing neflib sources, run `.\sync-local-neflib.ps1` (from PowerShell; from a Developer Command Prompt use `powershell -File .\sync-local-neflib.ps1`) to stamp `ports/neflib/portfile.cmake` with a fresh hash of the neflib sources — vcpkg's cache is keyed off the port files, not `neflib/`'s contents, so without this step edits would not trigger a rebuild

Unset `NEFCON_LOCAL_NEFLIB` (and re-run `prepare-deps.bat`) to go back to the published package. Note that the submodule's pinned commit and the registry's baseline in `vcpkg-configuration.json` are independent pins that can drift; the registry baseline is what CI and release builds actually use, the submodule is purely a development convenience.

## Installation

Binaries are available to download in the [releases](https://github.com/nefarius/nefcon/releases/latest) page, just download and extract. However, if you are using a package manager, you can use one of the following options:

### Scoop

> [!IMPORTANT]  
> This is a community-maintained source and might lag behind GitHub releases.

[`nefcon`](https://scoop.sh/#/apps?q=nefcon&s=0&d=1&o=true) is available in the [Extras](https://github.com/ScoopInstaller/Extras) bucket:

```text
scoop bucket add extras
scoop install nefcon
```

### Winget

> [!IMPORTANT]  
> This is a community-maintained source and might lag behind GitHub releases.

[`nefcon`](https://github.com/microsoft/winget-pkgs/tree/master/manifests/n/Nefarius/nefcon) is available in the [winget-pkgs](https://github.com/microsoft/winget-pkgs) repository:

```text
winget install nefcon
```

## Command Reference

All commands require **Administrator** privileges unless noted. Paths may be absolute or relative to the current working directory. GUID format: `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx` (brackets optional). Check exit code `ERROR_SUCCESS_REBOOT_REQUIRED` (3010) when a reboot is needed.

| Command | Description |
|---------|-------------|
| `--install-driver` | Primitive driver install (Win10 1903+); uses `DiInstallDriverW` |
| `--uninstall-driver` | Primitive driver uninstall |
| `--inf-default-install` | Legacy INF with `[DefaultInstall]` (e.g. Btrfs, volume controllers) |
| `--inf-default-uninstall` | Legacy INF with `[DefaultUninstall]` section |
| `--create-device-node` | Create ROOT-enumerated virtual device |
| `--remove-device-node` | Remove all matching devices and driver; cleans driver store when unused |
| `--add-class-filter` | Add upper/lower filter to device class |
| `--remove-class-filter` | Remove upper/lower filter |
| `--create-driver-service` | Create kernel driver service |
| `--remove-driver-service` | Delete kernel driver service |
| `--reenumerate-affected` | Bring back devices detached via `--remove-driver-service --attempt-detach-affected` |
| `--delete-file-on-reboot` | Schedule file deletion on next reboot |
| `--find-hwid` | Search devices by partial hardware ID (no admin) |
| `--enable-bluetooth-service` | Enable local Bluetooth service |
| `--disable-bluetooth-service` | Disable local Bluetooth service |
| `remove [HardwareID]` | devcon-compatible device removal (device only, driver stays in store) |
| `-v, --version` | Display version |

### Driver installation

**`--install-driver`** — Installs a [primitive driver](https://learn.microsoft.com/en-us/windows-hardware/drivers/develop/creating-a-primitive-driver) via `DiInstallDriverW`. Use for INF-based software packages targeting Windows 10 1903+ that are not tied to hardware.

- **Required:** `--inf-path` (path to INF, absolute or relative to CWD)
- **Pitfalls:** INF must exist; reboot may be required (check exit code)
- **When to use:** Primitive drivers, DCH-compliant packages

**`--uninstall-driver`** — Uninstalls a primitive driver via `DiUninstallDriverW`.

- **Required:** `--inf-path`
- **Pitfalls:** Same as `--install-driver`

**`--inf-default-install`** — Installs an INF with `[DefaultInstall]` via `InstallHInfSection`. Use for legacy INFs (e.g. file system drivers like Btrfs).

- **Required:** `--inf-path`
- **Optional:** `--attempt-restart-affected` — attempt to restart devices affected by any class filter changes the INF declares, best-effort (a reboot may still be required for devices that could not be restarted); add `--class-guid` to also restart devices of an extra class not declared in the INF, and `--restart-timeout` to change the per-device timeout in milliseconds (default `10000`)
- **When to use:** Legacy INFs with `[DefaultInstall]`; not for primitive drivers

**`--inf-default-uninstall`** — Uninstalls an INF with `[DefaultUninstall]` section.

- **Required:** `--inf-path`
- **Optional:** `--attempt-restart-affected`, `--class-guid`, `--restart-timeout` — same semantics as `--inf-default-install`

### Device node management

**`--create-device-node`** — Creates a ROOT-enumerated virtual device.

- **Required:** `--hardware-id`, `--class-name`, `--class-guid`
- **Optional:** `--no-duplicates` — skips creation if a device with the same hardware ID already exists (returns success). Recommended for upgrade paths to avoid duplicate device instances.
- **When to use:** Software-enumerated devices (e.g. HidHide, virtual HID)

**`--remove-device-node`** — Removes all devices matching hardware ID and class GUID, plus the driver from the driver store when no device uses it anymore. Also removes matching devices that currently have no driver loaded.

- **Required:** `--hardware-id`, `--class-guid`
- **Behavior:** One run removes all matching devices (not just a single occurrence); removes the driver copy from the driver store if no remaining device uses it
- **Pitfalls:** Reboot may be required

### Class filter manipulation

**`--add-class-filter`** — Adds a service to a device class upper or lower filter list.

- **Required:** `--position` (`upper` or `lower`), `--service-name`, `--class-guid`
- **Optional:** `--attempt-restart-affected` — attempt to restart present devices of `--class-guid`, best-effort (a reboot may still be required for devices that could not be restarted); `--restart-timeout` sets the per-device timeout in milliseconds (default `10000`)
- **Pitfalls:** Without `--attempt-restart-affected`, reconnect affected devices or reboot to apply
- **When to use:** Filter drivers (e.g. HidHide on HIDClass)

**`--remove-class-filter`** — Removes a service from the filter list.

- **Required:** Same as `--add-class-filter`
- **Optional:** Same as `--add-class-filter`

### Driver service management

**`--create-driver-service`** — Creates a kernel driver service.

- **Required:** `--bin-path` (path to .sys), `--service-name`, `--display-name`
- **Pitfalls:** Binary must exist; does not start the service

**`--remove-driver-service`** — Stops (waiting for `SERVICE_STOPPED`) and deletes a kernel driver service.

- **Required:** `--service-name`
- **Optional:** `--stop-timeout` — milliseconds to wait for the service to stop before giving up, default `10000`
- **Optional:** `--attempt-detach-affected` — before stopping/deleting the service, best-effort detach every present device currently bound to it (releasing the driver's file locks so the .sys can be safely replaced/deleted), best-effort (a reboot may still be required for devices that could not be detached); successfully detached devices are recorded to a state file for a later `--reenumerate-affected` call. `--restart-timeout` sets the per-device detach timeout in milliseconds (default `10000`); `--state-file` overrides where the state is written (default a per-service-name file under `%TEMP%\nefconc`)
- **Pitfalls:** Stopping the service does not by itself release devnodes still bound to it; use `--attempt-detach-affected` if you intend to replace or delete the driver's files afterwards. Some drivers never advertise support for being stopped live (no unload routine) — the service is still marked for deletion in that case, but a reboot is required to fully remove it (reflected in the exit code)
- **When to use:** Live driver upgrades/removal — pair with `--reenumerate-affected` after replacing the driver files

**`--reenumerate-affected`** — Re-enumerates the parent devnodes of devices previously detached via `--remove-driver-service --attempt-detach-affected`, making Windows re-discover and re-bind a driver to them (e.g. after replacing the `.sys` file). Single-use: the state file is deleted after processing, whether or not every devnode could be re-enumerated.

- **Required:** `--service-name` — must match the one used with `--attempt-detach-affected`
- **Optional:** `--restart-timeout` — milliseconds to wait per devnode re-enumeration attempt, default `10000`; `--state-file` overrides where the state is read from (default matches `--remove-driver-service`'s default)
- **Pitfalls:** If the state file is missing (e.g. `--attempt-detach-affected` detached nothing), the command fails; a devnode that could not be re-enumerated may require a reboot
- **When to use:** After replacing/upgrading a driver's files following a `--remove-driver-service --attempt-detach-affected` detach

### Utilities

**`--delete-file-on-reboot`** — Marks a file for deletion on next reboot. May take ownership if access denied.

- **Required:** `--file-path`
- **When to use:** Cleaning up driver files, locked files

**`--find-hwid`** — Searches for devices by partial hardware ID. Does not require admin.

- **Required:** `--hardware-id` (partial match)
- **Exit codes:** `ERROR_NOT_FOUND` if no match
- **When to use:** Discovering device hardware IDs before install/remove

**`--enable-bluetooth-service`** / **`--disable-bluetooth-service`** — Toggle a local Bluetooth service.

- **Required:** `--service-name`, `--service-guid`

### Logging

- `--default-log-file=.\log.txt` — Write execution details to file
- `--verbose` — Enable diagnostic logging

### devcon compatibility

**`install [INFFile] [HardwareID]`** — Drop-in for [`devcon install`](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/devcon-install). Creates ROOT-enumerated device and installs driver. The `/r` flag is not supported; check exit code for reboot requirement.

- **Optional:** `--no-duplicates` — skips device node creation if a device with the same hardware ID already exists; still updates the driver. Ideal for upgrade/reinstall scenarios.
- **Optional:** `--remove-duplicates` — when used together with `--no-duplicates`, removes all but one matching device node before the driver update. Solves the common problem of multiple device nodes with the same hardware ID accumulating due to past setup failures or script reruns. Has no effect without `--no-duplicates` (a warning is logged).

**`remove [HardwareID]`** — Drop-in for [`devcon remove`](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/devcon-remove). Removes all present devices whose hardware ID matches (case-insensitive). The behavior of `devcon remove` has been intentionally replicated 1:1 so that existing scripts and setup tools relying on this semantic continue to work without modification.

- **No class GUID required** — enumerates across all device classes automatically.
- **Important:** Unlike `--remove-device-node`, this command **only removes the device node** via `DIF_REMOVE`. The driver package remains in the driver store. Use `--remove-device-node` when you also want to clean the driver from the store.

---

## Examples

Use `nefconc` for console output, `nefconw` for windowless execution (e.g. in setup makers). Run `nefconc.exe --help` for all options.

### Primitive / PnP driver installation

```text
nefconw --install-driver --inf-path "Path\To\Inf.inf"
nefconw --uninstall-driver --inf-path "Path\To\Inf.inf"
```

### Legacy INF installation

Use `--inf-default-install` for INFs with `[DefaultInstall]` (e.g. file system drivers). Use `--install-driver` for primitive drivers (Win10 1903+).

```text
nefconw --inf-default-install --inf-path "F:\Downloads\btrfs-1.8\btrfs.inf"
nefconw --inf-default-uninstall --inf-path "F:\Downloads\btrfs-1.8\btrfs.inf"

# Attempt to restart devices affected by the INF's class filter changes (best-effort; a reboot may still be required)
nefconw --inf-default-install --inf-path "F:\Downloads\HidHide\HidHide.inf" --attempt-restart-affected --restart-timeout 5000
```

### Device node management

```text
nefconw --create-device-node --hardware-id root\HidHide --class-name System --class-guid 4D36E97D-E325-11CE-BFC1-08002BE10318
nefconw --remove-device-node --hardware-id root\HidHide --class-guid 4D36E97D-E325-11CE-BFC1-08002BE10318

# Upgrade-safe: only creates the node if it doesn't already exist
nefconw --create-device-node --hardware-id root\HidHide --class-name System --class-guid 4D36E97D-E325-11CE-BFC1-08002BE10318 --no-duplicates
```

### Class filter manipulation

```text
nefconw --add-class-filter --position upper --service-name HidHide --class-guid 745a17a0-74d3-11d0-b6fe-00a0c90f57da
nefconw --remove-class-filter --position upper --service-name HidHide --class-guid 745a17a0-74d3-11d0-b6fe-00a0c90f57da

# Attempt to restart present devices of the class (best-effort; a reboot may still be required)
nefconw --add-class-filter --position upper --service-name HidHide --class-guid 745a17a0-74d3-11d0-b6fe-00a0c90f57da --attempt-restart-affected
```

### Driver service management

```text
nefconw --create-driver-service --bin-path "C:\Drivers\MyDriver.sys" --service-name MyDriver --display-name "My Driver"
nefconw --remove-driver-service --service-name MyDriver

# Live driver upgrade: detach bound devices first so the .sys can be safely replaced, then bring them back.
# Check the exit code before continuing: 0 means removal succeeded; ERROR_SUCCESS_REBOOT_REQUIRED (3010)
# or any other nonzero value means stop here and reboot / resolve the error before retrying the sequence.
nefconw --remove-driver-service --service-name MyDriver --attempt-detach-affected
copy /y "C:\Drivers\MyDriver-new.sys" "C:\Drivers\MyDriver.sys"
nefconw --create-driver-service --bin-path "C:\Drivers\MyDriver.sys" --service-name MyDriver --display-name "My Driver"
nefconw --reenumerate-affected --service-name MyDriver
```

### Utilities

```text
nefconw --delete-file-on-reboot --file-path "C:\Windows\System32\drivers\olddriver.sys"
nefconc --find-hwid --hardware-id "USB\VID_1234"
nefconw --enable-bluetooth-service --service-name "My BLE Service" --service-guid 0000180a-0000-1000-8000-00805f9b34fb
nefconw --disable-bluetooth-service --service-name "My BLE Service" --service-guid 0000180a-0000-1000-8000-00805f9b34fb
```

### devcon compatibility

```text
nefconw install "Path\To\Inf.inf" "root\MyDevice"

# Upgrade-safe: skips node creation if device exists, still updates the driver
nefconw install "Path\To\Inf.inf" "root\MyDevice" --no-duplicates

# Upgrade-safe with cleanup: removes duplicate device nodes, keeps one, then updates the driver
nefconw install "Path\To\Inf.inf" "root\MyDevice" --no-duplicates --remove-duplicates

# Remove all present devices matching the hardware ID (driver stays in store)
nefconw remove "root\MyDevice"
```

## `devcon` emulation

The [`devcon install`](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/devcon-install) and [`devcon remove`](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/devcon-remove) commands are implemented as drop-in replacements. See [Command Reference](#command-reference) for details. The `/r` flag is not supported; check the exit code to determine if a reboot is required.

> **`remove` vs `--remove-device-node`:** The `remove` command intentionally mirrors original `devcon remove` behavior — it invokes `DIF_REMOVE` to delete the device node but **does not** touch the driver package in the driver store. If you need full cleanup (device *and* driver), use `--remove-device-node` with `--class-guid` instead.

## For developers

The driver and device management logic is implemented in [neflib](https://github.com/nefarius/neflib). Key modules include `Devcon.hpp` (InstallDriver, UninstallDriver, InfDefaultInstall, Create, etc.) and `ClassFilter.hpp`. For implementation details, API behavior, or to contribute fixes, see the [neflib repository](https://github.com/nefarius/neflib).

## 3rd party credits

This project uses the following 3rd party resources:

- [Argh! A minimalist argument handler](https://github.com/adishavit/argh)
- [Scoped coloring of Windows console output](https://github.com/jrebacz/colorwin)
- [Convenient high-level C++ wrappers around Windows Registry Win32 APIs](https://github.com/GiovanniDicanio/WinReg)
- [Single header C++ logging library](https://github.com/amrayn/easyloggingpp)
- [Microsoft Detours](https://github.com/microsoft/Detours)
- [A modern C++ scope guard that is easy to use but hard to misuse](https://github.com/ricab/scope_guard)
- [Windows Implementation Libraries (WIL)](https://github.com/microsoft/wil)
- [My opinionated collection of C++ utilities](https://github.com/nefarius/neflib)
- [Replacing Device Console (DevCon.exe)](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/devcon-migration)
- [PnPUtil](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil)
- [DevCon Install](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/devcon-install)
