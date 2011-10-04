/*
 *  linux/drivers/s390/misc/z90crypt.h
 *
 *  z90crypt 1.2.1
 *
 *  Copyright (C)  2001, 2003 IBM Corporation
 *  Author(s): Robert Burroughs (burrough@us.ibm.com)
 *             Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug support: Jochen Roehrig (roehrig@de.ibm.com)
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

#define VERSION_Z90CRYPT_H "$Revision: 1.3.4.5 $"

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


#define z90crypt_VERSION 1
#define z90crypt_RELEASE 2           // pcixcc
#define z90crypt_VARIANT 1

// This structure is DEPRECATED and the corresponding ioctl() has been replaced
// with individual ioctl()s for each piece of data!  This structure will
// NOT survive into version 1.3, so switch to the new ioctl()s.
#define MASK_LENGTH 64 // mask length
typedef struct ica_z90_status_t {
  int totalcount;
  int leedslitecount;   // PCICA
  int leeds2count;      // PCICC
  //int PCIXCCcount; // not in struct to maintain backward compatibility
  int requestqWaitCount;
  int pendingqWaitCount;
  int totalOpenCount;
  int cryptoDomain;
  unsigned char status[MASK_LENGTH]; // 0=not there. 1=PCICA. 2=PCICC. 3=PCIXCC
  unsigned char qdepth[MASK_LENGTH]; // # work elements waiting for each device
} ica_z90_status;

// May have some porting issues here

/**********************************************************************/
/* ica_rsa_modexpo_t                                                  */
/*                                                                    */
/*    char         *inputdata;                                        */
/*    unsigned int  inputdatalength;                                  */
/*    char         *outputdata;                                       */
/*    unsigned int  outputdatalength;                                 */
/*    char         *b_key;                                            */
/*    char         *n_modulus;                                        */
/*                                                                    */
/* NOTES:                                                             */
/*  - Requirements:                                                   */
/*    - outputdatalength is at least as large as inputdatalength.     */
/*    - All key parts are right justified in their fields, padded on  */
/*      the left with zeroes.                                         */
/*    - length(b_key) = inputdatalength                               */
/*    - length(n_modulus) = inputdatalength                           */
/**********************************************************************/
typedef struct _ica_rsa_modexpo {
  char         *inputdata;
  unsigned int  inputdatalength;
  char         *outputdata;
  unsigned int  outputdatalength;
  char         *b_key;
  char         *n_modulus;
} ica_rsa_modexpo_t;

typedef ica_rsa_modexpo_t ica_rsa_modmult_t;

/**********************************************************************/
/* ica_rsa_modexpo_crt_t                                              */
/*                                                                    */
/*    char         *inputdata;                                        */
/*    unsigned int  inputdatalength;                                  */
/*    char         *outputdata;                                       */
/*    unsigned int  outputdatalength;                                 */
/*    char         *bp_key;                                           */
/*    char         *bq_key;                                           */
/*    char         *np_prime;                                         */
/*    char         *nq_prime;                                         */
/*    char         *u_mult_inv;                                       */
/*                                                                    */
/* NOTES:                                                             */
/*  - Requirements:                                                   */
/*    - inputdatalength is even.                                      */
/*    - outputdatalength is at least as large as inputdatalength.     */
/*    - All key parts are right justified in their fields, padded on  */
/*      the left with zeroes.                                         */
/*    - length(bp_key)     = inputdatalength/2 + 8                    */
/*    - length(bq_key)     = inputdatalength/2                        */
/*    - length(np_key)     = inputdatalength/2 + 8                    */
/*    - length(nq_key)     = inputdatalength/2                        */
/*    - length(u_mult_inv) = inputdatalength/2 + 8                    */
/**********************************************************************/
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



#define Z90_IOCTL_MAGIC 'z'  // NOTE:  Need to allocate from linux folks

/*
 * Note: Some platforms only use 8 bits to define the parameter size.  As
 * the macros in ioctl.h don't seem to mask off offending bits, they look
 * a little unsafe.  We should probably just not use the parameter size
 * at all for these ioctls.  I don't know if we'll ever run on any of those
 * architectures, but seems easier just to not count on this feature.
 */

