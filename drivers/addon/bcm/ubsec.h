
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
 * ubsec.h: Interface functions and defintions for the ubsec Software Reference
 * library.
 *
 * This file should be included by any files using the UBSEC 5501 SRL
 */

/*
 * Revision History:
 *
 *  Oct 99 SOR Created.
 *  Sep 00 SOR 5820 Support Added.
 *  Jul 01 RJT 5821 Support Added (1.2a)
 *  Oct 01 SRM 64 bit port.
 */

#ifndef _UBSEC_H_
#define _UBSEC_H_

#include "ubslinux.h"
/*
 * Device_Context is handle for all ubsec device operations. It
 * is assigned at initialization time.
 */
typedef void *ubsec_DeviceContext_t, **ubsec_DeviceContext_pt;

#ifdef PAD_ALIGN64_APP32
#define PADIT(name,size)  char name[size];
#else
#define PADIT(name,size)
#endif

#include "ubssys.h"

/*
 * List of Vendor/Device IDs supported by the library.
 */
#define BROADCOM_VENDOR_ID  0x14e4  /* Broadcom vendor ID */
#define BROADCOM_DEVICE_ID_5801  0x5801 /* Release board. */
#define BROADCOM_DEVICE_ID_5802  0x5802 /* Release board. */
#define BROADCOM_DEVICE_ID_5805  0x5805 /* Release board  */
#define BROADCOM_DEVICE_ID_5820  0x5820 /* Release board  */
#define BROADCOM_DEVICE_ID_5821  0x5821 /* Release board  */
#define BROADCOM_DEVICE_ID_5822  0x5822 /* Release board  */

/* Macro to determine device type based on deviceID */
#define UBSEC_IS_CRYPTO_DEVICEID(DeviceID) ((DeviceID) == BROADCOM_DEVICE_ID_5801)

/* 
 * Version of the SRL.
 */
#define UBSEC_VERSION_MAJOR 0x1
#define UBSEC_VERSION_MINOR 0x4
#define UBSEC_VERSION_REV   ' '  /* Single alphanumeric character, start with blank, then a,b,c... */


/*
 * Cryptographic parameter definitions
 */
#define UBSEC_DES_KEY_LENGTH 2    /* long */
#define UBSEC_3DES_KEY_LENGTH 6   /* long */
#define UBSEC_MAX_CRYPT_KEY_LENGTH UBSEC_3DES_KEY_LENGTH
#define UBSEC_IV_LENGTH		2 /* long */
#define UBSEC_IV_LENGTH_BYTES	8

#define UBSEC_MAC_KEY_LENGTH	64 /* Bytes */
#define UBSEC_MD5_LENGTH	16 /* Bytes */
#define UBSEC_SHA1_LENGTH	20 /* Bytes */
#define UBSEC_HMAC_LENGTH   20 /* Max of MD5/SHA1 */


/*
 * HMAC State type defines the current (inner/outer)
 * Hash state values. 
 */
typedef struct ubsec_HMAC_State_s {
  unsigned char	InnerState[UBSEC_HMAC_LENGTH];
  unsigned char	OuterState[UBSEC_HMAC_LENGTH];
} ubsec_HMAC_State_t, *ubsec_HMAC_State_pt;


typedef unsigned char* ubsec_MemAddress_t;


/* 
 * Generic Fragment information type. Length
 * and physical address of fragment defined
 * here.
 */
typedef struct ubsec_FragmentInfo_s {
  int                 FragmentLength;  /* Length of the fragment.     */
  PADIT(FragmentAddress_pad,8)
  ubsec_MemAddress_t  FragmentAddress; /* Virtual or Physical address */
} ubsec_FragmentInfo_t, *ubsec_FragmentInfo_pt;


/*
 * HMAC Block type. Used to generate a HMAC state which is
 * passed to the API.
 */
typedef unsigned char ubsec_HMAC_Block_t[UBSEC_MAC_KEY_LENGTH],*ubsec_HMAC_Block_pt;

/*
 * HMAC Block type. Used to generate a HMAC state which is
 * passed to the API.
 */
