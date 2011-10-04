/*
 *	Adaptec AAC series RAID controller driver
 *	(c) Copyright 2001 Red Hat Inc.	<alan@redhat.com>
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
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
/*
 * This file is for backwards compatibility with older kernel versions
 */


#define scsi_host_template SHT
#define DMA_BIDIRECTIONAL	SCSI_DATA_UNKNOWN
#define DMA_TO_DEVICE		SCSI_DATA_WRITE
#define DMA_FROM_DEVICE		SCSI_DATA_READ
#define DMA_NONE		SCSI_DATA_NONE
#define iminor(x) MINOR(x->i_rdev)
#define scsi_host_alloc(t,s) scsi_register(t,s)
#define scsi_host_put(s) scsi_unregister(s)
#ifndef pci_set_consistent_dma_mask
#define pci_set_consistent_dma_mask(d,m) 0
#endif
#define scsi_scan_host(s)
#define scsi_add_host(s,d) 0
#if (defined(MODULE))
# define scsi_remove_host(s) \
	for (index = 0; (index < aac_count) \
	 || !aac_devices[index] \
	 || (aac_devices[index] == (struct aac_dev *)s->hostdata); ++index); \
	if (index < aac_count) scsi_unregister_module(MODULE_SCSI_HA,s->hostt)
#else
# define scsi_remove_host(s)
#endif
#if (!defined(__devexit_p))
# if (defined(MODULE))
#  define __devexit_p(x) x
# else
#  define __devexit_p(x) NULL
# endif
#endif
#define scsi_device_online(d) ((d)->online)




    
