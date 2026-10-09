//
//  XenonEthernet.hpp
//  Xbox 360 Ethernet controller driver
//
//  Copyright © 2026 John Davis. All rights reserved.
//

#ifndef XenonEthernet_hpp
#define XenonEthernet_hpp

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IOGatedOutputQueue.h>
#include <IOKit/network/IOMbufMemoryCursor.h>
#include <IOKit/pci/IOPCIDevice.h>

#include "XenonCommon.hpp"
#include "XenonEthernet_Regs.hpp"

//
// Medium types.
//
enum {
  kXenonMediumType10Half = 0,
  kXenonMediumType10Full,
  kXenonMediumType100Half,
  kXenonMediumType100Full,
  kXenonMediumTypeAuto,
  kXenonMediumTypeCount
};

//
// Descriptor transmit state.
//
typedef struct {
  mbuf_t  packet;
  UInt32  segmentCount;
} XenonEthernetTxDescState;

//
// Represents the Xbox 360 Ethernet controller driver.
//
class XenonEthernet : public IOEthernetController {
  OSDeclareDefaultStructors(XenonEthernet);
  XenonDeclareLogFunctions("eth");
  typedef IOEthernetController super;

public:
  // IOService overrides.
  bool start(IOService *provider);
  void free(void);

  // IONetworkController overrides.
  IOReturn enable(IONetworkInterface *interface);
  IOReturn disable(IONetworkInterface *interface);
  UInt32 outputPacket(mbuf_t m, void *param);
  bool createWorkLoop(void);
  IOWorkLoop* getWorkLoop(void) const;
  IOOutputQueue* createOutputQueue(void);
  const OSString* newVendorString(void) const;
  const OSString* newModelString(void) const;

  // IOEthernetController overrides.
  IOReturn getHardwareAddress(IOEthernetAddress *addrP);

private:
  IOPCIDevice         *_pciParent;
  IOMemoryMap         *_mmioMap;
  volatile void       *_mmioMem;
  UInt32              _phyAddr;
  bool                _isEnabled;

  OSDictionary            *_mediumDict;
  IONetworkMedium         *_mediumTypes[kXenonMediumTypeCount];
  IOEthernetAddress       _macAddress;

  IOWorkLoop              *_workLoop;
  IOEthernetInterface     *_ethInterface;
  IOInterruptEventSource  *_intEventSource;

  // BSD interface statistics (netstat -ni). Populated as packets are received/sent.
  IONetworkStats          *_netStats;

  // Transmit descriptors.
  XenonEthernetDescriptor     *_txDesc;
  IOBufferMemoryDescriptor    *_txDescBufferDesc;
  IOPhysicalAddress           _txDescPhysAddr;
  XenonEthernetTxDescState    _txDescStates[kXenonEthernetTxDescCount];
  UInt32                      _txReadIndex;
  UInt32                      _txWriteIndex;
  IOOutputQueue               *_txQueue;
  IOMbufLittleMemoryCursor    *_txCursor;

  // Receive descriptors.
  XenonEthernetDescriptor     *_rxDesc;
  IOBufferMemoryDescriptor    *_rxDescBufferDesc;
  IOPhysicalAddress           _rxDescPhysAddr;
  mbuf_t                      _rxPackets[kXenonEthernetRxDescCount];
  UInt32                      _rxIndex;
  IOMbufLittleMemoryCursor   *_rxCursor;

  // Register read/writes.
  inline void writeReg8(UInt32 offset, UInt8 data) {
    *((volatile UInt8*)_mmioMem + offset) = data;
  }
  inline UInt8 readReg8(UInt32 offset) {
    return *((volatile UInt8*)_mmioMem + offset);
  }
  inline void writeReg16(UInt32 offset, UInt16 data) {
    OSWriteLittleInt16(_mmioMem, offset, data);
  }
  inline UInt16 readReg16(UInt32 offset) {
    return OSReadLittleInt16(_mmioMem, offset);
  }
  inline void writeReg32(UInt32 offset, UInt32 data) {
    OSWriteLittleInt32(_mmioMem, offset, data);
  }
  inline UInt32 readReg32(UInt32 offset) {
    return OSReadLittleInt32(_mmioMem, offset);
  }

  // Hardware functions.
  void handleInterrupt(IOInterruptEventSource *intEventSource, int count);
  void readMacAddress(IOEthernetAddress *macAddress);
  void writeMacAddress(IOEthernetAddress *macAddress);
  void softReset(void);
  bool initHardware(void);

  // PHY functions.
  bool phyWriteWithAddr(UInt32 addr, UInt32 offset, UInt16 data);
  bool phyReadWithAddr(UInt32 addr, UInt32 offset, UInt16 *data);
  inline bool phyWrite(UInt32 offset, UInt16 data) {
    return phyWriteWithAddr(_phyAddr, offset, data);
  }
  inline bool phyRead(UInt32 offset, UInt16 *data) {
    return phyReadWithAddr(_phyAddr, offset, data);
  }
  bool phyProbe(void);
  bool phyReset(void);
  bool phyAddMedium(IOMediumType type, UInt32 speed, UInt32 index);
  bool phyGetSupportedMediums(void);
  IONetworkMedium* phyGetActiveMedium(void);
  void phyUpdateLinkStatus(void);

  // Transmit/receive functions.
  bool allocateTxRxBuffers(void);
  bool setRxDescriptorPacket(XenonEthernetDescriptor *desc, mbuf_t packet);
  bool initTransmitReceiveBuffers(void);
  UInt32 sendTxPacket(mbuf_t packet);
  void handleTxInterrupt(void);
  void handleRxInterrupt(void);
};

#endif
