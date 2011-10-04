/*
 * dell_rbu.c
 * Bios Update driver for Dell systems
 * Author:	Dell Inc
 *			Abhay Salunke <abhay_salunke@dell.com>
 *
 * Copyright (c) 2004 Dell Inc.
 *
 * Remote BIOS Update (rbu) driver is used for updating DELL BIOS by creating 
 * entries in the /proc file systems on Linux 2.4 and lower kernels
 * This driver tries to allocates contiguos physical pages large enough 
 * to accomodate the BIOS image size specified by the user. The user 
 * supplied BIOS image is then copied in to the allocated contiguous pages.
 * You would still require to have some application to set the 
 * CMOS bit indicating the BIOS to update itself after a reboot.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/version.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <asm/io.h>
#include <asm/uaccess.h>

EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("Abhay Salunke <abhay_salunke@dell.com>");
MODULE_DESCRIPTION("Driver for updating BIOS image on DELL systems: \
		version 2.1");
MODULE_LICENSE("GPL");

#define BIOS_SCAN_LIMIT 0xffffffff
#define MAX_IMAGE_LENGTH 16

static struct _rbu_data {
	void *image_update_buffer;
	unsigned long image_update_buffer_size;
	unsigned long bios_image_size;
	unsigned long image_update_order_number;
	spinlock_t lock;
	unsigned long packet_read_count;
	unsigned long packet_write_count;
	unsigned long num_packets;
	unsigned long packetsize;
	/* Proc FS stuff. */
	struct proc_dir_entry *proc_root_dir;
	struct proc_dir_entry *proc_rbu_dir;
} rbu_data;

struct packet_data {
	struct list_head list;
	size_t length;
	void *data;
	int ordernum;
};

static unsigned long allocation_floor = 0x100000;

static struct packet_data packet_data_head;

static char image_type[MAX_IMAGE_LENGTH] = "mono";
static void
init_packet_head(void)
{
	INIT_LIST_HEAD(&packet_data_head.list);
	rbu_data.packet_write_count = 0;
	rbu_data.packet_read_count = 0;
	rbu_data.num_packets = 0;
	rbu_data.packetsize = 0;
}

static int
fill_last_packet(void *data, size_t length)
{
	struct list_head *ptemp_list;
	struct packet_data *ppacket = NULL;
	int packet_count = 0;

	pr_debug("fill_last_packet: entry \n");

	/*
	 * check if we have any packets 
	 */
	if (0 == rbu_data.num_packets) {
		pr_debug("fill_last_packet: num_packets=0\n");
		return -ENOMEM;
	}

	packet_count = rbu_data.num_packets;

	ptemp_list = (&packet_data_head.list)->next;

	while (--packet_count)
		ptemp_list = ptemp_list->next;

	ppacket = list_entry(ptemp_list, struct packet_data, list);

	if ((rbu_data.packet_write_count + length) > rbu_data.packetsize) {
		printk(KERN_WARNING "fill_last_packet: packet size data "
			"overrun\n");
		return -ENOMEM;
	}

	pr_debug("fill_last_packet : buffer = %p\n", ppacket->data);

	/*
	 * copy the incoming data in to the new buffer 
	 */
	memcpy((ppacket->data + rbu_data.packet_write_count), data,
		length);

	if ((rbu_data.packet_write_count + length) == rbu_data.packetsize) {
		/*
		 * this was the last data chunk in the packet
		 * so reinitialize the packet data counter to zero
		 */
		rbu_data.packet_write_count = 0;
	} else
		rbu_data.packet_write_count += length;

	pr_debug("fill_last_packet: exit \n");
	return 0;
}

/*
 * get_free_pages_limited: This is a helper function which allocates free
 * pages based on an upper limit. On x86_64 or 64 bit arch the memory
 * allocation goes above 4GB space which is not addressable by the BIOS.
 * This function tries to get allocation below the limit (4GB) address. It 
 * first tries to allocate memory normally using the GFP_KERNEL argument
 * if the incoming limit is non-zero and if the returned physical memory
 * address exceeds the upper limit, the allocated pages are freed and the 
 * memory is reallocated using the GFP_DMA argument. 
 */
