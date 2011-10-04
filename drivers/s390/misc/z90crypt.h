/*
 *  linux/drivers/s390/misc/z90crypt.h
 *
 *  z90crypt 1.1.2
 *
 *  Copyright (C)  2001-2002 IBM Corporation
 *    Author(s): Robert Burroughs (burrough us ibm com)
 *               Eric Rossman (edrossma us ibm com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _LINUX_Z90CRYPT_H_
#define _LINUX_Z90CRYPT_H_

#include <linux/config.h>

#define VERSION_Z90CRYPT_H "$Revision: 1.3.4.1 $"

enum _sizelimits {
  ICA_DES_DATALENGTH_MIN = 8,
  ICA_DES_DATALENGTH_MAX = 32 * 1024 * 1024 - 8,
  ICA_SHA_DATALENGTH = 20,
  ICA_SHA_BLOCKLENGTH = 64,
  ICA_RSA_DATALENGTH_MIN = 256/8,
  ICA_RSA_DATALENGTH_MAX = 2048/8
};


typedef struct _ica_rng_rec {
  unsigned int nbytes;
  char * buf;
} ica_rng_t;


#ifdef CONFIG_ARCH_S390
#define z90crypt_VERSION 1
#define z90crypt_RELEASE 1           // added version.release.variant
                                     // added get_zeroed_page
#define z90crypt_VARIANT 2
#ifndef _PAD_RULES_DEF_
#define PCI_FUNC_KEY_DECRYPT 0x5044
#define PCI_FUNC_KEY_ENCRYPT 0x504B
// static char PKCS_PAD_RULE[8] = {0x50,0x4b,0x43,0x53,0x2d,0x31,0x2e,0x32};
// static char ZERO_PAD_RULE[8] = {0x5a,0x45,0x52,0x4f,0x2d,0x50,0x41,0x44};
#define ML 64 // mask length
#define _PAD_RULES_DEF_ 1
#endif 

typedef struct _ext_input {
  short functioncode;
  char paddingrulelength[2];          // must be 0x0a00
  char paddingrule[8];
  char rsv1[4];
} ext_input_t;

typedef struct ica_z90_status_t {
  int totalcount;
  int leedslitecount;
  int leeds2count;
  int requestqWaitCount;
  int pendingqWaitCount;
  int totalOpenCount;
  int cryptoDomain;
  unsigned char status[ML]; // 0=not there. 1=leedslite.  2=leeds2. 
  unsigned char qdepth[ML]; // number of work elements waiting for ea. device
} ica_z90_status;

// These are indexes into the z90crypt_synchronous_mask returned by
// the synchronous status ioctl call.  
typedef enum {
  CRYPT_ASSIST,
  DES_CBCE,
  DES_CBCD,
  TDES_CBCE,
  TDES_CBCD,
  DES_ECBE,
  DES_ECBD,
  TDES_ECBE,
  TDES_ECBD,
  RNG,
  MAC,
  TMAC,
  MIDSHA,
  LASTSHA,
  MAX_SYNCH} Z90CRYPT_SYNCHRONOUS_INDEXES;

typedef struct ica_z90_query_synchronous_t {
  int bufferLength;        // must be 64
  unsigned char * buffer;
} ica_z90_query_synchronous;

#endif

// May have some porting issues here

// On *input* to IOCTL, if the buffer pointed to by *outputdata*
// begins with a 12-byte string 'PD0a00...' or 'PE0a00...',
// the function code and padding rule will be taken from outputdata.
// Otherwise, the function code will be 'PD', and the pad rule 
// will be 'PKCS-1.2'.

typedef struct _ica_rsa_modexpo {
  char         *inputdata;
  unsigned int  inputdatalength;
  char         *outputdata;
  unsigned int  outputdatalength;
  char         *b_key;
  char         *n_modulus;
} ica_rsa_modexpo_t;

typedef ica_rsa_modexpo_t ica_rsa_modmult_t;

typedef struct _ica_rsa_modexpo_crt {
  char         *inputdata;
  unsigned int  inputdatalength;
  char         *outputdata;
  unsigned int  outputdatalength;
  char         *bp_key;
  char         *bq_key;
  char         *np_prime;
  char         *nq_prime;
  char         *u_mult_inv;
} ica_rsa_modexpo_crt_t;



typedef unsigned char ica_des_vector_t[8];
typedef unsigned char ica_des_key_t[8];
typedef ica_des_key_t ica_des_single_t[1];
typedef ica_des_single_t ica_des_triple_t[3];

enum _ica_mode_des {
  DEVICA_MODE_DES_CBC = 0,
  DEVICA_MODE_DES_ECB = 1
};

enum _ica_direction_des {
  DEVICA_DIR_DES_ENCRYPT = 0,
  DEVICA_DIR_DES_DECRYPT = 1
};

typedef struct _ica_des {
  unsigned int      mode;
  unsigned int      direction;
  unsigned char    *inputdata;
  unsigned int      inputdatalength;
  ica_des_vector_t *iv;
  ica_des_key_t    *keys;
  unsigned char    *outputdata;
  int              outputdatalength;
} ica_des_t;

typedef struct _ica_desmac {
  unsigned char    *inputdata;
  unsigned int      inputdatalength;
  ica_des_vector_t *iv;
  ica_des_key_t    *keys;
  unsigned char    *outputdata;
  int              outputdatalength;
} ica_desmac_t;
  


typedef unsigned char ica_sha1_result_t[ICA_SHA_DATALENGTH];

typedef struct _ica_sha1 {
  unsigned char     *inputdata;
  unsigned int       inputdatalength;
  ica_sha1_result_t *outputdata;
  ica_sha1_result_t *initialh;
} ica_sha1_t;



#define ICA_IOCTL_MAGIC 'z'  // NOTE:  Need to allocate from linux folks

/*
 * Note: Some platforms only use 8 bits to define the parameter size.  As 
 * the macros in ioctl.h don't seem to mask off offending bits, they look
 * a little unsafe.  We should probably just not use the parameter size
 * at all for these ioctls.  I don't know if we'll ever run on any of those
 * architectures, but seems easier just to not count on this feature.
 */

