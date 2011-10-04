#ifndef ASM_IA64_CYCLONE_H
#define ASM_IA64_CYCLONE_H

#ifdef		 CONFIG_IA64_CYCLONE
extern int use_cyclone;
extern int __init cyclone_setup(char*);
extern unsigned long do_gettimeoffset_cyclone(void);
extern void mark_timeoffset_cyclone(void);
extern void __init init_cyclone_clock(void);
#else		 /* CONFIG_IA64_CYCLONE */
#define use_cyclone 0
static inline void cyclone_setup(char* s) 
{
		 printk(KERN_ERR "Cyclone Counter: System not configured"
		 		 		 		 		 " w/ CONFIG_IA64_CYCLONE.\n");
}
static inline unsigned long do_gettimeoffset_cyclone(void){}
static inline void mark_timeoffset_cyclone(void){}
static inline void init_cyclone_clock(void){}
#endif		 /* CONFIG_IA64_CYCLONE */

#endif		 /* !ASM_IA64_CYCLONE_H */