static void *
get_free_pages_limited(unsigned long size, int *ordernum,
	unsigned long limit)
{
	unsigned long img_buf_phys_addr;
	void *pbuf = NULL;

	*ordernum = get_order(size);
	/*
	 * Check if we are not getting a very large file.
	 * This can happen as a user error in entering the file size
	 */
	if (*ordernum == BITS_PER_LONG) {
		pr_debug("get_free_pages_limited: Incoming size is"
			" very large\n");
		return NULL;
	}

	/*
	 * try allocating a new buffer to fit the request 
	 */
	pbuf = (unsigned char *) __get_free_pages(GFP_KERNEL, *ordernum);

	if (pbuf) {
		/*
		 * check if the image is with in limits 
		 */
		img_buf_phys_addr = (unsigned long) virt_to_phys(pbuf);

		if (limit && ((img_buf_phys_addr + size) > limit)) {
			pr_debug("Got memory above 4GB range, free this "
				"and try with DMA memory\n");
			/*
			 * free this memory as we need it with in 
			 * 4GB range 
			 */
			free_pages((unsigned long) pbuf, *ordernum);
			/*
			 * Try allocating a new buffer from the 
			 * GFP_DMA range as it is with in 16MB range.
			 */
			pbuf = (unsigned char *) __get_free_pages(GFP_DMA,
				*ordernum);
			if (!pbuf)
				pr_debug("Failed to get memory "
					"of size %ld "
					"using GFP_DMA\n", size);
		}
	}
	return pbuf;
}

static int
create_packet(size_t length)
{
	struct packet_data *newpacket;
	int ordernum = 0;
	int retval = 0;
	unsigned int packet_array_size = 0;
	void **invalid_addr_packet_array = 0;
	void *packet_data_temp_buf = 0;
	unsigned int idx = 0;

	pr_debug("create_packet: entry \n");

	if (!rbu_data.packetsize) {
		printk(KERN_WARNING "create_packet: packetsize not"
			" specified\n");
		retval = -EINVAL;
		goto out_noalloc;
	}

	ordernum = get_order(rbu_data.packetsize);

	packet_array_size = max( 
	       		(unsigned int)(allocation_floor / rbu_data.packetsize),
			(unsigned int)1);

	spin_unlock(&rbu_data.lock);

	newpacket = kmalloc(sizeof (struct packet_data), GFP_KERNEL);
	if (!newpacket) {
		printk(KERN_WARNING
			"dell_rbu:%s: failed to allocate new "
			"packet\n", __FUNCTION__);
		retval = -ENOMEM;
		spin_lock(&rbu_data.lock);
		goto out_noalloc;
	}
	
	memset(newpacket, 0, sizeof(struct packet_data));

	/* 
	 * BIOS errata mean we cannot allocate packets below 1MB or they will
	 * be overwritten by BIOS.
	 * 
	 * array to temporarily hold packets 
	 * that are below the allocation floor 
	 *
	 * NOTE: very simplistic because we only need the floor to be at 1MB
	 *       due to BIOS errata. This shouldn't be used for higher floors
	 *       or you will run out of mem trying to allocate the array.
	 */
	invalid_addr_packet_array = kmalloc(packet_array_size * sizeof(void*), 
						GFP_KERNEL);

	if (!invalid_addr_packet_array) {
		printk(KERN_WARNING
			"dell_rbu:%s: failed to allocate "
			"invalid_addr_packet_array \n",
			__FUNCTION__);
		retval = -ENOMEM;
		spin_lock(&rbu_data.lock);
		goto out_alloc_packet;
	}
	
	memset(invalid_addr_packet_array, 0, packet_array_size * sizeof(void*));

	while(!packet_data_temp_buf)
	{
		packet_data_temp_buf = (unsigned char *) 
			__get_free_pages(GFP_KERNEL, ordernum);
		if(!packet_data_temp_buf)
		{
			printk(KERN_WARNING
				"dell_rbu:%s: failed to allocate new "
				"packet\n", __FUNCTION__);
			retval = -ENOMEM;
			spin_lock(&rbu_data.lock);
			goto out_alloc_packet_array;
		}

		if ((unsigned long)virt_to_phys(packet_data_temp_buf) 
				< allocation_floor)
		{
			pr_debug("packet 0x%lx below floor at 0x%lx.\n", 
					(unsigned long)virt_to_phys(
						packet_data_temp_buf), 
					allocation_floor);
			invalid_addr_packet_array[idx++] = packet_data_temp_buf;
			packet_data_temp_buf = 0;
		}
	}
	spin_lock(&rbu_data.lock);

	newpacket->data = packet_data_temp_buf;

	pr_debug("create_packet: newpacket data at physical addr %lx\n", 
		(unsigned long)virt_to_phys(newpacket->data));

	newpacket->ordernum = ordernum;
	++rbu_data.num_packets;
	/*
	 * initialize the newly created packet headers 
	 */
	INIT_LIST_HEAD(&newpacket->list);
	list_add_tail(&newpacket->list, &packet_data_head.list);
	/*
	 * packets have fixed size 
	 */
	newpacket->length = rbu_data.packetsize;

	pr_debug("create_packet: exit \n");

out_alloc_packet_array:
	/* always free packet array */
	for(;idx>0;idx--)
	{
		pr_debug("freeing unused packet below floor 0x%lx.\n",
			(unsigned long)virt_to_phys(
				invalid_addr_packet_array[idx-1]));
		free_pages((unsigned long)invalid_addr_packet_array[idx-1],
			ordernum);
	}
	kfree(invalid_addr_packet_array);

out_alloc_packet:
	/* if error, free data */
	if(retval)
		kfree(newpacket);

out_noalloc:
	return retval;
}

