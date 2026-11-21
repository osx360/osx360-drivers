#include <IOKit/IOPlatformExpert.h>
#include <IOKit/graphics/IOFramebuffer.h>
#include <libkern/OSAtomic.h>
#include "XenosRegs.hpp"

namespace {
const IODisplayModeID kBootMode = 100;
const IODisplayModeID kScaledMode = 101;
const UInt32 kWidth = 1920;
const UInt32 kHeight = 1080;
const UInt32 kScaledWidth = 1280;
const UInt32 kScaledHeight = 720;
const UInt32 kRowBytes = kWidth * sizeof(UInt32);
// Keep the 1080p stride: XenonCoreGraphics copies the same surface in both modes.
const IOPhysicalAddress kLinearFramebuffer = 0x20000000 - 2 * kXenosFramebufferLength;
const char kPixelFormats[] = IO32BitDirectPixels "\0";

enum {
    kCrtcHorizontalTotal = 0x6000,
    kCrtcApply = 0x60ec,
    kSurfacePitch = 0x6120,
    kSurfaceEndX = 0x6134,
    kSurfaceEndY = 0x6138,
    kViewportStart = 0x6580,
    kScalerUpdate = 0x65cc,
    kScalerUpdateLock = 0x10000,
    kApplyTimeoutMs = 100,
};

enum ScalerField {
    kScalerEnable,
    kScalerTaps,
    kViewportSize,
    kViewportWidth,
    kHorizontalFilter,
    kHorizontalScale,
    kHorizontalPhase,
    kVerticalFilter,
    kVerticalScale,
    kVerticalPhase,
    kScalerFieldCount,
};

const UInt32 kScalerRegisters[kScalerFieldCount] = {
    0x6590, 0x6594, 0x6584, 0x65e8,
    0x65b0, 0x65b4, 0x65b8, 0x65c0, 0x65c4, 0x65c8,
};
// Xenos built-in two-tap filter for a 2:3 source/output ratio.
// The scale (8.24) and phase (16.16) are rounded to the register precision.
const UInt32 kTwoTaps = 0x101;
const UInt32 kBuiltInFilter = 0x100;
const UInt32 kTwoThirdsScale = 0x00aaaaa0;
const UInt32 kTwoThirdsPhase = 0x0001d400;
}

class XenonFramebufferTest : public IOFramebuffer {
    OSDeclareDefaultStructors(XenonFramebufferTest)

public:
    IOService *probe(IOService *provider, SInt32 *score);
    bool start(IOService *provider);
    void free();
    bool isConsoleDevice();
    IODeviceMemory *getApertureRange(IOPixelAperture aperture);
    const char *getPixelFormats();
    IOItemCount getDisplayModeCount();
    IOReturn getDisplayModes(IODisplayModeID *modes);
    IOReturn getInformationForDisplayMode(IODisplayModeID mode,
                                          IODisplayModeInformation *info);
    UInt64 getPixelFormatsForDisplayMode(IODisplayModeID mode, IOIndex depth);
    IOReturn getPixelInformation(IODisplayModeID mode, IOIndex depth,
                                 IOPixelAperture aperture, IOPixelInformation *info);
    IOReturn getCurrentDisplayMode(IODisplayModeID *mode, IOIndex *depth);
    IOReturn setDisplayMode(IODisplayModeID mode, IOIndex depth);
    IOReturn connectFlags(IOIndex connection, IODisplayModeID mode, IOOptionBits *flags);

private:
    IODeviceMemory *framebuffer;
    IOMemoryMap *displayMap;
    volatile void *displayRegisters;
    bool scaledModeAvailable;
    IODisplayModeID currentMode;
    UInt32 bootScaler[kScalerFieldCount];

    void prepareScaledMode();
    bool applyMode(IODisplayModeID mode);
    bool supportsMode(IODisplayModeID mode) const;
    bool waitForDisplayUpdate() const;
    UInt32 readDisplay(UInt32 reg) const;
    void writeDisplay(UInt32 reg, UInt32 value);
};

OSDefineMetaClassAndStructors(XenonFramebufferTest, IOFramebuffer)

IOService *XenonFramebufferTest::probe(IOService *provider, SInt32 *score)
{
    PE_Video video = {};
    IOService::getPlatform()->getConsoleInfo(&video);
    const bool modeMatches = video.v_width == kWidth && video.v_height == kHeight &&
                             video.v_rowBytes == kRowBytes &&
                             (video.v_depth == 24 || video.v_depth == 32);
    const bool useXenosBase = video.v_baseAddr == 0;
    const unsigned long framebufferBase = useXenosBase
        ? kLinearFramebuffer : video.v_baseAddr & ~3UL;
    const bool baseMatches = framebufferBase == kLinearFramebuffer;
    IOLog("XenonFramebufferTest: probe score=%ld, console=%lux%lu stride=%lu depth=%lu "
          "base=0x%08lx expected=0x%08lx\n",
          score ? static_cast<long>(*score) : 0L,
          video.v_width, video.v_height, video.v_rowBytes, video.v_depth,
          video.v_baseAddr, static_cast<unsigned long>(kLinearFramebuffer));
    if (!modeMatches || !baseMatches) {
        IOLog("XenonFramebufferTest: probe rejected (mode=%s, base=%s)\n",
              modeMatches ? "ok" : "mismatch", baseMatches ? "ok" : "mismatch");
        return NULL;
    }
    if (useXenosBase) {
        IOLog("XenonFramebufferTest: probe using Xenos framebuffer at 0x%08lx "
              "because the console base is zero\n", framebufferBase);
    }
    IOService *match = IOFramebuffer::probe(provider, score);
    IOLog("XenonFramebufferTest: probe %s after IOFramebuffer::probe, score=%ld\n",
          match ? "passed" : "rejected", score ? static_cast<long>(*score) : 0L);
    return match;
}

