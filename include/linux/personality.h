#ifndef _LINUX_PERSONALITY_H
#define _LINUX_PERSONALITY_H

/*
 * Handling of different ABIs (personalities).
 */

struct exec_domain;
struct pt_regs;

extern int		register_exec_domain(struct exec_domain *);
extern int		unregister_exec_domain(struct exec_domain *);
extern int		__set_personality(unsigned long);


/*
 * Sysctl variables related to binary emulation.
 */
extern unsigned long abi_defhandler_coff;
extern unsigned long abi_defhandler_elf;
extern unsigned long abi_defhandler_lcall7;
extern unsigned long abi_defhandler_libcso;
extern int abi_fake_utsname;


/*
 * Flags for bug emulation.
 *
 * These occupy the top three bytes.
 */
enum {
	MMAP_PAGE_ZERO =	0x0100000,
	ADDR_LIMIT_32BIT =	0x0800000,
	SHORT_INODE =		0x1000000,
	WHOLE_SECONDS =		0x2000000,
	STICKY_TIMEOUTS	=	0x4000000,
	ADDR_LIMIT_3GB = 	0x8000000,
};

/*
 * Personality types.
 *
 * These go in the low byte.  Avoid using the top bit, it will
 * conflict with error returns.
 */
enum {
	PER_LINUX =		0x0000,
	PER_LINUX_32BIT =	0x0000 | ADDR_LIMIT_32BIT,
	PER_SVR4 =		0x0001 | STICKY_TIMEOUTS | MMAP_PAGE_ZERO,
	PER_SVR3 =		0x0002 | STICKY_TIMEOUTS | SHORT_INODE,
	PER_SCOSVR3 =		0x0003 | STICKY_TIMEOUTS |
					 WHOLE_SECONDS | SHORT_INODE,
	PER_OSR5 =		0x0003 | STICKY_TIMEOUTS | WHOLE_SECONDS,
	PER_WYSEV386 =		0x0004 | STICKY_TIMEOUTS | SHORT_INODE,
	PER_ISCR4 =		0x0005 | STICKY_TIMEOUTS,
	PER_BSD =		0x0006,
	PER_SUNOS =		0x0006 | STICKY_TIMEOUTS,
	PER_XENIX =		0x0007 | STICKY_TIMEOUTS | SHORT_INODE,
	PER_LINUX32 =		0x0008,
	PER_LINUX32_3GB =	0x0008 | ADDR_LIMIT_3GB,
	PER_IRIX32 =		0x0009 | STICKY_TIMEOUTS,/* IRIX5 32-bit */
	PER_IRIXN32 =		0x000a | STICKY_TIMEOUTS,/* IRIX6 new 32-bit */
	PER_IRIX64 =		0x000b | STICKY_TIMEOUTS,/* IRIX6 64-bit */
	PER_RISCOS =		0x000c,
	PER_SOLARIS =		0x000d | STICKY_TIMEOUTS,
	PER_UW7 =		0x000e | STICKY_TIMEOUTS | MMAP_PAGE_ZERO,
	PER_HPUX =		0x000f,
	PER_OSF4 =		0x0010,			 /* OSF/1 v4 */
	PER_MASK =		0x00ff,
};


/*
 * Description of an execution domain.
 * 
 * The first two members are refernced from assembly source
 * and should stay where they are unless explicitly needed.
 */
typedef void (*handler_t)(int, struct pt_regs *);

struct exec_domain {
	const char		*name;		/* name of the execdomain */
	handler_t		handler;	/* handler for syscalls */
	unsigned char		pers_low;	/* lowest personality */
	unsigned char		pers_high;	/* highest personality */
	unsigned long		*signal_map;	/* signal mapping */
	unsigned long		*signal_invmap;	/* reverse signal mapping */
	struct map_segment	*err_map;	/* error mapping */
	struct map_segment	*socktype_map;	/* socket type mapping */
	struct map_segment	*sockopt_map;	/* socket option mapping */
	struct map_segment	*af_map;	/* address family mapping */
	struct module		*module;	/* module context of the ed. */
	struct exec_domain	*next;		/* linked list (internal) */
};

/*
 * Return the base personality without flags.
 */
#define personality(pers)	(pers & PER_MASK)

/*
 * Personality of the currently running process.
 */
#define get_personality		(current->personality)

/*
 * Change personality of the currently running process.
 */
#define set_personality(pers) \
	((current->personality == pers) ? 0 : __set_personality(pers))

/*
 * Load an execution domain.
 */
#define get_exec_domain(ep)				\
do {							\
	if (ep != NULL && ep->module != NULL)		\
		__MOD_INC_USE_COUNT(ep->module);	\
} while (0)

/*
 * Unload an execution domain.
 */
#define put_exec_domain(ep)				\
do {							\
	if (ep != NULL && ep->module != NULL)		\
		__MOD_DEC_USE_COUNT(ep->module);	\
} while (0)

#endif /* _LINUX_PERSONALITY_H */