static int
packetize_data(char *data, int length)
{
	int rc = 0;

	if (!rbu_data.packet_write_count) {
		if ((rc = create_packet(length)))
			return rc;
	}
	/*
	 * fill data in to the packet 
	 */
	if ((rc = fill_last_packet(data, length)))
		return rc;

	return rc;
}

int
do_packet_read(char *data, struct list_head *ptemp_list,
	int length, int bytes_read, int *list_read_count)
{
	void *ptemp_buf;
	struct packet_data *newpacket = NULL;
	int bytes_copied = 0;
	int j = 0;

	newpacket = list_entry(ptemp_list, struct packet_data, list);
	*list_read_count += newpacket->length;

	if (*list_read_count > bytes_read) {
		/* point to the start of unread data */
		j = newpacket->length - (*list_read_count - bytes_read);
		/* point to the offset in the packet buffer */
		ptemp_buf = (u8 *) newpacket->data + j;
		/* 
		 * check if there is enough room in 
		 * * the incoming buffer 
		 */
		if (length > (*list_read_count - bytes_read))
			/* 
			 * copy what ever is there in this 
			 * * packet and move on 
			 */
			bytes_copied = (*list_read_count - bytes_read);
		else
			/* copy the remaining */
			bytes_copied = length;
		memcpy(data, ptemp_buf, bytes_copied);
	}
	return bytes_copied;
}

static int
packet_read_list(char *data, size_t * pread_length)
{
	struct list_head *ptemp_list;
	int temp_count = 0;
	int bytes_copied = 0;
	int bytes_read = 0;
	int remaining_bytes = 0;
	char *pdest = data;

	/* check if we have any packets */
	if (!rbu_data.num_packets)
		return -ENOMEM;


	remaining_bytes = *pread_length;
	bytes_read = rbu_data.packet_read_count;

	ptemp_list = (&packet_data_head.list)->next;
	while (!list_empty(ptemp_list)) {
		bytes_copied = do_packet_read(pdest, ptemp_list,
			remaining_bytes, bytes_read, &temp_count);
		remaining_bytes -= bytes_copied;
		bytes_read += bytes_copied;
		pdest += bytes_copied;
		/*
		 * check if we reached end of buffer before reaching the
		 * last packet
		 */
		if (!remaining_bytes)
			break;
		ptemp_list = ptemp_list->next;
	}
	/* finally set the bytes read */
	*pread_length = bytes_read - rbu_data.packet_read_count;
	rbu_data.packet_read_count = bytes_read;
	return 0;
}

