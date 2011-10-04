/* File veth.c created by Kyle A. Lucke on Mon Aug  7 2000. */

/**************************************************************************/
/*                                                                        */
/* IBM eServer iSeries Virtual Ethernet Device Driver                     */
/* Copyright (C) 2001 Kyle A. Lucke (klucke@us.ibm.com), IBM Corp.        */
/*                                                                        */
/*  This program is free software; you can redistribute it and/or modify  */
/*  it under the terms of the GNU General Public License as published by  */
/*  the Free Software Foundation; either version 2 of the License, or     */
/*  (at your option) any later version.                                   */
/*                                                                        */
/*  This program is distributed in the hope that it will be useful,       */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/*  GNU General Public License for more details.                          */
/*                                                                        */
/*  You should have received a copy of the GNU General Public License     */
/*  along with this program; if not, write to the Free Software           */
/*  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  */
/*                                                                   USA  */
/*                                                                        */
/* This module contains the implementation of a virtual ethernet device   */
/* for use with iSeries LPAR Linux.  It utilizes low-level message passing*/
/* provided by the hypervisor to enable an ethernet-like network device   */
/* that can be used to enable inter-partition communications on the same  */
/* physical iSeries.                                                      */
/*                                                                        */
/* The iSeries LPAR hypervisor has currently defined the ability for a    */
/* partition to communicate on up to 16 different virtual ethernets, all  */
/* dynamically configurable, at least for an OS/400 partition.  The       */
/* dynamic nature is not supported for Linux yet.                         */
/*                                                                        */
/* Each virtual ethernet a given Linux partition participates in will     */
/* cause a network device with the form ethXX to be created,              */
/*                                                                        */
/* The virtual ethernet a given ethXX virtual ethernet device talks on    */
/* can be determined either by dumping /proc/iSeries/veth/vethX, where    */
/* X is the virtual ethernet number, and the netdevice name will be       */
/* printed out.  The virtual ethernet a given ethX device communicates on */
/* is also printed to the printk() buffer at module load time.            */
/*                                                                        */
/* This driver (and others like it on other partitions) is responsible for*/
/* routing packets to and from other partitions.  The MAC addresses used  */
/* by the virtual ethernets contain meaning, and should not be modified.  */
/* Doing so could disable the ability of your Linux partition to          */
/* communicate with the other OS/400 partitions on your physical iSeries. */
/* Similarly, setting the MAC address to something other than the         */
/* "virtual burned-in" address is not allowed, for the same reason.       */
/*                                                                        */
/* Notes:                                                                 */
/*                                                                        */
/* 1. Although there is the capability to talk on multiple shared         */
/*    ethernets to communicate to the same partition, each shared         */
/*    ethernet to a given partition X will use a finite, shared amount    */
/*    of hypervisor messages to do the communication.  So having 2 shared */
/*    ethernets to the same remote partition DOES NOT double the          */
/*    available bandwidth.  Each of the 2 shared ethernets will share the */
/*    same bandwidth available to another.                                */
/*                                                                        */
/* 2. It is allowed to have a virtual ethernet that does not communicate  */
/*    with any other partition.  It won't do anything, but it's allowed.  */
/*                                                                        */
/* 3. There is no "loopback" mode for a virtual ethernet device.  If you  */
/*    send a packet to your own mac address, it will just be dropped, you */
/*    won't get it on the receive side.  Such a thing could be done,      */
/*    but my default driver DOES NOT do so.                               */
/*                                                                        */
/* 4. Multicast addressing is implemented via broadcasting the multicast  */
/*    frames to other partitions.  It is the responsibility of the        */
/*    receiving partition to filter the addresses desired.                */
/*                                                                        */
/* 5. This module utilizes several different bottom half handlers for     */
/*    non-high-use path function (setup, error handling, etc.).  Multiple */
/*    bottom halves were used because only one would not keep up to the   */
/*    much faster iSeries device drivers this Linux driver is talking to. */
/*    All hi-priority work (receiving frames, handling frame acks) is done*/
/*    in the interrupt handler for maximum performance.                   */
/*                                                                        */
/* Tunable parameters:                                                    */
/*                                                                        */
/* VethBuffersToAllocate: This compile time option defaults to 120. It can*/
/* be safely changed to something greater or less than the default.  It   */
/* controls how much memory Linux will allocate per remote partition it is*/
/* communicating with.  The user can play with this to see how it affects */
/* performance, packets dropped, etc.  Without trying to understand the   */
/* complete driver, it can be thought of as the maximum number of packets */
/* outstanding to a remote partition at a time.                           */
/*                                                                        */
/**************************************************************************/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>
#ifdef SIOCETHTOOL
#include <linux/ethtool.h>
#endif
#include <asm/iSeries/mf.h>
#include <asm/uaccess.h>

#include "veth.h"
#include <asm/iSeries/HvLpConfig.h>
#include <asm/iSeries/veth-proc.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/iSeries_proc.h>
#include <asm/iSeries/iSeries_dma.h>
#include <asm/semaphore.h>
#include <linux/proc_fs.h>


