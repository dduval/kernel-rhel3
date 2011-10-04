/*
 *
 *  linux/drivers/s390/misc/z90main.c
 *
 *  z90crypt 1.2.1
 *
 *  Copyright (C)  2001, 2003 IBM Corporation
 *  Author(s): Robert Burroughs (burrough@us.ibm.com)
 *             Eric Rossman (edrossma@us.ibm.com)
 *
 *    Support for S390 Crypto Devices
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

#include <linux/spinlock.h>
#include <linux/interrupt.h>   // for tasklets
#include <linux/config.h>      // to determine whether 64-bit
#include <linux/random.h>      // get_random_bytes
#include <linux/delay.h>       // udelay
#include <linux/stddef.h>      // NULL
#include <linux/errno.h>       // some error numbers
#include <linux/slab.h>        // kmalloc
#include <asm/uaccess.h>       // copy_from_user
#include <linux/ioctl.h>       // to make z90crypt.h work properly
#include <linux/sched.h>       // for wait_event
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/kmod.h>
#include <linux/init.h>
#include "z90crypt.h"
#include "z90common.h"

#define VERSION_CODE(vers,rel,seq) ( ((vers)<<16) | ((rel)<<8) | (seq) )
#if LINUX_VERSION_CODE < VERSION_CODE(2,4,0) /* version < 2.4 */
#  error "This kernel is too old: not supported"
#endif
#if LINUX_VERSION_CODE > VERSION_CODE(2,7,0) /* version > 2.6 */
#  error "This kernel is too recent: not supported by this file"
#endif
 
#include <linux/errno.h>   /* error codes */
#include <linux/proc_fs.h>

#define VERSION_Z90MAIN_C "$Revision: 1.9.4.8 $"
static const char version[] =
       "z90crypt.o: z90main.o ("
       "z90main.c "   VERSION_Z90MAIN_C   "/"
       "z90common.h " VERSION_Z90COMMON_H "/"
       "z90crypt.h "  VERSION_Z90CRYPT_H  ")";

/****************************************************************************
 * Ioctl definitions  See z90crypt.h                                        *
 ****************************************************************************/
/*****************************************************************************
* Compiled defaults that may be modified in one of several ways:             *
* 1) at compile time:                                                        *
*    + in this file                                                          *
*    + on the command line                                                   *
*    + in the Makefile                                                       *
* 2) on the insmod command line                                              *
*****************************************************************************/

//
// You can specify a different major at compile time.
//
#ifndef Z90CRYPT_MAJOR
#define Z90CRYPT_MAJOR  0
#endif

//
// You can specify a different domain at compile time or on the insmod
// command line.
//
#ifndef DOMAIN_INDEX
#define DOMAIN_INDEX   -1
#endif

//
// This is the name under which the device is registered in /proc/modules.
//
#define REG_NAME        "z90crypt"

#define ENOBUFF        129     /* filp->private_data->...>work_elem_p->buffer
                                  is NULL                                   */
#define EWORKPEND      130     /* user issues ioctl while another pending   */
#define ERELEASED      131     /* user released while ioctl pending         */
#define EQUIESCE       132     /* z90crypt quiescing (no more work allowed) */
#define ETIMEOUT       133     /* request timed out                         */
#define EUNKNOWN       134     /* some unrecognized error occured           */
#define EGETBUFF       135     // Error getting buffer

//
// Cleanup should run every CLEANUPTIME seconds and should clean up requests
// older than CLEANUPTIME seconds in the past.
//
#ifndef CLEANUPTIME
#define CLEANUPTIME 15
#endif

//
// Config should run every CONFIGTIME seconds
//
#ifndef CONFIGTIME
#define CONFIGTIME 30
#endif

//
// The first execution of the config task should take place
// immediately after initialization
//
#ifndef INITIAL_CONFIGTIME
#define INITIAL_CONFIGTIME 1
#endif

//
// Reader should run every READERTIME milliseconds
//
#ifndef READERTIME
#define READERTIME 2
#endif

// turn long device array index into device pointer
#define LONG2DEVPTR(ndx) (z90crypt.z90c_device_p[(ndx)])

// turn short device array index into long device array index
#define SHRT2LONG(ndx)\
                     (z90crypt.z90c_overall_device_x.z90c_device_index[(ndx)])

// turn short device array index into device pointer
#define SHRT2DEVPTR(ndx) LONG2DEVPTR(SHRT2LONG(ndx))

//
// The default status is 0x00
//
#define STAT_DEFAULT  0x00 /* STAT_ROUTED bit is off,            */

#define STAT_ROUTED   0x80 /* bit 7: requests get routed to specific device
                                  otherwise, device is determined each write*/
#define STAT_FAILED   0x40 /* bit 6: this bit is set if the request failed
                                     before being sent to the hardware.     */
#define STAT_WRITTEN  0x30 /* bits 5,4: work to be done, not sent to device */
#define STAT_QUEUED   0x20 /* bits 5,4: work has been sent to a device      */
#define STAT_READPEND 0x10 /* bits 5,4: work done, we're returning data now */
#define STAT_NOWORK   0x00 /* bits off: no work on any queue                */


// Audit Trail.  Progress of a Work element
// audit[0]:  Unless noted otherwise, these bits are all set by the process
#define FP_COPYFROM   0x80 // Caller's buffer has been copied to work element
#define FP_BUFFREQ    0x40 // Low Level buffer requested
#define FP_BUFFGOT    0x20 // Low Level buffer obtained
#define FP_SENT       0x10 // Work element sent to a crypto device
                           // Above may be set by process or by reader task
#define FP_PENDING    0x08 // Work element placed on pending queue
                           // Above may be set by process or by reader task
#define FP_REQUEST    0x04 // Work element placed on request queue
#define FP_ASLEEP     0x02 // Work element about to sleep
#define FP_AWAKE      0x01 // Work element has been awakened
// audit[1]:  These bits are set by the reader task and/or the cleanup task
#define FP_NOTPENDING 0x80 // Work element removed from pending queue
#define FP_AWAKENING  0x40 // Caller about to be awakened
#define FP_TIMEDOUT   0x20 // Caller timed out
#define FP_RESPSIZESET 0x10 // Response size copied to work element
#define FP_RESPADDRCOPIED 0x08 // Response address copied to work element
#define FP_RESPBUFFCOPIED 0x04 // Response buffer copied to work element
#define FP_REMREQUEST 0x02 // Work element removed from request queue
#define FP_SIGNALED   0x01 // Work element was awakened by a signal
// audit[2]:  Reserved

//
// bits in work_element_t.status[0]
//
#define STAT_RDWRMASK 0x30                   // caller's current state
#define CHECK_RDWRMASK(statbyte) ((statbyte) & STAT_RDWRMASK)
#define SET_RDWRMASK(statbyte, newval) \
  {(statbyte) &= ~STAT_RDWRMASK; (statbyte) |= newval;}

//
// state of the file handle in private_data.status
//
#define STAT_OPEN 0
#define STAT_CLOSED 1

//
// PID() expands to the process ID of the current process
//
#define PID() (current->pid)

//
// BUFFERSIZE is the maximum size request buffer passed in an ioctl call
//
#define BUFFERSIZE 128

/*-------------------------------------------------------------------*/
/* Selected Constants.  The number of APs and the number of devices  */
/*-------------------------------------------------------------------*/
#ifndef Z90CRYPT_NUM_APS
#define Z90CRYPT_NUM_APS 64
#endif
#ifndef Z90CRYPT_NUM_DEVS
#define Z90CRYPT_NUM_DEVS Z90CRYPT_NUM_APS
#endif
#ifndef Z90CRYPT_NUM_TYPES
#define Z90CRYPT_NUM_TYPES 3
#endif
/*-------------------------------------------------------------------*/
/* Buffer size for receiving responses.  The maximum Response Size   */
/* is actually the maximum request size, since in an error condition */
/* the request itself may be returned unchanged.                     */
/*-------------------------------------------------------------------*/
#ifndef MAX_RESPONSE_SIZE
#define MAX_RESPONSE_SIZE 0x0000077C
#endif

/*-------------------------------------------------------------------*/
/* z90c_status_str                                                   */
/*                                                                   */
/* A count and status-byte mask                                      */
/*                                                                   */
/*-------------------------------------------------------------------*/
typedef struct z90c_status_str {
	int z90c_st_count;			// number of enabled devices
	int z90c_disabled_count;		// number of disabled devices
	int z90c_explicitly_disabled_count;	// number of devices disabled
						// via the proc fs
	UCHAR z90c_st_mask[Z90CRYPT_NUM_APS];
} Z90C_STATUS_STR;

/*-------------------------------------------------------------------*/
/* The array of device indexes is a mechanism for fast indexing into */
/* a long (and sparse) array.  For instance, if APs 3, 9 and 47 are  */
/* installed, z90CDeviceIndex[0] is 3, z90CDeviceIndex[1] is 9, and  */
/* z90CDeviceIndex[2] is 47.                                         */
/*-------------------------------------------------------------------*/
typedef struct z90c_device_x {
	int  z90c_device_index[Z90CRYPT_NUM_DEVS];
} Z90C_DEVICE_X;

/*-------------------------------------------------------------------*/
/* z90crypt is the topmost data structure in the hierarchy.          */
/*                                                                   */
/*-------------------------------------------------------------------*/
typedef struct z90crypt {
	int   z90c_max_count;            // Nr of possible crypto devices
	struct z90c_status_str z90c_mask; // Hardware Status for all APs
	int   z90c_q_depth_array[Z90CRYPT_NUM_DEVS]; // queue depths
	int   z90c_dev_type_array[Z90CRYPT_NUM_DEVS]; // device types
	struct z90c_device_x z90c_overall_device_x; // array device indexes
	struct z90c_device *z90c_device_p[Z90CRYPT_NUM_DEVS]; // Addr array
	UCHAR z90c_rsvd[8];              // Reserved
	UCHAR z90c_terminating;          // 1:  terminating
	BOOL  z90c_domain_established;   // TRUE:  domain has been found
	UCHAR z90c_stat[1];              // Reserved for add'l status info
	int   z90c_cdx;                  // Crypto Domain Index
	int   z90c_len;                  // Length of this data structure
	struct z90c_hdware_block * z90c_hdware_info;
} Z90CRYPT;

/*-------------------------------------------------------------------*/
/* z90c_hdware_block                                                 */
/*   There's a status_str and a device_index_array for each          */
/*   device type.  When created, this block will contain:            */
/*                                                                   */
/*     +----------------------...-----------------+                  */
/*     |   mask of available devices              |                  */
/*     +----+-------------------------------------+                  */
/*     |   3|                                                        */
/*     +----+-----------------...-----------------+                  */
/*     |   mask of available PCICCs               |                  */
/*     +----------------------...-----------------+                  */
/*     |   mask of available PCICAs               |                  */
/*     +----------------------...-----------------+                  */
/*     |   mask of available PCIXCCs              |                  */
/*     +----------------------...-----------------+                  */
/*     |   index array of available PCICCs        |                  */
/*     +----------------------...-----------------+                  */
/*     |   index array of available PCICAs        |                  */
/*     +----------------------...-----------------+                  */
/*     |   index array of available PCIXCCs       |                  */
/*     +----------------------...-----------------+                  */
/*     |   mask of device types                   |                  */
/*     +----------------------...-----------------+                  */
/*                                                                   */
/*-------------------------------------------------------------------*/
typedef struct z90c_hdware_block {
	struct z90c_status_str z90c_hdware_mask;
	int z90c_nr_types;
	struct z90c_status_str z90c_type_mask[Z90CRYPT_NUM_TYPES];
	struct z90c_device_x z90c_type_x_addr[Z90CRYPT_NUM_TYPES];
	UCHAR  z90c_device_type_array[Z90CRYPT_NUM_APS];
} Z90C_HDWARE_BLOCK;

/*-------------------------------------------------------------------*/
/* The queue depth array has the maximum number of elements in the   */
/* work queue for a given device.                                    */
/*-------------------------------------------------------------------*/
typedef struct z90c_q_depth_array {
	int z90c_queue_depth[Z90CRYPT_NUM_DEVS];
} Z90C_Q_DEPTH_ARRAY;

/*-------------------------------------------------------------------*/
/* All devices are arranged in a single array: 64 APs                */
/*-------------------------------------------------------------------*/
typedef struct z90c_device {
	CDEVICE_TYPE z90c_dev_type;      // PCICA, PCICC, or PCIXCC
	DEVSTAT z90c_dev_stat;           // current device status
	int    z90c_dev_self_x;          // Index in array
	int    z90c_dev_len;             // Length of this struct
	BOOL   z90c_disabled;            // Set when device is in error or
					 // is explicitly disabled
	BOOL   z90c_explicitly_disabled; // Set when device is explicitly
					 // disabled
	char   z90c_dev_sn[8];           // Reserved
	int    z90c_dev_cdx;             // Crypto Domain Index
	int    z90c_dev_q_depth;         // q depth
	UCHAR *z90c_dev_resp_p;          // Response buffer address
	int    z90c_dev_resp_l;          // Response Buffer length
	UCHAR  z90c_dev_stat_info[4];    // Reserved for additional stat info
	int    z90c_dev_caller_count;    // Number of callers
	int    z90c_dev_total_req_cnt;   // # of requests for device since load
	struct list_head z90c_dev_caller_list; // List of callers
} Z90C_DEVICE;

/*-------------------------------------------------------------------*/
/* z90c_caller                                                       */
/*                                                                   */
/* An array of these structures is pointed to from z90c_dev_callr    */
/* The length of the array depends on the device type. For APs,      */
/* there are 8.                                                      */
/*                                                                   */
/* The z90c_request is permanently associated with this descriptor.  */
/* The z90c_caller_buf is allocated to the user at OPEN.  At  WRITE, */
/* it contains the request; at READ, the response.   The function    */
/* send_to_crypto_device converts the request to device-dependent    */
/* form and use the caller's OPEN-allocated buffer for the response. */
/*                                                                   */
/*-------------------------------------------------------------------*/
typedef struct z90c_buffer {
	int z90c_buff_len;
	UCHAR * z90c_buff;
} Z90C_BUFFER;

typedef struct z90c_caller {
	int    z90c_caller_buf_l;        // length of original request
	UCHAR *z90c_caller_buf_p;        // Original request on WRITE
	int    z90c_caller_dev_dep_req_l; // length of device dependent requ
	UCHAR *z90c_caller_dev_dep_req_p; // Device dependent form of requ
	UCHAR  z90c_caller_status[4];    //
	UCHAR  z90c_caller_id[8];        // program supplied message id
	int    z90c_caller_func;         // mod_expo or crt
	HWRD   z90c_caller_function;     // decrypt or encrypt
	UCHAR  z90c_pad_rule[8];         // padding rule
	struct z90c_device *z90c_caller_dev_p; // pointer to a Z90C_DEVICE
	struct list_head z90c_caller_liste; // list element
	long   z90c_caller_sen_time;     // time at which req was sent
	long   z90c_caller_rec_time;     // time at which resp was received
	UCHAR  z90c_caller_dev_dep_req[MAX_RESPONSE_SIZE]; // response buffer
} Z90C_CALLER;

/*-------------------------------------------------------------------*/
/* A generic descriptor is returned by receive_from_crypto_device.   */
/* In most cases, it's a caller descriptor, but in case of error it  */
/* may be a device descriptor.                                       */
/*-------------------------------------------------------------------*/
typedef union z90c_descriptor {
	struct z90c_caller clr;
	struct z90c_device dev;
} Z90C_DESCRIPTOR;

/*-------------------------------------------------------------------*/
/* Function Codes.  Because the IOCTL buffer contains no indicator   */
/* of what sort of request this is, a function code is a necessary   */
/* input to send_to_crypto_device.                                   */
/*                                                                   */
/* These will be found in z90crypt.h                                 */
/*                                                                   */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/
/* Low level function prototypes                                     */
/*-------------------------------------------------------------------*/
int create_z90crypt (int * cdx_array);
int refresh_z90crypt (int * cdx_p);
int find_crypto_devices(Z90C_STATUS_STR *  deviceMask);
int create_crypto_device(int index);
int destroy_crypto_device(int index);
int get_crypto_request_buffer(UCHAR * psmid,
			int func,
			int buff_len,
			UCHAR * buff_ptr,
			CDEVICE_TYPE * devType_p,
			unsigned char * reqBuffp);
int send_to_crypto_device(UCHAR * psmid,
			int func,
			int buff_len,
			UCHAR * buff_ptr,
			CDEVICE_TYPE devType,
			int * devNr_p,
			unsigned char * reqBuff);
int receive_from_crypto_device(int index,
			UCHAR * psmid,
			int * buff_len_p,
			UCHAR * buff,
			UCHAR ** dest);
int query_z90crypt(Z90C_STATUS_STR * z90cryptStatus);
DEVSTAT query_crypto_device(int index);
int destroy_z90crypt(void);
int refresh_index_array(struct z90c_status_str *status_str,
			struct z90c_device_x *index_array);
int select_device (CDEVICE_TYPE *devType_p, int *device_nr_p);
int select_device_type (CDEVICE_TYPE *devType_p);
int get_test_msg(UCHAR *, UINT *);
int test_reply(UCHAR *);
int probe_device_type(Z90C_DEVICE *);
int probe_crypto_domain(int *);
int isPKCS1_2padded(unsigned char *, int);
int isPKCS1_1Padded(unsigned char *, int);
int remove_device(Z90C_DEVICE *);
int build_caller(char * psmid,
                int func,
                short function,
                UCHAR * pad_rule,
                int buff_len,
                char * buff_ptr,
                CDEVICE_TYPE dev_type,
                Z90C_CALLER * caller_p);
int unbuild_caller(Z90C_DEVICE * device_p,
                  Z90C_CALLER * caller_p);

//
// proc fs definitions
//
struct proc_dir_entry * z90crypt_entry;

/*--------------------------------------------------------------------------*
 * data structures                                                          *
 *--------------------------------------------------------------------------*/

//
// filp->private_data points to this structure
// work_element.opener points back to this structure
//
typedef struct private_data {
	unsigned char       status;      // 0: open  1: closed
} private_data_t;

//
// A work element is allocated for each rsa request
//
typedef struct work_element {
	struct private_data  *opener_p; // private data of opening pid
	pid_t                pid;       // pid of this work element
	int                  devindex;  // index of device processing this w_e
					// (If request did not specify device,
					// -1 until placed onto a queue)
	CDEVICE_TYPE         devtype;     // device type
	struct list_head     liste;       // used for requestq and pendingq
	char          buffer[BUFFERSIZE]; // local copy of user request
	int                  buffsize;    // size of the buffer for the request
	char      respbuff[RESPBUFFSIZE]; // response buffer
	int                  respsize;    // size of the response buffer
	char                 *respaddr;   // address of response in user space
	unsigned int         funccode;    // function code of request
	wait_queue_head_t    waitq;       // waitq for this w_e
	unsigned long        requestsent; // time at which the request was sent
	atomic_t             alarmrung;   // wake-up signal
	unsigned char        caller_id[8];// pid + counter, for this w_e
	unsigned char        status[1];   // bits to mark status of the request
	unsigned char        audit[3];    // record of work element's progress
	unsigned char      * requestptr;  // address of request buffer
	int                  returncode;  // return code of request
} work_element_t;

