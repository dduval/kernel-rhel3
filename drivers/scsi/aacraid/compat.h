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

#undef ssleep
#define ssleep(x) scsi_sleep((x)*HZ)
#define msleep(x) set_current_state(TASK_UNINTERRUPTIBLE); schedule_timeout(x)
#ifndef BUG_ON
#ifndef unlikely
#ifndef __builtin_expect
#define __builtin_expect(x, expected_value) (x)
#endif
#define unlikely(x) __builtin_expect((x),0)
#endif
#define BUG_ON(condition) do { if (unlikely((condition)!=0)) BUG(); } while (0)
#endif
#ifndef min
#define min(a,b) (((a)<(b))?(a):(b))
#endif

#include <linux/time.h>
static inline unsigned long get_seconds(void)
{
	struct timeval now;
	do_gettimeofday(&now);
	return now.tv_sec;
}
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
#ifndef scsi_scan_host
#define scsi_scan_host(s)
#endif
#define scsi_add_host(s,d) 0
#define scsi_remove_host(s)					\
	struct proc_dir_entry * entry = NULL;			\
	struct scsi_device *device;				\
	extern struct proc_dir_entry * proc_scsi;		\
	if (proc_scsi != (struct proc_dir_entry *)NULL)		\
	for (entry = proc_scsi->subdir;				\
	  entry != (struct proc_dir_entry *)NULL &&		\
	  (!entry->low_ino ||					\
	    (entry->namelen != 4) ||				\
	    memcmp ("scsi", entry->name, 4));			\
	  entry = entry->next);					\
	if (entry && entry->write_proc)				\
	for (device = s->host_queue;				\
	  device != (struct scsi_device *)NULL;			\
	  device = device->next)				\
		if (!device->access_count && !s->in_recovery) {	\
			char buffer[80];			\
			int length;				\
			mm_segment_t fs;			\
			device->removable = 1;			\
			sprintf (buffer, "scsi "		\
			  "remove-single-device %d %d %d %d\n", \
			  s->host_no, device->channel,		\
			  device->id, device->lun);		\
			length = strlen (buffer);		\
			fs = get_fs();				\
			set_fs(get_ds());			\
			length = entry->write_proc(		\
			  NULL, buffer, length, NULL);		\
			set_fs(fs);				\
		}
#if (!defined(__devexit_p))
# if (defined(MODULE))
#  define __devexit_p(x) x
# else
#  define __devexit_p(x) NULL
# endif
#endif
#define __user
#define scsi_device_online(d) ((d)->online)
#define __iomem
typedef u64 __le64;
typedef u32 __le32;
typedef u16 __le16;

#ifndef DMA_64BIT_MASK
#define DMA_64BIT_MASK ((dma_addr_t)0xffffffffffffffffULL)
#endif
#ifndef DMA_32BIT_MASK
#define DMA_32BIT_MASK ((dma_addr_t)0xffffffffULL)
#endif
#ifndef spin_trylock_irqsave
#define spin_trylock_irqsave(lock, flags) \
({ \
	local_irq_save(flags); \
	spin_trylock(lock) ? \
	1 : ({local_irq_restore(flags); 0 ;}); \
})
#endif






    
