/*
  File: linux/solaris_acl.h

  (C) 2002 Andreas Gruenbacher, <a.gruenbacher@computer.org>
*/


#ifndef __LINUX_SOLARIS_ACL_H
#define __LINUX_SOLARIS_ACL_H

#include <linux/posix_acl.h>

u32 *nfs_acl_encode(u32 *, u32 *, struct inode *, struct posix_acl *, int, int);
u32 *nfs_acl_decode(u32 *, u32 *, unsigned int *, struct posix_acl **);

static __inline__ u32 *nfs_acl_encode_limit(u32 *p, u32 *end, struct inode *inode, struct posix_acl *acl,
	int encode_entries, int typeflag, unsigned int acl_limit)
{
	int entries = acl ? acl->a_count : 0;
	if (entries >= acl_limit)
		return NULL;
	return nfs_acl_encode(p, end, inode, acl, encode_entries, typeflag);
}

static __inline__ u32 *nfs_acl_decode_limit(u32 *p, u32 *end, unsigned int *aclcnt, struct posix_acl **pacl, unsigned int acl_limit)
{
	int entries = ntohl(*p);
	
	if (entries >= acl_limit)
		return NULL;
	return nfs_acl_decode(p, end, aclcnt, pacl);
}
#endif  /* __LINUX_SOLARIS_ACL_H */