#define veth_printk(fmt, args...) \
printk(KERN_INFO "%s: " fmt, __FILE__, ## args)

#define veth_error_printk(fmt, args...) \
printk(KERN_ERR "(%s:%3.3d) ERROR: " fmt, __FILE__, __LINE__ , ## args)

static char *version __initdata = "v1.06 05/04/2003  Kyle Lucke, klucke@us.ibm.com\n";

static int probed __initdata = 0;
#define VethBuffersToAllocate 120

static struct VethFabricMgr *mFabricMgr = NULL;
static struct proc_dir_entry *veth_proc_root = NULL;
static atomic_t pending_callbacks;

DECLARE_MUTEX_LOCKED(VethProcSemaphore);

static int veth_open(struct net_device *dev);
static int veth_close(struct net_device *dev);
static int veth_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int veth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
static void veth_handleEvent(struct HvLpEvent *, struct pt_regs *);
static void veth_handleAck(struct HvLpEvent *);
static void veth_handleInt(struct HvLpEvent *);
static void veth_openConnections(void);
static void veth_openConnection(u8);
static void veth_closeConnection(u8);
static void veth_intFinishOpeningConnections(void *, int number);
static void veth_finishOpeningConnections(void *);
static void veth_finishOpeningConnectionsLocked(struct VethLpConnection *);
static int veth_multicast_wanted(struct VethPort *port, u64 dest);
static void veth_set_multicast_list(struct net_device *dev);

static void veth_sendCap(struct VethLpConnection *);
static void veth_sendMonitor(struct VethLpConnection *);
static void veth_takeCap(struct VethLpConnection *, struct VethLpEvent *);
static void veth_takeCapAck(struct VethLpConnection *, struct VethLpEvent *);
static void veth_takeMonitorAck(struct VethLpConnection *, struct VethLpEvent *);
static void veth_msgsInit(struct VethLpConnection *connection);
static void veth_recycleMsgByNum(struct VethLpConnection *, u16);
static void veth_recycleMsg(struct VethLpConnection *, struct VethMsg *);
static void veth_capTask(void *);
static void veth_capAckTask(void *);
static void veth_monitorAckTask(void *);
static void veth_takeFrames(struct VethLpConnection *, struct VethLpEvent *);
static int veth_pTransmit(struct sk_buff *skb, HvLpIndex remoteLp, struct net_device *dev);
static struct net_device_stats *veth_get_stats(struct net_device *dev);
static void veth_intFinishMsgsInit(void *, int);
static void veth_finishMsgsInit(struct VethLpConnection *connection);
static void veth_intFinishCapTask(void *, int);
static void veth_finishCapTask(struct VethLpConnection *connection);
static void veth_finishCapTaskLocked(struct VethLpConnection *connection);
static void veth_finishSendCap(struct VethLpConnection *connection);
static void veth_timedAck(unsigned long connectionPtr);
static void veth_startQueues(void);
static void veth_failMe(struct VethLpConnection *connection);

extern struct pci_dev *iSeries_veth_dev;
static struct net_device *veth_devices[16];
static int veth_dev_queue_stopped[16];
static int veth_num_devices;
static int veth_num_fragged;

int __init veth_probe(void)
{
	struct net_device *dev = NULL;
	struct VethPort *port = NULL;
	int vlansFound = 0;
	int displayVersion = 0;
	int rc;

	u16 vlanMap = HvLpConfig_getVirtualLanIndexMap();
	int vlanIndex = 0;
	veth_num_fragged = 0;

	memset(veth_devices, 0, sizeof(struct net_device *) * 16);
	memset(veth_dev_queue_stopped, 0, sizeof(int) * 16);
	veth_num_devices = 0;
	if (probed)
		return -ENODEV;
	probed = 1;

	while (vlanMap != 0) {
		int bitOn = vlanMap & 0x8000;

		if (bitOn) {
			vlansFound++;
			
			dev = alloc_etherdev(sizeof(struct VethPort));

			if (dev == NULL) {
				veth_error_printk("Unable to allocate net_device structure!\n");
				break;
			}

			port = mFabricMgr->mPorts[vlanIndex] = (struct VethPort *) dev->priv;
			memset(port, 0, sizeof(struct VethPort));
			rwlock_init(&(port->mMcastGate));
			mFabricMgr->mPorts[vlanIndex]->mDev = dev;
			veth_devices[veth_num_devices] = dev;
			veth_num_devices++;

			dev->dev_addr[0] = 0x02;
			dev->dev_addr[1] = 0x01;
			dev->dev_addr[2] = 0xFF;
			dev->dev_addr[3] = vlanIndex;
			dev->dev_addr[4] = 0xFF;
			dev->dev_addr[5] = HvLpConfig_getLpIndex_outline();
			dev->mtu = 9000;

			memcpy(&(port->mMyAddress), dev->dev_addr, 6);

			dev->open = &veth_open;
			dev->hard_start_xmit = &veth_start_xmit;
			dev->stop = &veth_close;
			dev->get_stats = veth_get_stats;
			dev->set_multicast_list = &veth_set_multicast_list;
			dev->do_ioctl = &veth_ioctl;

			/* display version info if adapter is found */
			if (!displayVersion) {
				/* set display flag to TRUE so that */
				/* we only display this string ONCE */
				displayVersion = 1;
				veth_printk("%s", version);
			}

			rc = register_netdev(dev);

			if(rc) {
				veth_printk("Failed to register an ethernet device (veth=%d)\n", vlanIndex);
				mFabricMgr->mPorts[vlanIndex] = NULL;
				veth_num_devices--;
				veth_devices[veth_num_devices] = NULL;
				kfree(dev);
			} else {
				veth_printk("Found an ethernet device %s (veth=%d) (addr=%p)\n", dev->name, vlanIndex, dev);
			}
		}

		++vlanIndex;
		vlanMap = vlanMap << 1;
	}

	if (vlansFound > 0)
		return 0;
	else
		return -ENODEV;
}

MODULE_AUTHOR("Kyle Lucke <klucke@us.ibm.com>");
MODULE_DESCRIPTION("iSeries Virtual ethernet driver");
MODULE_LICENSE("GPL");

int VethModuleReopen = 1;

void veth_proc_delete(struct proc_dir_entry *iSeries_proc)
{
	int i = 0;
	HvLpIndex thisLp = HvLpConfig_getLpIndex_outline();
	u16 vlanMap = HvLpConfig_getVirtualLanIndexMap();
	int vlanIndex = 0;

	for (i = 0; i < HvMaxArchitectedLps; ++i) {
		if (i != thisLp) {
			if (HvLpConfig_doLpsCommunicateOnVirtualLan(thisLp, i)) {
			        char name[10];
				sprintf(name, "lpar%d", i);
				remove_proc_entry(name, veth_proc_root);
			}
		}
	}

	while (vlanMap != 0) {
		int bitOn = vlanMap & 0x8000;

		if (bitOn) {
			char name[10];
			sprintf(name, "veth%d", vlanIndex);
			remove_proc_entry(name, veth_proc_root);
		}

		++vlanIndex;
		vlanMap = vlanMap << 1;
	}

	remove_proc_entry("veth", iSeries_proc);

	up(&VethProcSemaphore);
}

void __exit veth_module_cleanup(void)
{
        if(mFabricMgr != NULL) {

	    int i;
	    struct VethFabricMgr *myFm = mFabricMgr;
	    struct net_device *thisOne = NULL;

	    VethModuleReopen = 0;

	    for (i = 0; i < HvMaxArchitectedLps; ++i) {
		    struct VethLpConnection *connection = &(mFabricMgr->mConnection[i]);
		    unsigned long flags;
		    spin_lock_irqsave(&connection->mStatusGate, flags);
		    veth_closeConnection(i);
		    spin_unlock_irqrestore(&connection->mStatusGate, flags);
	    }

	    /* Stop interrupts coming from the Hypervisor */
	    HvLpEvent_unregisterHandler(HvLpEvent_Type_VirtualLan);

	    /* Stop rescheduling of tasks in ack callbacks */
	    mFabricMgr = NULL;
	    mb();

	    /* Wait for any scheduled tasks to complete */
	    flush_scheduled_tasks();

	    /* Wait for any outstanding callbacks to arrive */
	    while (atomic_read(&pending_callbacks));

	    /* Ensure all callbacks have finished running */
	    synchronize_irq();

	    down(&VethProcSemaphore);

	    iSeries_proc_callback(&veth_proc_delete);

	    down(&VethProcSemaphore);

	    for (i = 0; i < HvMaxArchitectedLps; ++i) {
		    if (myFm->mConnection[i].mNumberAllocated + myFm->mConnection[i].mNumberRcvMsgs > 0) {
			    mf_deallocateLpEvents(myFm->mConnection[i].mRemoteLp,
						  HvLpEvent_Type_VirtualLan,
						  myFm->mConnection[i].mNumberAllocated + myFm->mConnection[i].mNumberRcvMsgs,
						  NULL, NULL);
		    }

		    kfree(myFm->mConnection[i].mMsgs);
	    }

	    for (i = 0; i < HvMaxArchitectedVirtualLans; ++i) {
		    if (myFm->mPorts[i] != NULL) {
			    thisOne = myFm->mPorts[i]->mDev;
			    myFm->mPorts[i] = NULL;

			    mb();

			    if (thisOne != NULL) {
				    veth_printk("Unregistering %s (veth=%d)\n", thisOne->name, i);
				    unregister_netdev(thisOne);
				    kfree(thisOne);
			    }
		    }
	    }

	    kfree(myFm);
	}
}

module_exit(veth_module_cleanup);

void veth_proc_init(struct proc_dir_entry *iSeries_proc)
{
        long i = 0;
	HvLpIndex thisLp = HvLpConfig_getLpIndex_outline();
	u16 vlanMap = HvLpConfig_getVirtualLanIndexMap();
	long vlanIndex = 0;


	veth_proc_root = proc_mkdir("veth", iSeries_proc);
	if (!veth_proc_root)
		return;

	for (i = 0; i < HvMaxArchitectedLps; ++i) {
		if (i != thisLp) {
			if (HvLpConfig_doLpsCommunicateOnVirtualLan(thisLp, i)) {
				struct proc_dir_entry *ent;
				char name[10];
				sprintf(name, "lpar%d", (int) i);
				ent = create_proc_entry(name, S_IFREG | S_IRUSR, veth_proc_root);
				if (!ent)
					return;
				ent->nlink = 1;
				ent->owner = THIS_MODULE;
				ent->data = (void *) i;
				ent->read_proc = proc_veth_dump_connection;
				ent->write_proc = NULL;
			}
		}
	}

	while (vlanMap != 0) {
		int bitOn = vlanMap & 0x8000;

		if (bitOn) {
			struct proc_dir_entry *ent;
			char name[10];
			sprintf(name, "veth%d", (int) vlanIndex);
			ent = create_proc_entry(name, S_IFREG | S_IRUSR, veth_proc_root);
			if (!ent)
				return;
			ent->nlink = 1;
			ent->owner = THIS_MODULE;
			ent->data = (void *) vlanIndex;
			ent->read_proc = proc_veth_dump_port;
			ent->write_proc = NULL;
		}

		++vlanIndex;
		vlanMap = vlanMap << 1;
	}

	up(&VethProcSemaphore);
}

int __init veth_module_init(void)
{
	int status;
	int i;

	mFabricMgr = kmalloc(sizeof(struct VethFabricMgr), GFP_KERNEL);

	if(mFabricMgr == NULL) {
	        veth_printk("Unable to allocate fabric manager\n");
		return -ENOMEM;
	}

	memset(mFabricMgr, 0, sizeof(struct VethFabricMgr));
	veth_printk("Initializing veth module, fabric mgr (address=%p)\n", mFabricMgr);

	mFabricMgr->mEyecatcher = 0x56455448464D4752ULL;
	mFabricMgr->mThisLp = HvLpConfig_getLpIndex_outline();
	atomic_set(&pending_callbacks, 0);

	for (i = 0; i < HvMaxArchitectedLps; ++i) {
		mFabricMgr->mConnection[i].mEyecatcher = 0x564554484C50434EULL;
		veth_failMe(mFabricMgr->mConnection + i);
		spin_lock_init(&mFabricMgr->mConnection[i].mAckGate);
		spin_lock_init(&mFabricMgr->mConnection[i].mStatusGate);
	}

	status = veth_probe();

	if (status == 0) {
		veth_openConnections();
		iSeries_proc_callback(&veth_proc_init);
	}

	return status;
}

module_init(veth_module_init);

static void veth_failMe(struct VethLpConnection *connection)
{
	connection->mConnectionStatus.mSentCap = 0;
	connection->mConnectionStatus.mCapAcked = 0;
	connection->mConnectionStatus.mGotCap = 0;
	connection->mConnectionStatus.mGotCapAcked = 0;
	connection->mConnectionStatus.mSentMonitor = 0;
	connection->mConnectionStatus.mFailed = 1;
}

static int veth_open(struct net_device *dev)
{
	struct VethPort *port = (struct VethPort *) dev->priv;

	memset(&port->mStats, 0, sizeof(port->mStats));
	MOD_INC_USE_COUNT;

	netif_start_queue(dev);

	return 0;
}

static int veth_close(struct net_device *dev)
{
	netif_stop_queue(dev);

	MOD_DEC_USE_COUNT;

	return 0;
}

static struct net_device_stats *veth_get_stats(struct net_device *dev)
{
	struct VethPort *port = (struct VethPort *) dev->priv;

	return (&port->mStats);
}


static int veth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned char *frame = skb->data;
	HvLpIndex remoteLp = frame[5];
	int i = 0;
	int rc = 1;
	int individual_rc;
	int skb_len = skb->len;
	struct VethPort *port = (struct VethPort *) dev->priv;

	if (mFabricMgr == NULL) {
		veth_error_printk("NULL fabric manager with active ports!\n");
		netif_stop_queue(dev);
		return 1;
	}

	mb();

	if ((*frame & 0x01) != 0x01) {	/* broadcast or multicast */
		if ((remoteLp != mFabricMgr->mThisLp) && (HvLpConfig_doLpsCommunicateOnVirtualLan(mFabricMgr->mThisLp, remoteLp)))
			rc = veth_pTransmit(skb, remoteLp, dev);
		else
		{
		        dev_kfree_skb(skb);
			rc = 0;
		}
	} else {
		for (i = 0; i < HvMaxArchitectedLps; ++i) {
			if (i != mFabricMgr->mThisLp) {
				if (HvLpConfig_doLpsCommunicateOnVirtualLan(mFabricMgr->mThisLp, i)) {
				        struct sk_buff *clone = skb_clone(skb, GFP_ATOMIC);

					if(clone == NULL) {
						veth_error_printk("skb_clone failed %p\n", skb);
						rc = 0;
						break;
					}

					/* the ack handles deleting the skb */
					individual_rc = veth_pTransmit(clone, i, dev);

					/* tx failed, we need to free the sbk */
					if (individual_rc != 0)
					    dev_kfree_skb(clone);

					/* if we didn't fail from lack of buffers, the tx as a whole is successful */
					if(individual_rc != 1)
					    rc = 0;
				}
			}
		}

                /* broadcast/multicast - If every connection is out of buffers (highly unlikely) then we leave rc
                   set to  1 and stop the queue. If any connection fails for any reason other than out of buffers,
                   then we say the tx succeeded. 
                */ 
		if(rc == 0)
		    dev_kfree_skb(skb);
	}

	if (rc != 0)
	{
	    if (rc == 1) {
		/* reasons for stopping the queue:
		     - a non broadcast/multicast packet was destined for a connection that is out of buffers
                     - a broadcast/multicast packet and every connection was out of buffers
                */ 
		  
		int number = dev->dev_addr[3];
		++veth_dev_queue_stopped[number];
		netif_stop_queue(dev);
	    } else {
		/* reasons for not stopping the queue:
		     - a non broadcast/multicast packet was destined for a failed connection
		     - a broadcast/multicast packet and at least one connection had available buffers
                */
		dev_kfree_skb(skb);
		rc = 0;
	    }
	}
	else
	{
	    port->mStats.tx_packets++;
	    port->mStats.tx_bytes += skb_len;
	}

	return rc;
}

static int veth_pTransmit(struct sk_buff *skb, HvLpIndex remoteLp, struct net_device *dev)
{
	struct VethLpConnection *connection = mFabricMgr->mConnection + remoteLp;
	HvLpEvent_Rc returnCode;
	u32 dma_address, dma_length;
	int transmit_rc = 0;

	if (connection->mConnectionStatus.mFailed != 1) {
		struct VethMsg *msg = NULL;
		VETHSTACKPOP(&(connection->mMsgStack), msg);

		if (msg != NULL && ((skb->len - 14) <= 9000)) {
		        dma_length = skb->len;
		        dma_address = pci_map_single(iSeries_veth_dev, skb->data, dma_length, PCI_DMA_TODEVICE);

			/* Is it really necessary to check the length and address fields of the 
			   first entry here? */
			if (dma_address != NO_TCE) {
			        msg->mSkb = skb;
			        msg->mEvent.mSendData.mAddress[0] = dma_address;
			        msg->mEvent.mSendData.mLength[0]  = dma_length;
			        msg->mEvent.mSendData.mEofMask = 1;
			        set_bit(0, &(msg->mInUse));
			        returnCode = HvCallEvent_signalLpEventFast(remoteLp,
									   HvLpEvent_Type_VirtualLan,
									   VethEventTypeFrames,
									   HvLpEvent_AckInd_NoAck,
									   HvLpEvent_AckType_ImmediateAck,
									   connection->mSourceInst,
									   connection->mTargetInst,
									   msg->mIndex,
									   msg->mEvent.mFpData.mData1,
									   msg->mEvent.mFpData.mData2,
									   msg->mEvent.mFpData.mData3,
									   msg->mEvent.mFpData.mData4,
									   msg->mEvent.mFpData.mData5);
			} else {
			        struct VethPort *port = (struct VethPort *) dev->priv;
			        returnCode = -1;	/* Bad return code */
			        port->mStats.tx_errors++;
			}

			if (returnCode != HvLpEvent_Rc_Good) {
			        msg->mSkb = NULL;
				/* need to set in use to make veth_recycleMsg in case this was a mapping failure */
				set_bit(0, &(msg->mInUse));
			        veth_recycleMsg(connection, msg);
			        transmit_rc = 2;
			} else {
			        transmit_rc = 0;
			}
		} else {
		        if (msg != NULL) {
			        veth_recycleMsg(connection, msg);
			        transmit_rc = 2;
			} else {
			        transmit_rc = 1;
			}
		}
	} else {
		transmit_rc = 2;
	}

	return transmit_rc;
}

static int veth_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
#ifdef SIOCETHTOOL
    struct ethtool_cmd ecmd;

    if (cmd != SIOCETHTOOL)
	return -EOPNOTSUPP;
    if (copy_from_user(&ecmd, ifr->ifr_data, sizeof(ecmd)))
	return -EFAULT;
    switch (ecmd.cmd) {
	case ETHTOOL_GSET:
	    ecmd.supported =
	      (SUPPORTED_1000baseT_Full |
	       SUPPORTED_Autoneg | SUPPORTED_FIBRE);
	    ecmd.advertising =
	      (SUPPORTED_1000baseT_Full |
	       SUPPORTED_Autoneg | SUPPORTED_FIBRE);

	    ecmd.port = PORT_FIBRE;
	    ecmd.transceiver = XCVR_INTERNAL;
	    ecmd.phy_address = 0;
	    ecmd.speed = SPEED_1000;
	    ecmd.duplex = DUPLEX_FULL;
	    ecmd.autoneg = AUTONEG_ENABLE;
	    ecmd.maxtxpkt = 120;
	    ecmd.maxrxpkt = 120;
	    if(copy_to_user(ifr->ifr_data, &ecmd, sizeof(ecmd)))
		return -EFAULT;
	    return 0;

	case ETHTOOL_GDRVINFO: {
		struct ethtool_drvinfo info = {ETHTOOL_GDRVINFO};
		strncpy(info.driver, "veth", sizeof(info.driver) - 1);
		info.driver[sizeof(info.driver) - 1] = '\0';
		strncpy(info.version, "1.0", sizeof(info.version) - 1);
		if (copy_to_user(ifr->ifr_data, &info, sizeof(info)))
		    return -EFAULT;
		return 0;
	    }
	/* get link status */
	case ETHTOOL_GLINK: {
		struct ethtool_value edata = {ETHTOOL_GLINK};
		edata.data = 1;
		if (copy_to_user(ifr->ifr_data, &edata, sizeof(edata)))
		    return -EFAULT;
		return 0;
	    }

	default:
	    break;
    }

#endif
	return -EOPNOTSUPP;
}

