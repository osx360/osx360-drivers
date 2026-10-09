//
//  XenonEthernet.cpp
//  Xbox 360 Ethernet controller driver
//
//  Copyright © 2026 John Davis. All rights reserved.
//

#include "XenonEthernet.hpp"

OSDefineMetaClassAndStructors(XenonEthernet, super);

//
// Performs driver startup.
// Overrides IOService::start().
//
bool XenonEthernet::start(IOService *provider) {
  XenonCheckDebugArgs();

  _pciParent = OSDynamicCast(IOPCIDevice, provider);
  if (_pciParent == NULL) {
    XESYSLOG("Provider is not IOPCIDevice");
    return false;
  }
  _pciParent->retain();

  if (!super::start(provider)) {
    XESYSLOG("super::start() returned false");
    return false;
  }

  // Ensure PCI device is ready.
  _pciParent->setBusMasterEnable(true);
  _pciParent->setMemoryEnable(true);
  _pciParent->setIOEnable(false);

  // Map in MMIO registers.
  _mmioMap = _pciParent->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
  if (_mmioMap == NULL) {
    XESYSLOG("Failed to map Ethernet registers");
    return false;
  }

  _mmioMem = (volatile UInt8*) _mmioMap->getVirtualAddress();
  XEDBGLOG("Mapped Ethernet registers at 0x%X length 0x%X to %p", _mmioMap->getPhysicalAddress(),
    _mmioMap->getLength(), _mmioMem);

  _txQueue = getOutputQueue();
  if (_txQueue == NULL) {
    XESYSLOG("Failed to get output queue");
    return false;
  }
  _txQueue->retain();

  if (!allocateTxRxBuffers()) {
    XESYSLOG("Failed to allocate TX/RX buffers");
    return false;
  }

  _intEventSource = IOInterruptEventSource::interruptEventSource(this,
    OSMemberFunctionCast(IOInterruptEventSource::Action, this, &XenonEthernet::handleInterrupt), getProvider(), 0);
  if ((_intEventSource == NULL) || (getWorkLoop()->addEventSource(_intEventSource) != kIOReturnSuccess)) {
    XESYSLOG("Failed to create interrupt");
    return false;
  }
  _intEventSource->enable();

  // Probe the PHY and get the supported mediums.
  if (!phyProbe() || !phyGetSupportedMediums()) {
    XESYSLOG("Failed to get PHY");
    return false;
  }

  // Get the MAC, this assumes it was left by Xell.
  readMacAddress(&_macAddress);
  XEDBGLOG("Current MAC address: %02X:%02X:%02X:%02X:%02X:%02X",
    _macAddress.bytes[0], _macAddress.bytes[1], _macAddress.bytes[2],
    _macAddress.bytes[3], _macAddress.bytes[4], _macAddress.bytes[5]);

  // Configure the Ethernet interface.
  if (!attachInterface((IONetworkInterface **) &_ethInterface, false)) {
    XESYSLOG("Failed to attach interface");
    return false;
  }

  // Grab the BSD statistics buffer so netstat -ni reflects real traffic.
  IONetworkData *statsData = _ethInterface->getNetworkData(kIONetworkStatsKey);
  if (statsData != NULL) {
    _netStats = (IONetworkStats *) statsData->getBuffer();
  }

  _ethInterface->registerService();

  XEDBGLOG("Started Xenon Ethernet controller");
  return true;
}

//
// Releases resources used by the driver.
// Overrides IOService::free().
//
void XenonEthernet::free(void) {
  super::free();
}

//
// Called from IONetworkInterface to enable the network adapter for use.
// Overrides IONetworkController::enable(IONetworkInterface).
//
IOReturn XenonEthernet::enable(IONetworkInterface *interface) {
  XEDBGLOG("Enabling controller");

  if (_isEnabled) {
    return kIOReturnIOError;
  }

  if (!_pciParent->open(this)) {
    return kIOReturnIOError;
  }

  if (!initHardware()) {
    return kIOReturnIOError;
  }

  // Report the current link status.
  phyUpdateLinkStatus();

  _isEnabled = true;

  XEDBGLOG("Enabled controller");
  return kIOReturnSuccess;
}

//
// Called from IONetworkInterface to disable the network adapter.
// Overrides IONetworkController::disable(IONetworkInterface).
//
IOReturn XenonEthernet::disable(IONetworkInterface *interface) {
  XEDBGLOG("Disabling controller");

  _isEnabled = false;

  return kIOReturnSuccess;
}

//
// Creates a new IOWorkLoop instance. Called during IONetworkController::start().
// Overrides IONetworkController::createWorkLoop().
//
bool XenonEthernet::createWorkLoop(void) {
  _workLoop = IOWorkLoop::workLoop();
  return _workLoop != NULL;
}

//
// Gets this driver's IOWorkLoop instance.
// Overrides IONetworkController::getWorkLoop().
//
IOWorkLoop* XenonEthernet::getWorkLoop(void) const {
  return _workLoop;
}

//
// Creates a new IOGatedOutputQueue instance.
// Overrides IONetworkController::createOutputQueue().
//
IOOutputQueue* XenonEthernet::createOutputQueue(void) {
  return IOGatedOutputQueue::withTarget(this, getWorkLoop());
}

//
// Gets the vendor display string.
// Overrides IONetworkController::newVendorString().
//
const OSString* XenonEthernet::newVendorString(void) const {
  return OSString::withCString(kXenonEthernetVendorStr);
}

//
// Gets the model display string.
// Overrides IONetworkController::newModelString().
//
const OSString* XenonEthernet::newModelString(void) const {
  return OSString::withCString(kXenonEthernetModelStr);
}

//
// Transmits a packet.
// Overrides IONetworkController::outputPacket().
//
UInt32 XenonEthernet::outputPacket(mbuf_t m, void *param) {
  if (!_isEnabled) {
    freePacket(m);
    return kIOReturnOutputDropped;
  }

  return sendTxPacket(m);
}

//
// Gets the current MAC address of the network adapter.
// Overrides IOEthernetController::getHardwareAddress().
//
IOReturn XenonEthernet::getHardwareAddress(IOEthernetAddress *addrP) {
  bcopy(&_macAddress, addrP, sizeof (*addrP));
  return kIOReturnSuccess;
}
