
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
 * Character device header file.
 */
/*
 * Revision History:
 *
 * When       Who      What
 * June 2001  SRM      Added forced failure of a device/devices. 
 * Sept 2001  PW       Added selftest for bcmdiag
 * Sept 2000  DNA      Added SSL structures
 * May  2000  SOR/JTT  Created
 *
 */

#ifndef _UBSEC_IO_H_
#define _UBSEC_IO_H_

#include "ubsec.h"

#define UBSEC_MAJOR 99
#define MAX_COMMANDS 500
#define MAX_FRAGMENTS 64
#define MAX_FILE_SIZE  128*1024 /*(PAGE_SIZE << PAGE_ORDER_FOR_BUFS) 64*1024 */
#define PAGE_ORDER_FOR_BUFS 5

#define IV_LENGTH 8
#define MAX_CRYPT_KEY_LENGTH 24
#define MAC_KEY_LENGTH 64
#define MD5_LENGTH	16 /* Bytes */
#define SHA1_LENGTH	20 /* Bytes */

#define ENCODE			1
#define DECODE			2
#define CRYPT_3DES		4
#define CRYPT_DES		8
#define MAC_MD5			16
#define MAC_SHA1		32
#define CRYPT_NEW_KEY		128
#define DMA_FULL_NOBLOCK	512
#define USE_CALLBACK		1024
#define USE_MAC_STATES		2048

#define USING_CALLBACK(f) 	((f) & USE_CALLBACK)
#define DOING_DECODE(f)		((f) & DECODE)
#define USING_MAC_STATES(f)   	( (f) & (USE_MAC_STATES) )

#define UBSEC_DEVICE_NAME "cryptonet"
#define UBSEC_KEYDEVICE_NAME "cryptonet"

#define UBSEC_DEVICE_PRINT_NAME "BCM58xx"

/*
 * Command codes for read/write operations.
 */

#define ENCRYPT_CMD	0
#define KEYSETUP_CMD 	1
#define MATH_CMD 	2
#define RNG_CMD 	3


typedef int * PInt;



/*
 * Bulk encryption/decryption ioctl
 */

typedef struct ubsec_io_s {
  unsigned char	initial_vector[IV_LENGTH];
  unsigned char 	crypt_key[MAX_CRYPT_KEY_LENGTH]; 
  unsigned char 	mac_key[MAC_KEY_LENGTH]; 
  
  unsigned int	flags;
  unsigned int	result_status;
  unsigned int 	use_callback;
  unsigned long 	time_us;
  
  unsigned short 	crypt_header_skip;
  unsigned short 	filler;
  
  unsigned int 	num_packets;
  unsigned int 	num_fragments;
  
  void 		*source_buf;
  unsigned int 	source_buf_size;
  
  void		*dest_buf;
  unsigned int 	dest_buf_size;
  unsigned short mac_header_skip;
  unsigned int session_num;
  
} ubsec_io_t, *ubsec_io_pt;

/*
 * Key setup ioctl
 */

typedef struct ubsec_key_io_s {
	unsigned int 			result_status;
	unsigned int 			use_callback;
	unsigned long			time_us;
	ubsec_KeyCommandParams_t	key;
	ubsec_KeyCommand_t		command;
	unsigned long			user_context[2]; /* usable for anything by caller */
} ubsec_key_io_t, *ubsec_key_io_pt;

/*
 * Math Function ioctl
 */

typedef struct ubsec_math_io_s {
  unsigned int                  result_status;
  unsigned int                  use_callback;
  unsigned long                 time_us;
  ubsec_MathCommandParams_t	Math;
  ubsec_MathCommand_t           command;
  unsigned long                 user_context[2]; /* usable for anything by caller */
} ubsec_math_io_t, *ubsec_math_io_pt;

/*
 * RNG ioctl
 */

typedef struct ubsec_rng_io_s {
  unsigned int 			result_status;
  unsigned int 			use_callback;
  unsigned long			time_us;
  ubsec_RNGCommandParams_t	Rng;
  ubsec_RNGCommand_t		command;
  unsigned long			user_context[2]; /* usable for anything by caller */
} ubsec_rng_io_t, *ubsec_rng_io_pt;

