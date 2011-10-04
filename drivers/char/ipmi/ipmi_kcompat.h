/*
 * ipmi_kcompat.h
 *
 * Compatability functions for 2.6 kernel idioms on 2.4
 *
 * Copyright (c) 2005 Dell, Inc.
 *   by Matt Domsch <Matt_Domsch@dell.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __LINUX_IPMI_KCOMPAT_H
#define __LINUX_IPMI_KCOMPAT_H

#include <linux/notifier.h>

#define __user

#ifdef __x86_64__
#ifdef CONFIG_COMPAT

static __inline__ void __user *compat_alloc_user_space(long len)
{
	struct pt_regs *regs = (void *)current->thread.rsp0 - sizeof(struct pt_regs); 
	return (void __user *)regs->rsp - len; 
}

#endif /* CONFIG_COMPAT */
#endif /* __x86_64__ */

/*
 * Clean way to return from the notifier and stop further calls.
 */
#ifndef NOTIFY_STOP
#define NOTIFY_STOP		(NOTIFY_OK|NOTIFY_STOP_MASK)
#endif

#endif /* __LINUX_IPMI_KCOMPAT_H */
