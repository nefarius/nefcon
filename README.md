<img src="assets/NSS-128x128.png" align="right" />

# nefcon

Windows device driver installation and management tool.

[![MSBuild](https://github.com/nefarius/nefcon/actions/workflows/msbuild.yml/badge.svg)](https://github.com/nefarius/nefcon/actions/workflows/msbuild.yml)
[![GitHub All Releases](https://img.shields.io/github/downloads/nefarius/nefcon/total)](https://somsubhra.github.io/github-release-stats/?username=nefarius&repository=nefcon)

## About

This little self-contained, no-dependency tool can be built either as a console application or a Windows application which has no visible window (ideal to use in combination with setup makers). It offers a command-line-based driver (un-)installer and allows for simple manipulation of class filter entries. Run `nefconc.exe --help` to see all the options offered.

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