bool XenonFramebufferTest::start(IOService *provider)
{
    framebuffer = IODeviceMemory::withRange(kLinearFramebuffer, kXenosFramebufferLength);
    if (!framebuffer) {
        IOLog("XenonFramebufferTest: start failed to create framebuffer aperture\n");
        return false;
    }
    currentMode = kBootMode;
    prepareScaledMode();
    if (!IOFramebuffer::start(provider)) {
        IOLog("XenonFramebufferTest: IOFramebuffer::start failed\n");
        return false;
    }
    IOLog("XenonFramebufferTest: attached to inherited 1920x1080 framebuffer\n");
    return true;
}

void XenonFramebufferTest::free()
{
    if (displayMap)
        displayMap->release();
    if (framebuffer)
        framebuffer->release();
    IOFramebuffer::free();
}

bool XenonFramebufferTest::isConsoleDevice()
{
    return true;
}

IODeviceMemory *XenonFramebufferTest::getApertureRange(IOPixelAperture aperture)
{
    if (aperture != kIOFBSystemAperture || !framebuffer)
        return NULL;
    framebuffer->retain();
    return framebuffer;
}

const char *XenonFramebufferTest::getPixelFormats()
{
    return kPixelFormats;
}

IOItemCount XenonFramebufferTest::getDisplayModeCount()
{
    return scaledModeAvailable ? 2 : 1;
}

IOReturn XenonFramebufferTest::getDisplayModes(IODisplayModeID *modes)
{
    modes[0] = kBootMode;
    if (scaledModeAvailable)
        modes[1] = kScaledMode;
    return kIOReturnSuccess;
}

bool XenonFramebufferTest::supportsMode(IODisplayModeID mode) const
{
    return mode == kBootMode || (scaledModeAvailable && mode == kScaledMode);
}

IOReturn XenonFramebufferTest::getInformationForDisplayMode(IODisplayModeID mode,
                                                           IODisplayModeInformation *info)
{
    if (!supportsMode(mode))
        return kIOReturnUnsupportedMode;
    bzero(info, sizeof(*info));
    info->nominalWidth = mode == kBootMode ? kWidth : kScaledWidth;
    info->nominalHeight = mode == kBootMode ? kHeight : kScaledHeight;
    info->refreshRate = 60 << 16;
    return kIOReturnSuccess;
}

IOReturn XenonFramebufferTest::connectFlags(IOIndex connection, IODisplayModeID mode,
                                           IOOptionBits *flags)
{
    if (connection != 0 || !supportsMode(mode))
        return kIOReturnUnsupportedMode;
    *flags = kDisplayModeValidFlag;
    if (mode == kBootMode)
        *flags |= kDisplayModeSafeFlag | kDisplayModeDefaultFlag;
    return kIOReturnSuccess;
}

UInt64 XenonFramebufferTest::getPixelFormatsForDisplayMode(IODisplayModeID, IOIndex)
{
    return 0;
}

IOReturn XenonFramebufferTest::getPixelInformation(IODisplayModeID mode, IOIndex depth,
                                                   IOPixelAperture aperture, IOPixelInformation *info)
{
    IODisplayModeInformation modeInfo;
    if (depth != 0 || aperture != kIOFBSystemAperture ||
        getInformationForDisplayMode(mode, &modeInfo) != kIOReturnSuccess)
        return kIOReturnUnsupportedMode;
    bzero(info, sizeof(*info));
    info->bytesPerRow = kRowBytes;
    info->bitsPerPixel = 32;
    info->pixelType = kIORGBDirectPixels;
    info->componentCount = 3;
    info->bitsPerComponent = 8;
    info->componentMasks[0] = 0x00ff0000;
    info->componentMasks[1] = 0x0000ff00;
    info->componentMasks[2] = 0x000000ff;
    memcpy(info->pixelFormat, kPixelFormats, sizeof(IO32BitDirectPixels));
    info->activeWidth = modeInfo.nominalWidth;
    info->activeHeight = modeInfo.nominalHeight;
    return kIOReturnSuccess;
}

IOReturn XenonFramebufferTest::getCurrentDisplayMode(IODisplayModeID *mode, IOIndex *depth)
{
    if (mode)
        *mode = currentMode;
    if (depth)
        *depth = 0;
    return kIOReturnSuccess;
}