typedef unsigned char ubsec_HMAC_Key_t[UBSEC_MAC_KEY_LENGTH],*ubsec_HMAC_Key_pt;

/*
 * Initial Vector type for CBC operations.
 */
typedef long  ubsec_IV_t[UBSEC_IV_LENGTH], *ubsec_IV_pt;

/*
 * DES Key type definitions.
 */

/* Single DES Crypt key type. 3DES operation used 3 of these. */
typedef long ubsec_CryptKey_t[UBSEC_DES_KEY_LENGTH], *ubsec_CryptKey_pt;

/* Cipher command type defines Cipher/Authentication operation. */
typedef long ubsec_CipherCommand_t;

/* Status code is used by the SRL to indicate status */
typedef long ubsec_Status_t;


/*
 * Cipher command struture defines the parameters of a cipher
 * command, its input and output data areas along with the 
 * context.
 */
typedef struct ubsec_CipherCommandInfo_s {
  ubsec_CipherCommand_t   	Command;  /* Operation(s) to perform */
  ubsec_IV_pt	   	        InitialVector;   /* IV for CBC operation. */
  ubsec_CryptKey_pt 	        CryptKey;         /* For CBC operation. */
  ubsec_HMAC_State_pt            HMACState;    /*  Initialized HMAC state for authentication. */
  unsigned NumSource;                    /* Number of source fragments. */
  ubsec_FragmentInfo_pt 	SourceFragments; /* Source fragment list */
  UBS_UINT32 		NumDestination;  /* Number of Destination fragments. */
  ubsec_FragmentInfo_pt         DestinationFragments;    /* Destination fragment list */
  ubsec_FragmentInfo_t          AuthenticationInfo;       /* Authentication output location . */
  unsigned short   		 CryptHeaderSkip; /* Size of crypt header to skip. */
  void(*CompletionCallback)(unsigned long Context,ubsec_Status_t Result);  /* Callback routine on completion. */
  unsigned long	   		 CommandContext;    /* Context (ID) of this command). */
  } ubsec_CipherCommandInfo_t,*ubsec_CipherCommandInfo_pt;


/*
 * Cipher Command subtype flags.
 */
#define UBSEC_ENCODE		1
#define UBSEC_DECODE		2
#define UBSEC_3DES		4
#define UBSEC_DES		8
#define UBSEC_MAC_MD5		16
#define UBSEC_MAC_SHA1		32

/*
 *	Command field definitions.
 */
#define UBSEC_ENCODE_3DES (UBSEC_ENCODE+UBSEC_3DES)
#define UBSEC_DECODE_3DES (UBSEC_DECODE+UBSEC_3DES)
#define UBSEC_ENCODE_DES  (UBSEC_ENCODE+UBSEC_DES)
#define UBSEC_DECODE_DES  (UBSEC_DECODE+UBSEC_DES)

#define UBSEC_ENCODE_3DES_MD5   (UBSEC_ENCODE_3DES+UBSEC_MAC_MD5)
#define UBSEC_DECODE_3DES_MD5   (UBSEC_DECODE_3DES+UBSEC_MAC_MD5)
#define UBSEC_ENCODE_3DES_SHA1  (UBSEC_ENCODE_3DES+UBSEC_MAC_SHA1)
#define UBSEC_DECODE_3DES_SHA1  (UBSEC_DECODE_3DES+UBSEC_MAC_SHA1)
#define UBSEC_ENCODE_DES_MD5	(UBSEC_ENCODE_DES+UBSEC_MAC_MD5)
#define UBSEC_DECODE_DES_MD5	(UBSEC_DECODE_DES+UBSEC_MAC_MD5)
#define UBSEC_ENCODE_DES_SHA1	(UBSEC_ENCODE_DES+UBSEC_MAC_SHA1)
#define UBSEC_DECODE_DES_SHA1	(UBSEC_DECODE_DES+UBSEC_MAC_SHA1)

#define UBSEC_USING_CRYPT(f) ( (f) & (UBSEC_3DES | UBSEC_DES) )
#define UBSEC_USING_MAC(f)   ( (f) & (UBSEC_MAC_MD5 | UBSEC_MAC_SHA1) )

