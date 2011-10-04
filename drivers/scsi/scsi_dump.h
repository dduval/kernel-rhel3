#ifndef _SCSI_DUMP_H
#define _SCSI_DUMP_H

/*
 *  linux/drivers/scsi/scsi_dump.h
 *
 *  Copyright (C) 2004  FUJITSU LIMITED
 *  Written by Nobuhiro Tachino (ntachino@jp.fujitsu.com)
 *
 */
/*
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
 *
 */

struct scsi_dump_ops {
	int (*sanity_check)(Scsi_Device *);
	int (*quiesce)(Scsi_Device *);
	int (*shutdown)(Scsi_Device *);
	void (*poll)(Scsi_Device *);
	unsigned int no_write_cache_enable	: 1;
};

/*
 * Extended host template for dump for preserving binary compatibility.
 * Extra fields are referenced only when diskdump field of 
 * original template is set.
 */
typedef struct SHT_dump {
	Scsi_Host_Template hostt;
	struct scsi_dump_ops *dump_ops;
} Scsi_Host_Template_dump;


extern struct scsi_dump_ops *scsi_dump_find_template(Scsi_Host_Template *);
#if defined(CONFIG_SCSI_DUMP) || defined(CONFIG_SCSI_DUMP_MODULE)
extern int scsi_dump_register(Scsi_Host_Template *, struct scsi_dump_ops *);
extern int scsi_dump_unregister(Scsi_Host_Template *);
#else
static inline int scsi_dump_register(Scsi_Host_Template *hostt,
				     struct scsi_dump_ops *dump_ops)
{
	return 0;
}
static inline int scsi_dump_unregister(Scsi_Host_Template *hostt)
{
	return 0;
}
#endif

#endif /* _SCSI_DUMP_H */