static void
packet_empty_list(void)
{
	struct list_head *ptemp_list;
	struct list_head *pnext_list;
	struct packet_data *newpacket;

	ptemp_list = (&packet_data_head.list)->next;
	while (!list_empty(ptemp_list)) {
		newpacket =
			list_entry(ptemp_list, struct packet_data, list);
		pnext_list = ptemp_list->next;
		list_del(ptemp_list);
		ptemp_list = pnext_list;
		/*
		 * zero out the RBU packet memory before freeing 
		 * to make sure there are no stale RBU packets left in memory
		 */
		memset(newpacket->data, 0, rbu_data.packetsize);
		free_pages((unsigned long) newpacket->data,
			newpacket->ordernum);
		kfree(newpacket);
	}
	rbu_data.packet_write_count = 0;
	rbu_data.packet_read_count = 0;
	rbu_data.num_packets = 0;
	rbu_data.packetsize = 0;
}

/*
 img_update_free:
 Frees the buffer allocated for storing BIOS image
 Always called with lock held and returned with lock held
*/
static void img_update_free( void)
{
	if (rbu_data.image_update_buffer == NULL)
		return;
	
	/* 
	 zero out this buffer before freeing it to get rid of any stale 
	 BIOS image copied in memory.
	*/
	memset(rbu_data.image_update_buffer, 0, 
		rbu_data.image_update_buffer_size);
 	free_pages((unsigned long)rbu_data.image_update_buffer, 
		rbu_data.image_update_order_number);
	/* Re-initialize the rbu_data variables after a free */
	rbu_data.image_update_buffer = NULL;
	rbu_data.image_update_buffer_size = 0;
	rbu_data.bios_image_size = 0;
}

/*
 img_update_realloc:
 This function allocates the contiguous pages to accomodate the requested
 size of data. The memory address and size values are stored globally and 
 on every call to this function the new size is checked to see if more 
 data is required than the existing size. If true the previous memory is freed
 and new allocation is done to accomodate the new size. If the incoming size is 
 less then than the already allocated size, then that  memory is reused.
 This function is called with lock held and returna with lock held.
*/
static int img_update_realloc(unsigned long size)
{
	unsigned char *image_update_buffer = NULL;
	unsigned long rc;
	int ordernum =0;


	/* check if the buffer of sufficient size has been already allocated */
    if (rbu_data.image_update_buffer_size >= size) {
		/* check for corruption */
		if ((size != 0) && (rbu_data.image_update_buffer == NULL)) {
			pr_debug("img_update_realloc: corruption check "
				"failed\n");
			return -EINVAL;
		}
		/* we have a valid pre-allocated buffer with sufficient size */
		return 0;
    }

	/* free any previously allocated buffer */
	img_update_free();

	/* 
	 This has already been called as locked so we can now unlock 
	 and proceed to calling get_free_pages_limited as this function
	 can sleep
	*/
	spin_unlock(&rbu_data.lock);

	image_update_buffer = (unsigned char *)get_free_pages_limited(size,
		&ordernum,
		BIOS_SCAN_LIMIT);
	
	/* acquire the spinlock again */
	spin_lock(&rbu_data.lock);

	if (image_update_buffer != NULL) {
		rbu_data.image_update_buffer = image_update_buffer;
		rbu_data.image_update_buffer_size = PAGE_SIZE << ordernum;
		rbu_data.image_update_order_number = ordernum;
		memset(rbu_data.image_update_buffer,0, 
			rbu_data.image_update_buffer_size);
		pr_debug("img_update_realloc: success\n");
		rc = 0; 
	} else {
		pr_debug("Not enough memory for image update:order number = %d"
			",size = %ld\n",ordernum, size);
		rc = -ENOMEM;
	}

	return rc;
} /* img_update_realloc */