/*--------------------------------------------------------------------------*
 * High level function prototypes                                           *
 *--------------------------------------------------------------------------*/
int z90crypt_open(struct inode *, struct file *);
int z90crypt_release(struct inode *, struct file *);

ssize_t z90crypt_read(struct file *, char *, size_t, loff_t *);
ssize_t z90crypt_write(struct file *, const char *, size_t, loff_t *);

int z90crypt_ioctl(struct inode *, struct file *, unsigned int,unsigned long);
int z90crypt_rsa(private_data_t *, pid_t, unsigned int, unsigned long);
int z90crypt_prepare(work_element_t *, unsigned int, const char *);
int z90crypt_send(work_element_t *, const char *);
int z90crypt_process_results(work_element_t *, char *);

int z90crypt_schedule_reader_timer(void);
void z90crypt_schedule_reader_task(unsigned long);
void z90crypt_config_task(unsigned long);
int z90crypt_schedule_config_task(unsigned int);
void z90crypt_cleanup_task (unsigned long);
int z90crypt_schedule_cleanup_task(void);

int allocate_work_element(work_element_t **,private_data_t *, pid_t);
int create_work_element(work_element_t **, pid_t);
int init_work_element(work_element_t *, private_data_t *, pid_t);
int free_work_element(work_element_t *);
void destroy_private_data(private_data_t *);
void destroy_work_element(work_element_t *);

int z90crypt_status(char *, char **, off_t, int, int *, void*);
int z90crypt_status_write(struct file *, const char*, unsigned long, void *);
int sprinthx(unsigned char *, unsigned char *, unsigned char *, unsigned int);
int sprinthx4(unsigned char *, unsigned char *, unsigned int *, unsigned int);
int sprintrw(unsigned char *, unsigned char *, unsigned int);
int sprintcl(unsigned char *, unsigned char *, unsigned int);


/*--------------------------------------------------------------------------*/
/* Storage allocated at initialization and used throughout the life of      */
/* this insmod                                                              */
/*--------------------------------------------------------------------------*/
int z90crypt_major = Z90CRYPT_MAJOR;
int domain         = DOMAIN_INDEX;
Z90CRYPT z90crypt;
BOOL quiesce_z90crypt;
spinlock_t queuespinlock;
struct list_head request_list;
int requestq_count;
struct list_head pending_list;
int pendingq_count;
unsigned char z90crypt_random_seed[16];

void (z90crypt_reader_task)(unsigned long);
struct tasklet_struct reader_tasklet;
struct timer_list reader_timer;
struct timer_list config_timer;
struct timer_list cleanup_timer;
struct timer_list random_seeder;
atomic_t total_open;
atomic_t z90crypt_step;            // unique identifier for work element     

struct file_operations z90crypt_fops =
{
	owner:		THIS_MODULE,
	read:		z90crypt_read,
	write:		z90crypt_write,
	ioctl:		z90crypt_ioctl,
	open:		z90crypt_open,
	release:	z90crypt_release
};
/*---------------------------------------------------------------------------*/
/* Documentation values.                                                     */
/*---------------------------------------------------------------------------*/
MODULE_AUTHOR("zLinux Crypto Team: Robert H. Burroughs and Eric D. Rossman");
MODULE_DESCRIPTION("zLinux Cryptographic Coprocessor device driver,"
		   " Copyright 2001, 2003 IBM Corporation");
MODULE_LICENSE("GPL");
MODULE_PARM(domain, "i");
MODULE_PARM_DESC(domain, "domain index for device");

// We should not be exporting our symbols and clogging the symbol table
EXPORT_NO_SYMBOLS;

/**********************************************************************/
/* New status functions                                               */
/**********************************************************************/
static inline int get_status_totalcount(void)
{
	return z90crypt.z90c_hdware_info->z90c_hdware_mask.z90c_st_count;
}

static inline int get_status_PCICAcount(void)
{
	return z90crypt.z90c_hdware_info->z90c_type_mask[PCICA].z90c_st_count;
}

static inline int get_status_PCICCcount(void)
{
	return z90crypt.z90c_hdware_info->z90c_type_mask[PCICC].z90c_st_count;
}

static inline int get_status_PCIXCCcount(void)
{
	return z90crypt.z90c_hdware_info->z90c_type_mask[PCIXCC].z90c_st_count;
}

static inline int get_status_requestq_count(void)
{
	return requestq_count;
}

static inline int get_status_pendingq_count(void)
{
	return pendingq_count;
}

static inline int get_status_totalopen_count(void)
{
	return atomic_read(&total_open);
}

static inline int get_status_domain_index(void)
{
	return z90crypt.z90c_cdx;
}

static inline
unsigned char *get_status_status_mask(unsigned char status[Z90CRYPT_NUM_APS])
{
	int i, ix;

	memcpy(status, z90crypt.z90c_hdware_info->z90c_device_type_array,
	       Z90CRYPT_NUM_APS);

	for (i = 0; i < get_status_totalcount(); i++) {
		ix = SHRT2LONG(i);
		if (LONG2DEVPTR(ix)->z90c_explicitly_disabled)
			status[ix] = 0x0d;
	}

	return status;
}

static inline
unsigned char *get_status_qdepth_mask(unsigned char qdepth[Z90CRYPT_NUM_APS])
{
	int i, ix;

	memset(qdepth, 0, Z90CRYPT_NUM_APS);

	for (i = 0; i < get_status_totalcount(); i++) {
		ix = SHRT2LONG(i);
		qdepth[ix] = LONG2DEVPTR(ix)->z90c_dev_caller_count;
	}

	return qdepth;
}

static inline
unsigned int *get_status_perdevice_reqcnt(unsigned int reqcnt[Z90CRYPT_NUM_APS])
{
	int i, ix;

	memset(reqcnt, 0, Z90CRYPT_NUM_APS * sizeof(int));

	for (i = 0; i < get_status_totalcount(); i++) {
		ix = SHRT2LONG(i);
		reqcnt[ix] = LONG2DEVPTR(ix)->z90c_dev_total_req_cnt;
	}

	return reqcnt;
}


/*---------------------------------------------------------------------------*/
/* The module initialization code.                                           */
/*---------------------------------------------------------------------------*/
int __init z90crypt_init_module(void)
{
	int result,nresult;
	struct proc_dir_entry * entry;

	PDEBUG("init_module -> PID %d\n", PID());

	//
	// Register the major (or get a dynamic one).
	//
	result = register_chrdev(z90crypt_major, REG_NAME, &z90crypt_fops);
	if (result < 0) {
		PRINTKW("register_chrdev (major %d) failed with %d.\n",
		z90crypt_major, result);
		return result;
	}

	PDEBUG("Registered " DEV_NAME " with result %d\n", result);

	if (z90crypt_major == 0)
		z90crypt_major = result;

	result = create_z90crypt(&domain);
	if (result != OK) {
		PRINTKW("create_z90crypt (domain index %d) failed with %d.\n",
					domain, result);
		result = -ENOMEM;
		goto init_module_cleanup;
	}

	if (result == OK) {
		PRINTK("Loaded\n");
		PDEBUG("create_z90crypt (domain index %d) successful.\n", domain);
	} else {
	PRINTK("No devices at startup\n");
	}

	// generate hotplug event for device node generation

	z90crypt_hotplug_event(z90crypt_major, 0, Z90CRYPT_HOTPLUG_ADD);

	//
	// Initialize the config lock, the request queue, pending queue, reader
	// timer, configuration timer, cleanup timer, and boolean indicating that
	// the device is quiescing.
	//
	spin_lock_init(&queuespinlock);

	INIT_LIST_HEAD(&pending_list);
	pendingq_count=0;

	INIT_LIST_HEAD(&request_list);
	requestq_count=0;

	quiesce_z90crypt = FALSE;

	atomic_set(&total_open,0);
	atomic_set(&z90crypt_step,0);

	//
	// Set up the cleanup task.
	//
	init_timer(&cleanup_timer);
	cleanup_timer.function = z90crypt_cleanup_task;
	cleanup_timer.data = (unsigned long) NULL;
	cleanup_timer.expires = jiffies + (CLEANUPTIME * HZ);
	add_timer(&cleanup_timer);

	//
	// Set up the proc file system
	//
	entry = create_proc_entry("driver/z90crypt", 0644, NULL);
	if (entry) {
		entry->nlink = 1;
		entry->data = NULL;
		entry->read_proc = z90crypt_status;
		entry->write_proc = z90crypt_status_write;
	}
	else
		PRINTK("Couldn't create z90crypt proc entry\n");
	z90crypt_entry = entry;

	//
	// Set up the configuration task.
	//
	init_timer(&config_timer);
	config_timer.function = z90crypt_config_task;
	config_timer.data = (unsigned long) NULL;
	config_timer.expires = jiffies + (INITIAL_CONFIGTIME * HZ);
	add_timer(&config_timer);

	//
	// Set up the reader task
	//
	tasklet_init(&reader_tasklet, z90crypt_reader_task, (unsigned long) NULL);
	init_timer(&reader_timer);
	reader_timer.function = z90crypt_schedule_reader_task;
	reader_timer.data = (unsigned long) NULL;
	reader_timer.expires = jiffies + (READERTIME * HZ / 1000);
	add_timer(&reader_timer);

	return 0; // succeed

init_module_cleanup:
	if ((nresult = unregister_chrdev(z90crypt_major, REG_NAME))) {
		PRINTK("unregister_chrdev failed with %d.\n", nresult);
	} else {
		PDEBUG("unregister_chrdev successful.\n");
	}

	return result;
} // end init_module

/*--------------------------------------------------------------------------*/
/* The module termination code                                              */
/*--------------------------------------------------------------------------*/
void __exit z90crypt_cleanup_module(void)
{
	int nresult;

	PDEBUG("cleanup_module -> PID %d\n", PID());

	remove_proc_entry("driver/z90crypt", NULL);

	// generate hotplug event for device node removal

	z90crypt_hotplug_event(z90crypt_major, 0, Z90CRYPT_HOTPLUG_REMOVE);

	if ((nresult = unregister_chrdev(z90crypt_major, REG_NAME))) {
		PRINTK("unregister_chrdev failed with %d.\n", nresult);
	} else {
		PDEBUG("unregister_chrdev successful.\n");
	}

	//
	// Remove the tasks
	//
	tasklet_kill(&reader_tasklet);
	del_timer(&reader_timer);
	del_timer(&config_timer);
	del_timer(&cleanup_timer);

	destroy_z90crypt();
} // end cleanup_module



/*----------------------------------------------------------------------------*/
/* Functions running under a process id                                       */
/*                                                                            */
/* The I/O functions:                                                         */
/*     z90crypt_open                                                          */
/*     z90crypt_close                                                         */
/*     z90crypt_read                                                          */
/*     z90crypt_write                                                         */
/*     z90crypt_ioctl                                                         */
/*                                                                            */
/*     z90crypt_status           // proc-fs routine                           */
/*                                                                            */
/* Helper functions:                                                          */
/*     z90crypt_rsa                                                           */
/*       z90crypt_prepare                                                     */
/*       z90crypt_send                                                        */
/*       z90crypt_process_results                                             */
/*                                                                            */
/*       sprinthx                                                             */
/*       sprintrw                                                             */
/*       sprintcl                                                             */
/*                                                                            */
/* the fops struct is built here                                              */
/*                                                                            */
/* read() returns a string of random bytes.                                   */
/* write() always fails.                                                      */
/*                                                                            *//*----------------------------------------------------------------------------*/

//
// z90crypt_status -- returns z90crypt status as /proc/driver/z90crypt
//
int z90crypt_status(char * respbuff, char **start, off_t offset, int count,
			int * eof, void * data)
{
	unsigned char *workarea;
	int len = 0;

	// respbuff is a page.  Use the right half for a work area
	workarea = respbuff+2000;

	len += sprintf(respbuff+len,
		"\nz90crypt version: %1d.%1d.%1d\n",
		z90crypt_VERSION,
		z90crypt_RELEASE,
		z90crypt_VARIANT);
	len += sprintf (respbuff+len,
		"Cryptographic domain: %i\n",
		get_status_domain_index());
	len += sprintf (respbuff+len,
		"Total device count: %i\n",
		get_status_totalcount());
	len += sprintf (respbuff+len,
		"PCICA count: %i\n",
		get_status_PCICAcount());
	len += sprintf (respbuff+len,
		"PCICC count: %i\n",
		get_status_PCICCcount());
	len += sprintf (respbuff+len,
		"PCIXCC count: %i\n",
		get_status_PCIXCCcount());
	len += sprintf (respbuff+len,
		"requestq count: %i\n",
		get_status_requestq_count());
	len += sprintf (respbuff+len,
		"pendingq count: %i\n",
		get_status_pendingq_count());
	len += sprintf (respbuff+len,
		"Total open handles: %i\n\n",
		get_status_totalopen_count());
	len += sprinthx(
		"Online devices: 1 means PCICA, 2 means PCICC, 3 means PCIXCC",
		respbuff+len,
		get_status_status_mask(workarea),
		Z90CRYPT_NUM_APS);
	len += sprinthx("Waiting work element counts",
  		respbuff+len,
		get_status_qdepth_mask(workarea),
		Z90CRYPT_NUM_APS);
	len += sprinthx4(
                "Per-device successfully completed request counts",
  		respbuff+len,
		get_status_perdevice_reqcnt((unsigned int *)workarea),
		Z90CRYPT_NUM_APS);

	*eof = 1;
	memset(workarea, 0, Z90CRYPT_NUM_APS * sizeof(unsigned int));
	return len;
} // end z90crypt_status

void disable_card(int card_index)
{
	Z90C_DEVICE * devp = NULL;
	CDEVICE_TYPE devTp = -1;

	if ((devp = LONG2DEVPTR(card_index)) == NULL)
		return;
	if (devp->z90c_explicitly_disabled == FALSE) {
		devp->z90c_explicitly_disabled = TRUE;
		z90crypt.z90c_hdware_info->
			z90c_hdware_mask.z90c_explicitly_disabled_count++;
		if ((devTp = devp->z90c_dev_type) == -1)
			return;
		z90crypt.z90c_hdware_info->
			z90c_type_mask[devTp].z90c_explicitly_disabled_count++;
	}
}

void enable_card(int card_index)
{
	Z90C_DEVICE * devp = NULL;
	CDEVICE_TYPE devTp = -1;

	if ((devp = LONG2DEVPTR(card_index)) == NULL)
		return;
	if (devp->z90c_explicitly_disabled == TRUE) {
		devp->z90c_explicitly_disabled = FALSE;
		z90crypt.z90c_hdware_info->
			z90c_hdware_mask.z90c_explicitly_disabled_count--;
		if ((devTp = devp->z90c_dev_type) == -1)
			return;
		z90crypt.z90c_hdware_info->
			z90c_type_mask[devTp].z90c_explicitly_disabled_count--;
	}
}

int scan_char(unsigned char *bf, unsigned int len,
		unsigned int *offs, unsigned int *p_eof,
		unsigned char c)
{
	unsigned int i;
	unsigned char t;
	unsigned int found;

	found = 0;
	for (i = 0; i < len; i++) {
		t = *(bf+i);
		if (t == c) {
			found = 1;
			break;
		}
		if (t == '\0') {
			*p_eof = 1;
			break;
		}
		if (t == '\n') {
			break;
		}
	}
	*offs = i+1;
	return found;
} // end scan_char

int scan_string(unsigned char *bf, unsigned int len,
		unsigned int *offs, unsigned int * p_eof,
		unsigned char *s)
{
	unsigned int temp_len = 0;
	unsigned int temp_offs = 0;
	unsigned int found = 0, eof = 0;

	while((!eof) && (!found)) {
		found = scan_char(bf+temp_len,
				len-temp_len, &temp_offs, &eof, *s);

		temp_len += temp_offs;
		if (eof) {
			found = 0;
			break;
		}

		if (found) {
			if (len >= temp_offs+strlen(s)) {
				found = (!(strncmp(bf+temp_len-1,s,strlen(s))));
				if (found) {
					*offs = temp_len+strlen(s)-1;
					break;
				}
			}
			else {
				found = 0;
				*p_eof = 1;
				break;
			}
		}
	}

	return found;
} // end scan_string

int z90crypt_status_write(struct file * file, const char *buffer,
			unsigned long count, void * data)
{
        int rv, i, j, len, offs, found, eof;
        unsigned char *lBuf;
        unsigned int local_count;
        unsigned char c;
	unsigned char t[] = "Online devices";

#define LBUFSIZE 600
	lBuf = kmalloc(LBUFSIZE, GFP_KERNEL);
	if (!lBuf) {
		PRINTK("kmalloc for lBuf in z90crypt_status_write failed!\n");
		return 0;
	}

	if (count <= 0)
		return 0;

	if ((local_count = (unsigned int)count) > (LBUFSIZE-1))
		local_count = LBUFSIZE-1;

	if ((rv = copy_from_user(lBuf, buffer, local_count)) != 0) {
		kfree(lBuf);
		return -EFAULT;
	}

	lBuf[local_count-1] = '\0';

	len = 0;
	eof = 0;
	found = 0;
	while((!eof) && (len < LBUFSIZE)){
		found = scan_string(lBuf+len,local_count-len,&offs,&eof,t);
		len += offs;
		if (found == 1)
			break;
	}

	if (eof) {
		kfree(lBuf);
		return count;
	}

	if (found == 1)
		found = scan_char(lBuf+len,local_count-len,&offs, &eof, '\n');

	if ((found == 0) || (eof)) {
		kfree(lBuf);
		return count;
	}

	len += offs;
	j = 0;
	for (i = 0; i < 80; i++) {
		c = *(lBuf+len+i);
		switch (c){
			case 0x09:	// tab
				break;
			case 0x0a:	// line feed
				eof = 1;
				break;
			case 0x20:	// space
				break;
			case 0x30:	// '0'
				j++;
				break;
			case 0x31:	// '1'
				j++;
				break;
			case 0x32:	// '2'
				j++;
				break;
			case 0x33:	// '3'
				j++;
				break;
			case 0x44:	// 'd'
				disable_card(j);
				j++;
				break;
			case 0x45:	// 'e'
				enable_card(j);
				j++;
				break;
			case 0x64:	// 'D'
				disable_card(j);
				j++;
				break;
			case 0x65:	// 'E'
				enable_card(j);
				j++;
				break;
			default:
				eof = 1;
				break;
		}
		if (eof)
			break;
	} // end for i = ...

	kfree(lBuf);
	return count;
} // end z90crypt_status_write