/*
 * Ssl Mac Ioctl
 */

typedef struct ubsec_sslmac_io_s {
  unsigned int                   result_status;
  unsigned long                  time_us;
  ubsec_SSLCommand_t             command;
  unsigned int 	                 num_fragments;
  unsigned short                 HashAlgorithm;
  ubsec_DoubleSequenceNumber_t   SequenceNumber;
  unsigned char                  ContentType;
  unsigned short                 DataLength;
  ubsec_SSLMAC_key_t             key;
  unsigned char *                SourceBuffer;
  unsigned int                   SourceBufferBytes;
  unsigned char                  *HmacBuffer;
  unsigned int                   HmacBufferBytes;
} ubsec_sslmac_io_t, *ubsec_sslmac_io_pt;

/*
 *  Ssl Cipher Ioctl (SSL/TLS DES/3DES)
 */

typedef struct ubsec_sslcipher_io_s {
  unsigned int        result_status;
  unsigned long       time_us;
  unsigned int 	      num_fragments;
  ubsec_SSLCommand_t  command;
  unsigned char       InitialVector[IV_LENGTH];
  unsigned char       CryptKey[MAX_CRYPT_KEY_LENGTH]; 
  unsigned char *     SourceBuffer;
  unsigned int        SourceBufferBytes;
  unsigned char *     DestBuffer;
  unsigned int        DestBufferBytes;
} ubsec_sslcipher_io_t, *ubsec_sslcipher_io_pt;

/*
 *  Ssl Arc4 Ioctl
 */

#define ARC4_KEY                      	(1)
#define ARC4_STATE                    	(2)
#define ARC4_STATE_SUPPRESS_WRITEBACK 	(4)
#define ARC4_NULL_DATA_MODE           	(8)
#define ARC4_INDICESKEYSTATE_BUF_LEN  	(260)

typedef struct ubsec_arc4_io_s {
  unsigned int        result_status;
  unsigned long       time_us;
  unsigned int 	      num_fragments;
  ubsec_SSLCommand_t  command;
  unsigned char       KeyStateFlag;
  unsigned char *     KeyStateInBuffer;
  unsigned int        KeyStateInBufferBytes;
  unsigned char *     SourceBuffer;
  unsigned int        SourceBufferBytes;
  unsigned char *     DestBuffer;
  unsigned int        DestBufferBytes;
  unsigned char *     StateOutBuffer;
  unsigned int        StateOutBufferBytes;
} ubsec_arc4_io_t, *ubsec_arc4_io_pt;


/*
 * Tls Ioctl
 */

typedef struct ubsec_tlsmac_io_s {
  unsigned int                  result_status;
  unsigned long                 time_us;
  unsigned int 	                num_fragments;
  ubsec_SSLCommand_t            command;
  unsigned short                HashAlgorithm;
  ubsec_DoubleSequenceNumber_t  SequenceNumber;
  unsigned char                 ContentType;
  unsigned short                Version;
  unsigned short                DataLength;
  unsigned char                 MacKey[MAC_KEY_LENGTH]; 
  unsigned char                 InnerState[UBSEC_HMAC_LENGTH];
  unsigned char                 OuterState[UBSEC_HMAC_LENGTH];
  unsigned char *               SourceBuffer;
  unsigned int                  SourceBufferBytes;
  unsigned char *               HmacBuffer;
  unsigned int                  HmacBufferBytes;
} ubsec_tlsmac_io_t, *ubsec_tlsmac_io_pt;

/*
 * Raw Hash Ioctl
 */

typedef struct ubsec_hash_io_s {
  unsigned int                   result_status;
  unsigned long                  time_us;
  unsigned int                   num_fragments;
  ubsec_SSLCommand_t             command;
  int                            auth_alg;
  unsigned char *                SourceBuffer;
  unsigned int                   SourceBufferBytes;
  unsigned char                  HashBuffer[UBSEC_HMAC_LENGTH];
  unsigned int                   HashBufferBytes;
} ubsec_hash_io_t, *ubsec_hash_io_pt;