static int
read_rbu_mono_data_size(char *outbuf,
	char **start, off_t offset, int count, int *eof)
{
	*eof = 1;
	return sprintf(outbuf, "%lu\n", rbu_data.bios_image_size);
}

static int
write_rbu_mono_data_size(struct file *file,
	const char *buffer, int count)
{
	int retVal = count;
	unsigned long temp = rbu_data.bios_image_size;
	char *temp1;

	temp1 = kmalloc(count, GFP_KERNEL);
	if (!temp1) 
		return -ENOMEM;
	copy_from_user((void *)temp1, (void *)buffer, count);
	sscanf(temp1, "%lu", &rbu_data.bios_image_size);

	if (rbu_data.bios_image_size) {
		if (img_update_realloc(rbu_data.bios_image_size)) {
			rbu_data.bios_image_size = 0;
		}
	} else {
		/* free any allocated RBU memory */
		img_update_free();

	}
	kfree(temp1);
	return retVal;
}

static int
read_rbu_packet_data_size(char *outbuf,
	char **start, off_t offset, int count, int *eof)
{
	*eof = 1;
	return sprintf(outbuf, "%lu\n", rbu_data.packetsize);
}

static int
write_rbu_packet_data_size(struct file *file,
	const char *buffer, int count)
{
	char *temp;
	temp = kmalloc(count , GFP_KERNEL);
	if (!temp) 
		return -ENOMEM;
	copy_from_user((void *)temp, (void *)buffer, count);
	packet_empty_list();
	sscanf(temp, "%lu", &rbu_data.packetsize);
	kfree(temp);
	return count;
}

/*
 read_rbu_mono_data:
 Reads the BIOS image from previously allocated contiguous physical 
 memory to the buffer supplied in this call. 
 The reading is done in chunks of bytes supplied in the count argument.
 The reading stops when the total number of bytes read equals the image 
 size given previously.
 If the image size is not specified or if the image size is zero,
 this function returns failure.
*/
static int
read_rbu_mono_data(char *outbuf,
	char **start, off_t offset, int count, int *eof)
{
	u8 *image;
	int bytes_left;
	int bytes_read;

	pr_debug("%s: offset: %lu count: %d\n", __FUNCTION__, offset, count);

	/* check if we have something to return */
	if ((rbu_data.image_update_buffer == NULL) ||
		(rbu_data.bios_image_size == 0)) {
		pr_debug("%s: image_update_buffer: %p bios_image_size: %lu\n", 
			__FUNCTION__, rbu_data.image_update_buffer, 
			rbu_data.bios_image_size);
		return -ENODATA;
	}

	/* point to start of image */
	image = rbu_data.image_update_buffer;

	/* point to read offset in image */
	image += offset;

	/* compute bytes remaining to be read in image */
	bytes_left = rbu_data.bios_image_size - offset;

	/* determine number of bytes for this read */
	if (bytes_left > count) {
		bytes_read = count;
	} else {
		bytes_read = bytes_left;
		*eof = 1;
	}

	/* copy data to caller's buffer */
	memcpy(outbuf, image, bytes_read);

	/* check if image requires more than one read */
	if (rbu_data.bios_image_size > count)
		*start = outbuf;

	pr_debug("%s: bytes_left: %d bytes_read: %d eof: %d\n",
		__FUNCTION__, bytes_left, bytes_read, *eof);

	return bytes_read;
}