//
// open
//
int z90crypt_open (struct inode *inode, struct file *filp)
{
	private_data_t *private_data_p = NULL;

	if (quiesce_z90crypt)
		return -EQUIESCE;

	if (!(private_data_p = kmalloc(sizeof(private_data_t),GFP_KERNEL)))
		return -ENOMEM;

	memset((void *)private_data_p,0,sizeof(private_data_t));
	private_data_p->status = STAT_OPEN;

	filp->private_data = private_data_p;

	atomic_inc(&total_open);

	return 0;

} //end z90crypt_open


//
// close
//
int z90crypt_release (struct inode *inode, struct file *filp)
{
	private_data_t *private_data_p; // private data for this opener

	private_data_p = filp->private_data;

	PDEBUG("release -> PID %d (filp %p)\n", PID(), filp);

	private_data_p->status = STAT_CLOSED;

	destroy_private_data(private_data_p);

	atomic_dec(&total_open);

	return 0;
} // end z90crypt_release

ssize_t z90crypt_read (struct file *filp,char *buf,size_t count, loff_t *f_pos)
{
	/*
	 * This driver requires libica 1.3.5.
	 *
	 * Originally, we read from the hardware random generator here,
	 * but it turned out that its quality was not sufficient for
	 * a strong encryption. Thus, modern userland (libica) reads from
	 * /dev/urandom instead. Since we supply a proper revision libica,
	 * we do not need compatible read() implementation anymore.
	 */
	return -ENODEV;
}

/*-------------------------------------------------------------------*
 * Write - always fails                                              *
 *-------------------------------------------------------------------*/
ssize_t z90crypt_write (struct file *filp, const char *buf, size_t count,
			loff_t *f_pos)
{
	PDEBUG("write -> filp %p (PID %d)\n", filp, PID());
	return -EPERM;
}

/*-------------------------------------------------------------------*
 * Ioctl                                                             *
 *-------------------------------------------------------------------*/
int z90crypt_ioctl (struct inode *inode, struct file *filp, unsigned int cmd,
			unsigned long arg)
{
	int       ret = 0, i, loopLim, tempstat;
	pid_t     pid;
	private_data_t * private_data_p;

	private_data_p = filp->private_data;
	pid = PID();

	PDEBUG("ioctl -> filp %p (PID %d), cmd 0x%08X\n", filp, pid, cmd);
	PDEBUG("cmd 0x%08X: dir %s, size 0x%04X, type 0x%02X, nr 0x%02X\n",
         cmd,
         !_IOC_DIR(cmd) ? "NO"
           : ((_IOC_DIR(cmd) == (_IOC_READ|_IOC_WRITE)) ? "RW"
             : ((_IOC_DIR(cmd) == _IOC_READ) ? "RD"
               : "WR")),
         _IOC_SIZE(cmd), _IOC_TYPE(cmd), _IOC_NR(cmd));

	if (_IOC_TYPE(cmd) != Z90_IOCTL_MAGIC) {
		PRINTK("cmd 0x%08X contains a bad type\n", cmd);
		return -ENOTTY;
	}

	switch(cmd) {
		//
		// Request for RSA Clear-Key Operation
		//
		// arg is pointer to structure passed by caller.
		//
		case ICARSAMODEXPO:
		case ICARSACRT:
			if (quiesce_z90crypt)
				ret = -EQUIESCE;
			else {
				ret = -ENODEV; // Default if no devices
				loopLim = z90crypt.z90c_hdware_info->
						z90c_hdware_mask.z90c_st_count-
				         (z90crypt.z90c_hdware_info->
						z90c_hdware_mask.
							z90c_disabled_count +
				          z90crypt.z90c_hdware_info->
							z90c_hdware_mask.
						z90c_explicitly_disabled_count);
				for (i = 0; i < loopLim; i++) {
					ret = z90crypt_rsa(private_data_p,
							pid, cmd, arg);
					if (ret != -ERESTARTSYS)
						break;
				}
				if (ret == -ERESTARTSYS)
					ret = -ENODEV;
			}
			break;

		case Z90STAT_TOTALCOUNT:
			tempstat = get_status_totalcount();
			if ((ret = copy_to_user((void *)arg, (void *)&tempstat,
						sizeof(int))) != 0) {
				ret = -EFAULT;
			}
			break;

		case Z90STAT_PCICACOUNT:
			tempstat = get_status_PCICAcount();
			if ((ret = copy_to_user((void *)arg, (void *)&tempstat,
						sizeof(int))) != 0) {
				ret = -EFAULT;
			}
			break;

		case Z90STAT_PCICCCOUNT:
			tempstat = get_status_PCICCcount();
			if ((ret = copy_to_user((void *)arg, (void *)&tempstat,
						sizeof(int))) != 0) {
				ret = -EFAULT;
			}
			break;

		case Z90STAT_PCIXCCCOUNT:
			tempstat = get_status_PCIXCCcount();
			if ((ret = copy_to_user((void *)arg, (void *)&tempstat,
						sizeof(int))) != 0) {
				ret = -EFAULT;
			}
			break;

		case Z90STAT_REQUESTQ_COUNT:
			tempstat = get_status_requestq_count();
			if ((ret = copy_to_user((void *)arg, (void *)&tempstat,
						sizeof(int))) != 0) {
				ret = -EFAULT;
			}
			break;

		case Z90STAT_PENDINGQ_COUNT:
			tempstat = get_status_pendingq_count();
			if ((ret = copy_to_user((void *)arg, (void *)&tempstat,
						sizeof(int))) != 0) {
				ret = -EFAULT;
			}
			break;

		case Z90STAT_TOTALOPEN_COUNT:
			tempstat = get_status_totalopen_count();
			if ((ret = copy_to_user((void *)arg, (void *)&tempstat,
						sizeof(int))) != 0) {
				ret = -EFAULT;
			}
			break;

		case Z90STAT_DOMAIN_INDEX:
			tempstat = get_status_domain_index();
			if ((ret = copy_to_user((void *)arg, (void *)&tempstat,
						sizeof(int))) != 0) {
				ret = -EFAULT;
			}
			break;

		case Z90STAT_STATUS_MASK:
		{
			unsigned char *status;
			status = kmalloc(Z90CRYPT_NUM_APS, GFP_KERNEL);
			if (!status) {
				PRINTK("kmalloc for status in z90crypt_ioctl failed!\n");
				ret = -ENOMEM;
				break;
			}
			get_status_status_mask(status);
			if ((ret = copy_to_user((void *)arg, (void *)status,
						Z90CRYPT_NUM_APS)) != 0) {
				ret = -EFAULT;
			}
			kfree(status);
			break;
		}

		case Z90STAT_QDEPTH_MASK:
		{
			unsigned char *qdepth;
			qdepth = kmalloc(Z90CRYPT_NUM_APS, GFP_KERNEL);
			if (!qdepth) {
				PRINTK("kmalloc for qdepth in z90crypt_ioctl failed!\n");
				ret = -ENOMEM;
				break;
			}
			get_status_qdepth_mask(qdepth);
			if ((ret = copy_to_user((void *)arg, (void *)qdepth,
						Z90CRYPT_NUM_APS)) != 0) {
				ret = -EFAULT;
			}
			kfree(qdepth);
			break;
		}

		case Z90STAT_PERDEV_REQCNT:
		{
			unsigned int *reqcnt;
			reqcnt = kmalloc(sizeof(int)*Z90CRYPT_NUM_APS, GFP_KERNEL);
			if (!reqcnt) {
				PRINTK("kmalloc for reqcnt in z90crypt_ioctl failed!\n");
				ret = -ENOMEM;
				break;
			}
			get_status_perdevice_reqcnt(reqcnt);
			if ((ret = copy_to_user((void *)arg, (void *)reqcnt,
					Z90CRYPT_NUM_APS * sizeof(int))) != 0) {
				ret = -EFAULT;
			}
			kfree(reqcnt);
			break;
		}

		//
		// NOTE: THIS IS DEPRECATED.  USE THE NEW STATUS CALLS
		//
		// Request for status.
		//
		// arg is pointer to user's buffer.
		//
		// Status is returned in raw binary format.
		//
		case ICAZ90STATUS:
		{
			ica_z90_status *pstat;

			PDEBUG("in ioctl got ICAZ90STATUS!\n");

			pstat = kmalloc(sizeof(ica_z90_status), GFP_KERNEL);
			if (!pstat) {
				PRINTK("kmalloc for pstat in z90crypt_ioctl failed!\n");
				ret = -ENOMEM;
				break;
			}

			pstat->totalcount        = get_status_totalcount();
			pstat->leedslitecount    = get_status_PCICAcount();
			pstat->leeds2count       = get_status_PCICCcount();
			pstat->requestqWaitCount = get_status_requestq_count();
			pstat->pendingqWaitCount = get_status_pendingq_count();
			pstat->totalOpenCount    = get_status_totalopen_count();
			pstat->cryptoDomain      = get_status_domain_index();
			get_status_status_mask(pstat->status);
			get_status_qdepth_mask(pstat->qdepth);

			if ((ret = copy_to_user((void *)arg,
						(void *)pstat,
						sizeof(ica_z90_status))) != 0) {
				ret = -EFAULT;
			}
                        kfree(pstat);
			break;
		}

		//
		// Request for device shutdown.
		//
		case Z90QUIESCE:
			if(current->euid != 0) {
				PRINTK("QUIESCE failed for euid %d\n", current->euid);
				ret = -EACCES;
			}
			else {
				PRINTK("QUIESCE device from PID %d\n", pid);
				quiesce_z90crypt = TRUE;
			}
			break;

		default:  // user passed an invalid IOCTL number
			PDEBUG("cmd 0x%08X contains invalid ioctl code\n",cmd);
			ret = -ENOTTY;
	} // end switch(cmd)

	return ret;
} // end z90crypt_ioctl

int allocate_work_element(work_element_t ** work_elem_pp,
			struct private_data * priv_data_p,
			pid_t pid)
{
	int rv = 0;
	work_element_t * we_p = NULL;

	if ((rv = create_work_element(&we_p, pid)) != 0)
		return (rv);
	init_work_element(we_p, priv_data_p, pid);
	*work_elem_pp = we_p;
	return (rv);
} // end allocate_work_element


int create_work_element(work_element_t ** work_elem_pp, pid_t pid)
{
	work_element_t * work_elem_p = NULL;

	// A page is 4K bytes.  Currently the maximum size needed is
	// sizeof(work_element_t) + sizeof(Z90C_CALLER) = 2336 bytes
	if (!(work_elem_p = (work_element_t *)get_zeroed_page(GFP_KERNEL)))
		return -ENOMEM;
	*work_elem_pp = work_elem_p;
	return 0;
} // end create_work_element


int init_work_element(work_element_t * work_elem_p,
			struct private_data * priv_data_p,
			pid_t pid)
{
	int step;

	work_elem_p->requestptr =
			(unsigned char *)work_elem_p + sizeof(work_element_t);
	// Come up with a unique id for this caller.
	step = atomic_inc_return(&z90crypt_step);
	memcpy (work_elem_p->caller_id+0, (void *) &pid,  sizeof(pid));
	memcpy (work_elem_p->caller_id+4, (void *) &step, sizeof(step));
	work_elem_p->pid = pid;
	work_elem_p->opener_p = priv_data_p;

	// Set the status byte to the defaults
	//   not STAT_ROUTED
	//   not STAT_FAILED
	//   STAT_NOWORK
	work_elem_p->status[0] = STAT_DEFAULT;

	// Set the audit trail
	work_elem_p->audit[0] = 0;
	work_elem_p->audit[1] = 0;

	// Initialize response size to none
	work_elem_p->respsize = 0;

	// Initialize return code
	work_elem_p->returncode = OK;

	// Initialize device information
	work_elem_p->devindex = -1; // send_to_crypto selects the device
	work_elem_p->devtype = -1;  // getCryptoBuffer selects the type

	// Set up the wait queue for this work element.
	atomic_set(&work_elem_p->alarmrung,0);
	init_waitqueue_head(&work_elem_p->waitq);

	// Not in the request list or the pending list yet
	INIT_LIST_HEAD(&(work_elem_p->liste));

	return 0;

} // end init_work_element

int free_work_element(work_element_t * work_elem_p)
{
	int rv = 0;

	// Put the work element back
	destroy_work_element(work_elem_p);

	return (rv);
} // end free_work_element

void destroy_private_data(private_data_t * private_data_p)
{
	memset(private_data_p, 0, sizeof(private_data_t));
	kfree(private_data_p);
} // end destroy_private_data

void destroy_work_element(work_element_t *work_elem_p)
{
	free_page((long)work_elem_p);
} // end destroy_work_element

/*-------------------------------------------------------------------*/
/*                                                                   */
/* z90crypt_rsa:  rsa encryptions and decryptions.                   */
/*                                                                   */
/* 1.  Call allocate_work_element to get storage                     */
/*                                                                   */
/* 2.  Call z90crypt_prepare to copy input data to the work element  */
/*     and prepare the data for submission to a crypto device        */
/*                                                                   */
/* 3.  Enqueue the work element to the list of work elements         */
/*     waiting for any crypto device                                 */
/*                                                                   */
/* 4.  Sleep while awaiting results                                  */
/*                                                                   */
/* 5.  When awakened:                                                */
/*       Copy results to user storage                                */
/*       Give back the work element                                  */
/*                                                                   */
/* 6.  Return to caller                                              */
/*                                                                   */
/*-------------------------------------------------------------------*/
int z90crypt_rsa(private_data_t *private_data_p, pid_t pid,
                 unsigned int cmd, unsigned long arg)
{
	int rv = 0;
	int keep_work_element = 0;
	work_element_t * work_elem_p;
	struct list_head * lptr = NULL;  // ptr to list element in workelement
	struct list_head * hptr = NULL;  // ptr to list head

	rv = allocate_work_element(&work_elem_p, private_data_p, pid);
	if (rv != 0) {
		 return rv;
	}

	do {
		//
		// Prepare the work element for crypto operations
		//
		if ((rv=z90crypt_prepare(work_elem_p,cmd,(const char *)arg))) {
			break;
		}

		//
		// Place the work on a device or on the request queue
		//
		if ((rv = z90crypt_send(work_elem_p, (const char *)arg))) {
			PDEBUG("PID %d trying to write: error %d detected in "
				"send.\n", pid, rv);
			break;
		}

		//
		// Await results
		//
		work_elem_p->audit[0] |= FP_ASLEEP;
		wait_event(
		    work_elem_p->waitq,atomic_read(&work_elem_p->alarmrung));

		//
		// Put the results into user space
		//
		work_elem_p->audit[0] |= FP_AWAKE;

		rv = work_elem_p->returncode;

		if (rv == OK) {
			rv = z90crypt_process_results(work_elem_p, (char *)arg);
		}

	} while(0);

	//
	// Cleanup.  In all cases except status STAT_QUEUED, free the
	//           work element
	//
	if ((work_elem_p->status[0] & STAT_FAILED)) {
		//
		// If we timed out the device
		//
		// If we released device and the request is on request queue
		//   (STAT_WRITTEN), remove it from the request queue
		//
		// If we released device and request is no longer on a queue
		// (STAT_READPEND)
		//
		// If we released device and request is on the pending queue
		// (STAT_PENDING), leave it alone; let reader_task clean up.
		//
		// If we didn't release the device and we didn't timeout the
		//   request, mark it as not failed.
		//
		switch (rv) {
			// EINVAL *after* receive is almost always padding error
			// issued by a PCICC card.  A PCICA doesn't check
			// padding, and since results should be consistent when
			// this device driver is used by libica, we convert this
			// return value to -EGETBUFF.  This triggers a resort to
			// software in libica, which should produce results
			// identical to what a PCICA would have done.
			// Mind, this is almost certainly an error condition, so
			// all we're guaranteeing here is the *same* error
			// condition no matter which card is in use.
			// Things will have gone very slowly, but this is an
			//error path after all.
			case -EINVAL:
				if (work_elem_p->devtype == PCICC ||
				    work_elem_p->devtype == PCIXCC)
					rv = -EGETBUFF;
				break;

			// ETIMEOUT can happen because device has gone offline.
			// In that case, the work should be retried if other
			// devices are available.
			case -ETIMEOUT:
				if (z90crypt.z90c_mask.z90c_st_count > 0)
					rv = -ERESTARTSYS;
				else
					rv = -ENODEV;

				// fall through to clean up request queue
			case -ERESTARTSYS:
				// fall through to clean up request queue
			case -ERELEASED:
				switch(CHECK_RDWRMASK(work_elem_p->status[0])) {
					case STAT_WRITTEN: // Request off queue
						spin_lock_irq(&queuespinlock);
						hptr = &request_list;
						list_for_each(lptr, hptr) {
							if (lptr ==
						     &(work_elem_p->liste)){
								list_del(lptr);
							--requestq_count;
							}
						}
						hptr = &pending_list;
						list_for_each(lptr, hptr) {
							if (lptr ==
				  		     &(work_elem_p->liste)){
								list_del(lptr);
							--pendingq_count;
								break;
							}
						}
						spin_unlock_irq(&queuespinlock);

						break;

					case STAT_READPEND:
					case STAT_NOWORK:
						break;

					case STAT_QUEUED:
					//
					//  Reader_task cleans up
                       			 //
						keep_work_element = 1;
						break;

					default:
						break;
				} // end switch(status)
				break;

			default:
				work_elem_p->status[0] ^= STAT_FAILED;
				break;

		} // end switch(rv)

	} // end if STAT_FAILED

	//
	// In all cases except STAT_QUEUED (see above), free the work element
	//
	if (!(keep_work_element))
		free_work_element(work_elem_p);

	return rv;

} // end z90crypt_rsa