/*
 * Status codes
 */
#define UBSEC_STATUS_SUCCESS              0
#define UBSEC_STATUS_NO_DEVICE           -1
#define UBSEC_STATUS_TIMEOUT             -2
#define UBSEC_STATUS_INVALID_PARAMETER   -3
#define UBSEC_STATUS_DEVICE_FAILED       -4
#define UBSEC_STATUS_DEVICE_BUSY         -5
#define UBSEC_STATUS_NO_RESOURCE         -6
#define UBSEC_STATUS_CANCELLED           -7

  /* 
   * SRL  API function prototypes.
   */
#ifndef OS_DeviceInfo_t
#define OS_DeviceInfo_t void *
#endif

#ifndef OS_MemHandle_t
#define OS_MemHandle_t void *
#endif

#ifndef UBSECAPI
#define UBSECAPI
#endif

  /* Initialize the device */
UBSECAPI ubsec_Status_t
ubsec_InitDevice(unsigned short DeviceID,
		 unsigned long BaseAddress,
		 unsigned int irq,
		 unsigned int CipherPipeLineDepth,
		 unsigned int KeyPipeLineDepth,
		 ubsec_DeviceContext_pt Context,
		 OS_DeviceInfo_t OSContext);

/*
 * Perform self test of device.
 */
UBSECAPI ubsec_Status_t
ubsec_TestCryptoDevice(ubsec_DeviceContext_t Context,void(*CompletionCallback)(unsigned long PacketContext,ubsec_Status_t Result),unsigned long CompletionContext);

UBSECAPI ubsec_Status_t
ubsec_TestKeyDevice(ubsec_DeviceContext_t Context,void(*CompletionCallback)(unsigned long PacketContext,ubsec_Status_t Result),unsigned long CompletionContext);


  /* Reset the device */
UBSECAPI ubsec_Status_t
ubsec_ResetDevice( ubsec_DeviceContext_t Context);

  /* Shutdown the device. */
UBSECAPI ubsec_Status_t
ubsec_ShutdownDevice( ubsec_DeviceContext_t Context);

  /* Enable device interrupts */
ubsec_Status_t
ubsec_EnableInterrupt( ubsec_DeviceContext_t Context);

  /* Disable device interrupts */
unsigned long
ubsec_DisableInterrupt( ubsec_DeviceContext_t Context);

  /* Poll device for completion of commands */
ubsec_Status_t
ubsec_PollDevice( ubsec_DeviceContext_t Context);

  /* Cipher command execute function. */
UBSECAPI ubsec_Status_t
ubsec_CipherCommand(ubsec_DeviceContext_t Context,
		    ubsec_CipherCommandInfo_pt command,
		    int *NumCommands);

  /* Initialize HMAC state */
UBSECAPI ubsec_Status_t
ubsec_InitHMACState(ubsec_HMAC_State_pt HMAC_State,
	      ubsec_CipherCommand_t type,
	      ubsec_HMAC_Key_pt Key) ;

/* ISR functions are here to allow direct call
   by the wrapper. */
UBSECAPI long ubsec_ISR(ubsec_DeviceContext_t Context);
UBSECAPI void ubsec_ISRCallback(ubsec_DeviceContext_t Context);

/*
 *
 * Public key operational definitions.
 *
 */


/*
 * The long key type is used as a generic type to hold public
 * key information.
 * KeyValue points to an array of 32-bit integers. The convention of these keys
 * is such that element[0] of this array holds the least significant part of
 * the total "key" (multi-precision integer).
 * Keylength holds the number of significant bits in the key, i.e. the bit
 * position of the most significant "1" bit, plus 1.
 * For example, the multi-precision integer ("key")
 *    0x0102030405060708090A0B0C0D0E0F00
 * has 121 significant bits (KeyLength), and would be arranged in the N-element 
 * array (pointed to by KeyLength) of 32-bit integers as
 *    array[0] = 0x0D0E0F00
 *    array[1] = 0x090A0B0C
 *    array[2] = 0x05060708
 *    array[3] = 0x01020304
 *    array[4] = 0x00000000
 *        ...
 *    array[N-1] = 0x00000000
 */

