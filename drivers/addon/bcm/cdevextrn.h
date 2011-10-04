
/*
 *  Broadcom Cryptonet Driver software is distributed as is, without any warranty
 *  of any kind, either express or implied as further specified in the GNU Public
 *  License. This software may be used and distributed according to the terms of
 *  the GNU Public License.
 *
 * Cryptonet is a registered trademark of Broadcom Corporation.
 */
/******************************************************************************
 *
 *  Copyright 2000
 *  Broadcom Corporation
 *  16215 Alton Parkway
 *  PO Box 57013
 *  Irvine CA 92619-7013
 *
 *****************************************************************************/
/* 
 * Broadcom Corporation uBSec SDK 
 */
/*
 * Revision History:
 *
 * May 2000 SOR/JTT Created.
 */
/*
 * Character device externs header file.
 * 10/09/2001 SRM 64 bit port
 */

#ifndef __CDEVEXTRN_H__
#define __CDEVEXTRN_H__

extern int InitDevices(int NumberOfCryptoMCRs,int NumberOfKeyMCRs);
extern int init_keyif(void);
extern void shutdown_keyif(void);
extern int init_mathif(void);
extern void shutdown_mathif(void);
extern int init_cryptoif(void);
extern void shutdown_cryptoif(void);
extern int init_rngif(void);
extern void shutdown_rngif(void);
extern int init_arc4if(void);
extern void shutdown_arc4if(void);

int ubsec_keysetup(ubsec_DeviceContext_t pContext,
	       ubsec_key_io_t *pKeyIOInfo);
extern int ubsec_math(ubsec_DeviceContext_t pContext,
	   ubsec_math_io_t *pIOInfo);
extern int ubsec_rng(ubsec_DeviceContext_pt pContext, ubsec_rng_io_t *rng_cmd);
extern int ubsec_tlsmac(ubsec_DeviceContext_t pContext, ubsec_tlsmac_io_t *tls_cmd);
extern int ubsec_sslmac(ubsec_DeviceContext_t pContext, ubsec_sslmac_io_t *ssl_cmd);
extern int ubsec_hash(ubsec_DeviceContext_t pContext, ubsec_hash_io_t *hash_cmd);
extern int ubsec_sslcipher(ubsec_DeviceContext_t pContext, 
			   ubsec_sslcipher_io_t *ssl_cmd, unsigned int features);
extern int ubsec_sslarc4(ubsec_DeviceContext_t pContext, ubsec_arc4_io_t *arc4_cmd);
extern int ubsec_dvt_handler(void *context, void* parameters);
extern int obsolete_chipinfo(ubsec_DeviceContext_t pContext, linux_chipinfo_io_t *ci_cmd);
extern int ubsec_chipinfo(ubsec_DeviceContext_t pContext, ubsec_chipinfo_io_t *ci_cmd);
extern int DumpDeviceInfo(PInt pm);
extern int TestDevices(PInt pm);
extern int FailDevices(PInt pm);
extern int GetHardwareVersion(PInt pm);
extern int TestDevice(int SelectedDevice);
extern void ubsec_DumpDeviceInfo(ubsec_DeviceContext_t Context);
extern int SetupFragmentList(ubsec_FragmentInfo_pt Frags, unsigned char *packet,int packet_len);

int
KeyCommandCopyin(unsigned long Command, 
		 ubsec_KeyCommandParams_pt pSRLParams, 
		 ubsec_KeyCommandParams_pt pDHIOparams,
		 unsigned char *KeyLoc,
		 ubsec_FragmentInfo_pt InputKeyInfo);

int
KeyCommandCopyout(unsigned long Command, 
		 ubsec_KeyCommandParams_pt pSRLParams, 
		 ubsec_KeyCommandParams_pt pIOparams,
		 unsigned char *KeyLoc);

int ubsec_keysetup_Diffie_Hellman(unsigned long command, 
	ubsec_KeyCommandParams_pt pIOparams, ubsec_KeyCommandParams_pt pSRLparams, 	
	unsigned char *KeyLoc);

int ubsec_keysetup_RSA(unsigned long command, 
	ubsec_KeyCommandParams_pt pIOparams, ubsec_KeyCommandParams_pt pSRLparams, 	
	unsigned char *KeyLoc);
int ubsec_keysetup_DSA(unsigned long command,
	ubsec_KeyCommandParams_pt pIOparams, ubsec_KeyCommandParams_pt pSRLparams, 	
	unsigned char *KeyLoc, ubsec_FragmentInfo_pt InputKeyInfo);

extern void tv_sub(struct timeval *out, struct timeval *in);
extern unsigned long stop_time(struct timeval *tv_stop);
extern void start_time(struct timeval *tv_start)  ;

#ifndef LINUX2dot2
extern int Gotosleep(wait_queue_head_t *WaitQ);
extern void Wakeup(wait_queue_head_t *WaitQ);
#else
extern int Gotosleep(struct wait_queue **WaitQ);
extern void Wakeup(struct wait_queue **WaitQ);
#endif

extern int do_encrypt(ubsec_DeviceContext_t pContext,ubsec_io_t *at,
		      unsigned int features);
extern int Selftest(ubsec_DeviceContext_t pContext,struct pci_dev* pDev);

extern void CmdCompleteCallback(unsigned long CallBackContext,ubsec_Status_t Result);

#endif /* __CDEVEXTRN_H__ */