//
// Prepare copies the user's data to kernel space and
// formats it for dispatch to a crypto device
//
int z90crypt_prepare(work_element_t * work_elem_p,
			unsigned int funccode, const char *caller_buf)
{
	int       ret = 0;

	//
	// Set the correct size for the structure used
	//
	if (funccode == ICARSAMODEXPO)
		work_elem_p->buffsize = sizeof(ica_rsa_modexpo_t);
	else
		work_elem_p->buffsize = sizeof(ica_rsa_modexpo_crt_t);

	//
	// Copy the structure from the user.
	//
	if (copy_from_user(work_elem_p->buffer,
				caller_buf,work_elem_p->buffsize)) {
		ret = -EFAULT;
		goto prepare_cleanup;
	}
	work_elem_p->audit[0] |= FP_COPYFROM;

	//
	// Set the function code in the work element.
	//
	SET_RDWRMASK(work_elem_p->status[0], STAT_WRITTEN);
	work_elem_p->funccode = funccode;

	//
	// Get a request buffer.  The lowlevel code will analyze the
	// request and return an error if the request can't be satisfied
	//
	work_elem_p->devtype = -1;   // no particular type can be specified
	work_elem_p->audit[0] |= FP_BUFFREQ;

	ret = get_crypto_request_buffer(work_elem_p->caller_id,
					work_elem_p->funccode,
					work_elem_p->buffsize,
					work_elem_p->buffer,
					&work_elem_p->devtype,
					work_elem_p->requestptr);
	switch(ret) {
		case 0:
			work_elem_p->audit[0] |= FP_BUFFGOT;
			break;
		case SEN_USER_ERROR:
			ret = -EINVAL;
			break;
		case SEN_QUEUE_FULL:
			ret = 0;
			break;
		case SEN_RELEASED:
			ret = -EFAULT;
			break;
		case REC_NO_RESPONSE:
			ret = -ENODEV;
		case SEN_NOT_AVAIL:
		default:
			PRINTK("Error in get_crypto_request_buffer: %d\n",ret);
			ret = -EGETBUFF;
	} // end switch(ret)

prepare_cleanup:
	if (CHECK_RDWRMASK(work_elem_p->status[0]) ==STAT_WRITTEN)
		SET_RDWRMASK(work_elem_p->status[0], STAT_DEFAULT);
	work_elem_p->devindex = -1;  // only dev *type* is important
	return ret;
} // end z90crypt_prepare

//
// Send puts the user's work on one of two queues:
//   the pending queue if the send was successful
//   the request queue if the send failed because device full or busy
//
int z90crypt_send(work_element_t * we_p, const char *buf)
{
	pid_t     pid;
	int       rv = 0;

	pid = PID();
	PDEBUG("send -> PID %d\n", pid);

	//
	// User is not allowed to have any outstanding requests.
	//
	if (CHECK_RDWRMASK(we_p->status[0]) != STAT_NOWORK) {
		PDEBUG("PID %d tried to send more work but has outstanding "
			"work.\n",pid);
		return -EWORKPEND;
	}
	// Reset device number
	we_p->devindex = -1;

	// Serialize on the request and pending queues
	spin_lock_irq(&queuespinlock);

	// Send the work to a crypto device

	rv = send_to_crypto_device(we_p->caller_id,
				we_p->funccode,
				we_p->buffsize,
				we_p->buffer,
				we_p->devtype,
				&we_p->devindex,
				we_p->requestptr);
	// Check the return value
	switch (rv) {
		case OK:
			// Time stamp
			we_p->requestsent = jiffies;
			// footprint
			we_p->audit[0] |= FP_SENT;
			// Put on the pending queue
			list_add_tail(&we_p->liste, &pending_list);
			++pendingq_count;
			we_p->audit[0] |= FP_PENDING;
			break;
		case SEN_BUSY:
		case SEN_QUEUE_FULL:
			rv = 0;            // Fake it, so the caller will sleep
			// reset device number
			we_p->devindex = -1;             // any device will do
			// Time stamp
			we_p->requestsent = jiffies;
			// Put on the request queue
			list_add_tail(&we_p->liste, &request_list);
			++requestq_count;
			we_p->audit[0] |= FP_REQUEST;
			break;

		case SEN_RETRY:
			rv = -ERESTARTSYS;
			break;

		case SEN_NOT_AVAIL:  // From here down the request is rejected
			PRINTK("*** No devices available.\n");
			rv = we_p->returncode = -ENODEV;
			we_p->status[0] |= STAT_FAILED;
			break;
		case REC_OPERAND_INVALID:
		case REC_OPERAND_SIZE:
		case REC_EVEN_MODULUS:
		case REC_INVALID_PADDING:
			rv = we_p->returncode = -EINVAL;
			we_p->status[0] |= STAT_FAILED;
			break;
		default:
			// Propagate return code
			we_p->returncode = rv;
			// Mark this element a failure
			we_p->status[0] |= STAT_FAILED;
			break;
	} // end switch (rv)

	// Mark the status
	if (rv != -ERESTARTSYS)
		SET_RDWRMASK(we_p->status[0], STAT_WRITTEN);

	// Unserialize
	spin_unlock_irq(&queuespinlock);

	// If this work element is on a device or the request queue, get it off
	if (rv == OK)
		tasklet_schedule(&reader_tasklet);

	return rv;
} // end z90crypt_send

//
// process_results copies the user's work from kernel space.
//
int z90crypt_process_results (work_element_t * work_elem_p, char *buf)
{
	pid_t     pid;
	int       retval = 0;

	pid = PID();

	PDEBUG("process_results ->work_elem_p %p (PID %d)\n", work_elem_p, pid);

	// Track the number of successful requests for this device
	LONG2DEVPTR(work_elem_p->devindex)->z90c_dev_total_req_cnt++;

	SET_RDWRMASK(work_elem_p->status[0], STAT_READPEND);

	if (!work_elem_p->buffer) {
		PRINTK("work_elem_p %p PID %d in STAT_READPEND: buffer NULL.\n",
			work_elem_p, pid);
		retval = -ENOBUFF;
		goto process_results_cleanup;
	}

	if ((retval =
		copy_to_user(buf,
			work_elem_p->buffer,work_elem_p->buffsize))!=0) {
		PDEBUG("copy_to_user failed:  rv = %d\n",retval);
		retval = -EFAULT;
		goto process_results_cleanup;
	}

	retval = work_elem_p->returncode;
	if (retval)
		goto process_results_cleanup;

	if (work_elem_p->respsize) {
		if (copy_to_user (work_elem_p->respaddr,
				work_elem_p->respbuff,
				work_elem_p->respsize)) {
			retval = -EFAULT;
			goto process_results_cleanup;
		}
	}

process_results_cleanup:

	SET_RDWRMASK(work_elem_p->status[0], STAT_NOWORK);

	return retval;
} // end z90crypt_process_results

/*-------------------------------------------------------------------*
 * Miscellaneous Utility functions                                   *
 *-------------------------------------------------------------------*/
int sprintcl(unsigned char * outaddr,
             unsigned char * addr,
             unsigned int len)
{
  int hl = 0;
  int i;

  for (i=0;i<len;i++) {
    hl += sprintf(outaddr+hl,"%01x",(unsigned int) addr[i]);
  }
  hl += sprintf(outaddr+hl," ");

  return hl;
}

int sprintrw(unsigned char * outaddr,
             unsigned char * addr,
             unsigned int len)
{
  int hl = 0;
  int inl = 0;
  int c, cx;

  hl += sprintf(outaddr,"    ");                         // left margin

  for (c=0;c<len/16;c++) {
    hl += sprintcl(outaddr+hl, addr+inl, 16);
    inl += 16;
  }

  cx = len%16;
  if (cx) {
    hl += sprintcl(outaddr+hl, addr+inl, cx);
    inl += cx;
  }

  hl += sprintf(outaddr+hl,"\n");

  return hl;
}

int sprinthx(unsigned char * title,
             unsigned char * outaddr,
             unsigned char * addr,
             unsigned int len)
{
  int hl = 0;
  int inl = 0;
  int r,rx;

  hl += sprintf(outaddr,"\n%s\n",title);

  for (r=0; r<len/64; r++) {
    hl += sprintrw(outaddr+hl, addr+inl, 64);
    inl += 64;
  }
  rx = len%64;
  if (rx) {
    hl += sprintrw(outaddr+hl, addr+inl, rx);
    inl += rx;
  }

  hl += sprintf(outaddr+hl,"\n");

  return hl;
}

int sprinthx4(unsigned char * title,
             unsigned char * outaddr,
             unsigned int * array,
             unsigned int len)
{
  int hl = 0;
  int r;

  hl += sprintf(outaddr,"\n%s\n",title);

  for (r = 0; r < len; r++)
  {
    if ((r % 8) == 0)
      hl += sprintf(outaddr+hl, "    ");
    hl += sprintf(outaddr+hl, "%08X ", array[r]);
    if ((r % 8) == 7)
      hl += sprintf(outaddr+hl, "\n");
  }

  hl += sprintf(outaddr+hl,"\n");

  return hl;
}

/*--------------------------------------------------------------------------*/
/* Functions that run under a timer, with no process id                     */
/*                                                                          */
/* The task functions:                                                      */
/*     z90crypt_reader_task                                                 */
/*     z90crypt_config_task                                                 */
/*     z90crypt_cleanup_task                                                */
/*                                                                          */
/* Helper functions:                                                        */
/*     z90crypt_schedule_reader_timer                                       */
/*     z90crypt_schedule_reader_task                                        */
/*     z90crypt_schedule_config_task                                        */
/*     z90crypt_schedule_cleanup_task                                       */
/*                                                                          */
/*--------------------------------------------------------------------------*/

void z90crypt_reader_task (unsigned long ptr)
{
  int       workavail,
            remaining,          /* number of unfinished requests            */
            index,
            rc, rv,
            buff_len;           /* Response length                          */
  static UCHAR buff[1024];      /* Response buffer                          */
  UCHAR     psmid[8],
            *respaddr;          /* Response address in user space           */
  BOOL      remove_from_queue;  //
  work_element_t *pq_p;         /* used to walk the pending queue           */
  work_element_t *rq_p;         /* from the request queue                   */
  struct list_head * lptr;      // pointer to list struct in work element
  struct list_head * tptr;      // temp pointer to list struct in work element
  struct list_head * hptr;      // pointer to list head

  workavail = 2;
  remaining = 0;
  buff_len = 0;
  respaddr = NULL;
  remove_from_queue = FALSE;
  pq_p = NULL;
  rq_p = NULL;
  PDEBUG("reader_task -> jiffies %ld\n", jiffies);

  while (workavail) {
    /*
     * Decrement workavail by 1.
     *
     * We need to have nothing on a queue twice in a row to stop looking, so
     * we decrement it each time before we scan, and we will reset it to 2 if
     * we find anything on the queues.
     *
     * Reset unfinished work count to zero.  If we leave the while loop after
     * checking each device, and this count is still zero, there is no work
     * still to be finished and thus, no reason to reschedule the reader_task.
     */
    workavail--;
    rc = rv = 0;

    // Serialize
    spin_lock_irq(&queuespinlock);

    // clear memory
    memset(buff, 0x00, sizeof(buff));

    for (index = 0; index < z90crypt.z90c_mask.z90c_st_count; index++) {
      /*
       * Dequeue finished work from hardware.
       *
       * Dequeue once from each device in round robin.  It seems like a fairer
       * algorithm than checking one device over and over again until it
       * returns empty and then going on to the next device.
       */


      // Initialize switches, return codes
      remove_from_queue = FALSE;
      rc = rv = OK;

      PDEBUG("About to receive.\n");

      rc = receive_from_crypto_device (SHRT2LONG(index),
                                    psmid,
                                    &buff_len,
                                    buff,
                                    &respaddr);
      PDEBUG("Dequeued: rc = %d.\n",
             rc);

      switch (rc) {
        case OK:
          remove_from_queue = TRUE;
          break;

        case REC_BUSY:
        case REC_NO_WORK:
        case REC_EMPTY:
        case REC_RETRY_THIS_DEVICE:
        case REC_FATAL_ERROR: // There is no work returned.  Move on.
          remove_from_queue = FALSE;
          break;

        case REC_NO_RESPONSE:
          remove_from_queue = FALSE;
          workavail = 0;
          break;

        case REC_OPERAND_INVALID:
        case REC_OPERAND_SIZE:
        case REC_EVEN_MODULUS:
        case REC_INVALID_PADDING:
          remove_from_queue = TRUE;
          break;

        default:
          PRINTK("Error %d detected in reader_task from device %d.\n",
                  rc,SHRT2LONG(index));
          rc = REC_NO_RESPONSE;
          remove_from_queue = FALSE;
          workavail = 0;
          break;

      } // end switch

      if (remove_from_queue == TRUE) {

        if (rc != REC_NO_RESPONSE) {
          // Now there's room for more work on the hardware--put some there
          rq_p = NULL;
          if (!(list_empty(&request_list))) {
            lptr = request_list.next;
            // Remove from queue
            list_del(lptr);
            requestq_count--;
            rq_p = list_entry(lptr, struct work_element, liste);
            rq_p->audit[1] |= FP_REMREQUEST;
            if (rq_p->devtype == SHRT2DEVPTR(index)->z90c_dev_type) {
              // Send to device
              rq_p->devindex = SHRT2LONG(index); // send to *this* device
              rv = send_to_crypto_device(rq_p->caller_id,
                                      rq_p->funccode,
                                      rq_p->buffsize,
                                      rq_p->buffer,
                                      rq_p->devtype,
                                      &rq_p->devindex,
                                      rq_p->requestptr);
              if (rv == OK) {
                // Time stamp
                rq_p->requestsent = jiffies; //track age on *this *queue
                rq_p->audit[0] |= FP_SENT;
                // Put on the pending queue
                list_add_tail(lptr, &pending_list);
                ++pendingq_count;
                rq_p->audit[0] |= FP_PENDING;
              } // end if rv is OK
              else {
                // Propagate return code
                switch (rv) {
                  case SEN_OPERAND_INVALID:
                  case REC_OPERAND_SIZE:
                  case REC_EVEN_MODULUS:
                  case SEN_PADDING_ERROR:
                    rq_p->returncode = -EINVAL;
                    break;
                  case SEN_NOT_AVAIL:
                  case SEN_RETRY:
                  case REC_NO_RESPONSE:
                  default:
                    if (z90crypt.z90c_mask.z90c_st_count > 1)
                      rq_p->returncode = -ERESTARTSYS;
                    else
                      rq_p->returncode = -ENODEV;
                    break;
                } // end switch on rv
                // Mark this element a failure
                rq_p->status[0] |= STAT_FAILED;
                // Give the caller the bad news
                rq_p->audit[1] |= FP_AWAKENING;
                atomic_set(&rq_p->alarmrung,1);
                wake_up(&rq_p->waitq);
              } // end if rv isn't OK
            } // end if device types match
            else {
              if (z90crypt.z90c_mask.z90c_st_count > 1)
                rq_p->returncode = -ERESTARTSYS;
              else
                rq_p->returncode = -ENODEV;
              // Mark this element a failure
              rq_p->status[0] |= STAT_FAILED;
              // Give the caller the bad news
              rq_p->audit[1] |= FP_AWAKENING;
              atomic_set(&rq_p->alarmrung,1);
              wake_up(&rq_p->waitq);
            } // end else device types don't match
          } // end if request list not empty

            // Since rc wasn't REC_NO_RESPONSE, reset workavail to 2 more tries.
            // We have to go two rounds without work to give up.
            workavail = 2;

        } // end if rc isn't REC_NO_RESPONSE

        //
        // Wake up task waiting for this data
        //

        // Search for this work element on the pending queue
        pq_p = NULL;
        hptr = &pending_list;
        list_for_each_safe(lptr,tptr,hptr) {
          pq_p = list_entry(lptr, struct work_element, liste);
          if (!(memcmp(pq_p->caller_id, psmid, sizeof(pq_p->caller_id)))){
            list_del(lptr);
            --pendingq_count;
            pq_p->audit[1] |= FP_NOTPENDING;
            break;
          } // end if the psmid's match
          else {
            pq_p = NULL;
          } // end else the psmid's don't match
        } // end list_for_each

        if(!pq_p) { // if Not Found
          PRINTK("device %d has work but no caller "
                 "exists on pending queue\n",
                 SHRT2LONG(index));
        } // end Not Found
        else  { // Found
          if (rc == OK) {
            pq_p->respsize = buff_len;
            pq_p->audit[1] |= FP_RESPSIZESET;
            if (buff_len) {
              pq_p->respaddr = respaddr;
              pq_p->audit[1] |= FP_RESPADDRCOPIED;
              memcpy(pq_p->respbuff,
                     buff,
                     buff_len);
              pq_p->audit[1] |= FP_RESPBUFFCOPIED;
            } // end if buff_len non zero
          } // end if rc is OK
          else {
            switch (rc) {
              case REC_OPERAND_INVALID:
              case REC_OPERAND_SIZE:
              case REC_EVEN_MODULUS:
              case REC_INVALID_PADDING:
                PDEBUG(" -EINVAL after application error %d\n",rc);
                pq_p->returncode = -EINVAL;
                pq_p->status[0] |= STAT_FAILED;
                break;
              case REC_NO_RESPONSE:
              default:
                if (z90crypt.z90c_mask.z90c_st_count > 1)
                  pq_p->returncode = -ERESTARTSYS;
                else
                  pq_p->returncode = -ENODEV;
                pq_p->status[0] |= STAT_FAILED;
                break;
            } // end switch on rc
          } // end else rc isn't OK
          if ((pq_p->status[0] != STAT_FAILED ||
               pq_p->returncode != -ERELEASED)) {
            pq_p->audit[1] |= FP_AWAKENING;
            atomic_set(&pq_p->alarmrung,1);
            wake_up(&pq_p->waitq);
          } // end if FAILED or RELEASED
        } // end else Found
      } // end if remove_from_queue = TRUE )

      /*
       * Update the count of remaining work on this queue.
       */
      if (rc == REC_FATAL_ERROR)
        remaining = 0;
      else
        if (rc != REC_NO_RESPONSE)
          remaining += SHRT2DEVPTR(index)->z90c_dev_caller_count;

    } // end for each device

    // Unserialize
    spin_unlock_irq(&queuespinlock);

  } // end while workavail

  //
  // Reschedule self, if there is still work on hardware.
  //
  if (remaining) {
    //
    // Schedule the reader_timer, if the previous step was successful.
    //
    spin_lock_irq(&queuespinlock);
    rv = z90crypt_schedule_reader_timer();  // ignore rv; it's always 0
    spin_unlock_irq(&queuespinlock);
  } // end if there's remaining work to do

} // end z90crypt_reader_task

void z90crypt_config_task (unsigned long ptr)
{
	int rc = 0;

	PDEBUG("config_task -> jiffies %ld\n", jiffies);

	if ((rc = refresh_z90crypt(&z90crypt.z90c_cdx))) {
		PRINTK("Error %d detected in refresh_z90crypt.\n", rc);
	}
	// If return was fatal, don't bother reconfiguring
	if (rc != TSQ_FATAL_ERROR && rc != RSQ_FATAL_ERROR) {
		// Reschedule self.
		if ((rc = z90crypt_schedule_config_task(CONFIGTIME))) {
			PRINTK("Error %d from schedule_config_task.\n", rc);
		} else {
			PDEBUG("schedule_config_task successful.\n");
		}
	}
} // end z90crypt_config_task

