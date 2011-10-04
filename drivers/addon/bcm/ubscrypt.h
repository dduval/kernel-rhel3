
/*
 * Broadcom Cryptonet Driver software is distributed as is, without any warranty
 * of any kind, either express or implied as further specified in the GNU Public
 * License. This software may be used and distributed according to the terms of
 * the GNU Public License.
 *
 * Cryptonet is a registered trademark of Broadcom Corporation.
 */

/******************************************************************************
 *
 * Copyright 2000
 * Broadcom Corporation
 * 16215 Alton Parkway
 * PO Box 57013
 * Irvine CA 92619-7013
 *
 *****************************************************************************/

/* 
 * Broadcom Corporation uBSec SDK 
 */

/*
 * ubscrypt.h:  This file contains all the structure definitions for the crypto 
 * functions of the BCM58xx chip family.
 * 
 * This file was previously named ubs5501.h:
 */ 

/*
 * Revision History:
 *
 * 09/xx/1999 SOR Created - ubs5501.h.
 * 12/01/1999 DWP Modified to handle Big Endian devices.  Activating SRL for
 *                Big Endian requires defining BIG_ENDIAN, rather than 
 *                LITTLE_ENDIAN
 * 12/03/1999 SOR Modifications to do all static address computations 
 *                at init time
 * 09/14/2000 SOR Added 5820 Support
 * 09/14/2000 SOR Created register control file and this file.
 * 04/03/2001 RJT Added support for CryptoNet device big-endian mode
 * 07/16/2001 RJT Added support for BCM5821
 * 10/04/2001 SRM 64bit port
 */

#ifndef _UBSCRYPT_H_
#define _UBSCRYPT_H_

/*
 * Device specfic Data Structures.
 *
 * These structures are determined by the hardware.
 * additional fields are added for simplicity.
 *
 * BCM - ubs5801 was previously the BSN-ubs5501.
 * so both definitions may be used in this file.
 */


/*
 * Packet Context Buffer structure
 *
 * 	Keeps the keys and instructions for a packet.
 *
 *	CryptoKeys		holds the keys for 3DES
 *	HMACInnerState		pre-computed HMAC inner state
 *	HMACOuterState		pre-computed HMAC outer state
 *				(2x16bit for MD5, 2x20bit for SHA1)
 *	ComputedIV		Crypto Initial Vector(from payload, if explicit)
 *				Processing Control Flags:
 *	reserved		reserved
 *	uAuthentication		MD5/SHA1/None
 *	Inbound		Inbound Packet
 *	Crypto			3DES-CBC/None
 *	CryptoOffset		Offset to skip authenticated but not encrypted
 *				header words.  Goes to start of IV data, in
 *				units of 32-bit words.
 */
typedef struct CipherContext_s {
#ifdef UBSEC_582x_CLASS_DEVICE 
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short       CryptoFlag;
  VOLATILE unsigned short       CryptoOffset;
#else
  VOLATILE unsigned short       CryptoOffset;
  VOLATILE unsigned short       CryptoFlag;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE UBS_UINT32	CryptoKey1[2];
  VOLATILE UBS_UINT32	CryptoKey2[2];
  VOLATILE UBS_UINT32	CryptoKey3[2];
  VOLATILE UBS_UINT32	ComputedIV[2];
  VOLATILE UBS_UINT32	HMACInnerState[5];
  VOLATILE UBS_UINT32	HMACOuterState[5];
#else /* UBSEC_580x */
  VOLATILE UBS_UINT32	CryptoKey1[2];
  VOLATILE UBS_UINT32	CryptoKey2[2];
  VOLATILE UBS_UINT32	CryptoKey3[2];
  VOLATILE UBS_UINT32	HMACInnerState[5];
  VOLATILE UBS_UINT32	HMACOuterState[5];
  VOLATILE UBS_UINT32	ComputedIV[2];
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short       CryptoFlag;
  VOLATILE unsigned short	CryptoOffset;
#else
  VOLATILE unsigned short	CryptoOffset;
  VOLATILE unsigned short       CryptoFlag;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
#endif /* 5820/21 conditional */
}   CipherContext_t, *CipherContext_pt;

