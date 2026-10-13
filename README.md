<div align="center">
  <img src="./docs/osx360.png" alt="OSX360" width="250" align>
  <h1>OSX360</h1>
</div>

[![Build Status](https://github.com/osx360/osx360-drivers/actions/workflows/main.yml/badge.svg?branch=main)](https://github.com/osx360/osx360-drivers/actions) [![Build OpenBIOS](https://github.com/osx360/openbios/actions/workflows/main.yml/badge.svg)](https://github.com/osx360/openbios/actions/workflows/main.yml)

Xbox 360 support kernel extensions for Mac OS X as part of [OSX360](https://github.com/osx360). This is an early attempt at running Mac OS X on the Xbox 360. There will be functions that do not work, and the system may lock up unexpectedly.

## Background

The Xbox 360 is a PowerPC-based "Xenon" console released in 2005. The Xenon processor is unique to the console, being a three core Cell-based design without AltiVec and instead having VMX128. VMX128 is not completely compatible with AltiVec, and Cell is different in behavior from the G5 used in various Mac models. Currently the processor is spoofed to appear as a G3 to prevent AltiVec usage, and any software requiring AltiVec or a G5 will not run at this time. Some 32-bit software that runs unmodified on a G5 may not function on this platform.

Xenon does not support some of the hardware features present in the G5, i.e. the register that controls `dcbz` behavior. While most of XNU will use the proper dcbz128 instruction, some portions do not and some userspace libraries such as CoreGraphics do not. The platform expert as part of this driver project will attempt to patch those to be invalid and trigger XNU's built-in instruction emulator. See [this](https://www.talospace.com/2018/08/making-your-talos-ii-into-power-mac_29.html) page for more background.

AltiVec is not supported on Xenon in favor of VMX128 which drops some AltiVec instructions. In theory these could be trapped/emulated to enable AltiVec software to run, but this has yet to be explored.

## Installation

#### Requirements
* An Xbox 360 that is modded either by hardware or with BadUpdate. OpenBIOS will be launched from a USB stick using XeLL. Non-4GB NAND consoles will need to have the USB configured as system storage if using ABadAvatar. Refer to [this](https://consolemods.org/wiki/Xbox_360:Bad_Update) page for more information.
* The latest release of [OpenBIOS Xbox 360 fork](https://github.com/osx360/openbios).
* The latest release of the drivers mkext from the Releases tab.
* Mac OS X Tiger 10.4.6 DVD (the CD version may also work, but is currently untested)
  * A USB installer can also work, but will require partitioning that is currently [documented here](docs/USBPartitioning.md).
  * Certain DVD drives require a tray cycle before working. So far this needs to be done on the DL10N, but others may be affected.
* Hard drive to install Mac OS X to. This will be destructive, do not use a stock Xbox 360 hard drive.
  * Any drive should work, but there are some issues with some WD drives and OpenBIOS currently.
  * A USB drive can also work, but is not documented currently and will require some additional work to start from OpenBIOS.

#### USB setup
Configure the USB for BadUpdate / ABadAvatar if needed. Place `openbios-xbox360.elf` and `Xbox360.mkext` from this repo's releases onto the root of the drive. Configure kboot.conf for XeLL as desired, an example configuration can be found [here](tools/kboot.conf).

#### Boot and installation
Start XeLL and launch OpenBIOS. OpenBIOS should drop into the shell.
* To startup from the installation DVD, run `boot cd:,\\:tbxi`.
* To startup from the hard drive, run `boot hd:,\\:tbxi`.
* To startup from the USB stick (if configured as an installer), run `boot ud:X,\\:tbxi` where `X` is the installer partition.
* To boot in verbose, run `setenv boot-args -v` prior to booting Mac OS X.
* To boot in single user, run `setenv boot-args -s` prior to booting Mac OS X.

#### Post installation
Mac OS X may require modifications to IOAudioFamily and IONetworkingFamily for audio and ethernet to work. You'll need to edit both to ensure they are loaded at bootup.

`sudo vi /System/Library/Extensions/IOAudioFamily.kext/Contents/Info.plist`
`sudo vi /System/Library/Extensions/IONetworkingFamily.kext/Contents/Info.plist`

For both files, type `i` to enter insert mode and append the following after the last key, but before the closing plist:

```
<key>OSBundleRequired</key>
<string>Root</string>
```

Save and quit with `:wq`.

After both edits are made, run `sudo touch /System/Library/Extensions` to force a kext cache rebuild and reboot. The next bootup may take several minutes as the system will boot without a kext cache. Once booted, audio and networking should be fully functional.

## Support status

### Version status

| Version                                            | Supported                                |
|----------------------------------------------------|------------------------------------------|
| 10.0 Cheetah                                       | No, released prior to 64-bit support     |
| 10.1 Puma                                          | No, released prior to 64-bit support     |
| 10.2 Jaguar                                        | Partial, only boots to single user mode. The G5 version of 10.2.7 must be used. |
| 10.3 Panther                                       | Partial, only boots to single user mode. |
| 10.4 Tiger                                         | Mostly functional, freezes may occur either during boot or sometime after. |
| 10.5 Leopard                                       | No, requires AltiVec                     |
| 10.6 Snow Leopard (beta/unofficial PowerPC builds) | No, requires AltiVec                     |

### Xbox 360 hardware support status
| Hardware                                          | Supported                                     |
|---------------------------------------------------|-----------------------------------------------|
| Interrupt controller                              | Yes                                           |
| PCI root bridge                                   | Yes                                           |
| USB 1.1 (OHCI) controllers                        | Yes                                           |
| USB 2.0 (EHCI) controllers                        | Yes                                           |
| WiFi via USB                                      | No                                            |
| Audio interface (rear A/V)                        | Yes, not all media may work                   |
| Xenos graphics interface                          | Basic framebuffer only, no resolution changes |
| System management controller (SMC)                | Yes, shutdown/restart, CD drive, RTC read     |
| Ethernet controller                               | Partially, receiving packets may stop working |
| SATA DVD-ROM                                      | Yes, some drives require a tray cycle         |
| SATA Hard Drive                                   | Yes, some drives may not function             |

## Extensions
* XenonAudo: Audio support
* XenonCoreGraphics: Core graphics support
* XenonEthernet: Ethernet support
* XenonPCI: PCI host bridge support
* XenonPlatform: Platform expert and interrupt controller support
* XenonSATA: SATA controller and DVD drive support
* XenonSMC: SMC support

## Credits
- [Apple](https://www.apple.com) for Mac OS X
- [Goldfish64](https://github.com/Goldfish64) for this software
- [JoJo](https://github.com/buddyjojo) for graphics implementation
- [Lilu](https://github.com/acidanthera/Lilu) for kernel patching and function hooking basis
- [Free60](https://free60.org) for various documents/info
- [libxenon](https://github.com/Free60Project/libxenon) for graphics implementation and other hardware info
- [https://github.com/freedreno-zz/freedreno](freedreno) for graphics implementation
- [Xenon Emulator](https://github.com/xenon-emu/xenon) for hardware workings
