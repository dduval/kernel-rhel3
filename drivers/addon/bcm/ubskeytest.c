
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
 * ubskeytest.c: Ubsec Key Bring up Diagnostics routines
 */

/*
 * Revision History:
 *
 * 02/05/2001 PW Created.
 * 04/23/2001 RJT Added support for CPU-DMA memory synchronization
 * 10/09/2001 SRM 64 bit port
 */


#include "ubsincl.h"

#ifndef ASYNC
static void callback(unsigned long PacketContext,ubsec_Status_t Result);
#endif

/* computing Y=G**X/N */

/*  1. base DHP g - G */
static UBS_UINT32 G[TEST_KEY_BLOCK_SIZE/4]= {
  0x62A13103, 0xC095F9CE, 0xF2434441, 0x898B31C8,
  0xEAF84EFB, 0x0D3C831F, 0x6E1C8601, 0xE54D28EE,
  0xBE7244F0, 0xAE38B60B, 0x8711944A, 0x43F30F4C,
  0xA29FF11D, 0x6D2E9F77, 0x46DEBA6B, 0xB3D3C7A2
};

/* 2. Mod DHP n - N */
static UBS_UINT32 N[TEST_KEY_BLOCK_SIZE/4]= {
  0x99286689, 0x748EB488, 0xD146DD17, 0xCF124E36,
  0xA9C4BCDD, 0xC9571116, 0x4BEEE715, 0x5B1DCC42,
  0x6AAB6260, 0xCC4CCF11, 0xCB631589, 0x12CD8785,
  0xBF512BBC, 0x70197EA2, 0x0AE59A21, 0xB70636E2
};

/* 3. DHP x - UserX  */
static UBS_UINT32 UserX[TEST_KEY_BLOCK_SIZE/4]= {
  0xE910C7CA, 0x225387FF, 0xA8EB0590, 0xAD40FA33,
  0x85A46F95, 0x63E5D86F, 0xFF2F4AF8, 0x2D9999A8,
  0xA3CDB938, 0x1D4574DA, 0x69BF06C8, 0xBACF2C6D,
  0x4E997E30, 0xC5EB1F75, 0x8C546D95, 0xF6020408
};

/* 4.  result DHP y - Y */
static UBS_UINT32 Y[TEST_KEY_BLOCK_SIZE/4]= {
  0x0377E5D2, 0x142CEF26, 0xF9F9023C, 0xE7ED8561, 
  0x153459E1, 0x26BE6B17, 0x47F62506, 0xD272E194, 
  0xAD15BFA0, 0x2344F145, 0x3E671269, 0x8CFB6945, 
  0x7C6070F8, 0x039BF0A2, 0xEE99CE09, 0x31FC14FC 
};


/* These will be used as type OS_MemHandle_t. However, some systems 
	define OS_MemHandle_t as void*; some compilers don't like to use
	void* in declarations */
static unsigned char *G_Handle, *N_Handle, *UserX_Handle, *Y_Handle, *X_Handle;
static ubsec_KeyCommandInfo_pt Kcmd;

static ubsec_Status_t CompareKeyResults(void);
static void KeyCallback(unsigned long PacketContext,ubsec_Status_t Result);


/*
 * ubsec_TestKeyDevice
 */