static void veth_set_multicast_list(struct net_device *dev)
{
	char *addrs;
	struct VethPort *port = (struct VethPort *) dev->priv;
	u64 newAddress = 0;
	unsigned long flags;

	write_lock_irqsave(&port->mMcastGate, flags);

	if (dev->flags & IFF_PROMISC) {	/* set promiscuous mode */
		port->mPromiscuous = 1;
	} else {
		struct dev_mc_list *dmi = dev->mc_list;

		if ((dev->flags & IFF_ALLMULTI) || (dev->mc_count > 12)) {
			port->mAllMcast = 1;
		} else {
			int i;
			/* Update table */
			port->mNumAddrs = 0;

			for (i = 0; ((i < dev->mc_count) && (i < 12)); i++) {	/* for each address in the list */
				addrs = dmi->dmi_addr;
				dmi = dmi->next;
				if ((*addrs & 0x01) == 1) {	/* multicast address? */
					memcpy(&newAddress, addrs, 6);
					newAddress &= 0xFFFFFFFFFFFF0000;

					port->mMcasts[port->mNumAddrs] = newAddress;
					mb();
					port->mNumAddrs = port->mNumAddrs + 1;
				}
			}
		}
	}

	write_unlock_irqrestore(&port->mMcastGate, flags);
}