/*
 *  Chip Info Ioctl (to be phased out)
 */

typedef struct linux_chipinfo_io_s {
  unsigned int     result_status;
  unsigned long    time_us;
  unsigned int     max_key_len;
} linux_chipinfo_io_t, *linux_chipinfo_io_pt;


/*
 * Bulk encryption/decryption read/write. Passed to driver by application prepended onto
 * the data to be encrypted or decrypted. Since the driver supports scatter/gather, this
 * is not a real problem. For encryption, data must be padded on an eight byte boundary.
 * This structure is updated and returned to the caller.
 */

typedef struct ubsec_encrypt_rw_s {
	unsigned int		cmd_type;		/* ENCRYPT_CMD */
	unsigned long		user_context[2];
	ubsec_CipherCommand_t	command;	

	unsigned char		initial_vector[IV_LENGTH];
	unsigned char 		crypt_key[MAX_CRYPT_KEY_LENGTH]; 
	unsigned char 		mac_key[MAC_KEY_LENGTH]; 

	unsigned int		flags;
	unsigned int		result_status;
	unsigned long 		time_us;

	unsigned short 		source_buf_size;
	unsigned short	 	crypt_header_skip;

	unsigned short		num_packets;		/* must be one for now */
	unsigned short 		num_fragments;

	unsigned char		authenticator[SHA1_LENGTH];

} ubsec_encrypt_rw_t, *ubsec_encrypt_rw_pt;


/*
 * Key setup read/write
 */
typedef struct ubsec_key_rw_s {
	unsigned int			cmd_type;		/* KEY_SETUP */
	unsigned long			user_context[2];	/* returned to user untouched */
	ubsec_KeyCommand_t		command;

	unsigned int			flags;
	unsigned int 			result_status;
        unsigned long	   	        time_us;

	ubsec_KeyCommandParams_t	key;

} ubsec_key_rw_t, *ubsec_key_rw_pt;


/*
 * Math func read/write
 */
typedef struct ubsec_math_rw_s {
	unsigned int			cmd_type;		/* MATH */
	unsigned long			user_context[2];	/* returned to user untouched */
	ubsec_MathCommand_t		command;

	unsigned int			flags;
	unsigned int 			result_status;
	unsigned long			time_us;

	ubsec_MathCommandParams_t	Math;

} ubsec_math_rw_t, *ubsec_math_rw_pt;


/*
 * RNG func read/write
 */
typedef struct ubsec_rng_rw_s {
	unsigned int			cmd_type;		/* RNG */
	unsigned long			user_context[2];	/* returned to user untouched */
	ubsec_RNGCommand_t		command;

	unsigned int			flags;
	unsigned int 			result_status;
	unsigned long			time_us;

	ubsec_RNGCommandParams_t	rng;

} ubsec_rng_rw_t, *ubsec_rng_rw_pt;

/*
 * Command packet used by read/write routines.
 * Filled in by caller, copied to kernel space.
 */
typedef union ubsec_rw_s {
	ubsec_encrypt_rw_t	encrypt;
	ubsec_key_rw_t		key;
	ubsec_math_rw_t		math;
	ubsec_rng_rw_t		rng;
} ubsec_rw_t, *ubsec_rw_pt;

/*********************************************************** Statistics */
typedef struct ubsec_stats_io_s {
  unsigned int        result_status;
  unsigned long       time_us;
  int                 device_num;
  ubsec_Statistics_t  dev_stats;
} ubsec_stats_io_t, *ubsec_stats_io_pt;

/* Function Ptrs ioctl */

