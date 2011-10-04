/*
 * audit.h
 *
 * Copyright (c) 2003 SuSE Linux AG
 * Written by okir@suse.de, based on ideas from systrace, written by
 * Niels Provos (OpenBSD) and ported to Linux by Marius Aamodt Eriksen.
 *
 * GPL goes here
 */

#ifndef _AUDIT_H
#define _AUDIT_H

#include <linux/limits.h>
#include <linux/sys.h>

#ifdef __KERNEL__
#include <asm/semaphore.h>
#endif

#define AUDIT_API_VERSION	0x20030801

#define AUD_MAX_HOSTNAME	256
#define AUD_MAX_ADDRESS		256
#define AUD_MAX_TERMINAL	256
#define AUD_MAX_EVNAME		16

/*
 * System call intercept policy
 */
struct audit_policy {
	unsigned int	code;
	unsigned int	action;
	unsigned int	filter;
};

#define AUDIT_IGNORE		0x0000
#define AUDIT_LOG		0x0001
/* Policy flags that can be set in filter rules using
 * the return() predicate
 */
#define AUDIT_VERBOSE		0x0002

#ifdef __KERNEL__
#define AUDIT_RETURN		0x0100
#define AUDIT_INVAL		0x0301
#endif

#ifdef __ia64__
#define MAX_SYSCALL 1300
#else
#define MAX_SYSCALL 299
#endif

/*
 * Special values for audit_policy.code
 */
enum {
	__AUD_POLICY_LAST_SYSCALL = MAX_SYSCALL,
	AUD_POLICY_FORK,
	AUD_POLICY_EXIT,
	AUD_POLICY_NETLINK,
	AUD_POLICY_LOGIN,
	AUD_POLICY_USERMSG,
	AUD_POLICY_CONTROL,

	__AUD_MAX_POLICY
};

/*
 * Filter setup.
 */
struct audit_filter {
	unsigned short	num;
	unsigned short	op;
	char		event[AUD_MAX_EVNAME];
	union {
	    struct {
		unsigned short	target;
		unsigned short	filter;
	    } apply;
	    struct {
		unsigned short	filt1, filt2;
	    } bool;
	    struct {
		unsigned int	action;
	    } freturn;
	    struct {
		u_int64_t	value;
		u_int64_t	mask;
	    } integer;
	    struct {
		char *		value;
	    } string;
	} u;
};

enum {
	/* Boolean operations */
	AUD_FILT_OP_AND = 0,		/* pair of filters */
	AUD_FILT_OP_OR,			/* pair of filters */
	AUD_FILT_OP_NOT,		/* single filter */
	AUD_FILT_OP_APPLY,		/* target + predicate filter */
	AUD_FILT_OP_RETURN,		/* return immediately */
	AUD_FILT_OP_TRUE,		/* always true */
	AUD_FILT_OP_FALSE,		/* always false */

	/* Filter predicates, taking one argument */
	AUD_FILT_OP_EQ = 0x10,		/* int */
	AUD_FILT_OP_NE,			/* int */
	AUD_FILT_OP_GT,			/* int */
	AUD_FILT_OP_GE,			/* int */
	AUD_FILT_OP_LE,			/* int */
	AUD_FILT_OP_LT,			/* int */
	AUD_FILT_OP_MASK,		/* int */
	AUD_FILT_OP_STREQ = 0x20,	/* string */
	AUD_FILT_OP_PREFIX,		/* path */
};
#define AUD_FILT_ARGTYPE_INT(op)	(((op) >> 4) == 1)
#define AUD_FILT_ARGTYPE_STR(op)	(((op) >> 4) == 2)

enum {
	/* target values < 128 denote syscall arguments 0 .. 127
	 * (in case anyone ever comes up with a system call
	 * taking 127 arguments :)
	 */
	AUD_FILT_TGT_USERMSG_EVNAME = 0xFD,
	AUD_FILT_TGT_MINOR_CODE = 0xFE,
	AUD_FILT_TGT_RETURN_CODE = 0xFF,

