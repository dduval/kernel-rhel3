
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
 * ubsextrn.h:  ubsec internal function prototype and variable declarations
 */

/*
 * Revision History:
 *
 * 09/xx/99 SOR Created
 * 10/09/01 SRM 64 bit port.
 */

#ifndef _UBSEXTRN_H_
#define _UBSEXTRN_H_

/*
 * Function prototype defintions.
 */
extern void completion_handler( void* p_void );
extern DeviceInfo_pt AllocDeviceInfo(unsigned long DeviceID,int NumberOfCipherMCRs,int NumberOfKeyMCRs,OS_DeviceInfo_t OSContext);
extern void  FreeDeviceInfo(DeviceInfo_pt pDevice );
extern ubsec_Status_t SetupInputFragmentList(MasterCommand_pt  pMCR,
		       Packet_pt pPacket,
		       int NumSource,
		       ubsec_FragmentInfo_pt SourceFragments);
extern ubsec_Status_t SetupOutputFragmentList(MasterCommand_pt  pMCR,
			Packet_pt pPacket,
			int NumFrags,
			ubsec_FragmentInfo_pt DestinationFragments,
			ubsec_FragmentInfo_pt pExtraFragment);
extern void revBytes(void *st, int len);
extern UBS_UINT32  rol(UBS_UINT32 x, int n);
extern void copywords(UBS_UINT32 *out, UBS_UINT32 *in,int num);
extern void InitSHA1State(ubsec_HMAC_State_pt HMAC_State,unsigned char *HashBlock);
extern void InitMD5State(ubsec_HMAC_State_pt HMAC_State,unsigned char *HashKey);
extern int WaitForCompletion(DeviceInfo_pt pDevice,unsigned long blockus,unsigned long MCRListIndex);
extern void Dump_Registers(DeviceInfo_pt pDevice,int dbg_flag);
extern void ubsec_DumpDeviceInfo(ubsec_DeviceContext_t Context);
extern void PushMCR(DeviceInfo_pt pDevice);
extern void FlushDevice(DeviceInfo_pt pDevice,ubsec_Status_t Status,unsigned int type);
extern int dump_MCR(DeviceInfo_pt pDevice,MasterCommand_pt pMCR,unsigned long MCRListIndex);
extern ubsec_Status_t DH_SetupPublicParams(MasterCommand_pt pMCR, ubsec_DH_Params_pt pDHParams);
extern ubsec_Status_t DH_SetupSharedParams(MasterCommand_pt pMCR, ubsec_DH_Params_pt pDHParams);
extern ubsec_Status_t RSA_SetupPublicParams(MasterCommand_pt pMCR, ubsec_RSA_Params_pt pRSAParams);
extern ubsec_Status_t RSA_SetupPrivateParams(MasterCommand_pt pMCR, ubsec_RSA_Params_pt pRSAParams);
extern ubsec_Status_t DSA_SetupSignParams(MasterCommand_pt pMCR, ubsec_DSA_Params_pt pDSAParams);
extern ubsec_Status_t DSA_SetupVerifyParams(MasterCommand_pt pMCR, ubsec_DSA_Params_pt pDSAParams);
extern MasterCommand_pt GetFreeMCR(  DeviceInfo_pt pDevice,int MCRList,ubsec_Status_t *Status);
extern void KeyFinishResult(unsigned long Context,ubsec_Status_t Result);

/*
 * External variable definitions.
 */

#endif  /* _UBSEXTRN_H_ */

