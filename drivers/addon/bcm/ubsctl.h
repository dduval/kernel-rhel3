
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
 * ubsctl.c: Ubsec device control macro definitions.
 */

/*
 * Revision History:
 *
 * 09/15/2000 SOR Created.
 * 07/16/2001 RJT Added support for BCM5821
 */

#ifndef _UBSCTL_H_
#define _UBSCTL_H_

enum ubsDmaRegistersNumber_e {
       	dmaMCR1=0,
	dmaControl,
	dmaStatus,
	dmaError,
	dmaMCR2
};


/*
 *
 * UBSEC PCI configuration space values
 *
 */

/*
 * DMA Control and Status Registers (32 bits each index)
 */
#define  MCR_ADDRESS  		0x0
/*  Writing the address of a valid Master Command Record to this
	//	register causes processing of the packtes within that record
	//	to begin.  This register must only be written when the
	//	'MCR1_FULL' bit of the DMA Status register is '0'.
*/
#define DMA_CONTROL			0x1
#if (UBS_CPU_ATTRIBUTE == UBS_LITTLE_ENDIAN)
#define UBSEC_RESET		0x80000000
#define MCR2INT_ENABLE		0x40000000
#define MCR1INT_ENABLE		0x20000000
#define OUTPUT_FRAG_MODE	0x10000000
#define UBS_LITTLE_ENDIAN_MODE	0x0C000000  /* (do both bits at once) */
#define UBS_BIG_ENDIAN_MODE	0x04000000  /* (do both bits at once) */
#define DMAERR_ENABLE		0x02000000
#define RNG_MODE_16		0x01800000
#define RNG_MODE_08		0x01000000
#define RNG_MODE_04		0x00800000
#define RNG_MODE_01		0x00000000
#define SW_NORM_EN              0x00400000
#else
#define UBSEC_RESET		0x00000080
#define MCR2INT_ENABLE		0x00000040
#define MCR1INT_ENABLE		0x00000020
#define OUTPUT_FRAG_MODE	0x00000010
#define UBS_LITTLE_ENDIAN_MODE	0x0000000C  /* (do both bits at once) */
#define UBS_BIG_ENDIAN_MODE	0x00000004  /* (do both bits at once) */
#define DMAERR_ENABLE		0x00000002
#define RNG_MODE_16		0x00008001
#define RNG_MODE_08		0x00000001
#define RNG_MODE_04		0x00008000
#define RNG_MODE_01		0x00000000
#define SW_NORM_EN              0x00004000 

#endif
/*  bits	    purpose
//  MCRnINT_ENABLE --           Enable MCR completion interrupt (def=0)
//  Output Fragment Mode --    '0' means get output fragment
//				size from data buffer length entry
//				'1' means get output fragment size from
//				low order bits of this register (def=0)
//  UBS_X_ENDIAN_MODE fields are comprised of the following two bits:
//  LE_CRYPTONET --             Little endian mode, 32-bit words.  '0' means
//				big endian data in DMA memory.  '1' means little
//				endian data in DMA memory. Must be set to '1' for
//                              BCM580x and BCM5820 devices (def=1).
//  NORMAL_PCI --               '0' means all PCI bus master data will be
//                              internally swapped by the CryptoNet chip.
//				'1' means industry standard PCI bus endianess.
//				Must be set to '1' for BCM580x and BCM5820 
//                              devices. Normally set to '1' (def=1).
//  DMAERR_ENABLE --            Enable DMA Error interrupt (def=0)
//  Output Fragment Size --     The size, in bytes of output
//				fragments.  Only used if Output Frag
//				Mode is enabled.
*/
#define DMA_STATUS			0x2
#if (UBS_CPU_ATTRIBUTE == UBS_LITTLE_ENDIAN)
#define MCR_BUSY			0x80000000
#define MCR1_FULL			0x40000000
#define MCR1_DONE			0x20000000
#define DMA_ERROR			0x10000000
#define MCR2_FULL			0x08000000
#define MCR2_DONE			0x04000000
#define MCR1_ALL_EMPTY			0x02000000
#define MCR2_ALL_EMPTY			0x01000000
#else
#define MCR_BUSY			0x00000080
#define MCR1_FULL			0x00000040
#define MCR1_DONE			0x00000020
#define DMA_ERROR			0x00000010
#define MCR2_FULL			0x00000008
#define MCR2_DONE			0x00000004
#define MCR1_ALL_EMPTY			0x00000002
#define MCR2_ALL_EMPTY			0x00000001
#endif
/*  bits	purpose
//  MCR_BUSY --	        If set, master access is in progress.
//  MCRn_FULL --        MCRn Address register is full.  When
//			'1', do not write to MCRn Address reg.
//  MCRn_DONE -- 	Completion status of MCRn.  This bit is
//			set regardless of the MCRnINT_EN completion
//			interrupt enable bit.  This bit is 
//			sticky, and is reset to zero by writing
//			a '1'.
//  DMA_ERROR --	DMA Error status.  This bit is set
//			regardless of the DMAERR_EN interrupt
//			enable bit.  This bit is sticky, and is
//			reset to zero by writing a '1'.
//  MCRn_ALL_EMPTY -- 	If set, indicates that chip completed all
//                      MCRs that were written to MCRn, i.e. there
//                      are no pending operations. This bit is
//			set regardless of the MCRnINT_EN completion
//			interrupt enable bit.  This bit is 
//			sticky, and is reset to zero by writing
//			a '1' to either the MCRn_ALL_EMPTY bit
//                      or to the associated MCRn_DONE bit.
*/
#define  DMA_ERROR_ADDRESS      0x3
/*  bits	purpose
//  2-31	Address of master access that resulted in a PCI fault.
//		The address points to a 32bit word.
//  1		1 = fault on a read.  0 = fault on a write.
*/