void z90crypt_cleanup_task (unsigned long ptr)
{
	work_element_t *pq_p;	// used to walk the pending queue
	struct list_head *lptr = NULL;	// used to walk the queues
	struct list_head *tptr = NULL;	// temp for running queues
	struct list_head *hptr = NULL;	// pointer to the queue head
	int       rc;
	int deviceCount = 0;
	long      timelimit;         /* time at which no task on the pending
					queue should have been started before
					(saves reading a volatile variable every
					time we check a task on the pending
					queue with no loss of accuracy */

	PDEBUG("cleanup_task -> jiffies %ld\n", jiffies);

	//
	// Check whether all the crypto devices are gone.  That would mean
	// that every waiting process--waiting on either the request queue
	// or the pending queue--will have to be posted and notified that
	// its work isn't going to get done.
	//

	spin_lock_irq(&queuespinlock);

	deviceCount = z90crypt.z90c_mask.z90c_st_count;

	if (deviceCount <= 0) {

		// Post everybody on the pending queue
		hptr = &pending_list;
		list_for_each_safe(lptr, tptr, hptr) {
			pq_p = list_entry(lptr, struct work_element, liste);
			pq_p->returncode = -ENODEV;
			pq_p->status[0] |= STAT_FAILED;
			// get this off any caller queue it may be on
			unbuild_caller(LONG2DEVPTR(pq_p->devindex),
					   (Z90C_CALLER *)pq_p->requestptr);
			list_del(lptr);
			pendingq_count--;
			pq_p->audit[1] |= FP_NOTPENDING;
			pq_p->audit[1] |= FP_AWAKENING;
			atomic_set(&pq_p->alarmrung,1);
			wake_up(&pq_p->waitq);
		} // end list_for_each

		// Post everybody on the request queue
		hptr = &request_list;
		list_for_each_safe(lptr, tptr, hptr) {
			pq_p = list_entry(lptr, struct work_element, liste);
			pq_p->returncode = -ENODEV;
			pq_p->status[0] |= STAT_FAILED;
			list_del(lptr);
			requestq_count--;
			pq_p->audit[1] |= FP_REMREQUEST;
			pq_p->audit[1] |= FP_AWAKENING;
			atomic_set(&pq_p->alarmrung,1);
			wake_up(&pq_p->waitq);
		} // end list_for_each

	} // end if deviceCount is zero
	else {
	/*
	 * Grab the earliest time at which a request should have been started
	 * (CLEANUPTIME seconds ago: HZ = 1 second)
	 *
	 * Tasks started before this will be set to STAT_FAILED (return code
	 * ETIMEOUT) and awoken to return to their caller.
	*/

		timelimit = jiffies - (CLEANUPTIME * HZ);

		/*
	 	* Walk pending queue and clean up requests more than CLEANUPTIME
	 	* seconds old.  The list is in strict chronological order, so a
		* work element younger than any that's not too old is OK.
		*/

		hptr = &pending_list;
		list_for_each_safe(lptr, tptr, hptr) {
			pq_p = list_entry(lptr, struct work_element, liste);
			if (pq_p->requestsent < timelimit) { // too old!
				pq_p->returncode = -ETIMEOUT;
				pq_p->status[0] |= STAT_FAILED;
				// get this off any caller queue it may be on
				unbuild_caller(LONG2DEVPTR(pq_p->devindex),
					(Z90C_CALLER *) pq_p->requestptr);
				list_del(lptr);
				pendingq_count--;
				pq_p->audit[1] |= FP_TIMEDOUT;
				pq_p->audit[1] |= FP_NOTPENDING;
				pq_p->audit[1] |= FP_AWAKENING;
				atomic_set(&pq_p->alarmrung,1);
				wake_up(&pq_p->waitq);
			} // end too old
			else {
				break;
			} // end else not too old
		} // end list_for_each

		//
		// If pending count is zero, items left on the request queue may
		// never be processed.
		//
		if (pendingq_count <= 0) {
			hptr = &request_list;
			list_for_each_safe(lptr, tptr, hptr) {
				pq_p = list_entry(lptr,
						struct work_element, liste);
				if (pq_p->requestsent < timelimit) { // too old!
					pq_p->returncode = -ETIMEOUT;
					pq_p->status[0] |= STAT_FAILED;
					list_del(lptr);
					requestq_count--;
					pq_p->audit[1] |= FP_TIMEDOUT;
					pq_p->audit[1] |= FP_REMREQUEST;
					pq_p->audit[1] |= FP_AWAKENING;
					atomic_set(&pq_p->alarmrung,1);
					wake_up(&pq_p->waitq);
				} // end too old
				else {
					break;
				} // end else not too old
			} // end list_for_each
		} // end if pendingq_count 0
	} // end else deviceCount isn't zero

	// Unserialize
	spin_unlock_irq(&queuespinlock);

	/*
	 * Reschedule self.
	*/
	if ((rc = z90crypt_schedule_cleanup_task())) {
		PRINTK("Error %d detected in schedule_cleanup_task.\n", rc);
	}

} // end z90crypt_cleanup_task

int z90crypt_schedule_reader_timer (void)
{
	int rv = 0;

	if (timer_pending(&reader_timer))
		return 0;

	//
	// Set a new expiration time and mod the timer
	//
	// HZ jiffies = 1 second.
	//
	// We will execute the reader task every READERTIME milliseconds.
	//
	if ((rv=mod_timer(&reader_timer, jiffies+(READERTIME*HZ/1000))) != 0)
		PRINTK("Timer pending while modifying reader timer\n");

	return 0;
} // end z90crypt_schedule_reader_timer

void z90crypt_schedule_reader_task (unsigned long ptr)
{
	tasklet_schedule(&reader_tasklet);
} // end z90crypt_schedule_reader_task

int z90crypt_schedule_config_task (unsigned int expiration)
{
	int rv = 0;

	//
	// Set a new expiration time and mod the timer.
	//
	if (timer_pending(&config_timer))
		return 0;

	if ((rv=mod_timer(&config_timer, jiffies+(expiration*HZ))) != 0)
		PRINTK("Timer pending while modifying config timer\n");

	return 0;
} // end z90crypt_schedule_config_task

int z90crypt_schedule_cleanup_task (void)
{
	int rv = 0;

	//
	// Set a new expiration and mod the timer
	//
	// HZ jiffies = 1 second.
	//
	// We will execute the cleanup task every CLEANUPTIME seconds.
	//
	if (timer_pending(&cleanup_timer))
		return 0;

	if ((rv=mod_timer(&cleanup_timer, jiffies+(CLEANUPTIME*HZ))) != 0)
		PRINTK("Timer pending while modifying cleanup timer\n");

	return 0;
} // end z90crypt_schedule_cleanup_task

/*-------------------------------------------------------------------*/
/*                                                                   */
/* Lowlevel Functions:                                               */
/*                                                                   */
/*   create_z90crypt:  creates and initializes basic data structures */
/*                                                                   */
/*   refresh_z90crypt:  re-initializes basic data structures         */
/*                                                                   */
/*   find_crypto_devices: returns a count and mask of hardware status*/
/*                                                                   */
/*   create_crypto_device:  builds the descriptor for a device       */
/*                                                                   */
/*   destroy_crypto_device:  unallocates the descriptor for a device */
/*                                                                   */
/*   send_to_crypto_device:  sends work to a device                  */
/*                                                                   */
/*   receive_from_crypto_device:  solicits completed work from device*/
/*                                                                   */
/*   query_crypto_device: returns status information about the device*/
/*                                                                   */
/*   destroy_z90crypt:  drains all work, unallocates structs         */
/*                                                                   */
/*-------------------------------------------------------------------*/


static UCHAR NULL_psmid[8] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};

/*-------------------------------------------------------------------*/
/* MIN_MOD_SIZE is a PCICC and PCIXCC limit.                         */
/* MAX_PCICC_MOD_SIZE is a hard limit for the PCICC.                 */
/* MAX_MOD_SIZE is a hard limit for the PCIXCC and PCICA.            */
/*-------------------------------------------------------------------*/
#define MIN_MOD_SIZE 64		// bytes
#define MAX_PCICC_MOD_SIZE 128	// bytes
#define MAX_MOD_SIZE 256	// bytes

/*-------------------------------------------------------------------*/
/* Used in device configuration functions                            */
/*-------------------------------------------------------------------*/
#define MAX_RESET 90

/*-------------------------------------------------------------------*/
/* Function prototypes                                               */
/*-------------------------------------------------------------------*/
HDSTAT query_on_line (int deviceNr, int cdx, int resetNr, int *q_depth,
                      int *dev_type);
DEVSTAT reset_device (int deviceNr, int cdx, int resetNr);
DEVSTAT send_to_AP(int dev_nr,
                 int cdx,
                 int msg_len,
                 UCHAR * msg_ext_p);
DEVSTAT receive_from_AP(int dev_nr,
                        int cdx,
                        int resp_l,
                        unsigned char * resp,
                        unsigned char * psmid);
int convert_request (UCHAR * buffer,
                    int func,
                    short function,
                    UCHAR * pad_rule,
                    int cdx,
                    CDEVICE_TYPE dev_type,
                    int * msg_l_p,
                    UCHAR * msg_p);
int convert_response (UCHAR * response,
                     CDEVICE_TYPE dev_type,
                     int icaMsg_l,
                     UCHAR * buffer,
                     short function,
                     UCHAR * pad_rule,
                     int *respbufflen,
                     UCHAR * respbuff);

int create_z90crypt (int * cdx_p)
/*********************************************************************/
/*                                                                   */
/* implied input:  z90crypt                                          */
/*                                                                   */
/*         input:  *cdx_p             --  -1 or the domain specified */
/*                                        by caller                  */
/*                                                                   */
/*        output:  *cdx_p             --  crypto domain index        */
/*                                                                   */
/* At start-up, z90crypt calls this method.                          */
/*                                                                   */
/*********************************************************************/
{
  int rv = OK;
  Z90C_HDWARE_BLOCK * hdware_blk_p = NULL;

  memset(&z90crypt,0,sizeof(struct z90crypt));
  z90crypt.z90c_domain_established = FALSE;
  z90crypt.z90c_len = sizeof(struct z90crypt);
  z90crypt.z90c_max_count = Z90CRYPT_NUM_DEVS;
  z90crypt.z90c_cdx = *cdx_p;

  do {
    if ((hdware_blk_p = (Z90C_HDWARE_BLOCK *)
        kmalloc(sizeof(struct z90c_hdware_block),GFP_ATOMIC)) == NULL){
      PDEBUG("kmalloc for hardware block failed\n");
      rv = ENOMEM;
      break;
    }
    memset(hdware_blk_p,0,sizeof(struct z90c_hdware_block));

    z90crypt.z90c_hdware_info = hdware_blk_p;

    hdware_blk_p->z90c_nr_types = Z90CRYPT_NUM_TYPES;

  } while(0);

  return (rv);

}; // end create_z90crypt

int refresh_z90crypt(int * cdx_p)
/*********************************************************************/
/*                                                                   */
/*        inputs:  no explicit inputs                                */
/*                                                                   */
/* implied input:  z90crypt                                          */
/*                                                                   */
/* This function, called by the config task at startup and           */
/* periodically thereafter, returns the status of each device.       */
/*                                                                   */
/* Ensures the z90crypt has been initialized.                        */
/*                                                                   */
/* If no devices have been found yet, probes for crypto domain.      */
/*                                                                   */
/* Calls find_crypto_devices. Compares results with previous results:*/
/*                                                                   */
/* If there hasn't been any change:                                  */
/*   returns 0                                                       */
/*                                                                   */
/* If there's been any change:                                       */
/*                                                                   */
/* a.  Serializes by getting the queuespinlock                       */
/*                                                                   */
/* b.  For each device:                                              */
/*                                                                   */
/*     1) if the device was not HD_ONLINE but is now,                */
/*          calls create_crypto_device                               */
/*     2) if the device was HD_ONLINE but isn't any longer,          */
/*          calls destroy_crypto_device                              */
/*                                                                   */
/* c.  copies the new hardware status mask into                      */
/*     z90crypt.z90c_hdware_info->z90c_hdware_mask.                  */
/*                                                                   */
/* d.  Refreshes the short index arrays for:                         */
/*     1) the overall device index array z90crypt.z90c_mask          */
/*     2) the PCICA device index array                               */
/*          z90crypt.z90c_hdware_info->z90c_type_mask[0]             */
/*     3) the PCICC device index array                               */
/*          z90crypt.z90c_hdware_info->z90c_type_mask[1]             */
/*                                                                   */
/*     Note that all three of these masks will have been modified    */
/*     by create_crypto_device and/or destroy_crypto_device          */
/*                                                                   */
/* e.  Unserializes by giving back the queuespinlock                 */
/*                                                                   */
/* f.  Returns 0                                                     */
/*                                                                   */
/*********************************************************************/
{
  int i;
  int rv = OK;
  Z90C_STATUS_STR local_mask;
  Z90C_DEVICE * devPtr;
  int j;
  int indx;
  UCHAR oldStat;
  UCHAR newStat;
  BOOL return_unchanged = TRUE;

  if (z90crypt.z90c_len != sizeof(struct z90crypt))
    return ENOTINIT;

  // If the terminating flag is on, don't refresh
  if (z90crypt.z90c_terminating == 1)
    return TSQ_FATAL_ERROR;

  // If no devices have been found yet, first probe for domain
  if (z90crypt.z90c_hdware_info->z90c_hdware_mask.z90c_st_count == 0 &&
      z90crypt.z90c_domain_established == FALSE)
    rv = probe_crypto_domain(cdx_p);

  if (z90crypt.z90c_terminating == 1)
    return TSQ_FATAL_ERROR;

  if (rv == 0) {
    if (*cdx_p) {
      z90crypt.z90c_cdx = *cdx_p;
      z90crypt.z90c_domain_established = TRUE;
    }
  }
  else {
    switch (rv)
    {
      case Z90C_AMBIGUOUS_DOMAIN:
        PRINTK("More than one domain defined to this LPAR\n");
        break;

      case Z90C_INCORRECT_DOMAIN:
        PRINTK("Specified domain doesn't match assigned domain\n");
        break;

      default:
        PRINTK("Probe Domain returned %d\n",rv);
        break;
    }
    return (rv);
  }

  if ((rv = find_crypto_devices(&local_mask)) != OK) {
    PRINTK("Find Crypto Devices returned %d\n",rv);
    return (rv);
  }

  if ((rv = memcmp((void *)&local_mask,
                   (void *)&(z90crypt.z90c_hdware_info->z90c_hdware_mask),
                   sizeof(struct z90c_status_str)))== OK) { // no change
    return_unchanged = TRUE;
    for (i=0;i<Z90CRYPT_NUM_TYPES;i++) {

      // Check for disabled cards.  If any device is marked disabled,
      // destroy it.

      for (j = 0; j < z90crypt.z90c_hdware_info->z90c_type_mask[i].z90c_st_count;j++)      {
        indx = z90crypt.z90c_hdware_info->z90c_type_x_addr[i].z90c_device_index[j];
        devPtr = z90crypt.z90c_device_p[indx];
        if (devPtr != NULL && devPtr->z90c_disabled == TRUE) {
          local_mask.z90c_st_mask[indx] = HD_NOT_THERE;
          return_unchanged = FALSE;
        } // end if
      } // end for j ...
    } // end for i ...

    if (return_unchanged == TRUE)
      return OK;
  }

  rv = OK;

  spin_lock_irq(&queuespinlock);

  for (i =  0; i < z90crypt.z90c_max_count; i++) {
    oldStat = z90crypt.z90c_hdware_info->z90c_hdware_mask.z90c_st_mask[i];
    newStat = local_mask.z90c_st_mask[i];
    if (oldStat == HD_ONLINE && newStat != HD_ONLINE) {
      destroy_crypto_device(i);
    }
    else if (oldStat != HD_ONLINE && newStat == HD_ONLINE) {
      rv = create_crypto_device(i);
      if (rv >= REC_FATAL_ERROR)
        break;
      if (rv != OK) {
        local_mask.z90c_st_mask[i] = HD_NOT_THERE;
	local_mask.z90c_st_count--; // fix for Carlos' problem
      }
    }
  }

  if (rv >= REC_FATAL_ERROR)
    return rv;

  memcpy((void *)z90crypt.z90c_hdware_info->z90c_hdware_mask.z90c_st_mask,
         (void *)local_mask.z90c_st_mask,
         sizeof(local_mask.z90c_st_mask));
  z90crypt.z90c_hdware_info->z90c_hdware_mask.z90c_st_count =
						local_mask.z90c_st_count;
  z90crypt.z90c_hdware_info->z90c_hdware_mask.z90c_disabled_count =
						local_mask.z90c_disabled_count;

  refresh_index_array(&z90crypt.z90c_mask,
                    &z90crypt.z90c_overall_device_x);

  for (i=0;i<Z90CRYPT_NUM_TYPES;i++) {
    refresh_index_array(
                &(z90crypt.z90c_hdware_info->z90c_type_mask[i]),
                &(z90crypt.z90c_hdware_info->z90c_type_x_addr[i]));
  } // end for i

  spin_unlock_irq(&queuespinlock);

  return (rv);

}; // end refresh_z90crypt

int find_crypto_devices(Z90C_STATUS_STR * deviceMask)
/*********************************************************************/
/*                                                                   */
/*         inputs:  none                                             */
/*                                                                   */
/* implied inputs:  the z90crypt                                     */
/*                                                                   */
/*        outputs:  deviceMask                                       */
/*                                                                   */
/*        process:  1.  Zeroes the count                             */
/*                  2.  For each possible device number | cdx        */
/*                        calls queryDevStat to get the status       */
/*                        if the device is online or active, records */
/*                        the queue depth in the queue depth array.  */
/*                        Note that access to this array is *not*    */
/*                        serialized.                                */
/*                  3.  Returns the deviceMask                       */
/*                                                                   */
/*  This function is called by refresh_z90crypt.                     */
/*                                                                   */
/*  Note that this process is completely non-intrusive, therefore    */
/*  the configlock need not be held by a process calling this        */
/*  function.                                                        */
/*                                                                   */
/*********************************************************************/
{
  int rv= OK;
  int i;                      // loop counter
  int q_depth;                // number of work elements at one time
  int dev_type;               // device type
  HDSTAT hd_stat;             // device status

  deviceMask->z90c_st_count = 0;
  deviceMask->z90c_disabled_count = 0;

  for (i=0;i<z90crypt.z90c_max_count;i++) {
    hd_stat = query_on_line(i,z90crypt.z90c_cdx,MAX_RESET,&q_depth, &dev_type);
    if (hd_stat == HD_TSQ_EXCEPTION) {
      rv = TSQ_FATAL_ERROR;
      z90crypt.z90c_terminating = 1;
      PRINTKC("find_crypto_devices: Exception during probe for crypto devices\n");
      break;
      }
    deviceMask->z90c_st_mask[i] = hd_stat;
    if (hd_stat == HD_ONLINE) {
      PDEBUG("find_crypto_devices: Got an online crypto!: %d\n",i);
      PDEBUG("find_crypto_devices: Got a queue depth of %d\n",q_depth);
      PDEBUG("find_crypto_devices: Got a device type of %d\n",dev_type);
      deviceMask->z90c_st_count++;
      if (q_depth <= 0) {
        rv = TSQ_FATAL_ERROR;
        break;
      }
      z90crypt.z90c_q_depth_array[i] = q_depth;
      z90crypt.z90c_dev_type_array[i] = dev_type;
    } // end if hd_stat = hd_online
  } // end for(i...

  return (rv);
} // end find_crypto_devices