#define ICASETBIND     _IOW(ICA_IOCTL_MAGIC, 0x01, int)
#define ICAGETBIND     _IOR(ICA_IOCTL_MAGIC, 0x02, int)
#define ICAGETCOUNT    _IOR(ICA_IOCTL_MAGIC, 0x03, int)
#define ICAGETID       _IOR(ICA_IOCTL_MAGIC, 0x04, int)
#define ICARSAMODEXPO  _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x05, 0)
#define ICARSACRT      _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x06, 0) 
#define ICARSAMODMULT  _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x07, 0)
#define ICADES         _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x08, 0)
#define ICADESMAC      _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x09, 0)
#define ICATDES        _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x0a, 0)
#define ICATDESSHA     _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x0b, 0)
#define ICATDESMAC     _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x0c, 0)
#define ICASHA1        _IOC(_IOC_READ|_IOC_WRITE, ICA_IOCTL_MAGIC, 0x0d, 0)
#define ICARNG         _IOC(_IOC_READ, ICA_IOCTL_MAGIC, 0x0e, 0)
#define ICAGETVPD      _IOC(_IOC_READ, ICA_IOCTL_MAGIC, 0x0f, 0)
#ifdef CONFIG_ARCH_S390
#define ICAZ90STATSZ   sizeof(ica_z90_status)
#define ICAZ90STATUS  _IOC(_IOC_READ, ICA_IOCTL_MAGIC, 0x10, ICAZ90STATSZ)
#define ICAZ90QUIESCE _IOC(_IOC_NONE, ICA_IOCTL_MAGIC, 0x11, 0)
#define ICAZ90HARDRESET _IOC(_IOC_NONE, ICA_IOCTL_MAGIC, 0x12,0)
#ifdef S390_TEST_
#define ICAZ90HARDERROR _IOC(_IOC_NONE, ICA_IOCTL_MAGIC, 0x13,0) // testing
#endif
#define ICAZ90QUERYSYNCH _IOC(_IOC_READ, ICA_IOCTL_MAGIC, 0x14,0)
#endif

#ifdef CONFIG_ARCH_S390
/*
 * errno definitions
 */
#define ENOBUFF        129     /* filp->private_data->...>work_elem_p->buffer
                                  is NULL                                   */
#define EWORKPEND      130     /* user issues ioctl while another pending   */
#define ERELEASED      131     /* user released while ioctl pending         */
#define EQUIESCE       132     /* z90crypt quiescing (no more work allowed) */
#define ETIMEOUT       133     /* request timed out                         */
#define EUNKNOWN       134     /* some unrecognized error occured           */
#define EGETBUFF       135     // Error getting buffer
#endif

#ifdef __KERNEL__

#ifndef assertk
#ifdef NDEBUG
#  define assertk(expr) do {} while (0)
#else
#  define assertk(expr) \
        if(!(expr)) {                                   \
        printk( "Assertion failed! %s,%s,%s,line=%d\n", \
        #expr,__FILE__,__FUNCTION__,__LINE__);          \
        }
#endif
#endif




typedef struct ica_worker {
  struct file_operations *icafops;
  void * private_data;  
} ica_worker_t;


extern int ica_register_worker(int partitionnum, ica_worker_t *device);
extern int ica_unregister_worker(int partitionnum, ica_worker_t *device);

#endif //__KERNEL__


#endif /* _LINUX_Z90CRYPT_H_ */