/*
 write_rbu_mono_data
 Writes from the incoming BIOS image file to the pre-allocated 
 contiguous physical memory pages. 
 The writes occur in chunks of memory supplied by the count. The writes 
 stops when the total memory supplied equals the image size given previously.
 If no memory size is previously specified or if the previsou specifies size 
 is zero the write returns error.
*/
static int
write_rbu_mono_data(struct file *file, const char *buffer, 
					int count)
{
	static unsigned long temp_count_mono = 0;
	unsigned char *pDest = NULL;
	unsigned char *ptemp = NULL;
	int retVal = 0;

	if (rbu_data.bios_image_size == 0) {
		temp_count_mono = 0;
		printk(KERN_WARNING "%s: BIOS image size not set\n", 
			__FUNCTION__);
		retVal = -EPERM;
		goto end;
	}

	if ((temp_count_mono + count) > rbu_data.bios_image_size) {
		printk(KERN_INFO
			"%s: data_over_run, temp_count %ld, "
			"count %d , bios_image_size %ld\n", __FUNCTION__,
			temp_count_mono, count, rbu_data.bios_image_size);
		retVal = 0;
		goto end;
	}

	pDest = rbu_data.image_update_buffer;

	ptemp = pDest + temp_count_mono;

	/* copy data from the user space */
	copy_from_user((void *) ptemp, (void *) buffer, count);

	if ((temp_count_mono + count) < rbu_data.bios_image_size) {
		temp_count_mono += count;
	}

	if ((temp_count_mono + count) == rbu_data.bios_image_size) {
		rbu_data.bios_image_size = temp_count_mono + count;
		temp_count_mono = 0;
	}

	retVal = count;
end:
	return retVal;
}

static int
read_rbu_packet_data(char *outbuf, char **start, off_t offset, int count,
					 int *eof)
{
	int rc;
	int bytes_left;
	int bytes_read;
	size_t data_length;
	unsigned long image_size;

	pr_debug("%s: offset: %lu count: %d\n", __FUNCTION__, offset, count);

	/* check to see if we have something to return */
	if (!rbu_data.num_packets) {
		pr_debug("read_rbu_packet_data: no packets written\n");
		return -ENOMEM;
	}

	image_size = rbu_data.num_packets * rbu_data.packetsize;

	if ( offset > image_size ) {
		printk(KERN_WARNING "read_rbu_packet_data: data underrun\n");
		return -EIO;
	}

	bytes_left = image_size - offset;
	data_length = min(bytes_left, count);

	if ((rc = packet_read_list(outbuf, &data_length)))
		return rc;

	/* determine number of bytes for this read */
	if (bytes_left > count) {
		bytes_read = count;
	} else {
		bytes_read = bytes_left;
		rbu_data.packet_read_count = 0;
		*eof = 1;
	}
	
	*start = outbuf;	

	pr_debug("%s:bytes_left %d :bytes_read %d :count %d :eof %d\n",
		__FUNCTION__, bytes_left, bytes_read, count ,*eof);

	return bytes_read;
}

static int
write_rbu_packet_data(struct file *file,
	const char *buffer, int count)
{
	static unsigned long temp_count_packet = 0;
	int retval = 0;
	char *temp;

	if (!rbu_data.packetsize) {
		temp_count_packet = 0;
		printk(KERN_WARNING "dell_rbu:%s: packet data size "
			"not set\n", __FUNCTION__);
		retval = -EINVAL;
		goto end;
	}

	if ((temp_count_packet + count) > rbu_data.packetsize) {
		printk(KERN_WARNING
			"%s: data_over_run, temp_count %ld, "
			"count %d , rbu_data.packetsize %ld\n", __FUNCTION__,
			temp_count_packet, count, rbu_data.packetsize);
		retval = 0;
		goto end;
	}

	temp = kmalloc(count, GFP_KERNEL);
	if (!temp) {
		printk(KERN_WARNING"dell_rbu: write_rbu_packet_data: kmalloc failed\n");
		return -ENOMEM;
	}

	copy_from_user((void *)temp, (void *)buffer, count);

	if ((retval = packetize_data(temp, count)) < 0 ) {
		pr_debug(KERN_WARNING "write_rbu_packet_data: packetize_data "
			"failed with status %d\n", retval);
		retval = -EIO;
		goto error_packetize_data;
	} 

	retval = count;
error_packetize_data:
	kfree(temp);
end:
	return retval;
}