int probe_crypto_domain(int * cdx_p)
/*********************************************************************/
/*                                                                   */
/*         inputs:  none                                             */
/*                                                                   */
/* implied inputs:  the z90crypt                                     */
/*                                                                   */
/*        outputs:  the crypto domain                                */
/*                                                                   */
/*        process:  1.  Zeroes the count                             */
/*                  2.  For each possible device number | cdx        */
/*                        calls queryDevStat to get the status       */
/*                        if the device is online or active.         */
/*                  3.  Returns the domain in which the above        */
/*                      condition was encountered.                   */
/*                                                                   */
/*  This function is called by refresh_z90crypt.                     */
/*                                                                   */
/*  Note that this process is completely non-intrusive, therefore    */
/*  the  configlock need not be held by a process calling this       */
/*  function.                                                        */
/*                                                                   */
/*********************************************************************/
{
  int rv = 0;
  int correct_cdx_found = 0;
  int i,j,k;                  // loop counters
  HDSTAT hd_stat;             // device status
  int cdx_array[16];          // array of found cdx's
  int q_depth = 0;
  int dev_type = 0;

  k = 0;
  do {
  for (i=0;i<z90crypt.z90c_max_count;i++) {
    hd_stat = HD_NOT_THERE;
    for (j = 0; j <= 15; cdx_array[j++] = -1);
    k = 0;
    for (j = 0; j <= 15; j++) {
      hd_stat = query_on_line(i, j, MAX_RESET, &q_depth, &dev_type);
      if (hd_stat == HD_TSQ_EXCEPTION) {
        z90crypt.z90c_terminating = 1;
        PRINTKC("probe_crypto_domain: Exception during probe for crypto domain\n");
        break;
      }
      if (hd_stat == HD_ONLINE) {
        cdx_array[k++] = j;
        if (*cdx_p == j) {
          correct_cdx_found  = 1;
          break;
        }
      }
    } // end for(j...

    if (correct_cdx_found == 1 || k != 0)
      break;

    if (z90crypt.z90c_terminating)
      break;

  } // end for(i...

  if (z90crypt.z90c_terminating)
    return (TSQ_FATAL_ERROR);

  if (correct_cdx_found) {
    rv = 0;
    break;
  }

  switch(k) {
    case 0:
      *cdx_p = 0;
      rv = 0;
      break;

    case 1:
      if (*cdx_p == -1 || z90crypt.z90c_domain_established == FALSE) {
        *cdx_p = cdx_array[0];
        rv = 0;
      }
      else {
        if (*cdx_p != cdx_array[0]) {
          PRINTK("probe_crypto_domain: Specified cdx: %d.  Assigned cdx: %d\n",*cdx_p,cdx_array[0]);
          rv = Z90C_INCORRECT_DOMAIN;
        }
        else
          rv = 0;
      }
      break;

    default:
      rv = Z90C_AMBIGUOUS_DOMAIN;
      break;
  } // end switch
  } while(0);

  return (rv);
} // end probe_crypto_domain

int refresh_index_array(struct z90c_status_str *status_str,
                      struct z90c_device_x *index_array)
/*********************************************************************/
/*                                                                   */
/*           input:  status_str -- a count and mask of device status */
/*                                                                   */
/*   implied input:  none                                            */
/*                                                                   */
/*          output:  index_array -- a count and array of indexes     */
/*                                                                   */
/*                   returns count                                   */
/*                                                                   */
/*********************************************************************/
{
  int i = -1;
  int count = 0;
  DEVSTAT stat;

  do {
    stat = status_str->z90c_st_mask[++i];
    if (stat == DEV_ONLINE)
      index_array->z90c_device_index[count++] = i;
  } while (i<Z90CRYPT_NUM_DEVS && count < status_str->z90c_st_count);

  return count;

} // end refresh_index_array

int create_crypto_device(int index)
/*********************************************************************/
/*                                                                   */
/*         inputs:  index                                            */
/*                                                                   */
/* implied inputs:  the z90crypt                                     */
/*                                                                   */
/* implied output:  the z90crypt  the device descriptor is           */
/*                                allocated and pointed to from      */
/*                                z90crypt.z90c_device_p[index]      */
/*                                                                   */
/* During refresh_z90crypt, the driver calls this function for any   */
/* device that has just come on line.  Note that this function       */
/* assumes the  configlock is held exclusive by the caller.          */
/*                                                                   */
/* The last step in this process is to invoke init_semaphore         */
/*********************************************************************/
{
  int rv = OK;
  Z90C_DEVICE * dev_ptr;
  struct z90c_status_str * type_str_p;
  int devstat = DEV_GONE;
  CDEVICE_TYPE deviceType = PCICA;
  int total_size;

  if((dev_ptr = z90crypt.z90c_device_p[index]) == NULL) {
    total_size = sizeof(struct z90c_device) +
                 z90crypt.z90c_q_depth_array[index] * sizeof(int);

    if ((dev_ptr =
            (Z90C_DEVICE *)kmalloc(total_size,GFP_ATOMIC))==NULL){
      PRINTK("create_crypto_device: Could not malloc device %i\n",index);
      return ENOMEM;
    }

    memset(dev_ptr,0,total_size);

    if ((dev_ptr->z90c_dev_resp_p=kmalloc(MAX_RESPONSE_SIZE,GFP_ATOMIC))
                                                                  ==NULL){
      kfree(dev_ptr);
      PRINTK("create_crypto_device: Could not malloc device %i receiving buffer\n",index);
      return ENOMEM;
    }
    dev_ptr-> z90c_dev_resp_l = MAX_RESPONSE_SIZE;
    INIT_LIST_HEAD(&(dev_ptr->z90c_dev_caller_list));
  }

  devstat = reset_device(index, z90crypt.z90c_cdx, MAX_RESET);
  if (devstat == DEV_RSQ_EXCEPTION) {
    PRINTK("create_crypto_device: Exception during reset device %d\n",index);
    kfree(dev_ptr->z90c_dev_resp_p);
    kfree(dev_ptr);
    return RSQ_FATAL_ERROR;
  }

  if (devstat == DEV_ONLINE) {
    dev_ptr->z90c_dev_self_x = index;
    dev_ptr->z90c_dev_type = z90crypt.z90c_dev_type_array[index];
    if (dev_ptr->z90c_dev_type == NILDEV) {
      rv = probe_device_type(dev_ptr);
      if (rv != OK) {
        kfree (dev_ptr->z90c_dev_resp_p);
        kfree (dev_ptr);
        return rv;
      }
    }
    deviceType = dev_ptr->z90c_dev_type;
    z90crypt.z90c_dev_type_array[index] = deviceType;
    switch(deviceType) {
      case PCICA:
        z90crypt.z90c_hdware_info->z90c_device_type_array[index] = 1;
        break;
      case PCICC:
        z90crypt.z90c_hdware_info->z90c_device_type_array[index] = 2;
        break;
      case PCIXCC:
        z90crypt.z90c_hdware_info->z90c_device_type_array[index] = 3;
        break;
      default:
        z90crypt.z90c_hdware_info->z90c_device_type_array[index] = -1;
        break;
    }
  }

  dev_ptr-> z90c_dev_len = sizeof(struct z90c_device);
  // XXX: 'q_depth' returned by the hardware is one less than the actual depth
  dev_ptr-> z90c_dev_q_depth = z90crypt.z90c_q_depth_array[index];
  dev_ptr-> z90c_dev_type = z90crypt.z90c_dev_type_array[index];
  dev_ptr-> z90c_dev_stat = devstat;
  dev_ptr-> z90c_disabled = FALSE;
  z90crypt.z90c_device_p[index] = dev_ptr;

  if (devstat == DEV_ONLINE) {

    if (z90crypt.z90c_mask.z90c_st_mask[index] != DEV_ONLINE) {
      z90crypt.z90c_mask.z90c_st_mask[index] = DEV_ONLINE;
      z90crypt.z90c_mask.z90c_st_count++;
    }

    type_str_p = &(z90crypt.z90c_hdware_info->z90c_type_mask[deviceType]);
    if (type_str_p->z90c_st_mask[index] != DEV_ONLINE) {
      type_str_p->z90c_st_mask[index] = DEV_ONLINE;
      type_str_p->z90c_st_count++;
    }
  }

  return OK;
} // end create_crypto_device

int destroy_crypto_device(int index)
/*********************************************************************/
/*                                                                   */
/*         inputs:  index                                            */
/*                                                                   */
/* implied inputs:  the z90crypt                                     */
/*                                                                   */
/*        outputs:  none                                             */
/*                                                                   */
/* implied output:  the z90crypt  the device descriptor is           */
/*                                unallocated and the pointer        */
/*                                z90crypt.z90c_device_p[index]      */
/*                                is nullified.                      */
/*                                                                   */
/* During refresh_z90crypt, the driver calls this function for any   */
/* device that has just gone off line.  Note that this function      */
/* assumes the  configlock is held exclusive by the caller.          */
/*                                                                   */
/*********************************************************************/
{
  Z90C_DEVICE * dev_ptr;
  CDEVICE_TYPE t = -1;
  BOOL disabledFlag = FALSE;

  dev_ptr = z90crypt.z90c_device_p[index];

  // remember device type; get rid of device struct
  if (dev_ptr != NULL) {
    disabledFlag = dev_ptr->z90c_disabled;
    t = dev_ptr->z90c_dev_type;
    if (dev_ptr->z90c_dev_resp_p != NULL)
      kfree(dev_ptr->z90c_dev_resp_p);
    kfree(dev_ptr);
  }

  // remove the address of the struct from the array of device struct addresses
  z90crypt.z90c_device_p[index] = (Z90C_DEVICE *) NULL;

  // if the type is valid, remove the device from the type_mask
  if (t != -1) {
    if (z90crypt.z90c_hdware_info->z90c_type_mask[t].z90c_st_mask[index] != 0x00) {
      z90crypt.z90c_hdware_info->z90c_type_mask[t].z90c_st_mask[index] = 0x00;
      z90crypt.z90c_hdware_info->z90c_type_mask[t].z90c_st_count--;
      if (disabledFlag == TRUE)
        z90crypt.z90c_hdware_info->z90c_type_mask[t].z90c_disabled_count--;
    }
  }

  // Get the device out of the overall status array
  if (z90crypt.z90c_mask.z90c_st_mask[index] != DEV_GONE) {
    z90crypt.z90c_mask.z90c_st_mask[index] = DEV_GONE;
    z90crypt.z90c_mask.z90c_st_count--;
  }

  // Change the device type to null (00)
  z90crypt.z90c_hdware_info->z90c_device_type_array[index] = 0;

  return OK;
} // end destroy_crypto_device


int build_caller(char * psmid,
                int func,
                short function,
                UCHAR * pad_rule,
                int buff_len,
                char * buff_ptr,
                CDEVICE_TYPE dev_type,
                Z90C_CALLER * caller_p)
/*********************************************************************/
/*  builds struct z90c_caller, converts message from generic format  */
/*  to device-dependent format.                                      */
/*                                                                   */
/*         inputs:  psmid -- an 8 byte identifier                    */
/*                                                                   */
/*                  func -- ICARSAMODEXPO or ICARSACRT as defined    */
/*                          in z90crypt.h                            */
/*                                                                   */
/*                  buff_len -- length of caller's buffer            */
/*                                                                   */
/*                  buff_ptr -- address of caller's buffer           */
/*                                                                   */
/*                  dev_type  -- a device type                       */
/*                                                                   */
/* implied inputs:  the z90crypt                                     */
/*                                                                   */
/*        outputs:  caller_p -- contents of a struct z90c_caller     */
/*                                                                   */
/*                  returns rv:                                      */
/*                      0 -- all went well                           */
/*                      4 --                                         */
/*                     12 -- device not available                    */
/*                                                                   */
/*        process:  1.  If something's wrong with the device type,   */
/*                      returns rv = device not available.           */
/*                                                                   */
/*                  2.  Fills in the input caller struct with        */
/*                      the correct information, returns rv = ok     */
/*                                                                   */
/*********************************************************************/
{
  int rv = OK;
  UCHAR * temp_up = NULL;

  if (dev_type != PCICC && dev_type != PCICA && dev_type != PCIXCC) {
    return (SEN_NOT_AVAIL);
  }

  memcpy(caller_p->z90c_caller_id,psmid,sizeof(caller_p->z90c_caller_id));
  caller_p->z90c_caller_func = func;
  caller_p->z90c_caller_function = function;
  caller_p->z90c_caller_dev_dep_req_p = caller_p->z90c_caller_dev_dep_req;
  caller_p->z90c_caller_dev_dep_req_l = MAX_RESPONSE_SIZE; // re-init buffer len
  memcpy(caller_p->z90c_pad_rule, pad_rule, sizeof(caller_p->z90c_pad_rule));
  caller_p->z90c_caller_buf_p = buff_ptr;
  INIT_LIST_HEAD(&(caller_p->z90c_caller_liste));

  if ((rv = convert_request(buff_ptr,
                           func,
                           function,
                           pad_rule,
                           z90crypt.z90c_cdx,
                           dev_type,
                           &(caller_p->z90c_caller_dev_dep_req_l),
                           caller_p->z90c_caller_dev_dep_req_p))!=OK) {
    PRINTK("Error from convert_request: %08x\n",rv);
    return (rv);
  }

  temp_up = &(caller_p->z90c_caller_dev_dep_req_p[4]);
  memcpy(temp_up, psmid, 8);

  return rv;

} // end build_caller

int unbuild_caller(Z90C_DEVICE * device_p,
                  Z90C_CALLER * caller_p)
/*********************************************************************/
/*  cleans up caller struct                                          */
/*  pushes the caller struct on the device's caller stack            */
/*                                                                   */
/*         inputs:  device_p  -- address of a device descriptor      */
/*                                                                   */
/*                  caller_p -- address of a struct z90c_caller      */
/*                                                                   */
/* implied inputs:  the z90crypt                                     */
/*                                                                   */
/*                                                                   */
/*                  returns rv:                                      */
/*                      8 -- queue empty                             */
/*                                                                   */
/*         process: 1.  clean up caller struct                       */
/*                                                                   */
/*                  2.  push the caller struct on the device caller  */
/*                      stack                                        */
/*                                                                   */
/*********************************************************************/
{
  if (caller_p != NULL) {
    if ((caller_p->z90c_caller_liste.next) &&
	(caller_p->z90c_caller_liste.prev)) {
	if (!(list_empty(&(caller_p->z90c_caller_liste)))) {
	   list_del(&(caller_p->z90c_caller_liste));
	   (device_p->z90c_dev_caller_count)--;
  	   INIT_LIST_HEAD(&(caller_p->z90c_caller_liste));
	}
    }
    memset(caller_p->z90c_caller_id,0,sizeof(caller_p->z90c_caller_id));
    caller_p->z90c_caller_func = 0;
    caller_p->z90c_caller_function = 0;
    caller_p->z90c_caller_dev_p = NULL;
    memset(caller_p->z90c_pad_rule, 0, sizeof(caller_p->z90c_pad_rule));
  }

  return 0;

} // end unbuild_caller

int remove_device(Z90C_DEVICE * device_p)
/*********************************************************************/
/*                                                                   */
/*         inputs:  device_p  -- address of a device descriptor      */
/*                                                                   */
/* implied inputs:  the z90crypt                                     */
/*                                                                   */
/*                  returns rv = 0                                   */
/*                                                                   */
/*        process:  If device isn't already disabled:                */
/*                                                                   */
/*                  a.  mark the device disabled                     */
/*                                                                   */
/*                  b.  increment the device type disabled count     */
/*                                                                   */
/*                  c.  increment the master device disabled         */
/*                      count                                        */
/*                                                                   */
/*********************************************************************/
{
  int rv = OK;
  CDEVICE_TYPE devTp = -1;

  if ((device_p != NULL) &&
      (device_p->z90c_disabled == FALSE)) {
    device_p->z90c_disabled = TRUE;
    devTp = device_p->z90c_dev_type;
    z90crypt.z90c_hdware_info->z90c_type_mask[devTp].z90c_disabled_count++;
    z90crypt.z90c_hdware_info->z90c_hdware_mask.z90c_disabled_count++;
  }

  return (rv);

} // end remove_device

int get_crypto_request_buffer(UCHAR * psmid,
				int func,
				int buff_len,
				UCHAR * buff_ptr,
				CDEVICE_TYPE * devType_p,
				unsigned char * caller_p)