	AUD_FILT_TGT_UID = 0x100,
	AUD_FILT_TGT_GID,
	AUD_FILT_TGT_DUMPABLE,
	AUD_FILT_TGT_EXIT_CODE,
	AUD_FILT_TGT_LOGIN_UID,

	AUD_FILT_TGT_FILE_MODE = 0x200,
	AUD_FILT_TGT_FILE_DEV,
	AUD_FILT_TGT_FILE_INO,
	AUD_FILT_TGT_FILE_UID,
	AUD_FILT_TGT_FILE_GID,
	AUD_FILT_TGT_FILE_RDEV_MAJOR,
	AUD_FILT_TGT_FILE_RDEV_MINOR,

	AUD_FILT_TGT_SOCK_FAMILY = 0x300,
	AUD_FILT_TGT_SOCK_TYPE,

	AUD_FILT_TGT_NETLINK_TYPE = 0x400,
	AUD_FILT_TGT_NETLINK_FLAGS,
	AUD_FILT_TGT_NETLINK_FAMILY,
};
#define AUD_FILT_TGT_SYSCALL_ATTR(x)	(((x) >> 8) == 0)
#define AUD_FILT_TGT_PROCESS_ATTR(x)	(((x) >> 8) == 1)
#define AUD_FILT_TGT_FILE_ATTR(x)	(((x) >> 8) == 2)
#define AUD_FILT_TGT_SOCK_ATTR(x)	(((x) >> 8) == 3)
#define AUD_FILT_TGT_NETLINK_ATTR(x)	(((x) >> 8) == 4)


/*
 * Login data
 */
struct audit_login {
	uid_t		uid;
	char		hostname[AUD_MAX_HOSTNAME];
	char		address[AUD_MAX_ADDRESS];
	char		terminal[AUD_MAX_TERMINAL];
};

/*
 * Message passing from user space
 */
struct audit_message {
	unsigned int	msg_type;
	char		msg_evname[AUD_MAX_EVNAME];
	void *		msg_data;
	size_t		msg_size;
};

/*
 * IOCTLs to configure the audit subsystem
 */
#define AUD_MAGIC '@'

/* The _IOR's are in fact wrong; they should be _IOW's :-( */
#define AUIOCATTACH		_IO(AUD_MAGIC, 101)
#define AUIOCDETACH		_IO(AUD_MAGIC, 102)
#define AUIOCSUSPEND		_IO(AUD_MAGIC, 103)
#define AUIOCRESUME		_IO(AUD_MAGIC, 104)
#define AUIOCCLRPOLICY		_IO(AUD_MAGIC, 105)
#define AUIOCSETPOLICY		_IOR(AUD_MAGIC, 106, struct audit_policy)
#define AUIOCIAMAUDITD		_IO(AUD_MAGIC, 107)
#define AUIOCSETAUDITID		_IO(AUD_MAGIC, 108)
#define AUIOCLOGIN		_IOR(AUD_MAGIC, 110, struct audit_login)
#define AUIOCUSERMESSAGE	_IOR(AUD_MAGIC, 111, struct audit_message)
#define AUIOCCLRFILTER		_IO(AUD_MAGIC, 112)
#define AUIOCSETFILTER		_IOR(AUD_MAGIC, 113, struct audit_filter)

/* Pass as ioctl(fd, AUIOCIAMAUDITD, AUDIT_TRACE_ALL) */
#define AUDIT_TRACE_ALL		1

/*
 * This message is generated whenever there is an ioctl on the audit device
 */
struct aud_msg_control {
	int			ioctl;
	int			result;
};

/*
 * This message is generated when a process forks
 * or exits, to help auditd with book-keeping.
 */
struct aud_msg_child {
        pid_t			new_pid;
};

/*
 * This message reports system call arguments.
 *
 * personality	execution domain (see linux/personality.h)
 * code		the system call code
 * result	return value of system call
 * length	length of data field
 * data field	contains all arguments, TLV encoded as follows:
 *
 *   type	4 octets	(AUD_ARG_xxx)
 *   length	4 octets	length of argument
 *   ...	N octets	argument data
 *
 * Note that path name arguments are subjected to a realpath()
 * style operation prior to sending them up to user land.
 */