/*
 * CryptoFlag Settings
 */
/* #ifdef UBS_LITTLE_ENDIAN */
#if (UBS_CPU_ATTRIBUTE == UBS_CRYPTONET_ATTRIBUTE)
#define CF_ENCODE   0x0000
#define CF_DECODE   0x4000
#define CF_3DES     0x8000
#define CF_MD5      0x1000
#define CF_SHA1     0x2000
#else
#define CF_ENCODE   0x0000
#define CF_DECODE   0x0040
#define CF_3DES     0x0080
#define CF_MD5      0x0010
#define CF_SHA1     0x0020
#endif

#ifdef UBSEC_582x_CLASS_DEVICE  /* SSL/TLS/ARC4 are for BCM5820/21 only */

/*
 * SSL MD5/SHA1 context.
 */
#define SSL_HMAC_PAD_VALUE_LONG 0x3636363636363636
#define SSL_HMAC_PAD_LENGTH_LONG 12

#define SSL_MAC_PAD_VALUE_LONG 0x3636363636363636
#define SSL_MAC_PAD_LENGTH_LONG 12

typedef struct SSL_HMACContext_s {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short       CryptoFlag;
  VOLATILE unsigned short	Reserved;
#else
  VOLATILE unsigned short	Reserved;
  VOLATILE unsigned short       CryptoFlag;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE unsigned char	HMACKey[20];
  VOLATILE unsigned char	HMACPad[48];
  VOLATILE UBS_UINT32	SequenceHigh;
  VOLATILE UBS_UINT32	SequenceLow;
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE UBS_UINT32	ReservedB : 8;
  VOLATILE UBS_UINT32	DataLength : 16;
  VOLATILE UBS_UINT32	ContentType : 8;
#else
  VOLATILE UBS_UINT32	ContentType : 8;
  VOLATILE UBS_UINT32	DataLength : 16;
  VOLATILE UBS_UINT32	ReservedB : 8;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
}   SSL_HMACContext_t, *SSL_HMACContext_pt, SSL_MACContext_t, *SSL_MACContext_pt;

/* TLS HMAC Context */
typedef struct TLS_HMACContext_s {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short       CryptoFlag;
  VOLATILE unsigned short	Reserved;
#else
  VOLATILE unsigned short	Reserved;
  VOLATILE unsigned short       CryptoFlag;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE UBS_UINT32	HMACInnerState[5];
  VOLATILE UBS_UINT32	HMACOuterState[5];
  VOLATILE UBS_UINT32	SequenceHigh;
  VOLATILE UBS_UINT32	SequenceLow;
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE UBS_UINT32	DataLengthHi:8;
  VOLATILE UBS_UINT32	Version:16;
  VOLATILE UBS_UINT32	ContentType:8;
  VOLATILE UBS_UINT32	ReservedG2:24;
  VOLATILE UBS_UINT32	DataLengthLo:8;
#else
  VOLATILE UBS_UINT32	ContentType:8;
  VOLATILE UBS_UINT32	Version:16;
  VOLATILE UBS_UINT32	DataLengthHi:8;
  VOLATILE UBS_UINT32	DataLengthLo:8;
  VOLATILE UBS_UINT32	ReservedG2:24;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
}   TLS_HMACContext_t, *TLS_HMACContext_pt;

/* SSL/TLS DES Context */
typedef struct SSL_CryptoContext_s {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short       CryptoFlag;
  VOLATILE unsigned short	Reserved;
#else
  VOLATILE unsigned short	Reserved;
  VOLATILE unsigned short       CryptoFlag;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE UBS_UINT32	CryptoKey1[2];
  VOLATILE UBS_UINT32	CryptoKey2[2];
  VOLATILE UBS_UINT32	CryptoKey3[2];
  VOLATILE UBS_UINT32	ComputedIV[2];
}   SSL_CryptoContext_t, *SSL_CryptoContext_pt;