/*********************************************************************/
/*                                                                   */
/*         inputs:  psmid -- an 8 byte identifier                    */
/*                                                                   */
/*                  func -- ICARSAMODEXPO or ICARSACRT as defined in */
/*                          z90crypt.h                               */
/*                                                                   */
/*                  buff_len -- length of caller's buffer            */
/*                                                                   */
/*                  buff_ptr -- address of caller's buffer           */
/*                                                                   */
/*                  *devType_p -- PCICA, PCICC, PCIXCC, or ANYDEV    */
/*                                                                   */
/*                  caller_p  -- address of the request buffer       */
/*                                                                   */
/* The caller's buffer should contain an instance of either of two   */
/* types in z90crypt.h (please see the comments next to each of the  */
/* structure declarations for all the implementation details)        */
/*                                                                   */
/* 1. ica_rsa_modexpo_crt_t                                          */
/*                                                                   */
/* 2. ica_rsa_modexpo_t                                              */
/*                                                                   */
/* implied inputs:  the z90crypt                                     */
/*                                                                   */
/*        outputs:  *devType_p -- PCICA, PCICC, or PCIXCC            */
/*                                                                   */
/* implied output:  None.                                            */
/*                                                                   */
/*********************************************************************/
{
  int rv = 0;
  int parmLen = 0;
  ica_rsa_modexpo_t * mex_p;
  ica_rsa_modexpo_crt_t * crt_p;
  UCHAR *temp_buffer;
  short function = PCI_FUNC_KEY_ENCRYPT;
  UCHAR pad_rule[8];

  PDEBUG("device type input to get_crypto_request_buffer: %i\n",*devType_p);

  // Refuse request if z90c_terminating flag is on
  if (z90crypt.z90c_terminating == 1){
    return REC_NO_RESPONSE;
  }

  temp_buffer = kmalloc(256, GFP_KERNEL);
  if (!temp_buffer) {
    PRINTK("kmalloc for temp_buffer in get_crypto_request_buffer failed!\n");
    return SEN_NOT_AVAIL;
  }

  /*
   * The psmid can't be NULL or nothing works at all.
   *
   * The buffer can't be NULL or nothing works at all.
   *
   * Input data length has to be even, at least 64 and at most 256.
   *
   * Output data length will be checked before any store, must be
   * at least 16.
   */

  if (memcmp(psmid, NULL_psmid, 8) == 0) {
    kfree(temp_buffer);
    return SEN_FATAL_ERROR;
  }

  if (buff_ptr == NULL) {
    PRINTK("Buffer pointer Null in getCryptoRequestBuff\n");
    kfree(temp_buffer);
    return SEN_USER_ERROR;
  }

  mex_p = (ica_rsa_modexpo_t *)buff_ptr;
  crt_p = (ica_rsa_modexpo_crt_t *)buff_ptr;

  parmLen = mex_p->inputdatalength;
  PDEBUG("send: parmLen at top = %d\n",parmLen);
  PDEBUG("send: buffLen at top = %d\n",buff_len);
  if ((parmLen < 1) ||
      (parmLen > MAX_MOD_SIZE)) {
    PRINTK("send: inputdatalength[%d] is not valid\n",
           mex_p->inputdatalength);
    kfree(temp_buffer);
    return SEN_USER_ERROR;
  }

  if (mex_p->outputdatalength < mex_p->inputdatalength) {
    PRINTK("send: outputdatalength[%d] < inputdatalength[%d]\n",
           mex_p->outputdatalength, mex_p->inputdatalength);
    kfree(temp_buffer);
    return SEN_USER_ERROR;
  }

  if ((mex_p->inputdata == NULL) ||
      (mex_p->outputdata == NULL)) {
    PRINTK("send: inputdata[%p] or outputdata[%p] is NULL\n",
           mex_p->outputdata, mex_p->inputdata);
    kfree(temp_buffer);
    return SEN_USER_ERROR;
  }

  // As long as outputdatalength is big enough, we can set the outputdatalength
  // equal to the inputdatalength, since that is the number of bytes we will
  // copy in any case
  if (mex_p->outputdatalength > mex_p->inputdatalength)
    mex_p->outputdatalength = mex_p->inputdatalength;

  /*
   * Function must be ICARSAMODEXPO or ICARSACRT
   *
   * Buffer length has to be consistent with function
   *
   * None of the key or data address can be NULL
   */

  switch(func) {
    case ICARSAMODEXPO:
      if ((mex_p->b_key == NULL) ||
          (mex_p->n_modulus == NULL)) {
        rv = SEN_USER_ERROR;
      }
      break;

    case ICARSACRT:
      if (parmLen != 2*(parmLen/2)) {
        PRINTK("inputdatalength[%d] is not even for CRT form\n",
               crt_p->inputdatalength);
        rv = SEN_USER_ERROR;
      }
      if ((crt_p->bp_key == NULL) ||
          (crt_p->bq_key == NULL) ||
          (crt_p->np_prime == NULL) ||
          (crt_p->nq_prime == NULL) ||
          (crt_p->u_mult_inv == NULL)) {
        rv = SEN_USER_ERROR;
      }
      break;

    default:
      rv = SEN_USER_ERROR;
  }
  if (rv != OK) {
    kfree(temp_buffer);
    return rv;
  }

  // caller_p had better not be null
  if (caller_p == NULL) {
    kfree(temp_buffer);
    return SEN_USER_ERROR;
  }

  /*
   * Device type must be PCICA, PCICC, PCIXCC, or ANYDEV
   */
  if ((*devType_p != PCICA) &&
      (*devType_p != PCICC) &&
      (*devType_p != PCIXCC) &&
      (*devType_p != ANYDEV)) {
    kfree(temp_buffer);
    return SEN_USER_ERROR;
  }

  // We will select the most capable device (or verify that whatever type
  // we would like is available).
  if (select_device_type(devType_p) < 0) {
    kfree(temp_buffer);
    return SEN_NOT_AVAIL;
  }
  
  if (copy_from_user(temp_buffer,
                         mex_p->inputdata,
                         mex_p->inputdatalength) != 0) {
    kfree(temp_buffer);
    return SEN_RELEASED;
  }

  switch (*devType_p)
  {
    // PCICA does everything with a simple RSA mod-expo operation
    case PCICA:
      function = PCI_FUNC_KEY_ENCRYPT; // use "PKE" for encrypt and decrypt
      break;

    // PCIXCC does all Mod-Expo form with a simple RSA mod-expo operation,
    // and all CRT forms with a PKCS-1.2 format decrypt
    case PCIXCC:
      // Anything less than MIN_MOD_SIZE (512 bits, 64 bytes) MUST go to a
      // PCICA
      if (parmLen < MIN_MOD_SIZE) {
        kfree(temp_buffer);
        return SEN_NOT_AVAIL;
      }

      if (func == ICARSAMODEXPO)
        function = PCI_FUNC_KEY_ENCRYPT; // use "PKE" for Mod-Expo form keys
      else
        function = PCI_FUNC_KEY_DECRYPT; // use "PKD" for CRT form keys
      break;

    // PCICC does everything as a PKCS-1.2 format request
    case PCICC:
      // Anything less than MIN_MOD_SIZE (512 bits, 64 bytes) MUST go to a
      // PCICA
      if (parmLen < MIN_MOD_SIZE) {
        kfree(temp_buffer);
        return SEN_NOT_AVAIL;
      }

      // If modulus size (inputdatalength) exceeds MAX_PCICC_MOD_SIZE, we
      // cannot use a PCICC
      if (parmLen > MAX_PCICC_MOD_SIZE) {
        kfree(temp_buffer);
        return SEN_NOT_AVAIL;
      }

      // PCICC cannot handle input that is is PKCS#1.1 padded
      if (isPKCS1_1Padded(temp_buffer, mex_p->inputdatalength)) {
        kfree(temp_buffer);
        return SEN_NOT_AVAIL;
      }

      if (func == ICARSAMODEXPO) {
        if (isPKCS1_2padded(temp_buffer, mex_p->inputdatalength))
          function = PCI_FUNC_KEY_ENCRYPT;
        else
          function = PCI_FUNC_KEY_DECRYPT;
      }
      else // all CRT forms are decrypts, by definition
        function = PCI_FUNC_KEY_DECRYPT;
      break;
  }

  PDEBUG("function: %04x\n", function);

  rv = build_caller(psmid, func,
                    function,
                    pad_rule,
                    buff_len,
                    buff_ptr,
                    *devType_p,
                    (Z90C_CALLER *)caller_p);

  PDEBUG("rv from build_caller = %04x\n", rv);

  kfree(temp_buffer);
  return rv;
} // end get_crypto_request_buffer

int send_to_crypto_device(UCHAR * psmid,
                     int func,
                     int buff_len,
                     UCHAR * buff_ptr,
                     CDEVICE_TYPE devType,
                     int * devNr_p,
                     unsigned char * reqBuff)
/*********************************************************************/
/*                                                                   */
/*         inputs:  caller_p -- pointer to a caller struct           */
/*                                                                   */
/*         process: 2.  Time stamps the request                      */
/*                                                                   */
/*                  3.  Invokes send_to_AP                           */
/*                                                                   */
/*                  4.  An exception may be taken during             */
/*                      send, in which case DEV_SEN_EXCEPTION        */
/*                      will be returned.  In that case, return      */
/*                      SEN_FATAL_ERROR.                             */
/*                                                                   */
/*********************************************************************/
{
  int rv = 0;
  int dv = DEV_ONLINE;
  Z90C_CALLER * caller_p;
  Z90C_DEVICE * device_p;
  int dev_nr;

  if (!(reqBuff))
    return (SEN_FATAL_ERROR);

  caller_p = (Z90C_CALLER *) reqBuff;

  caller_p->z90c_caller_sen_time = jiffies;

  dev_nr = *devNr_p;

  if ((rv = select_device(&devType,&dev_nr))==-1) {
    if (z90crypt.z90c_hdware_info->z90c_hdware_mask.z90c_st_count != 0) {
      return SEN_RETRY;
    }
    else {
      return SEN_NOT_AVAIL;
    }
  }

  *devNr_p = dev_nr;
  rv = 0;	                     // re-init rv

  device_p = z90crypt.z90c_device_p[dev_nr];

  if (device_p->z90c_dev_type != devType)
    return SEN_RETRY;

  if (device_p == NULL)
    return SEN_NOT_AVAIL;

  if (device_p->z90c_dev_caller_count >= device_p->z90c_dev_q_depth) {
    return SEN_QUEUE_FULL;
  }

  do {
    PDEBUG("device number prior to send:  %i\n",dev_nr);
    dv = send_to_AP(dev_nr,
                    z90crypt.z90c_cdx,
                    caller_p->z90c_caller_dev_dep_req_l,
                    caller_p->z90c_caller_dev_dep_req_p);
    if (dv != DEV_ONLINE){
      if (dv==DEV_SEN_EXCEPTION) {
        rv = SEN_FATAL_ERROR;
        z90crypt.z90c_terminating = 1;
        PRINTKC("Exception during send to device %i\n",dev_nr);
        break;
      }
      switch(dv) {
        case DEV_GONE:
          PRINTK("Device %d not available\n", dev_nr);
          remove_device(device_p);
          rv = SEN_NOT_AVAIL;
          break;
        case DEV_EMPTY:
          rv = SEN_NOT_AVAIL;
          break;
        case DEV_NO_WORK:
          rv = SEN_FATAL_ERROR;
          break;
        case DEV_BAD_MESSAGE:
          rv = SEN_USER_ERROR;
          break;
        case DEV_QUEUE_FULL:
          rv = SEN_QUEUE_FULL;
          break;
        default:
          break;
      }
      break;
    } // end if dv not OK
  } while(0);

  // Get the caller on the list
  if (rv == OK) {
    list_add_tail(&(caller_p->z90c_caller_liste),
                  &(device_p->z90c_dev_caller_list));
    ++(device_p->z90c_dev_caller_count);
  }

  return (rv);
} // end send_to_crypto_device

int receive_from_crypto_device(int index,
                            UCHAR * psmid,
                            int * buff_len_p,
                            UCHAR * buff,
                            UCHAR ** dest_p_p)
/*********************************************************************/
/*                                                                   */
/*         inputs:  index -- index of the device from which          */
/*                           the work element is to be received      */
/*                                                                   */
/* implied inputs:  the z90crypt                                     */
/*                                                                   */
/*        outputs:  psmid     -- 8 byte identifier                   */
/*                                                                   */
/*                  *buff_len_p -- length of output                  */
/*                                                                   */
/*                  buff      -- output from receive                 */
/*                                                                   */
/*                  *dest_p_p -- user buffer that is the ultimate    */
/*                               destination of the output.          */
/*                                                                   */
/*                               When control returns, the caller's  */
/*                               results will have been copied to    */
/*                               buff, and the length of the result  */
/*                               will be stored in *buff_len_p.      */
/*                               *dest_p_p will hold the address     */
/*                               into which the output should be     */
/*                               copied from buff.                   */
/*                                                                   */
/*                               In the event of serious error, of   */
/*                               course, outputdata and              */
/*                               outputdatalength will be unchanged. */
/*                                                                   */
/*        process:  1.  Invoke receive_from_AP with input index,     */
/*                      z90crypt.z90c_cdx, the output buffer         */
/*                      pointed to from the z90c_device(index) and   */
/*                      response struct imbedded in                  */
/*                      z90c_device(index).                          */
/*                                                                   */
/*                  2.  If the receive_from_AP returns DEV_ONLINE    */
/*                      (thus indicating success):                   */
/*                                                                   */
/*                      a.  find the caller in the queue pointed to  */
/*                          from the z90c_device by matching the     */
/*                          psmid returned by receive_from_AP to the */
/*                          the psmid's in the device's callerQueue. */
/*                          If the psmid can't be matched, return    */
/*                          REC_USER_GONE.                           */
/*                                                                   */
/*                      b.  call convert_response to extract the     */
/*                          reply code and (if the reply code        */
/*                          indicates success) the result of the     */
/*                          operation from the response.             */
/*                                                                   */
/*                          A non-success reply code extracted by    */
/*                          convert_response will indicate one of    */
/*                          four conditions:                         */
/*                          1) wrong message type.  In that          */
/*                             case, convert_response will return    */
/*                             BAD_MESSAGE_DRIVER.  This routine     */
/*                             will return REC_FATAL_ERROR.          */
/*                          2) incorrect message part.  In that      */
/*                             case, convert_response will return    */
/*                             BAD_MESSAGE_USER.  This routine will  */
/*                             return REC_USER_ERROR.                */
/*                          3) incorrect message syntax.  In this    */
/*                             case, convert_response will return    */
/*                             BAD_MESSAGE_DRIVER.  This routine     */
/*                             will return REC_FATAL_ERROR.          */
/*                          4) storage access error.  In that case,  */
/*                             the REC_RELEASED will be returned.    */
/*                             This routine will simply propagate    */
/*                             the REC_RELEASED error code.          */
/*                                                                   */
/*                  3.  If receive_from_AP returns DEV_DECONFIGURED, */
/*                      DEV_CHECKSTOPPED or DEV_GONE, call           */
/*                      remove_device and return REC_NO_RESPONSE.    */
/*                                                                   */
/*                  4.  If receive_from_AP returns DEV_EMPTY, return */
/*                      REC_EMPTY.                                   */
/*                                                                   */
/*                  5.  If receive_from_AP returns DEV_BAD_MESSAGE,  */
/*                      return REC_NO_RESPONSE.  In that case, the   */
/*                      device is considered in error.               */
/*                                                                   */
/*                  6.  If receive_from_AP returns DEV_NO_WORK,      */
/*                      return REC_NO_WORK.                          */
/*                                                                   */
/*                  7.  An exception may be taken during             */
/*                      receive, in which case DEV_REC_EXCEPTION     */
/*                      will be returned.  In that case, return      */
/*                      REC_FATAL_ERROR.                             */
/*                                                                   */
/*                  8.  Return to caller.                            */
/*                                                                   */
/*                                                                   */
/*********************************************************************/
{
  DEVSTAT dv = DEV_EMPTY;
  int rv = 0;
  Z90C_DEVICE * devPtr;
  Z90C_CALLER * caller_p = NULL;
  CDEVICE_TYPE devType = PCICC;
  ica_rsa_modexpo_t * icaMsg_p;
  int temp_rv = 0;     //SCAFFOLDING
  struct list_head *ptr = NULL;
  struct list_head *tptr = NULL;
  struct list_head *hptr = NULL;

  memcpy(psmid, NULL_psmid, sizeof(NULL_psmid));

  // If the terminating flag is on, don't try receiving
  if (z90crypt.z90c_terminating == 1)
    return (REC_FATAL_ERROR);

  do {

    PDEBUG("Dequeue called for device %i\n",index);
    devPtr = z90crypt.z90c_device_p[index];
    if (devPtr == (void *)NULL) {
      rv = REC_NO_RESPONSE;
      break;
    }

    if (devPtr->z90c_dev_self_x != index) {
      PRINTK("Corrupt dev ptr in receive_from_AP\n");
      z90crypt.z90c_terminating = 1;
      rv = REC_FATAL_ERROR;
      break;
    }

    if (devPtr->z90c_disabled == TRUE) {
      rv = REC_NO_RESPONSE;
      break;
    }

    devType = devPtr->z90c_dev_type;

    if (!(devPtr->z90c_dev_resp_l) || !(devPtr->z90c_dev_resp_p)) {
      dv = DEV_REC_EXCEPTION;
      PRINTK("Response Length and Pointer at receive_from_AP: %d, %p\n",
             devPtr->z90c_dev_resp_l, devPtr->z90c_dev_resp_p);
    }
    else {
      dv = receive_from_AP(index,
                         z90crypt.z90c_cdx,
                         devPtr->z90c_dev_resp_l,
                         devPtr->z90c_dev_resp_p,
                         psmid);

    }
    if (dv == DEV_REC_EXCEPTION) {
      rv = REC_FATAL_ERROR;
      z90crypt.z90c_terminating = 1;
      PRINTKC("Exception during receive from device %i\n",index);
      break;
    }
    switch (dv) {
      case DEV_ONLINE:
        rv = OK;
        break;
      case DEV_EMPTY:
        rv = REC_EMPTY;
        break;
      case DEV_NO_WORK:
        rv = REC_NO_WORK;
        break;
      case DEV_BAD_MESSAGE:
      case DEV_GONE:
      case REC_HARDWARE_FAILED:
      default:
        rv = REC_NO_RESPONSE;
        break;
    }

    if (rv != OK)
      break;

    if (devPtr->z90c_dev_caller_count <= 0) {
      rv = REC_USER_GONE;
      break;
    }

    hptr = &(devPtr->z90c_dev_caller_list);
    list_for_each_safe(ptr,tptr,hptr) {
	  if (!(ptr)) {
		PRINTK("Nogood list in rcv\n");
		caller_p = NULL;
	 	break;
	  }
      caller_p = (Z90C_CALLER *)list_entry(ptr,
                                           struct z90c_caller,
                                           z90c_caller_liste);
	  if ((long)caller_p < 0) {
		caller_p = NULL;
	 	break;
	  }
      if (!(memcmp(caller_p->z90c_caller_id,
                   psmid,
                   sizeof(caller_p->z90c_caller_id)))) {
	if (!(list_empty(&(caller_p->z90c_caller_liste)))) {
          list_del(ptr);
          --(devPtr->z90c_dev_caller_count);
  	  INIT_LIST_HEAD(&(caller_p->z90c_caller_liste));
          break;
        }
      }
      else {
        caller_p = NULL;
      }
    }

    if (caller_p == NULL) {
      rv = REC_USER_GONE;
      break;
    }

    PDEBUG("caller_p after successful receive: %p\n",caller_p);
    rv = convert_response(devPtr->z90c_dev_resp_p,
                         devPtr->z90c_dev_type,
                         caller_p->z90c_caller_buf_l,
                         caller_p->z90c_caller_buf_p,
                         caller_p->z90c_caller_function,
                         caller_p->z90c_pad_rule,
                         buff_len_p,
                         buff);
    switch (rv) {
      case BAD_MESSAGE_USER:
        PDEBUG("convert_response: Device %d returned bad message user error %d\n", index, rv);
        rv = REC_USER_ERROR;
        break;
      case WRONG_DEVICE_TYPE:
      case REC_HARDWARE_FAILED:
      case BAD_MESSAGE_DRIVER:
        PRINTK("convert_response: Device %d returned hardware error %d\n", index, rv);
        rv = REC_NO_RESPONSE;
        break;
      case REC_RELEASED:
        PDEBUG("convert_response: Device %d returned REC_RELEASED = %d\n", index, rv);
        break;
      default:
        PDEBUG("convert_response: Device %d returned rv = %d\n", index, rv);
        break;
    }


  } while(0);

  switch(rv) {
    case OK:
      PDEBUG("Successful receive from device %i\n",index);
      icaMsg_p = (ica_rsa_modexpo_t *)(caller_p->z90c_caller_buf_p);
      PDEBUG("icaMsg_p as pointer: %p\n",icaMsg_p);
      PDEBUG("dest_p_p as pointer: %p\n",dest_p_p);
      *dest_p_p = icaMsg_p->outputdata;
      if (*buff_len_p == 0)
        PRINTK("Zero *buff_len_p in receive_from_crypto_device\n");
      break;
    case REC_NO_RESPONSE:
      remove_device(devPtr);
      break;
    default:
      break;
  }

  if (caller_p != NULL) {
    if ((temp_rv = unbuild_caller(devPtr,caller_p))!=OK){
      rv = REC_FATAL_ERROR;
      z90crypt.z90c_terminating = 1;
    }
  }

  return (rv);
} // end receive_from_crypto_device