typedef struct ubsec_LongKey_s {
  UBS_UINT32	KeyLength;	/* length in bits */
  PADIT(KeyValue_pad,8)
  OS_MemHandle_t  KeyValue;	/* pointer to 32-bit integer "key" array */
} ubsec_LongKey_t,*ubsec_LongKey_pt;


/*
 * Diffie-Hellman parameter type definition.
 */
typedef struct ubsec_DH_Params_t {
  ubsec_LongKey_t Y;		/* Public value, in (UBSEC_DH_SHARED), out (UBSEC_DH_PUBLIC) */
  ubsec_LongKey_t X;		/* Secret value, in (UBSEC_DH_SHARED), out (UBSEC_DH_PUBLIC) */
  ubsec_LongKey_t K;		/* Shared secret value, out (UBSEC_DH_SHARED) */
  ubsec_LongKey_t N;   		/* Modulus, in (UBSEC_DH_SHARED), out (UBSEC_DH_PUBLIC) */
  ubsec_LongKey_t G;	  	/* Generator, in (UBSEC_DH_PUBLIC) */
  ubsec_LongKey_t UserX;  	/* Optional user supplied secret value, in (UBSEC_DH_PUBLIC) */

  unsigned short RandomKeyLen;	/* Random key length*/
  unsigned short RNGEnable;	/* Generate random secret value if set, ignore user supplied. */
} ubsec_DH_Params_t,*ubsec_DH_Params_pt;


/*
 * RSA parameter type definition.
 */
typedef struct ubsec_RSA_Params_t {
  ubsec_LongKey_t OutputKeyInfo; /* Output data. */
  ubsec_LongKey_t InputKeyInfo;  /* Input data. */
  ubsec_LongKey_t ModN;      /* Modulo N value to be applied */
  ubsec_LongKey_t ExpE;      /* BaseG value to be applied. */
  ubsec_LongKey_t PrimeP;    /* Prime P value */
  ubsec_LongKey_t PrimeQ;    /* Prime Q value */
  ubsec_LongKey_t PrimeEdp;  /* Private exponent edp. */
  ubsec_LongKey_t PrimeEdq;  /* Private exponent edq.  */
  ubsec_LongKey_t Pinv;      /* Pinv value. */
} ubsec_RSA_Params_t,*ubsec_RSA_Params_pt;



/*
 * DSA parameter type definition.
 */
typedef struct ubsec_DSA_Params_t {
  unsigned int NumInputFragments;  /* Number of source fragments. */
  PADIT(InputFragments_pad,8)
  ubsec_FragmentInfo_pt InputFragments; /* Source fragment list for unhashed message */
  ubsec_LongKey_t SigR;		/* Signature R value (input on verify, output on sign) */
  ubsec_LongKey_t SigS;		/* Signature S value (input on verify, output on sign) */
  ubsec_LongKey_t ModQ;   	/* Modulo Q value to be applied */
  ubsec_LongKey_t ModP;   	/* Modulo P value to be applied */
  ubsec_LongKey_t BaseG;  	/* BaseG value to be applied. */
  ubsec_LongKey_t Key;    	/* User supplied public (verify) or private (sign) key. */
  ubsec_LongKey_t Random; 	/* Random value optionally provided by user (sign) */
  ubsec_LongKey_t V;		/* Verification value (verify) */
  unsigned short RandomLen; 	/* Random value length (sign) */
  unsigned short RNGEnable;    	/* Random value generated on-chip. (sign) */
  unsigned short HashEnable;    /* Enable Chip hash */
} ubsec_DSA_Params_t,*ubsec_DSA_Params_pt;

/* 
 * Generic key command parameters
 */
typedef union ubsec_KeyCommandParams_u {
  ubsec_DH_Params_t DHParams;    /* DH parameters  */
  ubsec_RSA_Params_t RSAParams;  /* RSA Parameters */
  ubsec_DSA_Params_t DSAParams;  /* RSA Parameters */
} ubsec_KeyCommandParams_t,*ubsec_KeyCommandParams_pt;