/* ARC4  DES Context */


typedef struct ARC4_CryptoContext_s {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short StateInfo;
  VOLATILE unsigned short Reserved;
#else
  VOLATILE unsigned short Reserved;
  VOLATILE unsigned short StateInfo;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE unsigned char KeyState[UBSEC_ARC4_KEYSTATE_BYTES];
}   ARC4_CryptoContext_t, *ARC4_CryptoContext_pt;


/* Bit field definitions for state Information  */
#if (UBS_CPU_ATTRIBUTE == UBS_CRYPTONET_ATTRIBUTE)
#define ARC4_STATE_NULL_DATA   0x1000
#define ARC4_STATE_WRITEBACK   0x0800
#define ARC4_STATE_STATEKEY    0x0400
#else
#define ARC4_STATE_NULL_DATA   0x0010
#define ARC4_STATE_WRITEBACK   0x0008
#define ARC4_STATE_STATEKEY    0x0004
#endif


/* Pure Hash Context */
typedef struct Hash_Context_s {
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short CryptoFlag;
  VOLATILE unsigned short Reserved; 
#else
  VOLATILE unsigned short Reserved; 
  VOLATILE unsigned short CryptoFlag;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
}   Hash_Context_t, *Hash_Context_pt;

#endif /* UBSEC_582x_CLASS_DEVICE */

/*
 * Generic union to encompass all cipher context types
 */

typedef union CryptoContext_u {
  CipherContext_t Cipher;
#ifdef UBSEC_582x_CLASS_DEVICE
  SSL_HMACContext_t SSL_Mac;
  TLS_HMACContext_t TLS_HMac;
  SSL_CryptoContext_t SSL_Crypto;
  ARC4_CryptoContext_t ARC4_Crypto;
  Hash_Context_t Hash;
#endif
}   CryptoContext_t, *CryptoContext_pt;

typedef struct PacketContextUnaligned_s {
#ifdef UBSEC_582x_CLASS_DEVICE
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short cmd_structure_length;
  VOLATILE unsigned short operation_type; 
#else
  VOLATILE unsigned short operation_type; 
  VOLATILE unsigned short cmd_structure_length;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
#endif /* UBSEC_582x_CLASS_DEVICE */
  VOLATILE CryptoContext_t Context;
  VOLATILE UBS_UINT32  PhysicalAddress;
} PacketContextUnaligned_t;


#define PKTCONTEXT_ALIGNMENT 64 /* Boundary to which PacketContexts will be aligned. Must be power of 2 */

#if (SYS_CACHELINE_SIZE >= PKTCONTEXT_ALIGNMENT) 
  #define PKTCONTEXT_ALIGNMENT_PAD (PKTCONTEXT_ALIGNMENT - (sizeof(PacketContextUnaligned_t) & (PKTCONTEXT_ALIGNMENT-1)))
#else
  #undef PKTCONTEXT_ALIGNMENT_PAD
#endif

/***********************************************************************/
/* Hardware (DMA) version of above structure that is cacheline sized   */
/* (an integer multiple of SYS_CACHELINE_SIZE bytes in length).        */
/* Any changes made to either structure must be mirrored in the other  */
/***********************************************************************/

typedef struct PacketContext_s {
#ifdef UBSEC_582x_CLASS_DEVICE
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short cmd_structure_length;
  VOLATILE unsigned short operation_type; 
#else
  VOLATILE unsigned short operation_type; 
  VOLATILE unsigned short cmd_structure_length;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
#endif /* 582x conditional */
  VOLATILE CryptoContext_t Context;
  VOLATILE UBS_UINT32  PhysicalAddress;
#if (SYS_CACHELINE_SIZE >= PKTCONTEXT_ALIGNMENT) 
  /***********************************************************************/
  /**** If PacketContextUnaligned_t is cacheline sized, the following ****/
  /**** pad array will have a subscript of zero. Under this condition ****/
  /**** the following line should be commented out.                   ****/ 
  unsigned char pad[PKTCONTEXT_ALIGNMENT_PAD];                        /***/
  /***********************************************************************/
#endif
} PacketContext_t,*PacketContext_pt;

