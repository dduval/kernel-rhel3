
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
 * ubstest.c: Ubsec Bring  up Diagnostics routines
 */

/*
 * Revision History:
 *
 * 01/31/2000 SOR Created.
 * 04/03/2001 RJT Added support for CryptoNet device big-endian mode
 * 04/23/2001 RJT Added support for CPU-DMA memory synchronization
 * 07/30/2001 DPA Changed test from 3DES to DES
 */

#include "ubsincl.h"

#ifndef ASYNC
static void callback(unsigned long PacketContext,ubsec_Status_t Result);
#endif
#define UBS_SELFTEST_MD5   /* Undefine for SHA1 authentication */

static unsigned char InitialVector[]={1, 2, 3, 4, 5, 6, 7, 8 };

static unsigned char CryptKey[]={
    1,  2,  3,  4,  5,  6,  7,  8, 
    9, 10, 11, 12, 13, 14, 15, 16, 
    17, 18, 19, 20, 21, 22, 23, 24
  };

static unsigned char MacKey[]=   { 
     1,  2,  3,  4,  5,  6,  7,  8, 
     9, 10, 11, 12, 13, 14, 15, 16, 
    17, 18, 19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48,
    49, 50, 51, 52, 53, 54, 55, 56, 
    57, 58, 59, 60, 61, 62, 63, 64
  };

 
/* Input data to the encryption function. */
static unsigned char InputData[TEST_CIPHER_BLOCK_SIZE]= {
 0x30 ,0x31 ,0x32 ,0x33 ,0x34 ,0x35 ,0x36 ,0x37,
 0x38 ,0x39 ,0x0a ,0x61 ,0x62 ,0x63 ,0x64 ,0x65,  
 0x66 ,0x67 ,0x68 ,0x69 ,0x6a ,0x0a ,0x6b ,0x6c,
 0x6d ,0x6e ,0x6f ,0x70 ,0x71 ,0x72 ,0x73 ,0x74,
 0x0a ,0x75 ,0x76 ,0x77 ,0x78 ,0x79 ,0x7a ,0x2e,
 0x2e ,0x2e ,0x2e ,0x0a ,0x41 ,0x42 ,0x43 ,0x44,
 0x45 ,0x46 ,0x47 ,0x48 ,0x49 ,0x4a ,0x0a ,0x4b,
 0x4c ,0x4d ,0x4e ,0x4f ,0x50 ,0x51 ,0x52 ,0x53
};


/* Encoded data value - the golden data */
static unsigned char EncodedData[TEST_CIPHER_BLOCK_SIZE]= {
0x6C ,0x12 ,0x11 ,0x49 ,0x33 ,0xC7 ,0xD3 ,0x64,  
0xD3 ,0x03 ,0x56 ,0xB6 ,0x64 ,0x0A ,0xEC ,0xF0,
0x68 ,0xCD ,0xCC ,0x77 ,0xD1 ,0x4E ,0x9A ,0xCC,
0x9C ,0xAC ,0x5D ,0x78 ,0x1A ,0x10 ,0x63 ,0x42,
0xCC ,0xE0 ,0x46 ,0xEB ,0x7C ,0x8F ,0x06 ,0xCC,
0xE7 ,0x3A ,0x2A ,0x4F ,0x04 ,0xD2 ,0xF7 ,0x85,
0xF9 ,0xF5 ,0xB7 ,0xE8 ,0xFC ,0xEB ,0x3D ,0x01,
0xF6 ,0x35 ,0xD4 ,0xD0 ,0x2F ,0x48 ,0x44 ,0xB6
};     