static void veth_handleEvent(struct HvLpEvent *event, struct pt_regs *regs)
{
	if (event->xFlags.xFunction == HvLpEvent_Function_Ack) {
		veth_handleAck(event);
	} else if (event->xFlags.xFunction == HvLpEvent_Function_Int) {
		veth_handleInt(event);
	}
}

static void veth_handleAck(struct HvLpEvent *event)
{
	struct VethLpConnection *connection = &(mFabricMgr->mConnection[event->xTargetLp]);
	struct VethLpEvent *vethEvent = (struct VethLpEvent *) event;

	switch (event->xSubtype) {
	case VethEventTypeCap:
		{
			veth_takeCapAck(connection, vethEvent);
			break;
		}
	case VethEventTypeMonitor:
		{
			veth_takeMonitorAck(connection, vethEvent);
			break;
		}
	default:
		{
			veth_error_printk("Unknown ack type %d from lpar %d\n", event->xSubtype, connection->mRemoteLp);
		}
	};
}

static void veth_handleInt(struct HvLpEvent *event)
{
	int i = 0;
	struct VethLpConnection *connection = &(mFabricMgr->mConnection[event->xSourceLp]);
	struct VethLpEvent *vethEvent = (struct VethLpEvent *) event;

	switch (event->xSubtype) {
	case VethEventTypeCap:
		{
			veth_takeCap(connection, vethEvent);
			break;
		}
	case VethEventTypeMonitor:
		{
			/* do nothing... this'll hang out here til we're dead, and the hypervisor will return it for us. */
			break;
		}
	case VethEventTypeFramesAck:
		{
			for (i = 0; i < VethMaxFramesMsgsAcked; ++i) {
				u16 msg = vethEvent->mDerivedData.mFramesAckData.mToken[i];
				veth_recycleMsgByNum(connection, msg);
			}
			break;
		}
	case VethEventTypeFrames:
		{
			veth_takeFrames(connection, vethEvent);
			break;
		}
	default:
		{
			veth_error_printk("Unknown interrupt type %d from lpar %d\n", event->xSubtype, connection->mRemoteLp);
		}
	};
}

static void veth_openConnections()
{
	int i = 0;

	HvLpEvent_registerHandler(HvLpEvent_Type_VirtualLan, &veth_handleEvent);

	/* Now I need to run through the active lps and open connections to the ones I'm supposed to
	   open to. */

	for (i = HvMaxArchitectedLps - 1; i >= 0; --i) {
		if (i != mFabricMgr->mThisLp) {
    		        struct VethLpConnection *connection = &(mFabricMgr->mConnection[i]);
		        unsigned long flags;

			if (HvLpConfig_doLpsCommunicateOnVirtualLan(mFabricMgr->mThisLp, i)) {
			        spin_lock_irqsave(&connection->mStatusGate, flags);
			        veth_openConnection(i);
			        spin_unlock_irqrestore(&connection->mStatusGate, flags);
			} else {
			        spin_lock_irqsave(&connection->mStatusGate, flags);
			        veth_closeConnection(i);
			        spin_unlock_irqrestore(&connection->mStatusGate, flags);
			}
		}
	}
}

static void veth_intFinishOpeningConnections(void *parm, int number)
{
	struct VethLpConnection *connection = (struct VethLpConnection *) parm;
	connection->mAllocTaskTq.data = parm;
	connection->mNumberAllocated = number;
	atomic_dec(&pending_callbacks);
	if (!mFabricMgr)
		return;
	schedule_task(&connection->mAllocTaskTq);
}

static void veth_finishOpeningConnections(void *parm)
{
	unsigned long flags;
	struct VethLpConnection *connection = (struct VethLpConnection *) parm;
	spin_lock_irqsave(&connection->mStatusGate, flags);
	veth_finishOpeningConnectionsLocked(connection);
	spin_unlock_irqrestore(&connection->mStatusGate, flags);
}

static void veth_finishOpeningConnectionsLocked(struct VethLpConnection *connection)
{
	if (connection->mNumberAllocated >= 2) {
		connection->mConnectionStatus.mCapMonAlloced = 1;
		veth_sendCap(connection);
	} else {
		veth_error_printk("Couldn't allocate base msgs for lpar %d, only got %d\n", connection->mRemoteLp,
				  connection->mNumberAllocated);
		veth_failMe(connection);
	}
}

static void veth_openConnection(u8 remoteLp)
{
	unsigned long flags;
	HvLpInstanceId source;
	HvLpInstanceId target;
	u64 i = 0;
	struct VethLpConnection *connection = &(mFabricMgr->mConnection[remoteLp]);
	
	INIT_TQUEUE(&connection->mCapTaskTq, veth_capTask, NULL);
	INIT_TQUEUE(&connection->mCapAckTaskTq, veth_capAckTask, NULL);
	INIT_TQUEUE(&connection->mMonitorAckTaskTq, veth_monitorAckTask, NULL);
	INIT_TQUEUE(&connection->mAllocTaskTq, veth_finishOpeningConnections, NULL);

	connection->mRemoteLp = remoteLp;

	spin_lock_irqsave(&connection->mAckGate, flags);

	memset(&connection->mEventData, 0xFF, sizeof(connection->mEventData));
	connection->mNumAcks = 0;

	HvCallEvent_openLpEventPath(remoteLp, HvLpEvent_Type_VirtualLan);

	/* clean up non-acked msgs */
	for (i = 0; i < connection->mNumMsgs; ++i) {
		veth_recycleMsgByNum(connection, i);
	}

	connection->mConnectionStatus.mOpen = 1;

	source = connection->mSourceInst = HvCallEvent_getSourceLpInstanceId(remoteLp, HvLpEvent_Type_VirtualLan);
	target = connection->mTargetInst = HvCallEvent_getTargetLpInstanceId(remoteLp, HvLpEvent_Type_VirtualLan);

	if (connection->mConnectionStatus.mCapMonAlloced != 1) {
		connection->mAllocTaskTq.routine = (void *) (void *) veth_finishOpeningConnections;
		atomic_inc(&pending_callbacks);
		mf_allocateLpEvents(remoteLp,
				    HvLpEvent_Type_VirtualLan,
				    sizeof(struct VethLpEvent), 2, &veth_intFinishOpeningConnections, connection);
	} else {
		veth_finishOpeningConnectionsLocked(connection);
	}

	spin_unlock_irqrestore(&connection->mAckGate, flags);
}

