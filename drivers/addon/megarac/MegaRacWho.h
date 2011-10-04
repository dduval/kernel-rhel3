/*****************************************************************************
 *
 *  MegaRacWho.h : MegaRac 'who am I?'
 *
 ****************************************************************************/
#ifndef MEGARAC_WHO_AM_I_H
#define MEGARAC_WHO_AM_I_H

 /* some code has conditionals such as
        #if   OEM==0    ...do some AMI thing
        #elif OEM==1    ...do some DELL thing
   these defintions are for that purpose */
//#include "oem_code.h"
#define MEGARAC_OEM_AMI     0
#define MEGARAC_OEM_DELL    1

/*****************************************************************************
 * make determination about what is being compiled 
 *
 *  target OS choices:
 *      winNT   4.0
 *      winNT   2000
 *      Netware 4.x
 *      Netware 5.x
 *      SCO     Unix
 *      RedHat  Linux
 *      Sun     Solaris
 *
 *  software modules:
 *      driver
 *      api
 *
 ****************************************************************************/

#ifdef __KERNEL__                       /* manually specified (but mandatory) for driver development */
        #define MEGARAC_DRIVER_LINUX
	#define MEGARAC_DRIVER_IS
#else
	#define MEGARAC_API_LINUX
#endif


/*******************************************
 *  Linux
 ******************************************/
typedef int BOOL;
#define TRUE        1
#define FALSE       0
#include "MegaRacDrvrLx.h"

extern BOOL MegaRACInit(unsigned long);
extern void MegaRACFinish(void);

/****************************************************************
 *   taken from winNT for compatability with all other platforms
 ****************************************************************/
#if ! ( defined(MEGARAC_DRIVER_WIN_NT) || \
        defined(MEGARAC_DRIVER_WIN_2000) )
    #define PCI_TYPE0_ADDRESSES  6
   typedef struct _PCI_COMMON_CONFIG {
    unsigned short  VendorID;                   
    unsigned short  DeviceID;                   
    unsigned short  Command;                    
    unsigned short  Status;
    unsigned char   RevisionID;                 
    unsigned char   ProgIf;                     
    unsigned char   SubClass;                   
    unsigned char   BaseClass;                  
    unsigned char   CacheLineSize;              
    unsigned char   LatencyTimer;               
    unsigned char   HeaderType;                 
    unsigned char   BIST;                       
    union {
        struct _PCI_HEADER_TYPE_0 {
            unsigned long   BaseAddresses[PCI_TYPE0_ADDRESSES];
            unsigned long   CIS;
            unsigned short  SubVendorID;
            unsigned short  SubSystemID;
            unsigned long   ROMBaseAddress;
            unsigned long   Reserved2[2];
            unsigned char   InterruptLine;      
            unsigned char   InterruptPin;       
            unsigned char   MinimumGrant;       
            unsigned char   MaximumLatency;     
        } type0;
    } u;
    unsigned char   DeviceSpecific[192];
   } PCI_COMMON_CONFIG, *PPCI_COMMON_CONFIG;
    #define PCI_COMMON_HDR_LENGTH (FIELD_OFFSET (PCI_COMMON_CONFIG, DeviceSpecific))
    #define PCI_MAX_DEVICES                     32
    #define PCI_MAX_FUNCTION                    8
    #define PCI_INVALID_VENDORID                0xFFFF
    /* Bit encodings for  PCI_COMMON_CONFIG.HeaderType */
    #define PCI_MULTIFUNCTION                   0x80
    #define PCI_DEVICE_TYPE                     0x00
    #define PCI_BRIDGE_TYPE                     0x01
    /* Bit encodings for PCI_COMMON_CONFIG.Command */
    #define PCI_ENABLE_IO_SPACE                 0x0001
    #define PCI_ENABLE_MEMORY_SPACE             0x0002
    #define PCI_ENABLE_BUS_MASTER               0x0004
    #define PCI_ENABLE_SPECIAL_CYCLES           0x0008
    #define PCI_ENABLE_WRITE_AND_INVALIDATE     0x0010
    #define PCI_ENABLE_VGA_COMPATIBLE_PALETTE   0x0020
    #define PCI_ENABLE_PARITY                   0x0040  
    #define PCI_ENABLE_WAIT_CYCLE               0x0080  
    #define PCI_ENABLE_SERR                     0x0100  
    #define PCI_ENABLE_FAST_BACK_TO_BACK        0x0200  
    /* Bit encodings for PCI_COMMON_CONFIG.Status */
    #define PCI_STATUS_FAST_BACK_TO_BACK        0x0080  
    #define PCI_STATUS_DATA_PARITY_DETECTED     0x0100
    #define PCI_STATUS_DEVSEL                   0x0600  
    #define PCI_STATUS_SIGNALED_TARGET_ABORT    0x0800
    #define PCI_STATUS_RECEIVED_TARGET_ABORT    0x1000
    #define PCI_STATUS_RECEIVED_MASTER_ABORT    0x2000
    #define PCI_STATUS_SIGNALED_SYSTEM_ERROR    0x4000
    #define PCI_STATUS_DETECTED_PARITY_ERROR    0x8000
    /* Bit encodes for PCI_COMMON_CONFIG.u.type0.BaseAddresses */
    #define PCI_ADDRESS_IO_SPACE                0x00000001  
    #define PCI_ADDRESS_MEMORY_TYPE_MASK        0x00000006  
    #define PCI_ADDRESS_MEMORY_PREFETCHABLE     0x00000008  
    #define PCI_TYPE_32BIT      0
    #define PCI_TYPE_20BIT      2
    #define PCI_TYPE_64BIT      4
    /* Bit encodes for PCI_COMMON_CONFIG.u.type0.ROMBaseAddresses */
    #define PCI_ROMADDRESS_ENABLED              0x00000001
#endif /* #if !defined(MEGARAC_DRIVER_WIN_*) */


#endif /* MEGARAC_WHO_AM_I_H */