/* Key command type defines Public key operation. */
typedef long ubsec_KeyCommand_t;


/* Key command types. */
#define UBSEC_DH          0x0001
#define UBSEC_RSA         0x0002
#define UBSEC_DSA         0x0004
#define UBSEC_KEY_PRIVATE 0x0010
#define UBSEC_KEY_PUBLIC  0x0020
#define UBSEC_SIGN        0x0040
#define UBSEC_VERIFY      0x0080

#define UBSEC_DH_PUBLIC    	(UBSEC_DH+UBSEC_KEY_PUBLIC)
#define UBSEC_DH_SHARED    	(UBSEC_DH+UBSEC_KEY_PRIVATE)
#define UBSEC_RSA_PUBLIC   	(UBSEC_RSA+UBSEC_KEY_PUBLIC)
#define UBSEC_RSA_PRIVATE  	(UBSEC_RSA+UBSEC_KEY_PRIVATE)
#define UBSEC_DSA_SIGN     	(UBSEC_DSA+UBSEC_SIGN)
#define UBSEC_DSA_VERIFY   	(UBSEC_DSA+UBSEC_VERIFY)

/*
 * Key command struture defines the parameters of a cipher
 * command, its input and output data areas along with the 
 * context.
 */
typedef struct ubsec_KeyCommandInfo_s {
  ubsec_KeyCommand_t   	Command;  /* Operation(s) to perform */
  ubsec_KeyCommandParams_t Parameters;  /* Associated parameters. */
  void(*CompletionCallback)(unsigned long Context,ubsec_Status_t Result);  /* Callback routine on completion. */
  unsigned long	 CommandContext;    /* Context (ID) of this command). */
  } ubsec_KeyCommandInfo_t,*ubsec_KeyCommandInfo_pt;

  /* Key command execute function. */
UBSECAPI ubsec_Status_t
ubsec_KeyCommand(ubsec_DeviceContext_t Context,
		    ubsec_KeyCommandInfo_pt command,
		    int *NumCommands);

/* 
 * Key normalization maniputaltion functions.
 */
long ubsec_NormalizeDataTo(ubsec_LongKey_pt pData,int NormalizeLen);
void ubsec_ShiftData(ubsec_LongKey_pt pData,long ShiftBits );

/* 
 * Generic Math command parameters. Parameters vary with the
 * command type.
 */
typedef struct  ubsec_MathCommandParams_s {
  ubsec_LongKey_t ModN;   	/* Modulo N value to be applied (Used by all commands) */
  ubsec_LongKey_t ModN2;   	/* Second Modulo N value to be applied (Used by DblModExp only) */
  ubsec_LongKey_t ParamA;   	/* Input Parameter 1 (Used by all commands) */
  ubsec_LongKey_t ParamB;   	/* Input Parameter 2 (Used by all commands except ModRem) */
  ubsec_LongKey_t ParamC;   	/* Input Parameter 3 (Used by DblModExp only) */
  ubsec_LongKey_t ParamD;   	/* Input Parameter 4 (Used by DblModExp only) */
  ubsec_LongKey_t Result;   	/* Result of math operation (Used by all commands) */
  ubsec_LongKey_t Result2;   	/* Second result of math operation (Used by DblModExp only) */
} ubsec_MathCommandParams_t,*ubsec_MathCommandParams_pt;

/* Math command type defines Math acceleration operation. */
typedef long ubsec_MathCommand_t;

/* Math command types. */
#define UBSEC_MATH_MODADD    0x0001
#define UBSEC_MATH_MODSUB    0x0002
#define UBSEC_MATH_MODMUL    0x0004
#define UBSEC_MATH_MODEXP    0x0008
#define UBSEC_MATH_MODREM    0x0010
#define UBSEC_MATH_DBLMODEXP 0x0040

/*
 * Math command struture defines the parameters of a Math
 * command, its input and output data areas along with the 
 * context.
 */