IOReturn XenonFramebufferTest::setDisplayMode(IODisplayModeID mode, IOIndex depth)
{
    if (depth != 0 || !supportsMode(mode))
        return kIOReturnUnsupportedMode;
    if (mode == currentMode)
        return kIOReturnSuccess;
    if (!waitForDisplayUpdate())
        return kIOReturnTimeout;
    if (!applyMode(mode)) {
        const bool restored = applyMode(currentMode);
        IOLog("XenonFramebufferTest: mode %ld failed, previous display state %s\n",
              static_cast<long>(mode), restored ? "restored" : "could not be restored");
        return kIOReturnIOError;
    }
    currentMode = mode;
    IOLog("XenonFramebufferTest: desktop %lux%lu, HDMI timing 1920x1080\n",
          mode == kBootMode ? kWidth : kScaledWidth,
          mode == kBootMode ? kHeight : kScaledHeight);
    return kIOReturnSuccess;
}

void XenonFramebufferTest::prepareScaledMode()
{
    IODeviceMemory *mmio = IODeviceMemory::withRange(kXenosMmioAddress, kXenosMmioLength);
    if (mmio) {
        displayMap = mmio->map(kIOMapInhibitCache);
        mmio->release();
    }
    if (!displayMap) {
        IOLog("XenonFramebufferTest: display registers unavailable for 720p desktop\n");
        return;
    }

    displayRegisters = reinterpret_cast<volatile void *>(displayMap->getVirtualAddress());
    for (unsigned int i = 0; i < kScalerFieldCount; ++i)
        bootScaler[i] = readDisplay(kScalerRegisters[i]);

    // Only enable switching over the known, unscaled 1080p60 boot setup.
    scaledModeAvailable = (readDisplay(kCrtcHorizontalTotal) & 0x1fff) == 2199 &&
                          readDisplay(kSurfacePitch) == kWidth &&
                          readDisplay(kSurfaceEndX) == kWidth &&
                          readDisplay(kSurfaceEndY) == kHeight &&
                          readDisplay(kViewportStart) == 0 &&
                          bootScaler[kScalerEnable] == 0 &&
                          bootScaler[kViewportSize] == ((kWidth << 16) | kHeight);
    IOLog("XenonFramebufferTest: 720p desktop %s over inherited 1080p HDMI\n",
          scaledModeAvailable ? "available" : "disabled (unexpected scanout)");
}

bool XenonFramebufferTest::applyMode(IODisplayModeID mode)
{
    UInt32 scaler[kScalerFieldCount];
    memcpy(scaler, bootScaler, sizeof(scaler));
    const bool scaled = mode == kScaledMode;
    const UInt32 height = scaled ? kScaledHeight : kHeight;
    if (scaled) {
        scaler[kScalerEnable] = 1;
        scaler[kScalerTaps] = kTwoTaps;
        scaler[kViewportSize] = (kScaledWidth << 16) | kScaledHeight;
        scaler[kViewportWidth] = kScaledWidth / 4 - 1;
        scaler[kHorizontalFilter] |= kBuiltInFilter;
        scaler[kHorizontalScale] = kTwoThirdsScale;
        scaler[kHorizontalPhase] = kTwoThirdsPhase;
        scaler[kVerticalFilter] |= kBuiltInFilter;
        scaler[kVerticalScale] = kTwoThirdsScale;
        scaler[kVerticalPhase] = kTwoThirdsPhase;
    }

    // The render target and hardware pitch stay fixed. Crop the visible
    // viewport and scale it while leaving the clock and transmitter alone.
    writeDisplay(kScalerUpdate, kScalerUpdateLock);
    for (unsigned int i = 0; i < kScalerFieldCount; ++i)
        writeDisplay(kScalerRegisters[i], scaler[i]);
    writeDisplay(kScalerUpdate, 0);
    writeDisplay(kSurfaceEndY, height);
    writeDisplay(kCrtcApply, 1);
    if (!waitForDisplayUpdate()) {
        writeDisplay(kCrtcApply, 0);
        IOLog("XenonFramebufferTest: display update timed out\n");
        return false;
    }

    for (unsigned int i = 0; i < kScalerFieldCount; ++i) {
        const UInt32 actual = readDisplay(kScalerRegisters[i]);
        if (actual != scaler[i]) {
            IOLog("XenonFramebufferTest: scaler register 0x%04lx read 0x%08lx, expected 0x%08lx\n",
                  kScalerRegisters[i], actual, scaler[i]);
            return false;
        }
    }
    return readDisplay(kSurfaceEndY) == height;
}

bool XenonFramebufferTest::waitForDisplayUpdate() const
{
    for (unsigned int ms = 0; ms < kApplyTimeoutMs; ++ms) {
        if (readDisplay(kCrtcApply) == 0)
            return true;
        IOSleep(1);
    }
    return readDisplay(kCrtcApply) == 0;
}

UInt32 XenonFramebufferTest::readDisplay(UInt32 reg) const
{
    const UInt32 value = OSReadBigInt32(displayRegisters, reg);
    OSSynchronizeIO();
    return value;
}

void XenonFramebufferTest::writeDisplay(UInt32 reg, UInt32 value)
{
    OSWriteBigInt32(displayRegisters, reg, value);
    OSSynchronizeIO();
}
