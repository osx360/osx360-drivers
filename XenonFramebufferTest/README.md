# XenonFramebufferTest 0.0.4

This experimental Tiger kext takes ownership of the Xbox 360 Slim's inherited 1920 × 1080 framebuffer. It offers 1920 × 1080 and 1280 × 720 as desktop modes. In 1280 × 720 mode, the Xenos scaler fills the 1920 × 1080 HDMI output. The HDMI link stays at 1080p60 in both modes, using the audio setup established by OpenBIOS.

## Xbox 360 Slim test results

Tested on an Xbox 360 Slim connected over HDMI, running Mac OS X 10.4.11 from a USB 2.0 SSD. The test used [OpenBIOS with USB 2.0 autoboot and the 1080p HDMI audio setup](https://github.com/blub3/openbios/tree/859256055b918cfa69bb7b5b3d999bfb702b4b78).

- After one cold boot, the probe used the Xenos framebuffer at `0x1f000000` when Tiger reported a zero console base. The I/O Registry showed `display0` attached to `XenonFramebufferTest`.
- Quartz listed both resolutions as usable desktop modes. A session test switched 1920 × 1080 → 1280 × 720 → 1920 × 1080 without rebooting. Finder reported the expected desktop bounds at each step, and the framebuffer driver logged both transitions.
- The console owner heard iTunes music over HDMI at both desktop resolutions. The scripted system alerts were inaudible. The HDMI audio result comes from the iTunes playback test.

## Build and boot the test

Use the PowerPC toolchain and the `MacPPCKernelSDK` checkout at the repository root. See the [project build instructions](../BUILDING.md). The tested build used GCC 4.2.1, the classic PowerPC linker, and [MacPPCKernelSDK commit `12b9fac`](https://github.com/Goldfish64/MacPPCKernelSDK/tree/12b9fac01a05c4ceeb27e42af53c76b810c135e7).

```sh
make -C XenonFramebufferTest OSX_VERSION=tiger
```

The build writes `XenonFramebufferTest/build_kext_tiger/XenonFramebufferTest.kext`. For the boot test, include it and the target installation's Tiger `IOGraphicsFamily` 1.4.2 in a separate startup mkext alongside the existing Xbox drivers, including `XenonCoreGraphics`. Point a test OpenBIOS ELF at that archive and keep the working ELF, startup mkext, boot configuration and 720p backup for rollback. Boot once with the test archive, then select the desktop mode in **System Preferences → Displays**.

The scaler settings follow the two-tap configuration documented in the [XSDK hardware tests](https://github.com/xenosworks/xsdk/blob/9e6de98f6b5d737bce0fecd2864edae94bdd2637/docs/HARDWARE.md#the-built-in-2-tap-filter-scales-1280x720-to-1920x1080-whole-2026-10-10).
