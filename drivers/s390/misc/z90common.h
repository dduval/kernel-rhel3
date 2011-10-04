/*
 *  linux/drivers/s390/misc/z90common.h
 *
 *  z90crypt 1.1.4
 *
 *  Copyright (C)  2001, 2003 IBM Corporation
 *  Author(s): Robert Burroughs (burrough@us.ibm.com)
 *             Eric Rossman (edrossma@us.ibm.com)
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
#ifndef _Z90COMMON_
#define _Z90COMMON_
#define VERSION_Z90COMMON_H "$Revision: 1.4.6.7 $"
#ifndef _TYPES_
#define _TYPES_
typedef unsigned char UCHAR;
typedef unsigned char BYTE; 
typedef unsigned short HWRD;
typedef unsigned int UINT;
typedef unsigned long ULONG;
typedef UCHAR BOOL;         
#define FALSE 0x00
#define TRUE  0x01
#endif                      
#define RESPBUFFSIZE 256
#define PCI_FUNC_KEY_DECRYPT 0x5044
#define PCI_FUNC_KEY_ENCRYPT 0x504B
#ifndef _DEVSTAT_
#define _DEVSTAT_
typedef enum {
	DEV_GONE,                       
	DEV_ONLINE,                     
	DEV_QUEUE_FULL,                 
	DEV_EMPTY,                      
	DEV_NO_WORK,                    
	DEV_BAD_MESSAGE,                
	DEV_TSQ_EXCEPTION,              
	DEV_RSQ_EXCEPTION,              
	DEV_SEN_EXCEPTION,              
	DEV_REC_EXCEPTION               
} DEVSTAT;
#endif
#ifndef _HDSTAT_
#define _HDSTAT_
typedef enum
{HD_NOT_THERE,                   
 HD_BUSY,                        
 HD_DECONFIGURED,                
 HD_CHECKSTOPPED,                
 HD_ONLINE,                      
 HD_TSQ_EXCEPTION                
} HDSTAT;
#endif
#ifndef OK
#define OK 0
#endif
#define ENOTINIT 4
#define EBADPARM 5
#define Z90C_NO_DEVICES 1
#define Z90C_AMBIGUOUS_DOMAIN 2
#define Z90C_INCORRECT_DOMAIN 3
#define SEN_RETRY  18     
#define SEN_NOT_AVAIL 16  
#define SEN_QUEUE_FULL 11
#define SEN_NO_DEVICES 20 
#define SEN_RELEASED  24  
#define REC_OPERAND_INVALID 8 
#define SEN_OPERAND_INVALID 8 
#define REC_OPERAND_SIZE 9    
#define REC_EVEN_MODULUS 10   
#define REC_HARDWARE_FAILED 12 
#define REC_INVALID_PADDING 17 
#define SEN_PADDING_ERROR   17 
#define WRONG_DEVICE_TYPE 20  
#define BAD_MESSAGE_USER 8
#define BAD_MESSAGE_DRIVER 16
#define REC_EMPTY 4       
#define REC_BUSY 6        
#define SEN_BUSY 7        
#define SEN_USER_ERROR 8  
#define REC_USER_ERROR 8  
#define REC_NO_WORK  11   
#define REC_NO_RESPONSE 13 
#define REC_RETRY_THIS_DEVICE 14 
#define REC_USER_GONE     15 
#define REC_RELEASED   28    
#define REC_FATAL_ERROR 32
#define SEN_FATAL_ERROR 33
#define TSQ_FATAL_ERROR 34
#define RSQ_FATAL_ERROR 35
#define PCICA	0
#define PCICC	1
#define PCIXCC	2
#define NILDEV	-1
#define ANYDEV	-1
typedef int CDEVICE_TYPE;
typedef enum {NIL0DEV, NIL1DEV, NIL2DEV,
		PCICC_HW, PCICA_HW,
		PCIXCC_HW, OTHER_HW,
		OTHER2_HW} HDEVICE_TYPE; 
#ifndef DEV_NAME
#define DEV_NAME        "z90crypt"
#endif
#define PRINTK(fmt, args...) printk(KERN_DEBUG DEV_NAME ": " fmt, ## args)
#define PRINTKW(fmt, args...) printk(KERN_WARNING DEV_NAME ": " fmt, ## args)
#define PRINTKC(fmt, args...) printk(KERN_CRIT DEV_NAME ": " fmt, ## args)
#undef PDEBUG             
#ifdef Z90CRYPT_DEBUG
#  define PDEBUG(fmt, args...) printk(KERN_DEBUG DEV_NAME ": " fmt, ## args)
#else
#  define PDEBUG(fmt, args...) 
#endif
#endif