typedef struct ubsec_MathCommandInfo_s {
  ubsec_MathCommand_t   	Command;  /* Operation(s) to perform */
  ubsec_MathCommandParams_t Parameters;  /* Associated parameters. */
  void(*CompletionCallback)(unsigned long Context,ubsec_Status_t Result);  /* Callback routine on completion. */
  unsigned long	 CommandContext;    /* Context (ID) of this command). */
  } ubsec_MathCommandInfo_t,*ubsec_MathCommandInfo_pt;

/*
 * Math acceleration command function.
 */
UBSECAPI ubsec_Status_t 
ubsec_MathCommand(ubsec_DeviceContext_t Context,
	      ubsec_MathCommandInfo_pt pCommand,
	      int *NumCommands);


/* 
 * Random number generation parameters
 */
typedef struct  ubsec_RNGCommandParams_s {
  ubsec_LongKey_t Result;   	/* Of RNG operation. */
} ubsec_RNGCommandParams_t,*ubsec_RNGCommandParams_pt;

/* RNG command type defines RNG acceleration operation. */
typedef long ubsec_RNGCommand_t;

/* RNG command types. */
#define UBSEC_RNG_DIRECT 0x0001
#define UBSEC_RNG_SHA1   0x0002

/*
 * RNG command struture defines the parameters of a RNG
 * command, its input and output data areas along with the 
 * context.
 */
typedef struct ubsec_RNGCommandInfo_s {
  ubsec_RNGCommand_t   	Command;  /* Operation(s) to perform */
  ubsec_RNGCommandParams_t Parameters;  /* Associated parameters. */
  void(*CompletionCallback)(unsigned long Context,ubsec_Status_t Result);  /* Callback routine on completion. */
  unsigned long	 CommandContext;    /* Context (ID) of this command). */
  } ubsec_RNGCommandInfo_t,*ubsec_RNGCommandInfo_pt;

/*
 * RNG acceleration command function.
 */
UBSECAPI ubsec_Status_t 
ubsec_RNGCommand(ubsec_DeviceContext_t Context,
	      ubsec_RNGCommandInfo_pt pCommand,
	      int *NumCommands);


/*
 * Ubsec Statistics information contains all statistics 
 * maintained by the driver.
 */
typedef struct ubsec_Statistics_s {
  UBS_UINT32 BlocksEncryptedCount;
  UBS_UINT32 BlocksDecryptedCount;
  UBS_UINT32 BytesEncryptedCount;
  UBS_UINT32 BytesDecryptedCount;
  UBS_UINT32 CryptoFailedCount;
  UBS_UINT32 IKECount;
  UBS_UINT32 IKEFailedCount;
  UBS_UINT32 DHPublicCount;
  UBS_UINT32 DHSharedCount;
  UBS_UINT32 RSAPublicCount;
  UBS_UINT32 RSAPrivateCount;
  UBS_UINT32 DSASignCount;
  UBS_UINT32 DSAVerifyCount;
  UBS_UINT32 DMAErrorCount;
  } ubsec_Statistics_t, *ubsec_Statistics_pt;


/*
 * Ubsec get statistical information function.
 */
UBSECAPI ubsec_Status_t 
ubsec_GetStatistics(ubsec_DeviceContext_t Context,
	      ubsec_Statistics_pt Dest);


/* 
 * SSL/TLS/ARC4 Command prototype definitions
 */

/*
 *  SSL Command definitions. These commands will be ored in with
 * crypto commands.
 */

#define UBSEC_SSL_HMAC         (0x01000)
#define UBSEC_SSL_MAC         (0x01000)
#define UBSEC_SSL_CRYPTO      (0x02000)
#define UBSEC_TLS             (0x04000)
#define UBSEC_ARC4            (0x08000)
#define UBSEC_HASH            (0x10000)

