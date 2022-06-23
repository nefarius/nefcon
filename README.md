<img src="assets/NSS-128x128.png" align="right" />

# nefcon

Windows device driver installation and management tool.

## About

This little self-contained, no-dependency tool can be built either as a console application or a Windows application which has no visible window (ideal to use in combination with setup makers). It offers a command-line-based driver (un-)installer and allows for simple manipulation of class filter entries.

## Examples

For a console example use `nefconc`, for windowless execution use `nefconw` binary.

### Installing a Primitive Driver

```text
.\nefconw --install-driver --inf-path "Path\To\Inf.inf"
```

### Uninstalling a Primitive Driver

```text
.\nefconw --uninstall-driver --inf-path "Path\To\Inf.inf"
```

### Modifying HIDClass upper filters

```text
.\nefconw --add-class-filter --position upper --service-name HidHide --class-guid 745a17a0-74d3-11d0-b6fe-00a0c90f57da
```

### Create virtual Root-enumerated device node

```text
.\nefconw --create-device-node --hardware-id root\HidHide --class-name System --class-guid 4D36E97D-E325-11CE-BFC1-08002BE10318
```

### Remove device(s) and driver

```text
.\nefconw --remove-device-node --hardware-id root\HidHide --class-guid 4D36E97D-E325-11CE-BFC1-08002BE10318
```

## 3rd party credits

This project uses the following 3rd party resources:

- [Argh! A minimalist argument handler](https://github.com/adishavit/argh)
- [Scoped coloring of Windows console output](https://github.com/jrebacz/colorwin)
- [Convenient high-level C++ wrappers around Windows Registry Win32 APIs](https://github.com/GiovanniDicanio/WinReg)