static void veth_closeConnection(u8 remoteLp)
{
	struct VethLpConnection *connection = &(mFabricMgr->mConnection[remoteLp]);
	unsigned long flags;

	del_timer_sync(&connection->mAckTimer);

	if (connection->mConnectionStatus.mOpen == 1) {
		HvCallEvent_closeLpEventPath(remoteLp, HvLpEvent_Type_VirtualLan);
		connection->mConnectionStatus.mOpen = 0;
		veth_failMe(connection);

		/* reset ack data */
		spin_lock_irqsave(&connection->mAckGate, flags);

		memset(&connection->mEventData, 0xFF, sizeof(connection->mEventData));
		connection->mNumAcks = 0;

		spin_unlock_irqrestore(&connection->mAckGate, flags);
	}

}

static void veth_msgsInit(struct VethLpConnection *connection)
{
	connection->mAllocTaskTq.routine = (void *) (void *) veth_finishMsgsInit;
	atomic_inc(&pending_callbacks);
	mf_allocateLpEvents(connection->mRemoteLp,
			    HvLpEvent_Type_VirtualLan,
			    sizeof(struct VethLpEvent),
			    connection->mMyCap.mUnionData.mFields.mNumberBuffers, &veth_intFinishMsgsInit, connection);
}

static void veth_intFinishMsgsInit(void *parm, int number)
{
	struct VethLpConnection *connection = (struct VethLpConnection *) parm;
	connection->mAllocTaskTq.data = parm;
	connection->mNumberRcvMsgs = number;
	atomic_dec(&pending_callbacks);
	if (!mFabricMgr)
		return;
	schedule_task(&connection->mAllocTaskTq);
}

static void veth_intFinishCapTask(void *parm, int number)
{
	struct VethLpConnection *connection = (struct VethLpConnection *) parm;
	connection->mAllocTaskTq.data = parm;
	if (number > 0)
		connection->mNumberLpAcksAlloced += number;
	atomic_dec(&pending_callbacks);
	if (!mFabricMgr)
		return;
	schedule_task(&connection->mAllocTaskTq);
}

static void veth_finishMsgsInit(struct VethLpConnection *connection)
{
	int i = 0;
	unsigned int numberGotten = 0;
	u64 amountOfHeapToGet = connection->mMyCap.mUnionData.mFields.mNumberBuffers * sizeof(struct VethMsg);
	char *msgs = NULL;
	unsigned long flags;
	spin_lock_irqsave(&connection->mStatusGate, flags);

	if (connection->mNumberRcvMsgs >= connection->mMyCap.mUnionData.mFields.mNumberBuffers) {
		msgs = kmalloc(amountOfHeapToGet, GFP_ATOMIC);

		connection->mMsgs = (struct VethMsg *) msgs;

		if (msgs != NULL) {
			memset(msgs, 0, amountOfHeapToGet);

			for (i = 0; i < connection->mMyCap.mUnionData.mFields.mNumberBuffers; ++i) {
				connection->mMsgs[i].mIndex = i;
				++numberGotten;
				VETHSTACKPUSH(&(connection->mMsgStack), (connection->mMsgs + i));
			}
			if (numberGotten > 0) {
				connection->mNumMsgs = numberGotten;
			}
		} else {
			connection->mMsgs = NULL;
		}
	}

	connection->mMyCap.mUnionData.mFields.mNumberBuffers = connection->mNumMsgs;

	if (connection->mNumMsgs < 10)
		connection->mMyCap.mUnionData.mFields.mThreshold = 1;
	else if (connection->mNumMsgs < 20)
		connection->mMyCap.mUnionData.mFields.mThreshold = 4;
	else if (connection->mNumMsgs < 40)
		connection->mMyCap.mUnionData.mFields.mThreshold = 10;
	else
		connection->mMyCap.mUnionData.mFields.mThreshold = 20;

	connection->mMyCap.mUnionData.mFields.mTimer = VethAckTimeoutUsec;

	veth_finishSendCap(connection);

	spin_unlock_irqrestore(&connection->mStatusGate, flags);
}

static void veth_sendCap(struct VethLpConnection *connection)
{
	if (connection->mMsgs == NULL) {
		connection->mMyCap.mUnionData.mFields.mNumberBuffers = VethBuffersToAllocate;
		veth_msgsInit(connection);
	} else {
		veth_finishSendCap(connection);
	}
}

static void veth_finishSendCap(struct VethLpConnection *connection)
{
	HvLpEvent_Rc returnCode = HvCallEvent_signalLpEventFast(connection->mRemoteLp,
								HvLpEvent_Type_VirtualLan,
								VethEventTypeCap,
								HvLpEvent_AckInd_DoAck,
								HvLpEvent_AckType_ImmediateAck,
								connection->mSourceInst,
								connection->mTargetInst,
								0,
								connection->mMyCap.mUnionData.mNoFields.mReserved1,
								connection->mMyCap.mUnionData.mNoFields.mReserved2,
								connection->mMyCap.mUnionData.mNoFields.mReserved3,
								connection->mMyCap.mUnionData.mNoFields.mReserved4,
								connection->mMyCap.mUnionData.mNoFields.mReserved5);

	if ((returnCode == HvLpEvent_Rc_PartitionDead) || (returnCode == HvLpEvent_Rc_PathClosed)) {
		connection->mConnectionStatus.mSentCap = 0;
	} else if (returnCode != HvLpEvent_Rc_Good) {
		veth_error_printk("Couldn't send cap to lpar %d, rc %x\n", connection->mRemoteLp, (int) returnCode);
		veth_failMe(connection);
	} else {
		connection->mConnectionStatus.mSentCap = 1;
	}
}

static void veth_takeCap(struct VethLpConnection *connection, struct VethLpEvent *event)
{
	if (!test_and_set_bit(0, &(connection->mCapTaskPending))) {
		connection->mCapTaskTq.data = connection;
		memcpy(&connection->mCapEvent, event, sizeof(connection->mCapEvent));
		schedule_task(&connection->mCapTaskTq);
	} else {
		veth_error_printk("Received a capabilities from lpar %d while already processing one\n", connection->mRemoteLp);
		event->mBaseEvent.xRc = HvLpEvent_Rc_BufferNotAvailable;
		HvCallEvent_ackLpEvent((struct HvLpEvent *) event);
	}
}

static void veth_takeCapAck(struct VethLpConnection *connection, struct VethLpEvent *event)
{
	if (!test_and_set_bit(0, &(connection->mCapAckTaskPending))) {
		connection->mCapAckTaskTq.data = connection;
		memcpy(&connection->mCapAckEvent, event, sizeof(connection->mCapAckEvent));
		schedule_task(&connection->mCapAckTaskTq);
	} else {
		veth_error_printk("Received a capabilities ack from lpar %d while already processing one\n",
				  connection->mRemoteLp);
	}
}

static void veth_takeMonitorAck(struct VethLpConnection *connection, struct VethLpEvent *event)
{
	if (!test_and_set_bit(0, &(connection->mMonitorAckTaskPending))) {
		connection->mMonitorAckTaskTq.data = connection;
		memcpy(&connection->mMonitorAckEvent, event, sizeof(connection->mMonitorAckEvent));
		schedule_task(&connection->mMonitorAckTaskTq);
	} else {
		veth_error_printk("Received a monitor ack from lpar %d while already processing one\n", connection->mRemoteLp);
	}
}