#define UBSEC_SSL_DES_ENCODE (UBSEC_SSL_CRYPTO+UBSEC_ENCODE+UBSEC_3DES)
#define UBSEC_SSL_DES_DECODE (UBSEC_SSL_CRYPTO+UBSEC_ENCODE+UBSEC_DES)
#define UBSEC_SSL_HMAC_MD5   (UBSEC_SSL_HMAC+UBSEC_MAC_MD5)
#define UBSEC_SSL_HMAC_SHA1  (UBSEC_SSL_HMAC+UBSEC_MAC_SHA1)
#define UBSEC_SSL_MAC_MD5    (UBSEC_SSL_MAC+UBSEC_MAC_MD5)
#define UBSEC_SSL_MAC_SHA1   (UBSEC_SSL_MAC+UBSEC_MAC_SHA1)
#define UBSEC_TLS_HMAC_MD5   (UBSEC_TLS+UBSEC_MAC_MD5)
#define UBSEC_TLS_HMAC_SHA1  (UBSEC_TLS+UBSEC_MAC_SHA1)
#define UBSEC_HASH_SHA1      (UBSEC_HASH | UBSEC_MAC_SHA1)
#define UBSEC_HASH_MD5       (UBSEC_HASH | UBSEC_MAC_MD5)

#define UBSEC_ARC4_STATE_WRITEBACK  0x0001
#define UBSEC_ARC4_STATE_STATEKEY   0x0002
#define UBSEC_ARC4_STATE_NULL_DATA  0x0004

#define UBSEC_SSL_COMMAND_MASK (UBSEC_SSL_MAC+UBSEC_SSL_CRYPTO+UBSEC_TLS+UBSEC_ARC4+UBSEC_HASH)
#define UBSEC_SSL_COMMAND(command) (command & (UBSEC_SSL_COMMAND_MASK))

/*
 * Type Definitions:
 */
typedef unsigned char ubsec_SSLMAC_key_t[UBSEC_HMAC_LENGTH], *ubsec_SSLMAC_key_pt;

/* Sequence number type. Double DWORD. */
typedef struct ubsec_DoubleSequenceNumber_s {
  UBS_UINT32 HighWord;
  UBS_UINT32 LowWord;
} ubsec_DoubleSequenceNumber_t, *ubsec_DoubleSequenceNumber_pt;

typedef struct ubsec_SSLMACParams_s {
  ubsec_FragmentInfo_t         OutputHMAC;      /* output MAC */
  ubsec_SSLMAC_key_t           key;             /* MAC key */
  ubsec_DoubleSequenceNumber_t SequenceNumber;  /* sequence number */
  unsigned char                ContentType;     /* content type */
  unsigned short               DataLength;
} ubsec_SSLMACParams_t, *ubsec_SSLMACParams_pt;

typedef struct ubsec_TLSHMACParams_s {
  ubsec_FragmentInfo_t          OutputHMAC; /* output MAC */
  ubsec_HMAC_State_pt           HMACState; /* HMAC State */
  ubsec_DoubleSequenceNumber_t   SequenceNumber;     /* sequence number */
  unsigned char ContentType;        /* content type */
  unsigned short Version;        /* Version */
  unsigned short DataLength;
} ubsec_TLSHMACParams_t, *ubsec_TLSHMACParams_pt;

typedef struct ubsec_SSLCipherParams_t {
  ubsec_IV_t         InitialVector;              /* initial vector */
  UBS_UINT32 CryptKey[UBSEC_3DES_KEY_LENGTH];
} ubsec_SSLCipherParams_t, *ubsec_SSLCipherParams_pt;

#define UBSEC_ARC4_KEYSTATE_BYTES (260)
typedef unsigned char ubsec_ARC4_State_t[UBSEC_ARC4_KEYSTATE_BYTES], *ubsec_ARC4_State_pt;

typedef struct ubsec_SSLARC4Params_t {
  ubsec_ARC4_State_pt  KeyStateIn;       /* key or state data */
  UBS_UINT32          KeyStateFlag;  /* start with key or start from flag */
  ubsec_FragmentInfo_t  state_out;       /* state upon completing this arc4 operation */
} ubsec_ARC4Params_t, *ubsec_ARC4Params_pt;

typedef struct ubsec_HashParams_t {
  ubsec_FragmentInfo_t          OutputHMAC; /* output MAC */
} ubsec_HashParams_t, *ubsec_HashParams_pt;

