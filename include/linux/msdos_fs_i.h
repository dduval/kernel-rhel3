#ifndef _MSDOS_FS_I
#define _MSDOS_FS_I

/*
 * MS-DOS file system inode data in memory
 */

struct msdos_inode_info {
	unsigned long mmu_private;
	int i_start;	/* first cluster or 0 */
	int i_logstart;	/* logical first cluster */
	int i_attrs;	/* unused attribute bits */
	int i_ctime_ms;	/* unused change time in milliseconds */
#ifdef __GENKSYMS__ /* preserve KMI/ABI ksyms compatibility for mod linkage */
	int i_location;	/* on-disk position of directory entry or 0 */
#else
	u64 i_location;	/* on-disk position of directory entry or 0 */
#endif /* __GENKSYMS__ */
	struct inode *i_fat_inode;	/* struct inode of this one */
	struct list_head i_fat_hash;	/* hash by i_location */
};

#endif