static void veth_recycleMsgByNum(struct VethLpConnection *connection, u16 msg)
{
	if (msg < connection->mNumMsgs) {
		struct VethMsg *myMsg = connection->mMsgs + msg;
		veth_recycleMsg(connection, myMsg);
	}
}

static void veth_recycleMsg(struct VethLpConnection *connection, struct VethMsg *myMsg)
{
	u32 dma_address, dma_length;

	if (test_and_clear_bit(0, &(myMsg->mInUse))) {
	        dma_address = myMsg->mEvent.mSendData.mAddress[0];
	        dma_length = myMsg->mEvent.mSendData.mLength[0];
	    
	        pci_unmap_single(iSeries_veth_dev, dma_address, dma_length, PCI_DMA_TODEVICE);

	        if (myMsg->mSkb != NULL) {
		    dev_kfree_skb_any(myMsg->mSkb);
		    myMsg->mSkb = NULL;
	        }

	        memset(&(myMsg->mEvent.mSendData), 0, sizeof(struct VethFramesData));
	        VETHSTACKPUSH(&connection->mMsgStack, myMsg);
	} else {
	        if (connection->mConnectionStatus.mOpen) {
		        veth_error_printk("Received a frames ack for msg %d from lpar %d while not outstanding\n", myMsg->mIndex,
				  connection->mRemoteLp);
		}
	}

	veth_startQueues();
}

static void veth_startQueues(void)
{
    int i;
    for (i=0; i < veth_num_devices; ++i)
    {
	netif_wake_queue(veth_devices[i]);
    }
}

static void veth_capTask(void *parm)
{
        struct VethLpConnection *connection = (struct VethLpConnection*)parm;
	struct VethLpEvent *event = &connection->mCapEvent;
	unsigned long flags;
	struct VethCapData *remoteCap = &(connection->mRemoteCap);
	u64 numAcks = 0;
	spin_lock_irqsave(&connection->mStatusGate, flags);
	connection->mConnectionStatus.mGotCap = 1;

	memcpy(remoteCap, &(event->mDerivedData.mCapabilitiesData), sizeof(connection->mRemoteCap));

	if ((remoteCap->mUnionData.mFields.mNumberBuffers <= VethMaxFramesMsgs) &&
	    (remoteCap->mUnionData.mFields.mNumberBuffers != 0) &&
	    (remoteCap->mUnionData.mFields.mThreshold <= VethMaxFramesMsgsAcked) &&
	    (remoteCap->mUnionData.mFields.mThreshold != 0)) {
		numAcks = (remoteCap->mUnionData.mFields.mNumberBuffers / remoteCap->mUnionData.mFields.mThreshold) + 1;

		if (connection->mNumberLpAcksAlloced < numAcks) {
			numAcks = numAcks - connection->mNumberLpAcksAlloced;
			connection->mAllocTaskTq.routine = (void *) (void *) veth_finishCapTask;
			atomic_inc(&pending_callbacks);
			mf_allocateLpEvents(connection->mRemoteLp,
					    HvLpEvent_Type_VirtualLan,
					    sizeof(struct VethLpEvent), numAcks, &veth_intFinishCapTask, connection);
		} else
			veth_finishCapTaskLocked(connection);
	} else {
		veth_error_printk("Received incompatible capabilities from lpar %d\n", connection->mRemoteLp);
		event->mBaseEvent.xRc = HvLpEvent_Rc_InvalidSubtypeData;
		HvCallEvent_ackLpEvent((struct HvLpEvent *) event);
	}

	clear_bit(0, &(connection->mCapTaskPending));
	spin_unlock_irqrestore(&connection->mStatusGate, flags);
}

static void veth_capAckTask(void *parm)
{
        struct VethLpConnection *connection = (struct VethLpConnection*)parm;
	struct VethLpEvent *event = &connection->mCapAckEvent;
	unsigned long flags;

	spin_lock_irqsave(&connection->mStatusGate, flags);

	if (event->mBaseEvent.xRc == HvLpEvent_Rc_Good) {
		connection->mConnectionStatus.mCapAcked = 1;

		if ((connection->mConnectionStatus.mGotCap == 1) && (connection->mConnectionStatus.mGotCapAcked == 1)) {
			if (connection->mConnectionStatus.mSentMonitor != 1)
				veth_sendMonitor(connection);
		}
	} else {
		veth_printk("Bad rc(%d) from lpar %d on capabilities\n", event->mBaseEvent.xRc, connection->mRemoteLp);
		veth_failMe(connection);
	}

	clear_bit(0, &(connection->mCapAckTaskPending));
	spin_unlock_irqrestore(&connection->mStatusGate, flags);
}

static void veth_monitorAckTask(void *parm)
{
        struct VethLpConnection *connection = (struct VethLpConnection*)parm;
	unsigned long flags;

	spin_lock_irqsave(&connection->mStatusGate, flags);

	veth_failMe(connection);

	veth_printk("Monitor ack returned for lpar %d\n", connection->mRemoteLp);

	if (connection->mConnectionStatus.mOpen) {
		veth_closeConnection(connection->mRemoteLp);

		udelay(100);
		
		schedule_task(&connection->mMonitorAckTaskTq);
	} else {
		if (VethModuleReopen)
			veth_openConnection(connection->mRemoteLp);
		else {
			int i = 0;

			for (i = 0; i < connection->mNumMsgs; ++i) {
				veth_recycleMsgByNum(connection, i);
			}
		}
		clear_bit(0, &(connection->mMonitorAckTaskPending));
	}

	spin_unlock_irqrestore(&connection->mStatusGate, flags);
}

#define number_of_pages(v, l) ((((unsigned long)(v) & ((1 << 12) - 1)) + (l) + 4096 - 1) / 4096)
#define page_offset(v) ((unsigned long)(v) & ((1 << 12) - 1))