static int
read_rbu_data_size(char *outbuf, char **start, off_t offset, int count, 
				   int *eof, void *data)
{
	int ret_count = count;

	spin_lock(&rbu_data.lock);

	if (!strcmp(image_type, "mono"))
		ret_count = read_rbu_mono_data_size(outbuf, start, offset, 
			count, eof);
	else if (!strcmp(image_type, "packet"))
		ret_count = read_rbu_packet_data_size(outbuf, start, offset,
			count, eof);
	else
		printk(KERN_ERR "dell_rbu: invalid image type"
                        " %s specified\n", image_type);
	spin_unlock(&rbu_data.lock);
	return ret_count;
}


static int
write_rbu_data_size(struct file *file, const char *buffer, unsigned long count, 
			   void *data) 
{

	int ret_count = 0;

	spin_lock(&rbu_data.lock);

	if (!strcmp(image_type, "mono"))
		ret_count = write_rbu_mono_data_size(file, buffer, (int)count);
	else if (!strcmp(image_type, "packet"))
		ret_count = write_rbu_packet_data_size(file, buffer, (int)count);
	else
		printk(KERN_ERR "dell_rbu: invalid image type"
                        " %s specified\n", image_type);

	spin_unlock(&rbu_data.lock);
	return ret_count;
}


static int
read_rbu_data(char *outbuf, char **start, off_t offset, int count, int *eof, 
			  void *data)
{
	int ret_count = count;

	spin_lock(&rbu_data.lock);

	if (!strcmp(image_type, "mono"))
		ret_count = read_rbu_mono_data(outbuf, start, offset, count, eof);
	else if (!strcmp(image_type, "packet"))
		ret_count = read_rbu_packet_data(outbuf, start, offset, count, eof);
	else
		printk(KERN_ERR "dell_rbu: invalid image type"
			" %s specified\n", image_type);

	spin_unlock(&rbu_data.lock);
	return ret_count;
}


static int
write_rbu_data(struct file *file, const char *buffer, unsigned long count, 
			   void *data) 
{

	int ret_count = 0;

	spin_lock(&rbu_data.lock);

	if (!strcmp(image_type, "mono"))
		ret_count = write_rbu_mono_data(file, buffer,(int) count);
	else if (!strcmp(image_type, "packet"))
		ret_count = write_rbu_packet_data(file, buffer,(int) count);
	else
		printk(KERN_ERR "dell_rbu: invalid image type"
                        " %s specified\n", image_type);

	spin_unlock(&rbu_data.lock);
	return ret_count;
}

static int
read_image_type(char *outbuf,
	char **start, off_t offset, int count, int *eof, void *data)
{
	*eof = 1;
	return sprintf(outbuf, "%s\n", image_type);
}

static int
write_image_type(struct file *file,
	const char *buffer, unsigned long count, void *data)
{
	char *temp;
	temp = kmalloc(count, GFP_KERNEL);
	if(!temp) 
		return -ENOMEM;
	/* copy data from the user space */
	copy_from_user((void *)temp, (void *) buffer, count); 

	spin_lock(&rbu_data.lock);
	if (strstr(temp, "packet"))
		strcpy(image_type, "packet");
	else
		strcpy(image_type, "mono");

	/* we must free all previous allocations */
	packet_empty_list();
	img_update_free();
	spin_unlock(&rbu_data.lock);
	kfree(temp);
	return count;
}

/*
 dcd_remove_proc_entries:
 Called from the driver unload routine , it removes the /proc entries
*/
void
dcd_remove_proc_entries(void)
{
        if (rbu_data.proc_rbu_dir == NULL) {
                printk(KERN_WARNING "dcdrbu_exit: rbu_data.proc_rbu_dir is"
			" NULL \n");
                return;
        }
        if (rbu_data.proc_root_dir == NULL) {
                printk(KERN_WARNING "dcdrbu_exit: rbu_data.proc_root_dir is"
			" NULL \n");
                return;
        }
        /* remove rbudata file */
        remove_proc_entry("image_type", rbu_data.proc_rbu_dir);
        pr_debug("dcdrbu_exit: removing /proc/dell/rbu/rbudatasize entry\n");
        /* remove rbudata file */
        remove_proc_entry("rbudatasize", rbu_data.proc_rbu_dir);
        pr_debug("dcdrbu_exit: removing /proc/dell/rbu/rbudata entry\n");
        /* remove rbudata file */
        remove_proc_entry("rbudata", rbu_data.proc_rbu_dir);
        pr_debug("dcdrbu_exit: removing /proc/dell/rbu entry\n");
        /* remove rbu dir */
        remove_proc_entry("rbu", rbu_data.proc_root_dir);
        pr_debug("dcdrbu_exit: removing /proc/dell/ entry\n");
        /* remove dell dir */
        remove_proc_entry("dell", NULL);
}