#ifdef UBS_SELFTEST_MD5
/* Authentication Info for MD5 authentication */
static unsigned char AuthData[TEST_AUTH_DATA_SIZE] = {
0xF4 ,0x2E ,0x0C ,0x27 ,0x98 ,0xD5 ,0x57 ,0x6D,
0x8C ,0x5D ,0x2D ,0xF0 ,0x78 ,0xE3 ,0x3F ,0x92,
0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00,
0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00
};
#else
/* Authentication Info for SHA1 authentication*/
static unsigned char AuthData[TEST_AUTH_DATA_SIZE] = {
0xB0 ,0x15 ,0xEC ,0x4E ,0x5C ,0x5E ,0x93 ,0x01 ,
0x30 ,0x37 ,0xFD ,0x24 ,0x1C ,0xC4 ,0x41 ,0xA6 ,
0x11 ,0xF5 ,0x0F ,0x06 ,0x00 ,0x00 ,0x00 ,0x00 , 
0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00 ,0x00
};
#endif

/*
 * Static Prototype definitions.
 */
static ubsec_Status_t CompareCryptoResults(DeviceInfo_pt pDevice, unsigned char *pData);
static void CryptoCallback(unsigned long PacketContext,ubsec_Status_t Result);


/*
 * ubsec_TestCryptoDevice
 * 
 * Perform testing on a ubsec device.
 */