ubsec_Status_t
ubsec_TestKeyDevice(ubsec_DeviceContext_t Ubsec_Context,void(*CompletionCallback)(unsigned long PacketContext,ubsec_Status_t Result),unsigned long CompletionContext)
{
  unsigned char* pG;
  unsigned char* pN;
  unsigned char* pX;
  unsigned char* pUserX;
  unsigned char* pY;
  int num_packets=1;
  int i, Status;
  UBS_UINT32 SaveConfig;
  DeviceInfo_pt pDevice=(DeviceInfo_pt)Ubsec_Context;

  Dbg_Print(DBG_TEST,("ubskeytest: key self-test..\n"));
  /* 
   * First reset the device and complete any pending requests.
   */
  ubsec_ResetDevice(Ubsec_Context);

  /* First allocate and clear the Key Command area. */
  if((Kcmd = (ubsec_KeyCommandInfo_t *)OS_AllocateMemory(sizeof(ubsec_KeyCommandInfo_t))) == NULL) {
    return(UBSEC_STATUS_NO_RESOURCE);
  }
  RTL_MemZero(Kcmd, sizeof(ubsec_KeyCommandInfo_t));

  if((G_Handle = (unsigned char*)OS_AllocateDMAMemory(pDevice,TEST_KEY_BLOCK_SIZE)) == NULL) {
    return(UBSEC_STATUS_NO_RESOURCE);
  }
  pG = (unsigned char*)OS_GetVirtualAddress(G_Handle);
  RTL_MemZero(pG, TEST_KEY_BLOCK_SIZE);

  if((N_Handle = OS_AllocateDMAMemory(pDevice,TEST_KEY_BLOCK_SIZE)) == NULL) {
    OS_FreeDMAMemory(G_Handle,TEST_KEY_BLOCK_SIZE);
    return(UBSEC_STATUS_NO_RESOURCE);
  }
  pN = (unsigned char*)OS_GetVirtualAddress(N_Handle);
  RTL_MemZero(pN, TEST_KEY_BLOCK_SIZE);

  if((UserX_Handle = OS_AllocateDMAMemory(pDevice,TEST_KEY_BLOCK_SIZE)) == NULL) {
    OS_FreeDMAMemory(N_Handle,TEST_KEY_BLOCK_SIZE);
    OS_FreeDMAMemory(G_Handle,TEST_KEY_BLOCK_SIZE);
    return(UBSEC_STATUS_NO_RESOURCE);
  }
  pUserX = (unsigned char*)OS_GetVirtualAddress(UserX_Handle);
  RTL_MemZero(pUserX, TEST_KEY_BLOCK_SIZE);

  if((Y_Handle = OS_AllocateDMAMemory(pDevice,TEST_KEY_BLOCK_SIZE)) == NULL) {
    OS_FreeDMAMemory(UserX_Handle,TEST_KEY_BLOCK_SIZE);
    OS_FreeDMAMemory(N_Handle,TEST_KEY_BLOCK_SIZE);
    OS_FreeDMAMemory(G_Handle,TEST_KEY_BLOCK_SIZE);
    return(UBSEC_STATUS_NO_RESOURCE);
  }
  pY = (unsigned char*)OS_GetVirtualAddress(Y_Handle);
  RTL_MemZero(pY, TEST_KEY_BLOCK_SIZE);

  if((X_Handle = OS_AllocateDMAMemory(pDevice,TEST_KEY_BLOCK_SIZE)) == NULL) {
    OS_FreeDMAMemory(Y_Handle,TEST_KEY_BLOCK_SIZE);
    OS_FreeDMAMemory(UserX_Handle,TEST_KEY_BLOCK_SIZE);
    OS_FreeDMAMemory(N_Handle,TEST_KEY_BLOCK_SIZE);
    OS_FreeDMAMemory(G_Handle,TEST_KEY_BLOCK_SIZE);
    return(UBSEC_STATUS_NO_RESOURCE);
  }
  pX = (unsigned char*)OS_GetVirtualAddress(X_Handle);
  RTL_MemZero(pX, TEST_KEY_BLOCK_SIZE);


Dbg_Print(DBG_TEST,("\npG, 	  pN, 	    pUserX,   pY, 	pX\n0x%x 0x%x 0x%x 0x%x 0x%x\n\n", pG, pN, pUserX, pY, pX));


  /* Copy the selftest data (TEST_KEY_BLOCK_SIZE chars * 8 bits/char) bits */

 #if defined(UBS_OVERRIDE_LONG_KEY_MODE)
  copywords((UBS_UINT32 *)pG,(UBS_UINT32 *)G,(TEST_KEY_BLOCK_SIZE+3)/4);
  copywords((UBS_UINT32 *)pN,(UBS_UINT32 *)N,(TEST_KEY_BLOCK_SIZE+3)/4);
  copywords((UBS_UINT32 *)pUserX,(UBS_UINT32 *)UserX,(TEST_KEY_BLOCK_SIZE+3)/4);
 #else 
  RTL_Memcpy(pG,G,TEST_KEY_BLOCK_SIZE);
  RTL_Memcpy(pN,N,TEST_KEY_BLOCK_SIZE);
  RTL_Memcpy(pUserX,UserX,TEST_KEY_BLOCK_SIZE);
 #endif /* UBS_OVERRIDE_LONG_KEY_MODE */

  /* Make sure DMA memory actually holds recent CPU-initialized buffer data */
  OS_SyncToDevice(UserX_Handle,0,TEST_KEY_BLOCK_SIZE);

  /* set up command parameters */
  Kcmd->Command= UBSEC_DH_PUBLIC;
  Kcmd->Parameters.DHParams.Y.KeyLength = 8*TEST_KEY_BLOCK_SIZE; /* in bits */
  Kcmd->Parameters.DHParams.N.KeyLength = 8*TEST_KEY_BLOCK_SIZE; /* in bits */
  Kcmd->Parameters.DHParams.G.KeyLength = 8*TEST_KEY_BLOCK_SIZE; /* in bits */

  /* need to setup X as well, as the chip write back the userX to this */
  Kcmd->Parameters.DHParams.X.KeyLength = 8*TEST_KEY_BLOCK_SIZE; /* in bits */
  Kcmd->Parameters.DHParams.UserX.KeyLength = 8*TEST_KEY_BLOCK_SIZE; /* in bits */

  Kcmd->Parameters.DHParams.Y.KeyValue = Y_Handle;         /* Memory handle */
  Kcmd->Parameters.DHParams.N.KeyValue = N_Handle;         /* Memory handle */
  Kcmd->Parameters.DHParams.G.KeyValue = G_Handle;         /* Memory handle */
  Kcmd->Parameters.DHParams.X.KeyValue = X_Handle;         /* Memory handle */ 
  Kcmd->Parameters.DHParams.UserX.KeyValue = UserX_Handle; /* Memory handle */ 

  /* (usually random) UserX provided manually (by S/W) */
  Kcmd->Parameters.DHParams.RNGEnable  = 0; 

  SaveConfig=UBSEC_READ_CONFIG(pDevice); 

  /* Are we completing sync or async */
  if (CompletionCallback) {
    pDevice->KeySelfTestCallBack=CompletionCallback;
    pDevice->KeySelfTestContext=CompletionContext;
    Kcmd->CompletionCallback=KeyCallback;
    Kcmd->CommandContext=(unsigned long) pDevice;
  }
  else {
    /* Turn off interrupts while waiting. */
    UBSEC_DISABLE_INT(pDevice);
  }

  switch (Status=ubsec_KeyCommand(Ubsec_Context,Kcmd,&num_packets) ) {
  case UBSEC_STATUS_SUCCESS:
    break;
  case UBSEC_STATUS_TIMEOUT:
    Dbg_Print(DBG_FATAL,( "ubsec:  Key SelfTest Command timeout\n"));
    ubsec_ResetDevice(Ubsec_Context);
    goto Free_DMAMemory_and_Return_Status;
    break;
  case UBSEC_STATUS_INVALID_PARAMETER:
    Dbg_Print(DBG_FATAL,( "ubsec:  Key SelfTest Failed Invalid parameter\n"));
    goto Free_DMAMemory_and_Return_Status;
    break;
  case UBSEC_STATUS_NO_RESOURCE:
    Dbg_Print(DBG_FATAL,( "ubsec:  Key SelfTest Failed No Resource\n"));
    goto Free_DMAMemory_and_Return_Status;
  default:
    Dbg_Print(DBG_FATAL,( "ubsec:  Key SelfTest Failure unknown %x\n",Status));
    goto Free_DMAMemory_and_Return_Status;
    break;
  }

  /* Are we completing sync or async */
  if (CompletionCallback) {
    return(Status);
  }

    
#ifndef BLOCK
 while ((Status=WaitForCompletion(pDevice,(UBS_UINT32)100000,UBSEC_KEY_LIST))
	== UBSEC_STATUS_SUCCESS); /* wait for them all to complete */
#endif

  /* Restore configuration status  */
  UBSEC_WRITE_CONFIG(pDevice,SaveConfig);

#ifdef BLOCK
  /* Invalid parameter means that no more left on pending queue */
  if (Status != UBSEC_STATUS_INVALID_PARAMETER) {
#else
  if (Status != UBSEC_STATUS_SUCCESS) {
#endif
    /* Keep error Status for return */;
  }
  else {
    /* Make sure CPU sees current state of recently DMA'd data buffers */
    OS_SyncToCPU(X_Handle,0,TEST_KEY_BLOCK_SIZE);
    OS_SyncToCPU(Y_Handle,0,TEST_KEY_BLOCK_SIZE);
    /* then get new Status from compare test */
    Status = CompareKeyResults();
  }

 Free_DMAMemory_and_Return_Status:

	OS_FreeDMAMemory(X_Handle,TEST_KEY_BLOCK_SIZE);
	OS_FreeDMAMemory(Y_Handle,TEST_KEY_BLOCK_SIZE);
	OS_FreeDMAMemory(UserX_Handle,TEST_KEY_BLOCK_SIZE);
	OS_FreeDMAMemory(N_Handle,TEST_KEY_BLOCK_SIZE);
	OS_FreeDMAMemory(G_Handle,TEST_KEY_BLOCK_SIZE);
	OS_FreeMemory(Kcmd,sizeof(ubsec_KeyCommandInfo_t));
	return (Status);

}

/*
 * Compare the results of the key operation.
 */
static ubsec_Status_t
CompareKeyResults(void)
{
  unsigned char *pX, *pY;
  int i;

  pY = (unsigned char*)OS_GetVirtualAddress(Y_Handle);
  pX = (unsigned char*)OS_GetVirtualAddress(X_Handle);

  /* cmp with golden data */
  /* print out a few bytes to see it for real */
  /* Y points to golden data, pY points to data written by the chip */
    Dbg_Print(DBG_TEST,("\nCompare ..."));
    Dbg_Print(DBG_TEST,("\npY %x %x %x %x %x %x %x %x ...",*(pY),*(pY+1),*(pY+2),*(pY+3),*(pY+4),*(pY+5),*(pY+6),*(pY+7))); 
    Dbg_Print(DBG_TEST,("\n Y %x %x %x %x %x %x %x %x ...\n\n",*(Y),*(Y+1),*(Y+2),*(Y+3),*(Y+4),*(Y+5),*(Y+6),*(Y+7))); 


#if defined(UBS_OVERRIDE_LONG_KEY_MODE)
  for (i=0 ; i<((TEST_KEY_BLOCK_SIZE+3)/4) ; i++) {
    if (BYTESWAPLONG(((UBS_UINT32*)pY)[i]) != ((UBS_UINT32*)Y)[i]) {
	Dbg_Print(DBG_TEST,("ubstest: key self-test results do not match\n"));
	return(UBSEC_STATUS_DEVICE_FAILED);
    }
  }
  for (i=0 ; i<((TEST_KEY_BLOCK_SIZE+3)/4) ; i++) {
    if (BYTESWAPLONG(((UBS_UINT32*)pX)[i]) != ((UBS_UINT32*)UserX)[i]) {
	Dbg_Print(DBG_TEST,("ubstest: key self-test results do not match\n"));
	return(UBSEC_STATUS_DEVICE_FAILED);
    }
  }
#else
  if (RTL_Memcmp(pY,Y,TEST_KEY_BLOCK_SIZE)) {
    Dbg_Print(DBG_TEST,("ubstest: key self-test results do not match\n"));
    return(UBSEC_STATUS_DEVICE_FAILED);
  }
  if (RTL_Memcmp(pX,UserX,TEST_KEY_BLOCK_SIZE)) {
    Dbg_Print(DBG_TEST,("ubstest: key self-test results do not match\n"));
    return(UBSEC_STATUS_DEVICE_FAILED);
  }
#endif /* UBS_OVERRIDE_LONG_KEY_MODE */


  Dbg_Print(DBG_TEST,("key self-test passed successfully.\n"));

  return(UBSEC_STATUS_SUCCESS);
}

/*
 * KeyCallback: Intermediate callback routine for key selftest.
 * This is called when the key operation completes. We need to check
 * the result of the key operation and indicate the status to the 
 * initiator of the test.
 */
static void KeyCallback(unsigned long Context,ubsec_Status_t Result)
{
  DeviceInfo_pt pDevice=(DeviceInfo_pt)Context;

  if (Result == UBSEC_STATUS_SUCCESS) { 
    /* Make sure CPU sees current state of recently DMA'd data buffers */
    OS_SyncToCPU(X_Handle,0,TEST_KEY_BLOCK_SIZE);
    OS_SyncToCPU(Y_Handle,0,TEST_KEY_BLOCK_SIZE);
    Result=CompareKeyResults();
  }

  OS_FreeDMAMemory(X_Handle,TEST_KEY_BLOCK_SIZE);
  OS_FreeDMAMemory(Y_Handle,TEST_KEY_BLOCK_SIZE);
  OS_FreeDMAMemory(UserX_Handle,TEST_KEY_BLOCK_SIZE);
  OS_FreeDMAMemory(N_Handle,TEST_KEY_BLOCK_SIZE);
  OS_FreeDMAMemory(G_Handle,TEST_KEY_BLOCK_SIZE);
  OS_FreeMemory(Kcmd,sizeof(ubsec_KeyCommandInfo_t));

  (*pDevice->KeySelfTestCallBack)(pDevice->KeySelfTestContext,Result);

}