#define ICASETBIND      _IOW(Z90_IOCTL_MAGIC, 0x01, int)
#define ICAGETBIND      _IOR(Z90_IOCTL_MAGIC, 0x02, int)
#define ICAGETCOUNT     _IOR(Z90_IOCTL_MAGIC, 0x03, int)
#define ICAGETID        _IOR(Z90_IOCTL_MAGIC, 0x04, int)
#define ICARSAMODEXPO   _IOC(_IOC_READ|_IOC_WRITE, Z90_IOCTL_MAGIC, 0x05, 0)
#define ICARSACRT       _IOC(_IOC_READ|_IOC_WRITE, Z90_IOCTL_MAGIC, 0x06, 0)
#define ICARSAMODMULT   _IOC(_IOC_READ|_IOC_WRITE, Z90_IOCTL_MAGIC, 0x07, 0)
#define ICADES          _IOC(_IOC_READ|_IOC_WRITE, Z90_IOCTL_MAGIC, 0x08, 0)
#define ICADESMAC       _IOC(_IOC_READ|_IOC_WRITE, Z90_IOCTL_MAGIC, 0x09, 0)
#define ICATDES         _IOC(_IOC_READ|_IOC_WRITE, Z90_IOCTL_MAGIC, 0x0a, 0)
#define ICATDESSHA      _IOC(_IOC_READ|_IOC_WRITE, Z90_IOCTL_MAGIC, 0x0b, 0)
#define ICATDESMAC      _IOC(_IOC_READ|_IOC_WRITE, Z90_IOCTL_MAGIC, 0x0c, 0)
#define ICASHA1         _IOC(_IOC_READ|_IOC_WRITE, Z90_IOCTL_MAGIC, 0x0d, 0)
#define ICARNG          _IOC(_IOC_READ, Z90_IOCTL_MAGIC, 0x0e, 0)
#define ICAGETVPD       _IOC(_IOC_READ, Z90_IOCTL_MAGIC, 0x0f, 0)
#define ICAZ90STATUS    _IOR(Z90_IOCTL_MAGIC, 0x10, ica_z90_status)

// unrelated to ICA callers
#define Z90QUIESCE      _IO(Z90_IOCTL_MAGIC, 0x11)

// New status calls
#define Z90STAT_TOTALCOUNT      _IOR(Z90_IOCTL_MAGIC, 0x40, int)
#define Z90STAT_PCICACOUNT      _IOR(Z90_IOCTL_MAGIC, 0x41, int)
#define Z90STAT_PCICCCOUNT      _IOR(Z90_IOCTL_MAGIC, 0x42, int)
#define Z90STAT_PCIXCCCOUNT     _IOR(Z90_IOCTL_MAGIC, 0x43, int)
#define Z90STAT_REQUESTQ_COUNT  _IOR(Z90_IOCTL_MAGIC, 0x44, int)
#define Z90STAT_PENDINGQ_COUNT  _IOR(Z90_IOCTL_MAGIC, 0x45, int)
#define Z90STAT_TOTALOPEN_COUNT _IOR(Z90_IOCTL_MAGIC, 0x46, int)
#define Z90STAT_DOMAIN_INDEX    _IOR(Z90_IOCTL_MAGIC, 0x47, int)
#define Z90STAT_STATUS_MASK     _IOR(Z90_IOCTL_MAGIC, 0x48, char[64])
#define Z90STAT_QDEPTH_MASK     _IOR(Z90_IOCTL_MAGIC, 0x49, char[64])
#define Z90STAT_PERDEV_REQCNT   _IOR(Z90_IOCTL_MAGIC, 0x4a, int[64])

