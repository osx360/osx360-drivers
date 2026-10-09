//
//  XenonEthernet_HW.cpp
//  Xbox 360 Ethernet controller driver
//
//  Copyright © 2026 John Davis. All rights reserved.
//

#include "XenonEthernet.hpp"

//
// Interrupt handler.
//
void XenonEthernet::handleInterrupt(IOInterruptEventSource *intEventSource, int count) {
  UInt32 intStatus = readReg32(kXenonEthernetRegIntStatus);
  writeReg32(kXenonEthernetRegIntStatus, intStatus);

  if (intStatus & kXenonEthernetIntLink) {
    phyUpdateLinkStatus();
  }

  // Handle RxHalt/TxHalt together with the done bits. The engine raises Halt and stops
  // whenever its internal cursor catches up to a descriptor the driver has not re-armed
  // yet (e.g. a receive burst briefly outran this handler). We must drain the ring AND
  // re-poll the engine to restart it; otherwise it stays halted and all traffic stops.
  if (intStatus & (kXenonEthernetIntRxDone | kXenonEthernetIntRxHalt)) {
    handleRxInterrupt();
  }

  if (intStatus & (kXenonEthernetIntTxDone | kXenonEthernetIntTxHalt)) {
    handleTxInterrupt();
  }
}

//
// Reads the current MAC address from the hardware.
//
void XenonEthernet::readMacAddress(IOEthernetAddress *macAddress) {
  for (int i = 0; i < kIOEthernetAddressSize; i++) {
    macAddress->bytes[i] = readReg8(kXenonEthernetRegRxMacAddress + i);
  }
}

//
// Writes a MAC address to the hardware.
//
void XenonEthernet::writeMacAddress(IOEthernetAddress *macAddress) {
  for (int i = 0; i < kIOEthernetAddressSize; i++) {
    writeReg8(kXenonEthernetRegRxMacAddress + i, macAddress->bytes[i]);
  }
}

void XenonEthernet::softReset(void) {
  // Mask and ack all interrupts.
  writeReg32(kXenonEthernetRegIntMask, 0);
  writeReg32(kXenonEthernetRegIntStatus, 0xFFFFFFFF);

  // Perform reset.
  writeReg32(kXenonEthernetRegIntControl, kXenonEthernetIntReset);
  readReg32(kXenonEthernetRegIntControl);
  OSSynchronizeIO();
  writeReg32(kXenonEthernetRegIntControl, 0);

  // Ensure TX and RX are stopped.
  writeReg32(kXenonEthernetRegTxControl, 0x1A00);
  writeReg32(kXenonEthernetRegRxControl, 0x1A00);

  writeReg32(kXenonEthernetRegRxHashTable1, 0);
  writeReg32(kXenonEthernetRegRxHashTable2, 0);

  // Mask and ack all interrupts.
  writeReg32(kXenonEthernetRegIntMask, 0);
  writeReg32(kXenonEthernetRegIntStatus, 0xFFFFFFFF);

  // Clear the PHY.
  writeReg32(kXenonEthernetRegPhy, 0);
}

//
// Initialize the hardware.
//
bool XenonEthernet::initHardware(void) {
  // Soft reset.
  softReset();

  // Setup TX/RX.
  if (!initTransmitReceiveBuffers()) {
    return false;
  }

  writeMacAddress(&_macAddress);

  writeReg32(kXenonEthernetRegTxMacControl, 0x60);
  writeReg32(kXenonEthernetRegRxWakeOnLan, 0);
  writeReg32(kXenonEthernetRegRxWakeOnLanData, 0);



  writeReg16(kXenonEthernetRegRxMpsControl, 0x05F2);
  writeReg16(kXenonEthernetRegRxMacControl, 0x0E38);


  writeReg32(kXenonEthernetRegStationControl, 0x04001001);
  writeReg32(kXenonEthernetRegIntControl, 0x08550001);

  writeReg32(kXenonEthernetRegIntStatus, 0xFFFFFFFF);
  writeReg32(kXenonEthernetRegIntMask, kXenonEthernetIntLink | kXenonEthernetIntTxHalt | kXenonEthernetIntTxDone | kXenonEthernetIntRxHalt | kXenonEthernetIntRxDone);

  // Start transmit.
  writeReg32(kXenonEthernetRegTxControl, 0x1A00 | kXenonEthernetRegTxControlEnable);
  writeReg32(kXenonEthernetRegRxControl, 0x00101c11);

  // Begin servicing packets.
  _txQueue->setCapacity(kXenonEthernetTxQueueLength);
  _txQueue->start();

  return true;
}
