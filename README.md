# <img src="assets/NSS-128x128.png" align="left" />nefcon

[![Build status](https://github.com/nefarius/nefcon/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/nefarius/nefcon/actions/workflows/build.yml)
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
| `--install-filter-driver` | Ordered, race-safe INF-based filter driver install against slow service startup |
| `--uninstall-filter-driver` | Ordered, race-safe filter removal; never leaves a dangling filter entry, race-safe against slow service teardown |
| `--remove-driver-store-package` | Surgically purge one or more packages from the driver store by original INF name, without touching any device node |
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

### Filter driver install/remove (ordered, race-safe)

These two commands wrap the same underlying operations as the sections above into a single ordered, race-safe sequence instead of a hand-rolled multi-command flow. This is *not* a transactional/all-or-nothing guarantee: each step still executes independently, and if a later step fails, earlier steps that already succeeded are not rolled back (see each command's Pitfalls entry for the resulting partial states and how to recover). What they do guarantee is the specific ordering and internal retries that close the race conditions and dangling-state pitfalls described below. Prefer them over the individual `--inf-default-install` / `--add-class-filter` / `--remove-class-filter` / `--remove-driver-service` sequences for filter drivers, unless you need to interleave custom steps.

**`--install-filter-driver`** — Installs an INF-based filter driver (equivalent to `--inf-default-install --attempt-restart-affected`), then waits for every filter service the INF declares to settle into a state the caller can trust before returning, instead of the caller having to probe it immediately afterwards.

- **Required:** `--inf-path`
- **Optional:** `--class-guid` — additional Device Class GUID to restart, on top of what the INF declares (this only extends the restart list; it does not add extra services to the settle/health check below); `--restart-timeout` — milliseconds to wait per device restart attempt, default `10000`; `--health-timeout` — milliseconds to wait for each declared filter service to reach `SERVICE_RUNNING` when a device of its class is present, default `10000`
- **Behavior:** A filter service is only expected to reach `SERVICE_RUNNING` if a device of its class is currently present. If no device of that class is present, the service is expected to remain registered but stopped (demand-started) — that is logged, not reported as an error. This is the fix for a caller that immediately checks "is the service present and running" right after install and mistakes a legitimately-still-starting or legitimately-not-yet-started service for a failed install. If the INF's class filter targets cannot be determined after installing it, the settle step cannot verify anything trustworthy, so it reports `ERROR_SUCCESS_REBOOT_REQUIRED` rather than silently returning success.
- **Pitfalls:** The INF install itself either fully succeeds or fails outright; but once it succeeds, a problem in the following restart/health-check phase (e.g. a service that never reaches `SERVICE_RUNNING`) is reported via a non-zero/reboot-required exit code without undoing the install — the filter driver stays installed either way
- **When to use:** Installing a class filter driver (e.g. HidHide-style) where you need to know the outcome is trustworthy before proceeding, without hand-rolling a wait loop

**`--uninstall-filter-driver`** — Removes a class filter entry and its driver service as one ordered sequence: the filter registry entry is removed and its removal is *confirmed* before the service is touched at all, so it is impossible to end up with a dangling `UpperFilters`/`LowerFilters` entry pointing at a since-deleted service (which can prevent the whole device class from starting). Once removal is confirmed, present devices of the class are automatically restarted, and only then is the driver service deleted; deletion is retried across a short window to absorb the brief race where the kernel hasn't yet released the driver image.

- **Required:** `--position` (`upper` or `lower`), `--service-name`, `--class-guid`
- **Optional:** `--restart-timeout` — milliseconds to wait per device restart attempt performed automatically after the filter entry is removed, default `10000` (a reboot may still be required for devices that could not be restarted); `--stop-timeout` — milliseconds to wait for the service to stop, default `10000`; `--retry-timeout` — milliseconds to keep retrying service deletion while the driver image is still in use, default `5000`; `--inf-path` — path to the *original* INF file; when given, also purges every driver store package matching this filter's class GUID + service name + the INF's original base name after the service has been deleted (surgical removal, no device nodes touched), default: not purged. If the given path doesn't exist, a warning is logged and the purge is skipped — the command still completes normally instead of failing. By default the purge additionally requires the package's own `[Version]` `Provider`/`DriverVer` to match `--inf-path` exactly (so only the version that was actually installed is removed, not other versions of the same driver that may also be in the store); add `--all-versions` to drop that extra check and remove every version of the matching package instead
- **Pitfalls:** If the filter registry entry cannot be confirmed removed, the command aborts *before* deleting the service, to avoid the bricking scenario described above — check the error and fix the underlying issue rather than forcing service deletion separately. Because later steps are not rolled back, a failure further along (device restart requiring a reboot, or service deletion exhausting its retries) can leave the filter entry already removed but the service still present; re-running the command is safe (filter removal is idempotent), or use `--remove-driver-service` standalone to finish deleting the service. The driver-store purge is opt-in only via `--inf-path`; it is never performed implicitly. If zero packages match, a warning is logged ("nothing purged") rather than reporting success; if some but not all matching packages fail to delete (e.g. still bound to a device), each failure is logged individually and a reboot is assumed to be required
- **When to use:** Removing a class filter driver cleanly, especially in unattended/scripted uninstalls where the two-command sequence's race conditions are unacceptable

**`--remove-driver-store-package`** — Standalone command to surgically purge one or more packages from the driver store by original INF name, without touching any device node and without needing to stop/uninstall anything else first. Unlike `--uninstall-filter-driver`'s built-in purge (which is scoped to the filter it just removed), this command can target any package(s) directly, and always considers every version present unless narrowed further.

- **Required:** `--inf-name` — one or more original INF base names to match (e.g. `mydriver.inf`), case-insensitive; comma-separated for more than one (e.g. `--inf-name "driver1.inf,driver2.inf"`), since repeating the flag itself is not supported and only the last occurrence would be used. Required so this command can never be used to sweep the entire driver store
- **Optional:** `--class-guid` — narrows matches to packages whose INF declares this device setup class; `--service-name` — narrows matches to packages whose INF registers this class filter service (via `UpperFilters`/`LowerFilters`); does **not** match a function driver's plain `[...Services] AddService` entry
- **Behavior:** Every populated criterion must match (logical AND); a package for which a requested criterion can't be determined (e.g. its class can't be read) is treated as not matching, never purged by accident. Every matching package is attempted independently — a failure on one (e.g. it is still bound to a present device) does not prevent the others from being deleted; each outcome is logged individually and the command exits non-zero if any matching package failed to delete
- **Pitfalls:** With only `--inf-name` given, every version of every package with that original name is purged, regardless of class or service — add `--class-guid`/`--service-name` to narrow this if multiple unrelated drivers could plausibly share the same generic INF name
- **When to use:** Cleaning up leftover/orphaned driver store entries (e.g. after a manual driver removal that didn't clean the store, or before reinstalling a driver from scratch) without going through a full filter-driver uninstall sequence

### Driver service management

**`--create-driver-service`** — Creates a kernel driver service.

- **Required:** `--bin-path` (path to .sys), `--service-name`, `--display-name`
- **Pitfalls:** Binary must exist; does not start the service

**`--remove-driver-service`** — Stops (waiting for `SERVICE_STOPPED`) and deletes a kernel driver service.

- **Required:** `--service-name`
- **Optional:** `--stop-timeout` — milliseconds to wait for the service to stop before giving up, default `10000`
- **Optional:** `--attempt-detach-affected` — before stopping/deleting the service, best-effort detach every present device currently bound to it (releasing the driver's file locks so the .sys can be safely replaced/deleted), best-effort (a reboot may still be required for devices that could not be detached); successfully detached devices are recorded to a state file for a later `--reenumerate-affected` call. `--restart-timeout` sets the per-device detach timeout in milliseconds (default `10000`); `--state-file` overrides where the state is written (default a per-service-name file under `%ProgramData%\nefconc`, access-restricted to Administrators/SYSTEM since a later elevated `--reenumerate-affected` call trusts it without further validation)
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
- `--verbose` — Enable diagnostic logging: in addition to nefcon's own per-step detail, this also surfaces intermediate neflib events that are otherwise invisible (restart strategy attempts and why one was rejected, INF install/uninstall dialog interception, driver service-deletion retries, ...). Warnings and errors are always shown regardless of `--verbose`

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

### Filter driver install/remove (ordered, race-safe)

```text
# Install: same result as --inf-default-install --attempt-restart-affected, plus a trustworthy
# post-install service state instead of the caller having to guess/probe immediately afterwards.
nefconw --install-filter-driver --inf-path "MyFilter.inf"

# Uninstall: filter entry is removed and confirmed gone before the service is deleted; deletion
# is retried for up to 5s (default) if the driver image isn't freed yet.
nefconw --uninstall-filter-driver --position upper --service-name KeyboardCaster --class-guid 4D36E96B-E325-11CE-BFC1-08002BE10318

# Same, plus purge the published driver-store package once the service is gone. By default this
# only removes the version matching MyFilter.inf's own [Version] identity.
nefconw --uninstall-filter-driver --position upper --service-name KeyboardCaster --class-guid 4D36E96B-E325-11CE-BFC1-08002BE10318 --inf-path "MyFilter.inf"

# Same, but remove every version of the package still in the store, not just the one matching
# MyFilter.inf's exact identity.
nefconw --uninstall-filter-driver --position upper --service-name KeyboardCaster --class-guid 4D36E96B-E325-11CE-BFC1-08002BE10318 --inf-path "MyFilter.inf" --all-versions

# Standalone driver-store cleanup, independent of any filter-driver uninstall.
nefconw --remove-driver-store-package --inf-name "myfilter.inf"

# Narrow to a specific class + filter service, e.g. if multiple unrelated drivers could share the name.
nefconw --remove-driver-store-package --inf-name "myfilter.inf" --class-guid 4D36E96B-E325-11CE-BFC1-08002BE10318 --service-name KeyboardCaster

# Multiple original INF names in one call (comma-separated; repeating --inf-name is not supported).
nefconw --remove-driver-store-package --inf-name "driver1.inf,driver2.inf"
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