/* Size of command fields. */
#define CIPHER_CONTEXT_SIZE (sizeof(CipherContext_t)+4) 
#define SSLMAC_CONTEXT_SIZE (sizeof(SSL_MACContext_t)+4)
#define SSLCRYPTO_CONTEXT_SIZE (sizeof(SSL_CryptoContext_t)+4) 
#define TLSHMAC_CONTEXT_SIZE (sizeof(TLS_HMACContext_t)+4) 
#define ARC4_CONTEXT_SIZE (sizeof(ARC4_CryptoContext_t)+4)
#define HASH_CONTEXT_SIZE (sizeof(Hash_Context_t)+4)

/*
 * Crypto operation types.
 */
/* #ifdef UBS_LITTLE_ENDIAN */
#if (UBS_CPU_ATTRIBUTE == UBS_CRYPTONET_ATTRIBUTE)
#define OPERATION_IPSEC      0x0000
#define OPERATION_SSL_HMAC   0x0001
#define OPERATION_SSL_MAC    0x0001
#define OPERATION_TLS_HMAC   0x0002
#define OPERATION_SSL_CRYPTO 0x0003
#define OPERATION_ARC4       0x0004
#define OPERATION_HASH       0x0005
#else
#define OPERATION_IPSEC      0x0000
#define OPERATION_SSL_HMAC   0x0100
#define OPERATION_SSL_MAC    0x0100
#define OPERATION_TLS_HMAC   0x0200
#define OPERATION_SSL_CRYPTO 0x0300
#define OPERATION_ARC4       0x0400
#define OPERATION_HASH       0x0500
#endif

#define NULL_PACKET_CONTEXT (PacketContext_pt) 0



/*--------------------------------------------------------------------------
 * Data Buffer Chain element
 *
 *	An element in the linked list of data buffers that makes up a 
 *	Packet
 *
 *	DataAddress				pointer to this buffer's data
 *	pNext					pointer to the next element
 *	DataLength				size in bytes of this element
 *	Reserved				reserved
 */

typedef struct DataBufChain_s	{
  VOLATILE UBS_UINT32 	DataAddress;           /* Physical address. */
  VOLATILE UBS_UINT32	pNext;  /* Physical address. */
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short  DataLength;
  VOLATILE unsigned short  Reserved;
#else
  VOLATILE unsigned short  Reserved;
  VOLATILE unsigned short  DataLength;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
} DataBufChain_t, *DataBufChain_pt;

/* This is the same structure as above but with
   a physical address location for efficiency */
typedef struct DataBufChainList_s	{
  VOLATILE UBS_UINT32 	DataAddress;           /* Physical address. */
  VOLATILE UBS_UINT32	pNext;  /* Physical address. */
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short  DataLength;
  VOLATILE unsigned short  Reserved;
#else
  VOLATILE unsigned short  Reserved;
  VOLATILE unsigned short  DataLength;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE UBS_UINT32 PhysicalAddress;
} DataBufChainList_t, *DataBufChainList_pt;

#define NULL_DATA_CHAIN ((DataBufChainList_pt) 0)

/*
 * Packet
 */
typedef struct Packet_s {
	 VOLATILE UBS_UINT32      PacketContextBuffer;
	 VOLATILE DataBufChain_t     InputHead;
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
	 VOLATILE unsigned short     Reserved;
	 VOLATILE unsigned short     PacketLength;
#else
	 VOLATILE unsigned short     PacketLength;
	 VOLATILE unsigned short     Reserved;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
	 VOLATILE DataBufChain_t     OutputHead;
	} Packet_t, *Packet_pt;