typedef struct ubsec_Function_Ptrs_s {
/* DeviceInfoList pointer */
void *PhysDeviceInfoList_Ptr; /* to contains &DeviceInfoList */

/* Allocate DMA Memory function */
void * (*OS_AllocateDMAMemory_Fptr)(ubsec_DeviceContext_t * ,
		int size);

/* Free DMA Memory function */
void  (*OS_FreeDMAMemory_Fptr)(void * ,
		int size);

/* Initialize HMAC state function */
ubsec_Status_t (*ubsec_InitHMACState_Fptr)(ubsec_HMAC_State_pt ,
	      ubsec_CipherCommand_t ,
	      ubsec_HMAC_Key_pt) ;

/* Cipher command execute function */
ubsec_Status_t (*ubsec_CipherCommand_Fptr)(ubsec_DeviceContext_t ,
		    ubsec_CipherCommandInfo_pt ,
		    int *);
/* RNG command execute function */
ubsec_Status_t (*ubsec_RNGCommand_Fptr)(ubsec_DeviceContext_t,
	      ubsec_RNGCommandInfo_pt ,
	      int *);

}ubsec_Function_Ptrs_t;

/*
 * Ioctl command codes.
 */

#define BRCM_IOC_MAGIC  'Y'
#define UBSEC_ENCRYPT_DECRYPT_FUNC  _IOWR(BRCM_IOC_MAGIC,  1, ubsec_io_pt)
#define UBSEC_KEY_SETUP_FUNC	    _IOWR(BRCM_IOC_MAGIC,  2, ubsec_key_io_pt)
#define UBSEC_MATH_FUNC		    _IOWR(BRCM_IOC_MAGIC,  3, ubsec_math_io_pt)
#define UBSEC_RNG_FUNC		    _IOWR(BRCM_IOC_MAGIC,  4, ubsec_rng_io_pt)
#define UBSEC_SSL_MAC_FUNC	    _IOWR(BRCM_IOC_MAGIC,  5, ubsec_sslmac_io_pt)
#define UBSEC_SSL_DES_FUNC	    _IOWR(BRCM_IOC_MAGIC,  6, ubsec_sslcipher_io_pt)
#define UBSEC_TLS_HMAC_FUNC	    _IOWR(BRCM_IOC_MAGIC,  7, ubsec_tlsmac_io_pt)
#define UBSEC_SSL_ARC4_FUNC	    _IOWR(BRCM_IOC_MAGIC,  8, ubsec_arc4_io_pt)
#define UBSEC_SSL_HASH_FUNC         _IOWR(BRCM_IOC_MAGIC,  9, ubsec_hash_io_pt)
#define UBSEC_CHIPINFO_FUNC         _IOWR(BRCM_IOC_MAGIC, 10, linux_chipinfo_io_pt)
#define UBSEC_SELFTEST              _IOWR(BRCM_IOC_MAGIC, 11, PInt)
#define UBSEC_GETVERSION            _IOWR(BRCM_IOC_MAGIC, 12, PInt)
#define UBSEC_RESERVED              _IOWR(BRCM_IOC_MAGIC, 13, PInt)
#define UBSEC_DEVICEDUMP            _IOWR(BRCM_IOC_MAGIC, 14, PInt)
#define UBSEC_EXTCHIPINFO_FUNC      _IOWR(BRCM_IOC_MAGIC, 15, ubsec_chipinfo_io_pt)
#define UBSEC_GETNUMCARDS           _IOWR(BRCM_IOC_MAGIC, 16, PInt)
#define UBSEC_GET_FUNCTION_PTRS     _IOWR(BRCM_IOC_MAGIC, 17, ubsec_Function_Ptrs_t *)
#define UBSEC_FAILDEVICE            _IOWR(BRCM_IOC_MAGIC, 32, PInt)
#define UBSEC_STATS_FUNC            _IOWR(BRCM_IOC_MAGIC, 64, ubsec_stats_io_pt)

#ifdef BCM_OEM_1
#define BCM_OEM_1_IOCTL1	 	_IO('L',0x11)
#define BCM_OEM_1_IOCTL2 		_IO('L',0x12)
#endif /* BCM_OEM_1 */

 
#define USING_MAC(f)   ( (f) & (MAC_MD5 | MAC_SHA1) )

#ifdef BCM_OEM_1
#include "bcm_oem1_device.h"
#endif

#endif  /* _UBSEC_IO_H_ */
