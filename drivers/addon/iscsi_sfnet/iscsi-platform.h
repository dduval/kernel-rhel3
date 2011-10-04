#ifndef ISCSI_PLATFORM_H_
#define ISCSI_PLATFORM_H_

/*
 * iSCSI driver for Linux
 * Copyright (C) 2001 Cisco Systems, Inc.
 * maintained by linux-iscsi-devel@lists.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * See the file COPYING included with this distribution for more details.
 *
 * $Id: iscsi-platform.h,v 1.1.2.1 2004/08/10 23:04:49 coughlan Exp $ 
 *
 * iscsi-platform.h
 *
 *    abstract platform dependencies
 * 
 */

#ifdef __KERNEL__
#  include <linux/config.h>
#  include <linux/version.h>
#  include <linux/stddef.h>
#  include <asm/byteorder.h>
#  include <linux/types.h>
#  include <linux/stddef.h>
#  include <linux/blk.h>
#  include <linux/string.h>
#  include <linux/random.h>
#  define AS_ERROR  KERN_ERR
#  define AS_NOTICE KERN_NOTICE
#  define AS_INFO   KERN_INFO
#  define AS_DEBUG  KERN_DEBUG
#  define logmsg(level, fmt, args...) \
      printk(level "iSCSI: session %p " fmt, session , ##args)
#  define debugmsg(level, fmt, arg...)  do { } while (0)
#  define iscsi_strtoul simple_strtoul
#  ifdef __BIG_ENDIAN
#    define WORDS_BIGENDIAN 1
#  endif
#else
#  include <stdlib.h>
#  include <stdio.h>
#  include <stddef.h>
#  include <stdint.h>
#  include <syslog.h>
#  include <ctype.h>
#  include <sys/ioctl.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <net/if.h>
#  include <string.h>
#  include <endian.h>
#  ifdef __BIG_ENDIAN
#    define WORDS_BIGENDIAN 1
#  endif
#  define AS_ERROR  LOG_ERR
#  define AS_NOTICE LOG_NOTICE
#  define AS_INFO   LOG_INFO
#  define AS_DEBUG  LOG_DEBUG
#  define iscsi_atoi      atoi
#  define iscsi_inet_aton inet_aton
#  define iscsi_strtoul   strtoul
extern void debugmsg(int level, const char *fmt, ...);
extern void errormsg(const char *fmt, ...);
extern void logmsg(int priority, const char *fmt, ...);
#endif
/* both the kernel and userland have the normal names available */
#define iscsi_memcmp    memcmp
#define iscsi_strcmp    strcmp
#define iscsi_strrchr   strrchr
#define iscsi_strncmp   strncmp
#define iscsi_strlen    strlen
#define iscsi_strncpy   strncpy
#define iscsi_sprintf   sprintf
#define iscsi_isdigit   isdigit
#define iscsi_isspace   isspace
#define iscsi_ntohl     ntohl
#define iscsi_ntohs     ntohs
#define iscsi_htonl     htonl
#define iscsi_htons     htons

#ifndef MIN
# define MIN(x,y) ((x) < (y) ? (x) : (y))
#endif

#ifndef MAX
# define MAX(x,y) ((x) >= (y) ? (x) : (y))
#endif

#endif