int query_z90crypt(Z90C_STATUS_STR * z90cryptStatus)
/*********************************************************************/
/*                                                                   */
/*         inputs:  none                                             */
/*                                                                   */
/* implied inputs:  the z90crypt                                     */
/*                                                                   */
/*         output:  z90cryptStatus                                   */
/*                                                                   */
/* The output is the status array imbedded in the z90crypt.          */
/*                                                                   */
/* Not sure why anybody would call this function.  Why not look in   */
/* the z90crypt?                                                     */
/*                                                                   */
/*********************************************************************/
{
  memcpy (z90cryptStatus,
          &(z90crypt.z90c_mask),
          sizeof(struct z90c_status_str));

  return 0;
}

DEVSTAT query_crypto_device(int index)
/*********************************************************************/
/*                                                                   */
/*         inputs:  an index                                         */
/*                                                                   */
/* implied inputs:  the z90crypt                                     */
/*                                                                   */
/*         output:  DEV_GONE, DEV_ONLINE, aut ceterum.               */
/*                                                                   */
/*                                                                   */
/* Not sure why anybody would call this function.  Why not look in   */
/* the z90crypt?                                                     */
/*                                                                   */
/*********************************************************************/
{
  DEVSTAT dev_stat;

  dev_stat = z90crypt.z90c_mask.z90c_st_mask[index];

  return (dev_stat);
} // end query_crypto_device

int destroy_z90crypt(void)
/*********************************************************************/
/*                                                                   */
/*  implied input:  the z90crypt                                     */
/*                                                                   */
/*         output:  none                                             */
/*                                                                   */
/* All work will be drained.  All structures pointed to from the     */
/* z90crypt will be unallocated.                                     */
/*                                                                   */
/*********************************************************************/
{
  int i;
  Z90C_DEVICE * dev_ptr;

  for (i=0;i<z90crypt.z90c_max_count;i++) {
    if ((dev_ptr = z90crypt.z90c_device_p[i]) !=
                                                (Z90C_DEVICE *)NULL)
      destroy_crypto_device(i);
  }

  if (z90crypt.z90c_hdware_info != NULL)
    kfree((void *)z90crypt.z90c_hdware_info);

  memset((void *)&z90crypt,
         0,
         sizeof(struct z90crypt));

  return OK;
}

int select_device_type (CDEVICE_TYPE *devType_p)
/*********************************************************************/
/* select_device_type:                                               */
/*                                                                   */
/*   If a PCICA card is present, returns PCICA                       */
/*   If a PCIXCC card is present, returns PCIXCC                     */
/*   If a PCICC card is present, returns PCICC                       */
/*   Otherwise returns -1                                            */
/*                                                                   */
/*********************************************************************/
{
  if ((*devType_p == PCICA) &&
      (z90crypt.z90c_hdware_info->
                              z90c_type_mask[PCICA].z90c_st_count) <=
      (z90crypt.z90c_hdware_info->
                        z90c_type_mask[PCICA].z90c_disabled_count +
       z90crypt.z90c_hdware_info->
                z90c_type_mask[PCICA].z90c_explicitly_disabled_count))
    return (-1);

  if ((*devType_p == PCICC) &&
      (z90crypt.z90c_hdware_info->
                              z90c_type_mask[PCICC].z90c_st_count) <=
      (z90crypt.z90c_hdware_info->
                        z90c_type_mask[PCICC].z90c_disabled_count +
       z90crypt.z90c_hdware_info->
                z90c_type_mask[PCICC].z90c_explicitly_disabled_count))
    return (-1);

  if ((*devType_p == PCIXCC) &&
      (z90crypt.z90c_hdware_info->
                              z90c_type_mask[PCIXCC].z90c_st_count) <=
      (z90crypt.z90c_hdware_info->
                        z90c_type_mask[PCIXCC].z90c_disabled_count +
       z90crypt.z90c_hdware_info->
                z90c_type_mask[PCIXCC].z90c_explicitly_disabled_count))
    return (-1);

  if (z90crypt.z90c_hdware_info->
                              z90c_type_mask[PCICA].z90c_st_count >
      (z90crypt.z90c_hdware_info->
                        z90c_type_mask[PCICA].z90c_disabled_count +
       z90crypt.z90c_hdware_info->
                z90c_type_mask[PCICA].z90c_explicitly_disabled_count)) {
    *devType_p = PCICA;
    return 0;
  }

  if (z90crypt.z90c_hdware_info->
                              z90c_type_mask[PCIXCC].z90c_st_count >
      (z90crypt.z90c_hdware_info->
                        z90c_type_mask[PCIXCC].z90c_disabled_count +
       z90crypt.z90c_hdware_info->
                z90c_type_mask[PCIXCC].z90c_explicitly_disabled_count)) {
    *devType_p = PCIXCC;
    return 0;
  }

  if (z90crypt.z90c_hdware_info->
                              z90c_type_mask[PCICC].z90c_st_count >
      (z90crypt.z90c_hdware_info->
                        z90c_type_mask[PCICC].z90c_disabled_count +
       z90crypt.z90c_hdware_info->
                z90c_type_mask[PCICC].z90c_explicitly_disabled_count)) {
    *devType_p = PCICC;
    return 0;
  }

  return -1;
} // end select_device_type

int select_device (CDEVICE_TYPE *devType_p, int *device_nr_p)
/*********************************************************************/
/*                                                                   */
/*           inputs:  devType -- PCICA, PCICC, PCIXCC, or ANYDEV     */
/*                                                                   */
/*                    device_nr -- a number in [0,...,63] or -1      */
/*                                                                   */
/*    implied input:  z90crypt                                       */
/*                                                                   */
/*          outputs:  *device_nr_p   -- a device number from 0 to 63 */
/*                                      or -1 if no device is        */
/*                                      available.                   */
/*                                                                   */
/*     return value:  same as *device_nr_p.  If no device is         */
/*                    available, returns -1.                         */
/*                                                                   */
/*   implied output:  none                                           */
/*                                                                   */
/*          process:  1.  if device_nr is specified, returns it.     */
/*                                                                   */
/*                    2.  if devType is specified, selects a device  */
/*                        at random from the list associated with    */
/*                        that type.                                 */
/*                                                                   */
/*                    3.  if devType is not specified, selects a     */
/*                        device at random from the master list.     */
/*                                                                   */
/*********************************************************************/
{
  int rv = 0;
  int nr = 0;
  unsigned int ix = 0xff000000;
  int i;
  int indx = -1;
  int devTp;
  struct z90c_device_x *index_p;
  Z90C_DEVICE * dev_ptr = NULL;
  int max;
  int low_count = 0x0000ffff;
  int low_indx = -1;

  PDEBUG("top of select_device:  device type = %d\n",*devType_p);
  PDEBUG("top of select_device:  device index = %d\n",*device_nr_p);

  max = 2 * z90crypt.z90c_mask.z90c_st_count;

  devTp = *devType_p;
  nr = *device_nr_p;

  if (nr >= 0 && nr < Z90CRYPT_NUM_DEVS) {
    dev_ptr = z90crypt.z90c_device_p[nr];
  }

  if (dev_ptr != NULL &&
      dev_ptr->z90c_dev_stat != DEV_GONE &&
      dev_ptr->z90c_disabled == FALSE &&
      dev_ptr->z90c_explicitly_disabled == FALSE) {
    *devType_p = dev_ptr->z90c_dev_type;
    return (nr);
  }
  else
    nr = -1;

  if (devTp == -1)
	  rv = select_device_type(&devTp);

  PDEBUG("select Device:  number to choose from: %d\n",nr);

  if (devTp == -1) {
    *device_nr_p = -1;
    return (-1);
  }

  nr = z90crypt.z90c_hdware_info->
                             z90c_type_mask[devTp].z90c_st_count;
  index_p = &(z90crypt.z90c_hdware_info->z90c_type_x_addr[devTp]);
  for (i=0;i<nr;i++){
    indx = index_p->z90c_device_index[i];
    dev_ptr = z90crypt.z90c_device_p[indx];

    if ((dev_ptr != NULL) &&
        (dev_ptr->z90c_dev_stat != DEV_GONE) &&
        (dev_ptr->z90c_disabled == FALSE) &&
        (dev_ptr->z90c_explicitly_disabled == FALSE) &&
        (devTp == dev_ptr->z90c_dev_type)) {
       low_count = dev_ptr->z90c_dev_caller_count;
       low_indx = indx;
       break;
    }
  }

  if (i == (nr - 1)) {
    *device_nr_p = low_indx;
    return low_indx;
  }

  if (i >= nr){
    *device_nr_p = -1;
    return -1;
  }

  for (ix = i+1;ix<nr;ix++){
    indx = index_p->z90c_device_index[ix];
    dev_ptr = z90crypt.z90c_device_p[indx];

    if ((dev_ptr != NULL) &&
        (dev_ptr->z90c_dev_stat != DEV_GONE) &&
        (dev_ptr->z90c_disabled == FALSE) &&
        (dev_ptr->z90c_explicitly_disabled == FALSE) &&
        (devTp == dev_ptr->z90c_dev_type))
       if (low_count>dev_ptr->z90c_dev_caller_count) {
         low_count = dev_ptr->z90c_dev_caller_count;
         low_indx = indx;
       }
  }

  *device_nr_p = low_indx;
  return (low_indx);

}; // end select_device

int probe_device_type(Z90C_DEVICE * devPtr)
/*********************************************************************/
/*                                                                   */
/*           inputs:  devPtr  -- address of a Z90C_DEVICE            */
/*                                                                   */
/*    implied input:  z90crypt                                       */
/*                                                                   */
/*          outputs:  devPtr-> z90c_dev_type                         */
/*                                                                   */
/*     return value:  propagated from low level send/receive         */
/*                    routines.  Otherwise, returns OK               */
/*                                                                   */
/*   implied output:  none                                           */
/*                                                                   */
/*          process:  1.  initialize device type to indeterminate    */
/*                                                                   */
/*                    2.  send_to_AP using a canned PCICC request    */
/*                                                                   */
/*                    3.  Do 6 times:                                */
/*                                                                   */
/*                        a.  sleep for a little while               */
/*                                                                   */
/*                        b.  receive_from_AP.                       */
/*                                                                   */
/*                        c.  consider the result:                   */
/*                            1)  No response:                       */
/*                                   a) device busy                  */
/*                                         continue                  */
/*                                   b) no work ready                */
/*                                         continue                  */
/*                                   c) anything else                */
/*                                         break                     */
/*                            2)  Type 86 response:                  */
/*                                   deviceType is PCICC             */
/*                                   break                           */
/*                            3)  Type 82 response                   */
/*                                   deviceType is PCICA             */
/*                                   break                           */
/*                                                                   */
/*                    4.  Return                                     */
/*                                                                   */
/*                                                                   */
/*********************************************************************/
{
  int rv = OK;
  int dv = DEV_ONLINE;
  int i;                           // loop counter
  int index;                       // device number
  unsigned char psmid[8];          // returned by receive_from_AP
  int testmsg_len = 384;           // sorry about that; you need that
                                   // much space
  unsigned char *dyn_testmsg;

  dyn_testmsg = kmalloc(testmsg_len, GFP_KERNEL);
  if (!dyn_testmsg) {
    PRINTK("kmalloc for dyn_testmsg failed in probe_device_type\n");
    return OK;  // Strange, but it will work. Since we didn't update the device
                // type, the next time around, we will reprobe and hopefully
                // have enough memory.
  }

  index = devPtr->z90c_dev_self_x;

  do {
    get_test_msg(dyn_testmsg, &testmsg_len);
    if ((dv = send_to_AP(index,
                         z90crypt.z90c_cdx,
                         testmsg_len-24,    // allow for 'header'
                         dyn_testmsg)) != OK){
      PDEBUG("dv returned by send during probe: %d\n",dv);
      if (dv==DEV_SEN_EXCEPTION) {
        rv = SEN_FATAL_ERROR;
        PRINTKC("Exception during send to device %i\n",index);
        break;
      }
      PDEBUG("return value from send_to_AP:  %i\n",rv);
      switch(dv) {
        case DEV_GONE:
          PDEBUG("Device %d not available\n", index);
          rv = SEN_NOT_AVAIL;
          break;
        case DEV_ONLINE:
          rv = OK;
          break;
        case DEV_EMPTY:
          rv = SEN_NOT_AVAIL;
          break;
        case DEV_NO_WORK:
          rv = SEN_FATAL_ERROR;
          break;
        case DEV_BAD_MESSAGE:
          rv = SEN_USER_ERROR;
          break;
        case DEV_QUEUE_FULL:
          rv = SEN_QUEUE_FULL;
          break;
        default:
          break;
      } // end switch (dv from send_to_AP)
    } // end if dv != OK

    if (rv != OK)
      break;

    for (i=0;i<6;i++) {
      mdelay(300);

      dv = receive_from_AP(index,
                           z90crypt.z90c_cdx,
                           devPtr->z90c_dev_resp_l,
                           devPtr->z90c_dev_resp_p,
                           psmid);

      if (dv == DEV_REC_EXCEPTION) {
        rv = REC_FATAL_ERROR;
        PRINTKC("Exception during receive from device %i\n",index);
        break;
      }
      PDEBUG("probeD:  dv returned by DQ = %d\n",dv);
      switch (dv) {
        case DEV_ONLINE:
          rv = OK;
          break;
        case DEV_EMPTY:
          rv = REC_EMPTY;
          break;
        case DEV_NO_WORK:
          rv = REC_NO_WORK;
          break;
        case DEV_BAD_MESSAGE:
        case DEV_GONE:
        default:
          rv = REC_NO_RESPONSE;
          break;
      } // end switch (dv from receive_from_AP)

      if ((rv != OK) && (rv != REC_NO_WORK))
        break;

      if (rv == OK)
         break;

    } // end for (i=0;....

    if (rv != OK)
      break;

    rv = test_reply(devPtr->z90c_dev_resp_p);

    if (rv == 0) {
      devPtr->z90c_dev_type = PCICC;
      break;
    }

    devPtr->z90c_dev_type = PCICA;
    rv = 0;

  } while(0);

  // In a general error case, the card is not marked online

  kfree(dyn_testmsg);
  return (rv);

} // end probe_device_type

// following checks where a string is PKCS#1.1 padded
int isPKCS1_1Padded (unsigned char * argP, int argL)
/********************************************************************/
/*                                                                  */
/* isPKCS1_1Padded:  Tests whether a string of bytes is (possibly)  */
/*                   padded in accordance with the PKCS#1.1         */
/*                   standard                                       */
/*                                                                  */
/*           input:  buff -- a string of bytes                      */
/*                                                                  */
/*                   bufflen -- length of the string                */
/*                                                                  */
/*         process:  1.  First byte must be 0                       */
/*                   2.  Second byte must be 0x01                   */
/*                   3.  There must be a string of 8 or more        */
/*                       0xff bytes                                 */
/*                   4.  There must be a zero byte followed by at   */
/*                       one byte.                                  */
/*                                                                  */
/*          output:  returns 1 if string could be PKCS1.1 padded    */
/*                   returns 0 if any of the above conditions is    */
/*                             violated.                            */
/*                                                                  */
/********************************************************************/
{
  int rv = 0;

  int i,j;
  unsigned char * p;
  int l;

  for (i=0;i<argL;i++) {
    if (argP[i])
      break;
  }

  p = argP + i;
  l = argL - i;

  do {

    if (i != 1)
      break;

    if (p[0] != 1)
      break;

    for (j=1;j<l;j++) {
      if (p[j] != 0xff)
        break;
    }

    if (j < 9)
      break;

    if (p[j] != 0)
      break;

    rv = 1;
  } while(0);

  return (rv);
}

int isPKCS1_2padded (unsigned char * buff, int bufflen)
/********************************************************************/
/*                                                                  */
/* isPKCS1_2padded:  Tests whether a string of bytes is (possibly)  */
/*                   padded in accordance with the PKCS#1.2         */
/*                   standard                                       */
/*                                                                  */
/*           input:  buff -- a string of bytes                      */
/*                                                                  */
/*                   bufflen -- length of the string                */
/*                                                                  */
/*         process:  1.  First byte must be 0                       */
/*                   2.  Second byte must be 0x02                   */
/*                   3.  There must be a string of 8 or more        */
/*                       non-zero bytes                             */
/*                   4.  There must be at least one zero byte       */
/*                                                                  */
/*          output:  returns 1 if string could be PKCS1.2 padded    */
/*                   returns 0 if any of the above conditions is    */
/*                             violated.                            */
/*                                                                  */
/********************************************************************/
{
  int rv = 0;
  int i;                              // loop counter

  do {
    if (bufflen < 12)
      break;

    if (buff[0] != 0)
      break;

    if (buff[1] != 0x02)
      break;

    for (i=2;((i<bufflen) && (buff[i]));i++);

    if (i > bufflen - 2)
      break;

    if (i < 9)
      break;

    rv = 1;

  } while(0);

  return (rv);

} // end isPKCS1_2padded



/*--------------------------------------------------------------------------*/
/* Issue a hotplug event                                                    */
/*--------------------------------------------------------------------------*/

void z90crypt_hotplug_event(int devmaj, int devmin, int action)
{
#ifdef CONFIG_HOTPLUG
	char *argv[3];
	char *envp[6];
	char  major[20];
	char  minor[20];

	sprintf(major, "MAJOR=%d", devmaj);
	sprintf(minor, "MINOR=%d", devmin);

	argv[0] = hotplug_path;
	argv[1] = "z90crypt";
	argv[2] = NULL;

	envp[0] = "HOME=/";
	envp[1] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";

	switch (action) {
		case Z90CRYPT_HOTPLUG_ADD:
			envp[2] = "ACTION=add";
			break;
		case Z90CRYPT_HOTPLUG_REMOVE:
			envp[2] = "ACTION=remove";
			break;
		default:
			BUG();
	}
	envp[3] = major;
	envp[4] = minor;
	envp[5] = NULL;

	call_usermodehelper(argv[0], argv, envp);
#endif
}

module_init(z90crypt_init_module);
module_exit(z90crypt_cleanup_module);