ubsec_Status_t
ubsec_TestCryptoDevice(ubsec_DeviceContext_t Ubsec_Context,void(*CompletionCallback)(unsigned long PacketContext,ubsec_Status_t Result),unsigned long CompletionContext)
{
  ubsec_CipherCommandInfo_t ubsec_commands[2]; /* One encode/one decode. */
  ubsec_HMAC_State_t HMAC_State;
  ubsec_FragmentInfo_t SourceFragments[2]; /* One for each command */
  ubsec_FragmentInfo_t DestinationFragments[2]; /* One for each command */
  unsigned char *pData;
  ubsec_MemAddress_t PhysicalAddress;
  int num_packets=1;
  int i, Status;
  UBS_UINT32 SaveConfig;
  DeviceInfo_pt pDevice=(DeviceInfo_pt)Ubsec_Context;

  Dbg_Print(DBG_TEST,("ubstest: crypto self-test..\n"));
  /* 
   * First reset the device and complete any pending requests.
   */
  ubsec_ResetDevice(Ubsec_Context);

  /* First clear the Data areas. */
  RTL_MemZero(ubsec_commands, sizeof(ubsec_commands));
  if ((pData=pDevice->SelfTestMemArea)==(unsigned char *) 0)
    return(UBSEC_STATUS_NO_RESOURCE);
  RTL_MemZero(pData,TEST_DATA_SIZE);
  
  /* We assume here that the TEST_DATA_SIZE (defined in ubsec.h) bytes at     */
  /* SelfTestMemAreaHandle are in contiguous physical memory. This assumption */
  /* allows us to forego fragmentation, and to perform physical pointer math. */
  /* SelfTestMemArea is an individually malloc'd buffer of TEST_DATA_SIZE     */
  /* bytes, significantly smaller than the typical 4096-byte physical memory  */
  /* page.                                                                    */

  PhysicalAddress=(ubsec_MemAddress_t)(OS_GetPhysicalAddress(pDevice->SelfTestMemAreaHandle));
  /* Set the fragments to point to the locations. */
  SourceFragments[0].FragmentLength=TEST_CIPHER_BLOCK_SIZE;   
  SourceFragments[0].FragmentAddress=PhysicalAddress; 
  DestinationFragments[0].FragmentLength=TEST_CIPHER_BLOCK_SIZE;
  DestinationFragments[0].FragmentAddress=PhysicalAddress+TEST_CIPHER_BLOCK_SIZE; 
  SourceFragments[1]=DestinationFragments[0];  /* This is the input to the decode */
  /* Final output should match the input. */
  DestinationFragments[1].FragmentLength=TEST_CIPHER_BLOCK_SIZE; 
  DestinationFragments[1].FragmentAddress=PhysicalAddress+(2*TEST_CIPHER_BLOCK_SIZE); 

  /* Crypto data is usually a byte stream in network byte order,         */
  /* (endianess does not apply). However, there may be external H/W      */
  /* (like the Galileo GT64120) which performs byteswapping on an        */
  /* address-window basis. Under this circumstance, the crypto data      */
  /* we're using here (in SRL CTRLMEM) would certainly live in a         */
  /* swappable memory window. We would therefore need to "cheat" when    */
  /* we are building our selftest crypto structures in endian-aware      */
  /* memory (CTRLMEM, the only kind that the SRL code uses).             */
  /* To compensate for those platforms where (CTRLMEM) memory is         */
  /* byteswapped when accessed by our device (as a PCI bus master),      */
  /* we'll use BYTESWAPLONG when copying selftest crypto data into       */
  /* (CTRLMEM) memory. This ensures that the data (as read by our        */
  /* device) always appears in the desired byte order on the PCI bus,    */
  /* based on what the hardware platform does with CTRLMEM-PCI bytes.    */
  /* Because BYTESWAPLONG operates on 32-bit integers, the data used     */
  /* in this self test must be treated as 32-bit integers (instead of    */
  /* a byte stream).                                                     */

  /* Now copy in the original source */

  if (UBSEC_EXTERNAL_BYTESWAP_HW(pDevice)) {
    /* 32-bit longs in CTRLMEM will be byteswapped by the external H/W.      */
    /* Remember that we're building selftest crypto data in CTRLMEM memory.  */
    /* Copy data by long ints, even though crypto data is intrinsically a    */
    /* byte stream. Swap here to compensate for the external byteswap H/W.   */
    for (i=0 ; i<(TEST_CIPHER_BLOCK_SIZE/4) ; i++) {
      ((UBS_UINT32 *)pData)[i] = \
	BYTESWAPLONG(((UBS_UINT32 *)InputData)[i]);
    }
  }
  else /* (Normal) H/W doesn't change endianess between CTRLMEM and PCI */
    RTL_Memcpy( pData,&InputData[0],TEST_CIPHER_BLOCK_SIZE);

  /* Make sure DMA memory actually holds recent CPU-initialized buffer data */
  OS_SyncToDevice(pDevice->SelfTestMemAreaHandle,0,TEST_CIPHER_BLOCK_SIZE);

  /* Now set up the command(s) */
#ifdef UBS_SELFTEST_MD5
  /* Initialize the MAC/Inner Outer Key */
  ubsec_InitHMACState(&HMAC_State,UBSEC_MAC_MD5,MacKey);
  /* First is encode. */
  ubsec_commands[0].Command=UBSEC_ENCODE_DES_MD5;
  /* Second is decode. */
  ubsec_commands[1].Command=UBSEC_DECODE_DES_MD5;
#else
  /* Initialize the MAC/Inner Outer Key */
  ubsec_InitHMACState(&HMAC_State,UBSEC_MAC_SHA1,MacKey);
  /* First is encode. */
  ubsec_commands[0].Command=UBSEC_ENCODE_DES_SHA1;
  /* Second is decode. */
  ubsec_commands[1].Command=UBSEC_DECODE_DES_SHA1;
#endif

  /* The rest can be done in a loop */
  for (i=0; i < 2 ; i++) {
    ubsec_commands[i].InitialVector=(ubsec_IV_pt)&InitialVector[0]; 
    ubsec_commands[i].CryptKey=(ubsec_CryptKey_pt)&CryptKey[0];
    ubsec_commands[i].HMACState=&HMAC_State; 
    ubsec_commands[i].NumSource=1; 
    ubsec_commands[i].SourceFragments=&SourceFragments[i];
    ubsec_commands[i].NumDestination=1; 
    ubsec_commands[i].DestinationFragments=&DestinationFragments[i]; 
    ubsec_commands[i].AuthenticationInfo.FragmentAddress=
                            PhysicalAddress+(3*TEST_CIPHER_BLOCK_SIZE);
    ubsec_commands[i].CryptHeaderSkip=0;
  }

  SaveConfig=UBSEC_READ_CONFIG(pDevice); 


  /* Are we completing sync or async */
  if (CompletionCallback) {
    pDevice->SelfTestCallBack=CompletionCallback;
    pDevice->SelfTestContext=CompletionContext;
    ubsec_commands[1].CompletionCallback=CryptoCallback;
    ubsec_commands[1].CommandContext=(unsigned long) pDevice;
  }
  else {
    /* Turn off interrupts while waiting. */
    UBSEC_DISABLE_INT(pDevice);
  }

    for (i=0; i < 2 ; i+=num_packets) {
      switch (Status=ubsec_CipherCommand(Ubsec_Context,&ubsec_commands[i],&num_packets) ) {
      case UBSEC_STATUS_SUCCESS:
	break;
      case UBSEC_STATUS_TIMEOUT:
	Dbg_Print(DBG_FATAL,( "ubsec:  BUD Command timeout\n"));
	ubsec_ResetDevice(Ubsec_Context);
	return(Status);
	break;
      case UBSEC_STATUS_INVALID_PARAMETER:
	Dbg_Print(DBG_FATAL,( "ubsec:  BUD Failed Invalid parameter\n"));
	return(Status);
	break;
      case UBSEC_STATUS_NO_RESOURCE:
	Dbg_Print(DBG_FATAL,( "ubsec:  BUD Failed No Resource\n"));
	return(Status);
      default:
	Dbg_Print(DBG_FATAL,( "ubsec:  BUD Failure unknown %x\n",Status));
	return(Status);
	break;
      }
    }

  /* Are we completing sync or async */
  if (CompletionCallback) {
    return(Status);
  }

    
#ifndef BLOCK
 while ((Status=WaitForCompletion(pDevice,(unsigned long)100000,UBSEC_CIPHER_LIST))
	== UBSEC_STATUS_SUCCESS); /* wait for them all to complete */
#endif

  /* Restore configuration status */
  UBSEC_WRITE_CONFIG(pDevice,SaveConfig);

#ifdef BLOCK
  /* Invalid parameter means that no more left on pending queue */
  if (Status != UBSEC_STATUS_INVALID_PARAMETER)
#else
  if (Status != UBSEC_STATUS_SUCCESS)
#endif
    return(Status);

  /* Make sure CPU sees current state of recently DMA'd data buffers */
  OS_SyncToCPU(pDevice->SelfTestMemAreaHandle,
	       TEST_CIPHER_BLOCK_SIZE,
	       2*TEST_CIPHER_BLOCK_SIZE+UBSEC_SHA1_LENGTH);

  return(CompareCryptoResults(pDevice,pData));
}



