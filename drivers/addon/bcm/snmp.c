
/*
 *  Broadcom Cryptonet Driver software is distributed as is, without any warranty
 *  of any kind, either express or implied as further specified in the GNU Public
 *  License. This software may be used and distributed according to the terms of
 *  the GNU Public License.
 *
 * Cryptonet is a registered trademark of Broadcom Corporation.
 */

/******************************************************************************
 *
 *  Copyright 2000
 *  Broadcom Corporation
 *  16215 Alton Parkway
 *  PO Box 57013
 *  Irvine CA 92619-7013
 *
 *****************************************************************************/
/* 
 * Broadcom Corporation uBSec SDK 
 */
/*
 *  snmp.c module for providing statistics thru snmp agent. 
 */
/* Revision History:
 *
 * May   2001 SRM Created
 */
#include "cdevincl.h"

extern DeviceInfo_t DeviceInfoList[MAX_SUPPORTED_DEVICES];

#ifdef UBSEC_SNMP_2_4
int stats5820_get_info(char *, char **, off_t, int);
#endif

#ifdef UBSEC_SNMP_2_2 

#define PROC_NET_BCM5820 PROC_NET_LAST + 32 
typedef int (get_info_t) (char *, char **, mode_t, int, int);

int stats5820_get_info(char *, char **, off_t, int, int);
static struct proc_dir_entry proc_net_bcm5820 = {
	PROC_NET_BCM5820, 7, "bcm5820",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	stats5820_get_info
};
int proc_net_create(const char *name, mode_t mode, get_info_t *get_info);
int proc_net_remove(const char*name);

int proc_net_create(const char *name, mode_t mode, get_info_t *get_info)
{
	return proc_net_register(&proc_net_bcm5820);
}

int proc_net_remove(const char *name)
{
	return proc_net_unregister(PROC_NET_BCM5820);
}
#endif

void init_snmp_stats_support()
{
	proc_net_create("bcm5820", 0, (get_info_t *) stats5820_get_info);
}

void shutdown_snmp_stats_support()
{
	proc_net_remove("bcm5820");
}

#ifdef UBSEC_SNMP_2_2
int stats5820_get_info(char *buf, char **start, off_t offset, int length, int dummy)
#else
int stats5820_get_info(char *buf, char **start, off_t offset, int length)
#endif
{
	int size, i;
	int len = 0;
	ubsec_Statistics_t stats;
	
	for (i = 0; i < NumDevices; i++)
	{
		size = sprintf(buf+len, "Cryptonet %d: ", i+1);
		len += size;
		if (DeviceInfoList[i].Context == NULL)
			continue;
		if (ubsec_GetStatistics(DeviceInfoList[i].Context, &stats) == UBSEC_STATUS_SUCCESS)
		{
			size = sprintf(buf+len,"%d %d %d %d %d %d %d %d %d %d %d\n",
				NumDevices,
				(i+1),
				stats.IKECount,
				stats.IKEFailedCount,
				stats.BytesEncryptedCount,
				stats.BytesDecryptedCount,
				stats.BlocksEncryptedCount,
				stats.BlocksDecryptedCount,
				stats.CryptoFailedCount,
				DeviceInfoList[i].DeviceFailuresCount,
				DeviceInfoList[i].DeviceStatus);
			len += size;
			/*
			size = sprintf(buf+len,"%ld %ld %ld %ld %ld %ld %ld\n",
				stats.DHPublicCount,
				stats.DHSharedCount,
				stats.RSAPublicCount,
				stats.RSAPrivateCount,
				stats.DSASignCount,
				stats.DSAVerifyCount,
				stats.DMAErrorCount);
			len += size;
			*/
			if (len > length)
				break;
		}
	}
	if (offset >= len)
	{
		*start = buf;
		return 0;
	}
	*start = buf + offset;
	len -= offset;
	return (len > length ? length : len);
}