static int
dcdrbu_init(void)
{
	struct proc_dir_entry *file;
	int retVal = 0;

	spin_lock_init(&rbu_data.lock);
	init_packet_head();

	/* make the parent /proc/dell dir */
	rbu_data.proc_root_dir = proc_mkdir("dell", 0);

	if (!rbu_data.proc_root_dir) {
		printk(KERN_WARNING "dell_rbu: Unable to create dell proc dir");
		retVal = -ENOMEM;
		goto error_proc_entry_dell;
	}

	rbu_data.proc_root_dir->owner = THIS_MODULE;

	/* make the parent /proc/dell/rbu dir */
	rbu_data.proc_rbu_dir =
		proc_mkdir("rbu", rbu_data.proc_root_dir);

	if (!rbu_data.proc_rbu_dir) {
		printk(KERN_WARNING "Unable to create dell RBU proc dir");
		retVal = -ENOMEM;
		goto error_proc_entry_rbu;
	}

	rbu_data.proc_rbu_dir->owner = THIS_MODULE;

	/* make the proc entry rbudata in /proc/dell/rbu dir */
	file = create_proc_entry("rbudatasize", 0,
		rbu_data.proc_rbu_dir);
	if (!file) {
		printk(KERN_WARNING
			"Unable to create proc entry rbudatasize");
		retVal = -ENOMEM;
		goto error_proc_entry_rbudata;
	} else {
		file->nlink = 1;
		file->data = &rbu_data;
		file->read_proc = read_rbu_data_size;
		file->write_proc = write_rbu_data_size;
		file->owner = THIS_MODULE;
	}

	/* make the proc entry rbudata in /proc/dell/rbu dir */
	file = create_proc_entry("rbudata", 0, rbu_data.proc_rbu_dir);
	if (!file) {
		printk(KERN_WARNING "Unable to create proc entry rbudata");
		retVal = -ENOMEM;
		goto error_proc_entry_rbudatasize;
	} else {
		file->nlink = 1;
		file->data = &rbu_data;
		file->read_proc = read_rbu_data;
		file->write_proc = write_rbu_data;
		file->owner = THIS_MODULE;
	}

	/* make the proc entry image_type in /proc/dell/rbu dir */
	file = create_proc_entry("image_type", 0,
		rbu_data.proc_rbu_dir);
	if (!file) {
		printk(KERN_WARNING
			"Unable to create proc entry image_type");
		retVal = -ENOMEM;
		goto error_proc_entry_image_type;
	} else {
		file->nlink = 1;
		file->data = &rbu_data;
		file->read_proc = read_image_type;
		file->write_proc = write_image_type;
		file->owner = THIS_MODULE;
	}

	return retVal;

/* error exits*/
error_proc_entry_image_type:
	remove_proc_entry("rbudatasize", rbu_data.proc_root_dir);
error_proc_entry_rbudatasize:
	remove_proc_entry("rbudata", rbu_data.proc_root_dir);
error_proc_entry_rbudata:
	remove_proc_entry("rbu", rbu_data.proc_root_dir);
error_proc_entry_rbu:
	remove_proc_entry("dell", NULL);
error_proc_entry_dell:
	return retVal;
}

static void
dcdrbu_exit(void)
{
	packet_empty_list();
        img_update_free();
	dcd_remove_proc_entries();
}

module_exit(dcdrbu_exit);
module_init(dcdrbu_init);