/*
 * Master Command Record structure
 *
 * 	The master command record is the structure that gets passed
 *	to the chip.  
 *
 *	NumberOfPackets	number of packets in this MCR
 *	Flags			completion status from chip
 *	PacketArray		array of packet structures
 */

typedef struct CallBackInfo_s {
  void(*CompletionCallback)(unsigned long Context,ubsec_Status_t Result);
  unsigned long	   		 CommandContext;
  }   CallBackInfo_t, *CallBackInfo_pt;

#if (UBS_CPU_ATTRIBUTE == UBS_CRYPTONET_ATTRIBUTE)
#define MCR_FLAG_COMPLETION    0x0001		/* bit [0] indicates done */
#define MCR_INTERRUPT_SUPPRESS 0x8000		/* bit [15] suppresses interrupt for 5821 MCR */
#else
#define MCR_FLAG_COMPLETION 0xff00		/* bit [8] indicates done */
#define MCR_INTERRUPT_SUPPRESS 0x0080		/* bit [7] suppresses interrupt for 5821 MCR */
#endif

/* MCR_DMA_MEM_OFFSET forces the PacketArray list to start on a 32-byte boundary.  */
/* This assumes that each MCR is aligned to at least a 32-byte boundary. That      */
/* means that OS_AllocateDMAMemory() must return physical memory aligned to        */
/* at least 32-byte boundaries (32, 64, 96 etc.) as defined by SYS_CACHELINE_SIZE. */
#if (SYS_CACHELINE_SIZE && !(SYS_CACHELINE_SIZE & 0x1F)) 
  #define MCR_DMA_MEM_OFFSET (sizeof(Packet_t) - 2*sizeof(unsigned short)) 
#else
  #define MCR_DMA_MEM_OFFSET 0
#endif

typedef struct MasterCommandUnaligned_s {
#if (SYS_CACHELINE_SIZE && !(SYS_CACHELINE_SIZE & 0x1F)) 
  unsigned char dma_pad[MCR_DMA_MEM_OFFSET];
#endif
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short	NumberOfPackets;
  VOLATILE unsigned short	Flags;
#else
  VOLATILE unsigned short	Flags;
  VOLATILE unsigned short	NumberOfPackets;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE Packet_t	PacketArray[MCR_MAXIMUM_PACKETS];
	  /*
	   * The following fields are not part of the MCR but are present here
	   * for easy access. 
	   */
  OS_MemHandle_t MCRMemHandle; /* Memory handle to use for current MCR */
  UBS_UINT32 MCRMemHandleOffset; /* Used with handle for OS_SyncTo calls */
  VOLATILE CallBackInfo_t CompletionArray[MCR_MAXIMUM_PACKETS];
  VOLATILE PacketContext_t  *ContextList;
  VOLATILE KeyContext_t  *KeyContextList[MCR_MAXIMUM_PACKETS];
  OS_MemHandle_t  ContextListHandle[MCR_MAXIMUM_PACKETS];
#ifdef STATIC_F_LIST
  VOLATILE DataBufChainList_t  InputFragmentList[MCR_MAXIMUM_PACKETS*UBSEC_MAX_FRAGMENTS];
  VOLATILE DataBufChainList_t  OutputFragmentList[MCR_MAXIMUM_PACKETS*UBSEC_MAX_FRAGMENTS];
#else
  VOLATILE DataBufChainList_t  *InputFragmentList;
  VOLATILE DataBufChainList_t  *OutputFragmentList;
  OS_MemHandle_t InputFragmentListHandle;
  OS_MemHandle_t OutputFragmentListHandle;
#endif
  VOLATILE UBS_UINT32 MCRPhysicalAddress;
  VOLATILE UBS_UINT32 MCRState;
  VOLATILE struct MasterCommand_s *pNextMCR; /* Pointer to next in list. */
  UBS_UINT32 Index;
} MasterCommandUnaligned_t;

#define MCR_ALIGNMENT 32 /* Boundary to which MCRs will be aligned. Must be power of 2 */