static void veth_takeFrames(struct VethLpConnection *connection, struct VethLpEvent *event)
{
	int i = 0;
	struct VethPort *port = NULL;
	struct BufList {
		union {
			struct {
				u32 token2;
				u32 garbage;
			} token1;
			u64 address;
		} addr;
		u64 size;
	};

	struct BufList myBufList[4];	/* max pages per frame */
	struct BufList remoteList[VethMaxFramesPerMsg];	/* max frags per frame */
	memset(myBufList, 0, sizeof(struct BufList) * 4);
	memset(remoteList, 0, sizeof(struct BufList) * VethMaxFramesPerMsg);

	do {
		int nfrags = 0;
		u16 length = 0;

		/* a 0 address marks the end of the valid entries */
		if (event->mDerivedData.mSendData.mAddress[i] == 0)
			break;

		/* make sure that we have at least 1 EOF entry in the remaining entries */
		if (!(event->mDerivedData.mSendData.mEofMask >> i)) {
			veth_printk("bad lp event: missing EOF frag in event mEofMask 0x%x i %d\n",
				    event->mDerivedData.mSendData.mEofMask, i);
			break;
		}

		/* add up length of non-EOF frags */
		do {
			remoteList[nfrags].addr.token1.token2 = event->mDerivedData.mSendData.mAddress[i + nfrags];
			remoteList[nfrags].addr.token1.garbage = 0;
			length += remoteList[nfrags].size = event->mDerivedData.mSendData.mLength[i + nfrags];
		}
		while (!(event->mDerivedData.mSendData.mEofMask & (1 << (i + nfrags++))));


		/* length == total length of all framgents */
		/* nfrags == # of fragments in this frame */

		if ((length - 14) <= 9000) {	/* save as 13 < length <= 9014 */
			struct sk_buff *skb = alloc_skb(length, GFP_ATOMIC);
			if (skb != NULL) {
				HvLpDma_Rc returnCode = HvLpDma_Rc_Good;

				/* build the buffer list for the dma operation */
				int numPages = number_of_pages((skb->data), length);	/* number of pages in this fragment of the complete buffer */
				myBufList[0].addr.address =
				    (0x8000000000000000LL | (virt_to_absolute((unsigned long) skb->data)));
				myBufList[0].size = (numPages > 1) ? (4096 - page_offset(skb->data)) : length;
				if (numPages > 1) {
					myBufList[1].addr.address =
					    (0x8000000000000000LL |
					     (virt_to_absolute((unsigned long) skb->data + myBufList[0].size)));
					myBufList[1].size = (numPages > 2) ? 4096 : length - myBufList[0].size;
					if (numPages > 2) {
						myBufList[2].addr.address =
						    (0x8000000000000000LL |
						     (virt_to_absolute
						      ((unsigned long) skb->data + myBufList[0].size + myBufList[1].size)));
						myBufList[2].size =
						    (numPages > 3) ? 4096 : length - myBufList[0].size - myBufList[1].size;
						if (numPages > 3) {
							myBufList[3].addr.address =
							    0x8000000000000000LL |
							    (virt_to_absolute
							     ((unsigned long) skb->data + myBufList[0].size + myBufList[1].size +
							      myBufList[2].size));
							myBufList[3].size =
							    length - myBufList[0].size - myBufList[1].size - myBufList[2].size;
						}
					}
				}
				returnCode = HvCallEvent_dmaBufList(HvLpEvent_Type_VirtualLan,
								    event->mBaseEvent.xSourceLp,
								    HvLpDma_Direction_RemoteToLocal,
								    connection->mSourceInst,
								    connection->mTargetInst,
								    HvLpDma_AddressType_RealAddress,
								    HvLpDma_AddressType_TceIndex,
								    0x8000000000000000LL |
								    (virt_to_absolute((unsigned long) &myBufList)),
								    0x8000000000000000LL |
								    (virt_to_absolute((unsigned long) &remoteList)), length);

				if (returnCode == HvLpDma_Rc_Good) {
					HvLpVirtualLanIndex vlan = skb->data[9];
					u64 dest = *((u64 *) skb->data) & 0xFFFFFFFFFFFF0000;

					if (((vlan < HvMaxArchitectedVirtualLans) && ((port = mFabricMgr->mPorts[vlan]) != NULL)) && ((dest == port->mMyAddress) ||	/* it's for me */
																      (dest == 0xFFFFFFFFFFFF0000) ||	/* it's a broadcast */
																      (veth_multicast_wanted(port, dest)) ||	/* it's one of my multicasts */
																      (port->mPromiscuous == 1))) {	/* I'm promiscuous */
						skb_put(skb, length);
						skb->dev = port->mDev;
						skb->protocol = eth_type_trans(skb, port->mDev);
						skb->ip_summed = CHECKSUM_NONE;
						netif_rx(skb);	/* send it up */
						port->mStats.rx_packets++;
						port->mStats.rx_bytes += length;

					} else {
						dev_kfree_skb_irq(skb);
					}
				} else {
					dev_kfree_skb_irq(skb);
				}
			}
		} else {
			break;
		}
		i += nfrags;
	} while (i < VethMaxFramesPerMsg);

	/* Ack it */

	{
		unsigned long flags;
		spin_lock_irqsave(&connection->mAckGate, flags);

		if (connection->mNumAcks < VethMaxFramesMsgsAcked) {
			connection->mEventData.mAckData.mToken[connection->mNumAcks] = event->mBaseEvent.xCorrelationToken;
			++connection->mNumAcks;

			if (connection->mNumAcks == connection->mRemoteCap.mUnionData.mFields.mThreshold) {
				HvLpEvent_Rc rc = HvCallEvent_signalLpEventFast(connection->mRemoteLp,
										HvLpEvent_Type_VirtualLan,
										VethEventTypeFramesAck,
										HvLpEvent_AckInd_NoAck,
										HvLpEvent_AckType_ImmediateAck,
										connection->mSourceInst,
										connection->mTargetInst,
										0,
										connection->mEventData.mFpData.mData1,
										connection->mEventData.mFpData.mData2,
										connection->mEventData.mFpData.mData3,
										connection->mEventData.mFpData.mData4,
										connection->mEventData.mFpData.mData5);

				if (rc != HvLpEvent_Rc_Good) {
					veth_error_printk("Bad lp event return code(%x) acking frames from lpar %d\n", (int) rc,
							  connection->mRemoteLp);
				}

				connection->mNumAcks = 0;

				memset(&connection->mEventData, 0xFF, sizeof(connection->mEventData));
			}

		}

		spin_unlock_irqrestore(&connection->mAckGate, flags);
	}
}

#undef number_of_pages
#undef page_offset

static void veth_timedAck(unsigned long connectionPtr)
{
	unsigned long flags;
	HvLpEvent_Rc rc;
	struct VethLpConnection *connection = (struct VethLpConnection *) connectionPtr;
	/* Ack all the events */
	spin_lock_irqsave(&connection->mAckGate, flags);

	if (connection->mNumAcks > 0) {
		rc = HvCallEvent_signalLpEventFast(connection->mRemoteLp,
						   HvLpEvent_Type_VirtualLan,
						   VethEventTypeFramesAck,
						   HvLpEvent_AckInd_NoAck,
						   HvLpEvent_AckType_ImmediateAck,
						   connection->mSourceInst,
						   connection->mTargetInst,
						   0,
						   connection->mEventData.mFpData.mData1,
						   connection->mEventData.mFpData.mData2,
						   connection->mEventData.mFpData.mData3,
						   connection->mEventData.mFpData.mData4, connection->mEventData.mFpData.mData5);

		if (rc != HvLpEvent_Rc_Good) {
			veth_error_printk("Bad lp event return code(%x) acking frames from lpar %d!\n", (int) rc,
					  connection->mRemoteLp);
		}

		connection->mNumAcks = 0;

		memset(&connection->mEventData, 0xFF, sizeof(connection->mEventData));
	}

	spin_unlock_irqrestore(&connection->mAckGate, flags);

	veth_startQueues();

	/* Reschedule the timer */
	connection->mAckTimer.expires = jiffies + connection->mTimeout;
	add_timer(&connection->mAckTimer);
}

static int veth_multicast_wanted(struct VethPort *port, u64 thatAddr)
{
	int returnParm = 0;
	int i;
	unsigned long flags;

	if ((*((char *) &thatAddr) & 0x01) != 1)
		return 0;

	read_lock_irqsave(&port->mMcastGate, flags);
	if (port->mAllMcast) {
		read_unlock_irqrestore(&port->mMcastGate, flags);
		return 1;
	}

	for (i = 0; i < port->mNumAddrs; ++i) {
		u64 thisAddr = port->mMcasts[i];

		if (thisAddr == thatAddr) {
			returnParm = 1;
			break;
		}
	}
	read_unlock_irqrestore(&port->mMcastGate, flags);

	return returnParm;
}

static void veth_sendMonitor(struct VethLpConnection *connection)
{
	HvLpEvent_Rc returnCode = HvCallEvent_signalLpEventFast(connection->mRemoteLp,
								HvLpEvent_Type_VirtualLan,
								VethEventTypeMonitor,
								HvLpEvent_AckInd_DoAck,
								HvLpEvent_AckType_DeferredAck,
								connection->mSourceInst,
								connection->mTargetInst,
								0, 0, 0, 0, 0, 0);

	if (returnCode == HvLpEvent_Rc_Good) {
		connection->mConnectionStatus.mSentMonitor = 1;
		connection->mConnectionStatus.mFailed = 0;

		/* Start the ACK timer */
		init_timer(&connection->mAckTimer);
		connection->mAckTimer.function = veth_timedAck;
		connection->mAckTimer.data = (unsigned long) connection;
		connection->mAckTimer.expires = jiffies + connection->mTimeout;
		add_timer(&connection->mAckTimer);

	} else {
		veth_error_printk("Monitor send to lpar %d failed with rc %x\n", connection->mRemoteLp, (int) returnCode);
		veth_failMe(connection);
	}
}

static void veth_finishCapTask(struct VethLpConnection *connection)
{
	unsigned long flags;
	spin_lock_irqsave(&connection->mStatusGate, flags);
	veth_finishCapTaskLocked(connection);
	spin_unlock_irqrestore(&connection->mStatusGate, flags);
}

