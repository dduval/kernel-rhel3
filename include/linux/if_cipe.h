/*
   CIPE - encrypted IP over UDP tunneling

   cipe.h - contains definitions, includes etc. common to all modules

   Copyright 1996-2000 Olaf Titz <olaf@bigred.inka.de>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version
   2 of the License, or (at your option) any later version.
*/
/* $Id: linux-2.4.0-cipe-1.4.5.patch,v 1.6 2001/04/17 18:50:11 arjanv Exp $ */

#ifndef _IF_CIPE_H_
#define _IF_CIPE_H_

/*** The kernel/user IOCTL interface ***/

/* ioctls for setup and key exchange */
/* #define SIOCxIFCIPxxx   (SIOCDEVPRIVATE+x) */
/* All ioctls are passed a struct ifreq <net/if.h> which contains the
   device name in ifr_name and a pointer to the actual control struct
   in ifr_data. */

#if 0
/* Get interface parameters. Currently unused */
#define SIOCGIFCIPPAR   (SIOCDEVPRIVATE+0)
struct  siocgifcippar {
    unsigned long       magic;
    /* SOCKS5 relayer */
    unsigned long       sockshost;
    unsigned short      socksport;
    /* Timeouts (in seconds) */
    int                 tmo_keyxchg;
    int                 tmo_keylife;
    /* Flags */
    int                 flags;
    int		        cttl;
};
#endif

/* Set interface parameters. */
#define SIOCSIFCIPPAR   (SIOCDEVPRIVATE+1)
struct  siocsifcippar {
    unsigned long       magic;
    /* SOCKS5 relayer */
    unsigned long       sockshost;
    unsigned short      socksport;
    /* Timeouts (in seconds) */
    int                 tmo_keyxchg;
    int                 tmo_keylife;
    /* Flags */
    int                 flags;
    int		        cttl;
};

/* Set a key. */
#define SIOCSIFCIPKEY   (SIOCDEVPRIVATE+2)
#define KEY_STATIC      1
#define KEY_SEND        2
#define KEY_RECV        3
#define KEY_INVAL       8
struct  siocsifcipkey {
    unsigned long       magic;
    int                 which;
    UserKey             thekey;
};

/* Attach a socket. */
#define SIOCSIFCIPATT   (SIOCDEVPRIVATE+3)
struct  siocsifcipatt {
    unsigned long       magic;
    int                 fd;
};

/* Allocate/deallocate a device. */
#define SIOCSIFCIPALL   (SIOCDEVPRIVATE+4)
#define SIOCSIFCIPUNA   (SIOCDEVPRIVATE+5)
struct  siocsifcipall {
    unsigned long       magic;
    int                 num;
    char                name[IFNAMSIZ];
};

/* Flag values. */
#define CIPF_MAY_CLEAR          0x0100
#define CIPF_MAY_STKEY          0x0200
#define CIPF_MAY_DYNIP          0x0400

#endif /* _IF_CIPE_H_ */