struct aud_msg_syscall {
	int		personality;

	/* System call codes can have major/minor number.
	 * for instance in the socketcall() case, major
	 * would be __NR_socketcall, and minor would be
	 * SYS_ACCEPT (or whatever the specific call is).
	 */
	int		major, minor;

	int		result;
	unsigned int	length;
	unsigned char	data[1];	/* variable size */
};

/*
 * The LOGIN message is generated by the kernel when
 * a user application performs an AUIOCLOGIN ioctl.
 */
struct aud_msg_login {
	unsigned int	uid;
	char		hostname[AUD_MAX_HOSTNAME];
	char		address[AUD_MAX_ADDRESS];
	char		terminal[AUD_MAX_TERMINAL];
	char		executable[PATH_MAX];
};

/*
 * Exit message
 */
struct aud_msg_exit {
	long		code;
};

/*
 * Network config (rtnetlink) call
 */
struct aud_msg_netlink {
	unsigned int	groups, dst_groups;
	int		result;
	unsigned int	length;
	unsigned char	data[1];	/* variable size */
};

/* Values for msg_type */
#define AUDIT_MSG_LOGIN		1
#define AUDIT_MSG_SYSCALL	2
#define AUDIT_MSG_EXIT		3
#define AUDIT_MSG_NETLINK	4
#define AUDIT_MSG_CONTROL     	5
#define AUDIT_MSG_USERBASE	256	/* user land messages start here */

/* Values for msg_arch */
enum {
	AUDIT_ARCH_I386,
	AUDIT_ARCH_PPC,
	AUDIT_ARCH_PPC64,
	AUDIT_ARCH_S390,
	AUDIT_ARCH_S390X,
	AUDIT_ARCH_X86_64,
	AUDIT_ARCH_IA64,
};


struct aud_message {
	u_int32_t	msg_seqnr;
	u_int16_t	msg_type;
	u_int16_t	msg_arch;	

	pid_t		msg_pid;
	size_t		msg_size;
	unsigned long	msg_timestamp;

	unsigned int	msg_audit_id;
	unsigned int	msg_login_uid;
	unsigned int	msg_euid, msg_ruid, msg_suid, msg_fsuid;
	unsigned int	msg_egid, msg_rgid, msg_sgid, msg_fsgid;

	/* Event name */
	char		msg_evname[AUD_MAX_EVNAME];

	union {
		char	dummy;
	} msg_data;
};

/*
 * Encoding of arguments passed up to auditd
 */
enum {
	/* value 0 is reserved */
	AUDIT_ARG_IMMEDIATE = 1,
	AUDIT_ARG_POINTER,
	AUDIT_ARG_STRING,
	AUDIT_ARG_PATH,
	AUDIT_ARG_NULL,
	AUDIT_ARG_ERROR,
	AUDIT_ARG_VECTOR,	/* for execve */

#ifdef __KERNEL__
	/* Internal use only */
	AUDIT_ARG_ARRAY = 100,
	AUDIT_ARG_FILEDESC,
#endif

	AUDIT_ARG_END = 0
};

#ifdef __KERNEL__

struct sk_buff;

#ifdef __ia64__
extern int  audit_intercept(struct pt_regs *, unsigned long *);
#else
extern int  audit_intercept(struct pt_regs *);
#endif
extern void audit_result(struct pt_regs *);
extern void audit_fork(struct task_struct *, struct task_struct *);
extern void audit_exit(struct task_struct *, long);
extern void audit_netlink_msg(struct sk_buff *, int);

#if defined(CONFIG_AUDIT) || defined(CONFIG_AUDIT_MODULE)
#define isaudit(tsk)		((tsk)->audit)
#else
#define isaudit(tsk)		0
#endif

#endif /* __KERNEL__ */

#endif /* _AUDIT_H */
