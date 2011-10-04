/*
 * The Coraid EDPv2 protocol definitions
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

struct edp2
{
	/* This structure follows the DIX ethernet header */
	u8 flag_err;
#define EDP_F_RESPONSE	0x80
#define EDP_F_ERROR	0x40
#define EDP_F_ERRMASK	0x0F
	u8 function;
	u32 tag __attribute((packed));
};

struct edp2_ata_fid0
{
	u8 flag_ver;
#define EDP_ATA_WRITE	0x80		/* I/O from host */
#define EDP_ATA_LBA48	0x40		/* Command is LBA48 */
#define EDP_ATA_DBIT	0x10		/* Dbit state in LBA48 */
	u8 err_feature;			
	u8 sector;
	u8 cmd_status;
	u8 lba0;
	u8 lba1;
	u8 lba2;
	u8 lba3;
	u8 lba4;
	u8 lba5;
	u8 res0;
	u8 res1;
	u32 data[0];
};

#define EDPT2_ATA_BADPARAM	0
#define EDPT2_ATA_DISKFAIL	1

struct edp2_ata_fid1
{
	u32	count;		/* Outstanding buffer limit */
	u32	firmware;	/* Firmware revision */
	u16	aoe;		/* Protocol supported */
	u8	shad;		/* Shelf ident */
	u8	slad;		/* Slot ident */
	u32	data[0];	/* Config string */
};

struct edp2_ata_fid2
{
	u32	tx;
	u32	rx;
	u32	tx_error;
	u32	rx_error;
	u32	rx_trunc;
	u32	rx_over;
	u32	rx_crc;
	u32	rx_short;
	u32	rx_align;
	u32	rx_violation;
	u32	tx_carrier;
	u32	tx_under;
	u32	tx_retrans;
	u32	tx_late;
	u32	tx_heartbeat;
	u32	tx_defer;
	u32	tx_retry;
};

struct edp2_ata_fid3
{
	u8	cmd;
#define EDP_ATA_FID3_CMD_CMP	0
#define EDP_ATA_FID3_CMD_NCMP	1
#define EDP_ATA_FID3_CLAIM	2
#define EDP_ATA_FID3_FORCE	3

	u8	res1, res2, res3;
	u32	data[0];
};


#define MAX_QUEUED	128

/*
 *	Used to hold together the edp2 device database
 */

struct edp2_device
{
	struct edp2_device *next;
	
	/* For upper layer */
	void *edp2_upper;
	int users;
	
	/* Location */
	struct net_device *dev;
	u8 shelf;
	u8 slot;
	u8 mac[6];
	
	/* Properties */
	u32 queue;
	u32 revision;
	u8  protocol;
	int dead:1;
	
	/* Time info */
	unsigned long last_ident;
	
	/* Protocol queue */
	int count;
	u16 tag;		/* bits 23-8 of the tag */
	struct sk_buff *skb[MAX_QUEUED];
	struct timer_list timer;
};

struct edp2_cb		/* Holds our data on the queue copy of the skb */
{
	int (*completion)(struct edp2_device *, struct edp2 *edp, unsigned long data, struct sk_buff *skb);
	unsigned long data;
	unsigned long timeout;
	int count;
};