#if (SYS_CACHELINE_SIZE >= MCR_ALIGNMENT) 
  #define MCR_ALIGNMENT_PAD (MCR_ALIGNMENT - (sizeof(MasterCommandUnaligned_t) & (MCR_ALIGNMENT-1)))
#else
  #undef MCR_ALIGNMENT_PAD 
#endif

/***********************************************************************/
/* Hardware (DMA) version of above structure that is 'alignably' sized */
/* (an integer multiple of 32 bytes in length).                        */
/* Any changes made to either structure must be mirrored in the other  */
/***********************************************************************/

typedef struct MasterCommand_s {
#if (SYS_CACHELINE_SIZE && !(SYS_CACHELINE_SIZE & 0x3)) 
  unsigned char dma_pad[MCR_DMA_MEM_OFFSET];
#endif
#if (UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN)
  VOLATILE unsigned short	NumberOfPackets;
  VOLATILE unsigned short	Flags;
#else
  VOLATILE unsigned short	Flags;
  VOLATILE unsigned short	NumberOfPackets;
#endif /* UBS_CRYPTONET_ATTRIBUTE conditional */
  VOLATILE Packet_t	PacketArray[MCR_MAXIMUM_PACKETS];
	  /*
	   * The following fields are not part of the MCR but are present here
	   * for easy access. 
	   */
  OS_MemHandle_t MCRMemHandle; /* Memory handle to use for current MCR */
  UBS_UINT32 MCRMemHandleOffset; /* Used with handle for OS_SyncTo calls */
  VOLATILE CallBackInfo_t CompletionArray[MCR_MAXIMUM_PACKETS];
  VOLATILE PacketContext_t  *ContextList;
  VOLATILE KeyContext_t  *KeyContextList[MCR_MAXIMUM_PACKETS];
  OS_MemHandle_t  ContextListHandle[MCR_MAXIMUM_PACKETS];
#ifdef STATIC_F_LIST
  VOLATILE DataBufChainList_t  InputFragmentList[MCR_MAXIMUM_PACKETS*UBSEC_MAX_FRAGMENTS];
  VOLATILE DataBufChainList_t  OutputFragmentList[MCR_MAXIMUM_PACKETS*UBSEC_MAX_FRAGMENTS];
#else
  VOLATILE DataBufChainList_t  *InputFragmentList;
  VOLATILE DataBufChainList_t  *OutputFragmentList;
  OS_MemHandle_t InputFragmentListHandle;
  OS_MemHandle_t OutputFragmentListHandle;
#endif
  VOLATILE UBS_UINT32 MCRPhysicalAddress;
  VOLATILE UBS_UINT32 MCRState;
  VOLATILE struct MasterCommand_s *pNextMCR; /* Pointer to next in list. */
  UBS_UINT32 Index;
#if (SYS_CACHELINE_SIZE >= MCR_ALIGNMENT) 
  /***********************************************************************/
  /**** If sizeof(MasterCommandUnaligned_t)%32 is zero, the following ****/
  /**** pad array will have a subscript of zero. Under this condition ****/
  /**** the following line should be commented out.                   ****/ 
  unsigned char pad[MCR_ALIGNMENT_PAD];                                /**/
  /***********************************************************************/
#endif
} MasterCommand_t, *MasterCommand_pt;

#define NULL_MASTER_COMMAND (MasterCommand_pt) 0

#define MCR_STATE_FREE   0x00 /* Not in use. */
#define MCR_STATE_ACTIVE   0x01 /* Packets in the MCR. */
#define MCR_STATE_PUSHED 0x02 /* Pushed onto device. */

#define UBSEC_IS_SSL_DEVICE(pDevice) ( (pDevice->DeviceID==BROADCOM_DEVICE_ID_5820) || \
(pDevice->DeviceID==BROADCOM_DEVICE_ID_5821) || \
(pDevice->DeviceID==BROADCOM_DEVICE_ID_5822) )

#endif /*  _UBSCRYPT_H_ */