static void veth_finishCapTaskLocked(struct VethLpConnection *connection)
{
	struct VethLpEvent *event = &connection->mCapEvent;
	struct VethCapData *remoteCap = &(connection->mRemoteCap);
	int numAcks = (remoteCap->mUnionData.mFields.mNumberBuffers / remoteCap->mUnionData.mFields.mThreshold) + 1;

	/* Convert timer to jiffies */
	if (connection->mMyCap.mUnionData.mFields.mTimer)
		connection->mTimeout = remoteCap->mUnionData.mFields.mTimer * HZ / 1000000;
	else
		connection->mTimeout = VethAckTimeoutUsec * HZ / 1000000;

	if (connection->mNumberLpAcksAlloced >= numAcks) {
		HvLpEvent_Rc returnCode = HvCallEvent_ackLpEvent((struct HvLpEvent *) event);

		if (returnCode == HvLpEvent_Rc_Good) {
			connection->mConnectionStatus.mGotCapAcked = 1;

			if (connection->mConnectionStatus.mSentCap != 1) {
				connection->mTargetInst =
				    HvCallEvent_getTargetLpInstanceId(connection->mRemoteLp, HvLpEvent_Type_VirtualLan);

				veth_sendCap(connection);
			} else if (connection->mConnectionStatus.mCapAcked == 1) {
				if (connection->mConnectionStatus.mSentMonitor != 1)
					veth_sendMonitor(connection);
			}
		} else {
			veth_error_printk("Failed to ack remote cap for lpar %d with rc %x\n", connection->mRemoteLp,
					  (int) returnCode);
			veth_failMe(connection);
		}
	} else {
		veth_error_printk("Couldn't allocate all the frames ack events for lpar %d\n", connection->mRemoteLp);
		event->mBaseEvent.xRc = HvLpEvent_Rc_BufferNotAvailable;
		HvCallEvent_ackLpEvent((struct HvLpEvent *) event);
	}
}

int proc_veth_dump_connection(char *page, char **start, off_t off, int count, int *eof, void *data) {
	char *out = page;
	long whichConnection = (long) data;
	int len = 0;
	struct VethLpConnection *connection = NULL;

	if ((whichConnection < 0) || (whichConnection > HvMaxArchitectedLps) || (mFabricMgr == NULL)) {
		veth_error_printk("Got bad data from /proc file system\n");
		len = sprintf(page, "ERROR\n");
	} else {
		int thereWasStuffBefore = 0;
		connection = &(mFabricMgr->mConnection[whichConnection]);

		out += sprintf(out, "Remote Lp:\t%d\n", connection->mRemoteLp);
		out += sprintf(out, "Source Inst:\t%04X\n", connection->mSourceInst);
		out += sprintf(out, "Target Inst:\t%04X\n", connection->mTargetInst);
		out += sprintf(out, "Num Msgs:\t%d\n", connection->mNumMsgs);
		out += sprintf(out, "Num Lp Acks:\t%d\n", connection->mNumberLpAcksAlloced);
		out += sprintf(out, "Num Acks:\t%d\n", connection->mNumAcks);

		if (connection->mConnectionStatus.mOpen) {
			out += sprintf(out, "<Open");
			thereWasStuffBefore = 1;
		}

		if (connection->mConnectionStatus.mCapMonAlloced) {
			if (thereWasStuffBefore)
				out += sprintf(out, "/");
			else
				out += sprintf(out, "<");
			out += sprintf(out, "CapMonAlloced");
			thereWasStuffBefore = 1;
		}

		if (connection->mConnectionStatus.mBaseMsgsAlloced) {
			if (thereWasStuffBefore)
				out += sprintf(out, "/");
			else
				out += sprintf(out, "<");
			out += sprintf(out, "BaseMsgsAlloced");
			thereWasStuffBefore = 1;
		}

		if (connection->mConnectionStatus.mSentCap) {
			if (thereWasStuffBefore)
				out += sprintf(out, "/");
			else
				out += sprintf(out, "<");
			out += sprintf(out, "SentCap");
			thereWasStuffBefore = 1;
		}

		if (connection->mConnectionStatus.mCapAcked) {
			if (thereWasStuffBefore)
				out += sprintf(out, "/");
			else
				out += sprintf(out, "<");
			out += sprintf(out, "CapAcked");
			thereWasStuffBefore = 1;
		}

		if (connection->mConnectionStatus.mGotCap) {
			if (thereWasStuffBefore)
				out += sprintf(out, "/");
			else
				out += sprintf(out, "<");
			out += sprintf(out, "GotCap");
			thereWasStuffBefore = 1;
		}

		if (connection->mConnectionStatus.mGotCapAcked) {
			if (thereWasStuffBefore)
				out += sprintf(out, "/");
			else
				out += sprintf(out, "<");
			out += sprintf(out, "GotCapAcked");
			thereWasStuffBefore = 1;
		}

		if (connection->mConnectionStatus.mSentMonitor) {
			if (thereWasStuffBefore)
				out += sprintf(out, "/");
			else
				out += sprintf(out, "<");
			out += sprintf(out, "SentMonitor");
			thereWasStuffBefore = 1;
		}

		if (connection->mConnectionStatus.mPopulatedRings) {
			if (thereWasStuffBefore)
				out += sprintf(out, "/");
			else
				out += sprintf(out, "<");
			out += sprintf(out, "PopulatedRings");
			thereWasStuffBefore = 1;
		}

		if (connection->mConnectionStatus.mFailed) {
			if (thereWasStuffBefore)
				out += sprintf(out, "/");
			else
				out += sprintf(out, "<");
			out += sprintf(out, "Failed");
			thereWasStuffBefore = 1;
		}

		if (thereWasStuffBefore)
			out += sprintf(out, ">");

		out += sprintf(out, "\n");

		out += sprintf(out, "Capabilities (System:<Version/Buffers/Threshold/Timeout>):\n");
		out += sprintf(out, "\tLocal:<");
		out += sprintf(out, "%d/%d/%d/%d>\n",
			       connection->mMyCap.mUnionData.mFields.mVersion,
			       connection->mMyCap.mUnionData.mFields.mNumberBuffers,
			       connection->mMyCap.mUnionData.mFields.mThreshold, connection->mMyCap.mUnionData.mFields.mTimer);
		out += sprintf(out, "\tRemote:<");
		out += sprintf(out, "%d/%d/%d/%d>\n",
			       connection->mRemoteCap.mUnionData.mFields.mVersion,
			       connection->mRemoteCap.mUnionData.mFields.mNumberBuffers,
			       connection->mRemoteCap.mUnionData.mFields.mThreshold,
			       connection->mRemoteCap.mUnionData.mFields.mTimer);
		len = out - page;
	}
	len -= off;
	if (len < count) {
		*eof = 1;
		if (len <= 0)
			return 0;
	} else
		len = count;
	*start = page + off;
	return len;
}

int proc_veth_dump_port(char *page, char **start, off_t off, int count, int *eof, void *data) {
	char *out = page;
	long whichPort = (long) data;
	int len = 0;
	struct VethPort *port = NULL;

	if ((whichPort < 0) || (whichPort > HvMaxArchitectedVirtualLans) || (mFabricMgr == NULL))
		len = sprintf(page, "Virtual ethernet is not configured.\n");
	else {
		int i = 0;
		u32 *myAddr;
		u16 *myEndAddr;
		port = mFabricMgr->mPorts[whichPort];

		if (port != NULL) {
			myAddr = (u32 *) & (port->mMyAddress);
			myEndAddr = (u16 *) (myAddr + 1);
			out += sprintf(out, "Net device:\t%p\n", port->mDev);
			out += sprintf(out, "Net device name:\t%s\n", port->mDev->name);
			out += sprintf(out, "Address:\t%08X%04X\n", myAddr[0], myEndAddr[0]);
			out += sprintf(out, "Promiscuous:\t%d\n", port->mPromiscuous);
			out += sprintf(out, "All multicast:\t%d\n", port->mAllMcast);
			out += sprintf(out, "Number multicast:\t%d\n", port->mNumAddrs);

			for (i = 0; i < port->mNumAddrs; ++i) {
				u32 *multi = (u32 *) & (port->mMcasts[i]);
				u16 *multiEnd = (u16 *) (multi + 1);
				out += sprintf(out, "   %08X%04X\n", multi[0], multiEnd[0]);
			}
		} else {
			out += sprintf(page, "veth%d is not configured.\n", (int) whichPort);
		}

		len = out - page;
	}
	len -= off;
	if (len < count) {
		*eof = 1;
		if (len <= 0)
			return 0;
	} else
		len = count;
	*start = page + off;
	return len;
}