/*
 * Compare the results of the crypto operation.
 */
static ubsec_Status_t
CompareCryptoResults(DeviceInfo_pt pDevice, unsigned char *pData)
{
  int i;
  /* Compare golden data with encrypted output data.                     */
  if (UBSEC_EXTERNAL_BYTESWAP_HW(pDevice)) {
    /* 32-bit longs in CTRLMEM will be byteswapped by external hardware.     */
    /* Remember that we're building selftest crypto data in CTRLMEM memory.  */
    /* Compare data by long ints, even though crypto data is intrinsically   */
    /* byte stream. Swap here to compensate for the external byteswap H/W.   */
    for (i=0 ; i<(TEST_CIPHER_BLOCK_SIZE/4) ; i++) {
      if (((UBS_UINT32*)(&pData[TEST_CIPHER_BLOCK_SIZE]))[i] != \
	  BYTESWAPLONG(((UBS_UINT32*)EncodedData)[i]) ) {
	Dbg_Print(DBG_TEST,("ubstest: golden data and encrypted data do not match\n"));
	return(UBSEC_STATUS_DEVICE_FAILED);
      }
    }
  }
  else {
    if (RTL_Memcmp(EncodedData, &pData[TEST_CIPHER_BLOCK_SIZE],TEST_CIPHER_BLOCK_SIZE))
      {
	Dbg_Print(DBG_TEST,("ubstest: golden data and encrypted data do not match\n"));
	return(UBSEC_STATUS_DEVICE_FAILED);
      }
  }

  Dbg_Print(DBG_TEST,("ubstest: golden data and encrypted data match\n"));

  /* Compare the decrypted-encrypted data with the original data         */

  if (UBSEC_EXTERNAL_BYTESWAP_HW(pDevice)) {
    /* 32-bit longs in CTRLMEM will be byteswapped by external hardware.     */
    /* Remember that we're building selftest crypto data in CTRLMEM memory.  */
    /* Compare data by long ints, even though crypto data is intrinsically   */
    /* byte stream. Swap here to compensate for the external byteswap H/W.   */
    for (i=0 ; i<(TEST_CIPHER_BLOCK_SIZE/4) ; i++) {
      if (((UBS_UINT32*)(&pData[2*TEST_CIPHER_BLOCK_SIZE]))[i] != \
	  BYTESWAPLONG(((UBS_UINT32*)InputData)[i]) ) {
	Dbg_Print(DBG_TEST,("ubstest:decrypted-encrypted data and original data do not match\n"));
	return(UBSEC_STATUS_DEVICE_FAILED);
      }  
    }
  }
  else {
    if (RTL_Memcmp(pData, &pData[2*TEST_CIPHER_BLOCK_SIZE],TEST_CIPHER_BLOCK_SIZE))
      {
	Dbg_Print(DBG_TEST,("ubstest:decrypted-encrypted data and original data do not match\n"));
	return(UBSEC_STATUS_DEVICE_FAILED);
      }
  }

  Dbg_Print(DBG_TEST,("ubstest: decrypted-encrypted data and original data match\n"));

  /* Compare final authentication */

  if (UBSEC_EXTERNAL_BYTESWAP_HW(pDevice)) {
    /* 32-bit longs in CTRLMEM will be byteswapped by external hardware.       */
    /* Compare data by long ints, even though crypto data is intrinsically     */
    /* a byte stream. Swap here to compensate for the external byteswap H/W.   */

#ifdef UBS_SELFTEST_MD5
    for (i=0 ; i<(UBSEC_MD5_LENGTH/4) ; i++) {
#else
    for (i=0 ; i<(UBSEC_SHA1_LENGTH/4) ; i++) {
#endif
      if (((UBS_UINT32*)(&pData[3*TEST_CIPHER_BLOCK_SIZE]))[i] != \
	  BYTESWAPLONG(((UBS_UINT32*)(&AuthData[0]))[i]) ) {
	Dbg_Print(DBG_TEST,("ubstest: Authentication data do not match\n"));
	return(UBSEC_STATUS_DEVICE_FAILED);
      }
    }
  }
else {
  
#ifdef UBS_SELFTEST_MD5
  if (RTL_Memcmp(&AuthData[0], &pData[(TEST_CIPHER_BLOCK_SIZE*3)],UBSEC_MD5_LENGTH)) {
#else
  if (RTL_Memcmp(&AuthData[0], &pData[(TEST_CIPHER_BLOCK_SIZE*3)],UBSEC_SHA1_LENGTH)) {
#endif
      Dbg_Print(DBG_TEST,("ubstest: Authentication data do not match\n"));
      return(UBSEC_STATUS_DEVICE_FAILED);
  }
}

  Dbg_Print(DBG_TEST,("ubstest: Authentication data matches\n"));
  Dbg_Print(DBG_TEST,("ubstest: crypto self-test passed successfully.\n"));
  return(UBSEC_STATUS_SUCCESS);
}


/*
 * CryptoCallback: Intermediate callback routine for crypto selftest.
 * This is called when the crypto operation completes. We need to check
 * the result of the crypto operation and indicate the status to the 
 * initiator of the test.
 */
static void CryptoCallback(unsigned long Context,ubsec_Status_t Result)
{
  DeviceInfo_pt pDevice=(DeviceInfo_pt)Context;

  if (Result == UBSEC_STATUS_SUCCESS) { 
    /* Make sure CPU sees current state of recently DMA'd data buffers */
    OS_SyncToCPU(pDevice->SelfTestMemAreaHandle,
	       TEST_CIPHER_BLOCK_SIZE,
	       2*TEST_CIPHER_BLOCK_SIZE+UBSEC_SHA1_LENGTH);
    Result=CompareCryptoResults(pDevice,pDevice->SelfTestMemArea);
  }

  (*pDevice->SelfTestCallBack)(pDevice->SelfTestContext,Result);
}