/**********************************************************************/
/* Interface notes:                                                   */
/*                                                                    */
/* A number of ioctl()s are declared above and are not actually       */
/* implemented.  This was in prep for possible future expansion.      */
/* In short, if you invoke one of them, you will receive a -1 with    */
/* errno set to ENOTTY, at which point you can fall back to your      */
/* software implementation, or return a failure to your caller.       */
/*                                                                    */
/* The ioctl()s which are implemented (along with relevant details)   */
/* are:                                                               */
/*                                                                    */
/*   ICARSAMODEXPO                                                    */
/*                                                                    */
/*     Perform an RSA operation using a Modulus-Exponent pair         */
/*     This takes an ica_rsa_modexpo_t struct as its arg.             */
/*                                                                    */
/*     NOTE: please refer to the comments preceding this structure    */
/*           for the implementation details for the contents of the   */
/*           block                                                    */
/*                                                                    */
/*   ICARSACRT                                                        */
/*                                                                    */
/*     Perform an RSA operation using a Chinese-Remainder Theorem key */
/*     This takes an ica_rsa_modexpo_crt_t struct as its arg.         */
/*                                                                    */
/*     NOTE: please refer to the comments preceding this structure    */
/*           for the implementation details for the contents of the   */
/*           block                                                    */
/*                                                                    */
/*   Z90STAT_TOTALCOUNT                                               */
/*                                                                    */
/*     Return an integer count of all device types together.          */
/*                                                                    */
/*   Z90STAT_PCICACOUNT                                               */
/*                                                                    */
/*     Return an integer count of all PCICAs.                         */
/*                                                                    */
/*   Z90STAT_PCICCCOUNT                                               */
/*                                                                    */
/*     Return an integer count of all PCICCs.                         */
/*                                                                    */
/*   Z90STAT_PCIXCCCOUNT                                              */
/*                                                                    */
/*     Return an integer count of all PCIXCCs.                        */
/*                                                                    */
/*   Z90STAT_REQUESTQ_COUNT                                           */
/*                                                                    */
/*     Return an integer count of the number of entries waiting to be */
/*     sent to a device.                                              */
/*                                                                    */
/*   Z90STAT_PENDINGQ_COUNT                                           */
/*                                                                    */
/*     Return an integer count of the number of entries sent to a     */
/*     device awaiting the reply.                                     */
/*                                                                    */
/*   Z90STAT_TOTALOPEN_COUNT                                          */
/*                                                                    */
/*     Return an integer count of the number of open file handles.    */
/*                                                                    */
/*   Z90STAT_DOMAIN_INDEX                                             */
/*                                                                    */
/*     Return the integer value of the Cryptographic Domain.          */
/*                                                                    */
/*   Z90STAT_STATUS_MASK                                              */
/*                                                                    */
/*     Return an 64 element array of unsigned chars for the status of */
/*     all devices.                                                   */
/*       0x01: PCICA                                                  */
/*       0x02: PCICC                                                  */
/*       0x03: PCIXCC                                                 */
/*       0x0d: device is disabled via the proc filesystem             */
/*                                                                    */
/*   Z90STAT_QDEPTH_MASK                                              */
/*                                                                    */
/*     Return an 64 element array of unsigned chars for the queue     */
/*     depth of all devices.                                          */
/*                                                                    */
/*   Z90STAT_PERDEV_REQCNT                                            */
/*                                                                    */
/*     Return an 64 element array of unsigned integers for the number */
/*     of successfully completed requests per device since the device */
/*     was detected and made available.                               */
/*                                                                    */
/*   ICAZ90STATUS (deprecated)                                        */
/*                                                                    */
/*     Return some device driver status in a ica_z90_status struct    */
/*     This takes an ica_z90_status struct as its arg.                */
/*                                                                    */
/*     NOTE: this ioctl() is deprecated, and has been replaced with   */
/*           single ioctl()s for each type of status being requested  */
/*                                                                    */
/*   Z90QUIESCE (not recommended)                                     */
/*                                                                    */
/*     Quiesce the driver.  This is intended to stop all new          */
/*     requests from being processed.  Its use is not recommended,    */
/*     except in circumstances where there is no other way to stop    */
/*     callers from accessing the driver.  Its original use was to    */
/*     allow the driver to be "drained" of work in preparation for    */
/*     a system shutdown.                                             */
/*                                                                    */
/*     NOTE: once issued, this ban on new work cannot be undone       */
/*           except by unloading and reloading the driver.            */
/*                                                                    */
/**********************************************************************/

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


typedef struct ica_worker {
  struct file_operations *icafops;
  void * private_data;
} ica_worker_t;

extern int ica_register_worker(int partitionnum, ica_worker_t *device);
extern int ica_unregister_worker(int partitionnum, ica_worker_t *device);

/* Event types for hotplug */

#define Z90CRYPT_HOTPLUG_ADD     1
#define Z90CRYPT_HOTPLUG_REMOVE  2

void z90crypt_hotplug_event(int, int, int);

#endif /* _LINUX_Z90CRYPT_H_ */
