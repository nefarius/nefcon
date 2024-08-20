# <img src="assets/NSS-128x128.png" align="left" />nefcon

[![MSBuild](https://github.com/nefarius/nefcon/actions/workflows/msbuild.yml/badge.svg)](https://github.com/nefarius/nefcon/actions/workflows/msbuild.yml)
[![GitHub All Releases](https://img.shields.io/github/downloads/nefarius/nefcon/total)](https://somsubhra.github.io/github-release-stats/?username=nefarius&repository=nefcon)
[![Discord](https://img.shields.io/discord/346756263763378176.svg)](https://discord.nefarius.at)
[![GitHub followers](https://img.shields.io/github/followers/nefarius.svg?style=social&label=Follow)](https://github.com/nefarius)
[![Mastodon Follow](https://img.shields.io/mastodon/follow/109321120351128938?domain=https%3A%2F%2Ffosstodon.org%2F&style=social)](https://fosstodon.org/@Nefarius)

Windows device driver installation and management tool.

## About

This little self-contained, no-dependency tool can be built either as a console application or a Windows application which has no visible window (ideal to use in combination with setup makers). It offers a command-line-based driver (un-)installer and allows for simple manipulation of class filter entries. Run `nefconc.exe --help` to see all the options offered.

## Motivation

Windows Device Driver management is and always has been hard. The APIs involved are old, moody and come with pitfalls. Historically the [`devcon`](https://github.com/microsoft/Windows-driver-samples/tree/b3af8c8f9bd508f54075da2f2516b31d05cd52c8/setup/devcon) tool or nowadays `pnputil` have been used to offload these tedious tasks, but unintuitive and sparsely documented command line arguments and error propagation make them poor candidates for automation in e.g. setup engines. Grown tired of these limitations I made this "devcon clone" available under a permissive license which offers the following highlighted features and more:

- Allows for true window-less execution
- Actively suppresses and works around user interaction inconsistencies ("reboot required" dialogs and OS-included bugs)
- Offers optional logging to `stdout` or file
- *Sane* command line arguments 😁
- [Class filter](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/filter-drivers) values manipulation
- Supports installation of [primitive drivers](https://learn.microsoft.com/en-us/windows-hardware/drivers/develop/creating-a-primitive-driver)

## Installation

Binaries are available to download in the [releases](https://github.com/nefarius/nefcon/releases/latest) page, just download and extract. However, if you are using a package manager, you can use one of the following options:

### Scoop

[`nefcon`](https://scoop.sh/#/apps?q=nefcon&s=0&d=1&o=true) is available in the [Extras](https://github.com/ScoopInstaller/Extras) bucket:

```text
scoop bucket add extras
scoop install nefcon
```

### Winget

[`nefcon`](https://github.com/microsoft/winget-pkgs/tree/master/manifests/n/Nefarius/nefcon) is available in the [winget-pkgs](https://github.com/microsoft/winget-pkgs) repository:

```text
winget install nefcon
```

## Examples

For a console example use `nefconc`, for windowless execution use `nefconw` binary.

### Installing a Primitive Driver

```text
nefconw --install-driver --inf-path "Path\To\Inf.inf"
```

### Uninstalling a Primitive Driver

```text
nefconw --uninstall-driver --inf-path "Path\To\Inf.inf"
```

### Modifying HIDClass upper filters

```text
nefconw --add-class-filter --position upper --service-name HidHide --class-guid 745a17a0-74d3-11d0-b6fe-00a0c90f57da
```

### Create virtual Root-enumerated device node

```text
nefconw --create-device-node --hardware-id root\HidHide --class-name System --class-guid 4D36E97D-E325-11CE-BFC1-08002BE10318
```

### Remove device(s) and driver

```text
nefconw --remove-device-node --hardware-id root\HidHide --class-guid 4D36E97D-E325-11CE-BFC1-08002BE10318
```

### Install file system volume controller driver

```text
nefconw --inf-default-install --inf-path "F:\Downloads\btrfs-1.8\btrfs.inf"
```

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