typedef union ubsec_SSLParams_u {
  ubsec_SSLMACParams_t    SSLMACParams;
  ubsec_SSLCipherParams_t SSLCipherParams;
  ubsec_TLSHMACParams_t    TLSHMACParams;
  ubsec_ARC4Params_t      ARC4Params;
  ubsec_HashParams_t      HashParams;
} ubsec_SSLCommandParams_t, *ubsec_SSLCommandParams_pt;

typedef UBS_UINT32 ubsec_SSLCommand_t;

typedef struct ubsec_SSLCommandInfo_s {
  ubsec_SSLCommand_t        Command;
  ubsec_SSLCommandParams_t  Parameters;
  unsigned long             CommandContext;
  unsigned int NumSource;                    /* Number of source fragments. */
  ubsec_FragmentInfo_pt 	SourceFragments; /* Source fragment list */
  UBS_UINT32 		NumDestination;  /* Number of Destination fragments. */
  ubsec_FragmentInfo_pt           DestinationFragments;    /* Destination fragment list */
  void(*CompletionCallback)(unsigned long Context, ubsec_Status_t Result);
} ubsec_SSLCommandInfo_t, *ubsec_SSLCommandInfo_pt;


  /* SSL command execute function. */
UBSECAPI ubsec_Status_t
ubsec_SSLCommand(ubsec_DeviceContext_t Context,
		    ubsec_SSLCommandInfo_pt command,
		    int *NumCommands);



/* 
 * Extended ChipInfo prototype definitions
 */

/* Parameter structure, used at IOCTL and in SRL */
typedef struct ubsec_chipinfo_io_s {
  unsigned int     Status;
  unsigned int     CardNum;
  unsigned int     MaxKeyLen;
  unsigned short   DeviceID;
  UBS_UINT32       BaseAddress;
  int              IRQ; 
  int              NumDevices;
  unsigned int     Features;
} ubsec_chipinfo_io_t, *ubsec_chipinfo_io_pt;


/* ubsec_chipinfo_io_pt->features bit definitions */
#define UBSEC_EXTCHIPINFO_SRL_BE         0x00000001
#define UBSEC_EXTCHIPINFO_CPU_BE         0x00000002
#define UBSEC_EXTCHIPINFO_ARC4_NULL      0x00000004
#define UBSEC_EXTCHIPINFO_ARC4           0x00000008
#define UBSEC_EXTCHIPINFO_3DES           0x00000010
#define UBSEC_EXTCHIPINFO_RNG            0x00000020
#define UBSEC_EXTCHIPINFO_DBLMODEXP      0x00000040
#define UBSEC_EXTCHIPINFO_KEY_OVERRIDE   0x00000080
#define UBSEC_EXTCHIPINFO_SSL            0x00000100

/* 
 * DVT prototype definitions
 */

/* Parameter structure, used at IOCTL and in SRL */
typedef struct DVT_Params_s {
  int           Command;
  int           CardNum;
  UBS_UINT32 	InParameter;    /* should this be left as unsigned long */
  UBS_UINT32 	OutParameter;   /* should this be left as unsigned long */
  unsigned long Status;
  /* Add new structure members to end of existing member list */
} DVT_Params_t, *DVT_Params_pt;

/* DVT Command codes */
#define UBSEC_DVT_MCR1_SUSPEND    1
#define UBSEC_DVT_MCR1_RESUME     2
#define UBSEC_DVT_MCR2_SUSPEND    3
#define UBSEC_DVT_MCR2_RESUME     4
#define UBSEC_DVT_NEXT_MCR1       5
#define UBSEC_DVT_NEXT_MCR2       6
#define UBSEC_DVT_PAGESIZE        7
#define UBSEC_DVT_ALL_MCR_RESUME  8

/* pDevice->DVTOptions bit definitions */
#define UBSEC_SUSPEND_MCR1        0x00000001
#define UBSEC_SUSPEND_MCR2        0x00000002



#endif /* _UBSEC_H_ */