/*
 * Number of device registers
 */
#define UBSEC_CRYPTO_DEVICE_REGISTERS 4
#define UBSEC_KEY_DEVICE_REGISTERS 5

#ifdef  UBSEC_PKEY_SUPPORT
#define UBSEC_MAXREGISTERS 5
#else
#define UBSEC_MAXREGISTERS 4
#endif

/*
 * Interrupt ack/enable masks. Different for each class of device.
 */
#define UBSEC_CRYPTO_DEVICE_IENABLE_MASK (MCR1INT_ENABLE | DMAERR_ENABLE)
#define UBSEC_CRYPTO_DEVICE_IACK_MASK    (MCR1_DONE | DMA_ERROR)
#define UBSEC_KEY_DEVICE_IENABLE_MASK (MCR1INT_ENABLE | MCR2INT_ENABLE | DMAERR_ENABLE)
#define UBSEC_KEY_DEVICE_IACK_MASK    (MCR1_DONE | MCR2_DONE | DMA_ERROR) 

/*
 * Read configuration control register
 */
#define UBSEC_READ_CONFIG(pDevice)  \
  OS_IOMemRead32(pDevice->ControlReg[dmaControl])

/*
 * Write configuration control register. Because we share interrupts with another device and
 * there is no way to stop the int mask we need to set the interrupt enable status
 * accordingly
 */
#define UBSEC_WRITE_CONFIG(pDevice,value)  			   \
  { pDevice->IRQEnabled=((value) & (pDevice->IntEnableMask)); \
    OS_IOMemWrite32(pDevice->ControlReg[dmaControl],value); }


#define UBSEC_IRQ_ENABLED(pDevice) \
           ((UBSEC_READ_CONFIG(pDevice) & pDevice->IntEnableMask) != 0)

/*
 * Reset the security accelerator hardware (chip). 
 */
#define UBSEC_RESET_DEVICE(pDevice) \
  { UBSEC_WRITE_CONFIG(pDevice,(UBSEC_READ_CONFIG(pDevice) | UBSEC_RESET));     \
    OS_Waitus(100000);                                                          \
    UBSEC_WRITE_CONFIG(pDevice,pDevice->ResetConfig); }

/*
 * Enable device interrupts. 
 */
#define UBSEC_ENABLE_INT(pDevice) \
  UBSEC_WRITE_CONFIG(pDevice,(UBSEC_READ_CONFIG(pDevice) | (pDevice->IntEnableMask)))

/*
 * Disable device interrupts. Because we share interrupts with another device and
 * there is no way to stop the int mask we need to set the interrupt enable status
 * accordingly
 */
#define UBSEC_DISABLE_INT(pDevice)\
  UBSEC_WRITE_CONFIG(pDevice,(UBSEC_READ_CONFIG(pDevice)&~(pDevice->IntEnableMask))) 


/*
 * Acknowledge device status
 */
#define UBSEC_ACK_INT(pDevice)\
OS_IOMemWrite32(pDevice->ControlReg[dmaStatus],OS_IOMemRead32(pDevice->ControlReg[dmaStatus]) & (pDevice->IntAckMask)) 


/*
 * Hardware marker (for logic analyzer), used for code profiling
 */
#ifdef UBSEC_HW_PROFILE_MARKER_ENABLE
#define UBSEC_HW_PROFILE_MARKER(pDevice,marker) OS_IOMemWrite32(pDevice->ControlReg[dmaError],marker) 
#else
#define UBSEC_HW_PROFILE_MARKER(pDevice,marker) 
#endif


/*
 * Macro that determines whether or not external byteswap hardware is in use.
 * This condition is inferred when the CryptoNet chip is configured for an  
 * endianess mode that is different from that defined by UBS_CRYPTONET_ATTRIBUTE.
 * CryptoNet endianess is determined by the state of the UBS_LITTLE_ENDIAN_MODE 
 * bits in the CryptoNet DMA_CONTROL register. This macro is used in ubstest.c
 */
#define UBSEC_EXTERNAL_BYTESWAP_HW(pDevice) \
(((UBS_CRYPTONET_ATTRIBUTE == UBS_BIG_ENDIAN) && ((pDevice->ResetConfig & UBS_LITTLE_ENDIAN_MODE) == UBS_LITTLE_ENDIAN_MODE)) || \
 ((UBS_CRYPTONET_ATTRIBUTE == UBS_LITTLE_ENDIAN) && ((pDevice->ResetConfig & UBS_LITTLE_ENDIAN_MODE) == UBS_BIG_ENDIAN_MODE)) )



/*
 * Acknowledge device status
 */
#define UBSEC_ACK_CONDITION(pDevice,Condition)\
OS_IOMemWrite32(pDevice->ControlReg[dmaStatus],OS_IOMemRead32(pDevice->ControlReg[dmaStatus]) & (Condition))

/*
 * Read device status
 */
#define UBSEC_READ_STATUS(pDevice) \
  OS_IOMemRead32(pDevice->ControlReg[dmaStatus])

/*
 * Write MCR address to device.
 */
#define UBSEC_WRITE_MCR(pDevice,pMCR,ListIndex) \
  OS_IOMemWrite32(pDevice->ControlReg[(ListIndex) ? dmaMCR2 : dmaMCR1 ],pMCR->MCRPhysicalAddress);


#endif /*  _UBSCTL_H_ */


