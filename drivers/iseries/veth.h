/* File veth.h created by Kyle A. Lucke on Mon Aug  7 2000. */

/* Change Activity: */
/* End Change Activity */

#ifndef _VETH_H
#define _VETH_H

#ifndef _HVTYPES_H
#include <asm/iSeries/HvTypes.h>
#endif
#ifndef _HVLPEVENT_H
#include <asm/iSeries/HvLpEvent.h>
#endif
#include <linux/netdevice.h>

#define VethEventNumTypes (4)
#define VethEventTypeCap (0)
#define VethEventTypeFrames (1)
#define VethEventTypeMonitor (2)
#define VethEventTypeFramesAck (3)

#define VethMaxFramesMsgsAcked (20)
#define VethMaxFramesMsgs (0xFFFF)
#define VethMaxFramesPerMsg (6)
#define VethAckTimeoutUsec (1000000)

#define VETHSTACKTYPE(T) struct VethStack##T
#define VETHSTACK(T) \
VETHSTACKTYPE(T) \
{ \
struct T *head; \
spinlock_t lock; \
}
#define VETHSTACKCTOR(s) do { (s)->head = NULL; spin_lock_init(&(s)->lock); } while(0)
#define VETHSTACKPUSH(s, p) \
do { \
unsigned long flags; \
spin_lock_irqsave(&(s)->lock,flags); \
(p)->next = (s)->head; \
(s)->head = (p); \
spin_unlock_irqrestore(&(s)->lock, flags); \
} while(0)

#define VETHSTACKPOP(s,p) \
do { \
unsigned long flags; \
spin_lock_irqsave(&(s)->lock,flags); \
(p) = (s)->head; \
if ((s)->head != NULL) \
{ \
(s)->head = (s)->head->next; \
} \
spin_unlock_irqrestore(&(s)->lock, flags); \
} while(0)

#define VETHQUEUE(T) \
struct VethQueue##T \
{ \
T *head; \
T *tail; \
spinlock_t lock; \
}
#define VETHQUEUECTOR(q) do { (q)->head = NULL; (q)->tail = NULL; spin_lock_init(&(q)->lock); } while(0)
#define VETHQUEUEENQ(q, p) \
do { \
unsigned long flags; \
spin_lock_irqsave(&(q)->lock,flags); \
(p)->next = NULL; \
if ((q)->head != NULL) \
{ \
(q)->head->next = (p); \
(q)->head = (p); \
} \
else \
{ \
(q)->tail = (q)->head = (p); \
} \
spin_unlock_irqrestore(&(q)->lock, flags); \
} while(0)

#define VETHQUEUEDEQ(q,p) \
do { \
unsigned long flags; \
spin_lock_irqsave(&(q)->lock,flags); \
(p) = (q)->tail; \
if ((p) != NULL) \
{ \
(q)->tail = (p)->next; \
(p)->next = NULL; \
} \
if ((q)->tail == NULL) \
(q)->head = NULL; \
spin_unlock_irqrestore(&(q)->lock, flags); \
} while(0)

struct VethFramesData {
	u32 mAddress[6];
	u16 mLength[6];
	u32 mEofMask:6;
	u32 mReserved:26;
};

struct VethFramesAckData {
	u16 mToken[VethMaxFramesMsgsAcked];
};

struct VethCapData {
	union {
		struct Fields {
			u8 mVersion;
			u8 mReserved1;
			u16 mNumberBuffers;
			u16 mThreshold;
			u16 mReserved2;
			u32 mTimer;
			u32 mReserved3;
			u64 mReserved4;
			u64 mReserved5;
			u64 mReserved6;
		} mFields;
		struct NoFields {
			u64 mReserved1;
			u64 mReserved2;
			u64 mReserved3;
			u64 mReserved4;
			u64 mReserved5;
		} mNoFields;
	} mUnionData;
};

struct VethFastPathData {
	u64 mData1;
	u64 mData2;
	u64 mData3;
	u64 mData4;
	u64 mData5;
};

struct VethLpEvent {
	struct HvLpEvent mBaseEvent;
	union {
		struct VethFramesData mSendData;
		struct VethCapData mCapabilitiesData;
		struct VethFramesAckData mFramesAckData;
		struct VethFastPathData mFastPathData;
	} mDerivedData;

};

struct VethMsg {
	struct VethMsg *next;
	union {
		struct VethFramesData mSendData;
		struct VethFastPathData mFpData;
	} mEvent;
	int mIndex;
	unsigned long mInUse;
	struct sk_buff *mSkb;
};


struct VethControlBlock {
	struct net_device *mDev;
	struct VethControlBlock *mNext;
	HvLpVirtualLanIndex mVlanId;
};

struct VethLpConnection {
	u64 mEyecatcher;
	HvLpIndex mRemoteLp;
	HvLpInstanceId mSourceInst;
	HvLpInstanceId mTargetInst;
	u32 mNumMsgs;
	struct VethMsg *mMsgs;
	int mNumberRcvMsgs;
	int mNumberLpAcksAlloced;
	union {
		struct VethFramesAckData mAckData;
		struct VethFastPathData mFpData;
	} mEventData;
	spinlock_t mAckGate;
	u32 mNumAcks;
	spinlock_t mStatusGate;
	struct {
		u64 mOpen:1;
		u64 mCapMonAlloced:1;
		u64 mBaseMsgsAlloced:1;
		u64 mSentCap:1;
		u64 mCapAcked:1;
		u64 mGotCap:1;
		u64 mGotCapAcked:1;
		u64 mSentMonitor:1;
		u64 mPopulatedRings:1;
		u64 mReserved:54;
		u64 mFailed:1;
	} mConnectionStatus;
	struct VethCapData mMyCap;
	struct VethCapData mRemoteCap;
	unsigned long mCapAckTaskPending;
	struct tq_struct mCapAckTaskTq;
	struct VethLpEvent mCapAckEvent;
	unsigned long mCapTaskPending;
	struct tq_struct mCapTaskTq;
	struct VethLpEvent mCapEvent;
	unsigned long mMonitorAckTaskPending;
	struct tq_struct mMonitorAckTaskTq;
	struct VethLpEvent mMonitorAckEvent;
	unsigned long mAllocTaskPending;
	struct tq_struct mAllocTaskTq;
	int mNumberAllocated;
	struct timer_list mAckTimer;
	u32 mTimeout;
	 VETHSTACK(VethMsg) mMsgStack;
};
#define HVMAXARCHITECTEDVIRTUALLANS 16
struct VethPort {
	struct net_device *mDev;
	struct net_device_stats mStats;
	int mLock;
	u64 mMyAddress;
	int mPromiscuous;
	int mAllMcast;
	rwlock_t mMcastGate;
	int mNumAddrs;
	u64 mMcasts[12];
};

struct VethFabricMgr {
	u64 mEyecatcher;
	HvLpIndex mThisLp;
	struct VethLpConnection mConnection[HVMAXARCHITECTEDLPS];
	spinlock_t mPortListGate;
	u64 mNumPorts;
	struct VethPort *mPorts[HVMAXARCHITECTEDVIRTUALLANS];
};

int proc_veth_dump_connection(char *page, char **start, off_t off, int count, int *eof, void *data);
int proc_veth_dump_port(char *page, char **start, off_t off, int count, int *eof, void *data);

#endif				/* _VETH_H */
