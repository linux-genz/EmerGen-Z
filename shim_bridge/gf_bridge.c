/*
 * (C) Copyright 2018 Hewlett Packard Enterprise Development LP.
 * All rights reserved.
 *
 * This source code file is part of the EmerGen-Z project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <asm-generic/bug.h>	// yes after the others

#include "genz_class.h"
#include "genz_device.h"

#include "fee.h"
#include "gf_bridge.h"

MODULE_LICENSE("GPL");
MODULE_VERSION(GFBRIDGE_VERSION);
MODULE_AUTHOR("Rocky Craig <rocky.craig@hpe.com>");
MODULE_DESCRIPTION("Bridge driver for EmerGen-Z project.");

// module parameters are global

int verbose = 0;
module_param(verbose, uint, 0644);
MODULE_PARM_DESC(verbose, "increase amount of printk info (0)");

DECLARE_WAIT_QUEUE_HEAD(bridge_reader_wait);

//-------------------------------------------------------------------------
// misc_register sets up a "hooking" fops for the first open call.  It
// extracts its misdevice, puts it in file->private, then install the real
// fops and calls open.  Duplicate the private extraction here.

static int gf_bridge_open(struct inode *inode, struct file *file)
{
	struct FEE_adapter *adapter;
	int n, ret;

	// FEE drivers must do this during open() whether they use
	// the return value or not.  Later APIs need it.
	adapter = file->private_data =
		genz_char_drv_1stopen_private_data(file);

	// pr_info("bridge_open() file->private_data @ 0x%p\n", adapter);

	// FIXME: got to come up with more 'local module' support for this.
	// Just keep it single user for now.  This code was from an earlier
	// incantation, the "file->private_data" voodoo is another way to
	// do single open.  I may need this later.

	ret = 0;
	if ((n = atomic_add_return(1, &adapter->nr_users) == 1)) {
		struct bridge_buffers *buffers;

		if (!(buffers = kzalloc(sizeof(*buffers), GFP_KERNEL))) {
			ret = -ENOMEM;
			goto alldone;
		}
		if (!(buffers->wbuf = kzalloc(adapter->max_buflen, GFP_KERNEL))) {
			kfree(buffers);
			ret = -ENOMEM;
			goto alldone;
		}
		mutex_init(&(buffers->wbuf_mutex));
		adapter->outgoing = buffers;
	} else {
		pr_warn(GFBRSP "Sorry, just exclusive open() for now\n");
		ret = -EBUSY;
		goto alldone;
	}

	PR_V1("open: %d users\n", atomic_read(&adapter->nr_users));

alldone:
	if (ret) 
		atomic_dec(&adapter->nr_users);
	return ret;
}

//-------------------------------------------------------------------------
// At any close of a process fd

static int gf_bridge_flush(struct file *file, fl_owner_t id)
{
	struct FEE_adapter *adapter = file->private_data;
	int nr_users, f_count;

	spin_lock(&file->f_lock);
	nr_users = atomic_read(&adapter->nr_users);
	f_count = atomic_long_read(&file->f_count);
	spin_unlock(&file->f_lock);
	if (f_count == 1) {
		atomic_dec(&adapter->nr_users);
		nr_users--;
	}

	PR_V1("flush: after (optional) dec: %d users, file count = %d\n",
		nr_users, f_count);
	
	return 0;
}

//-------------------------------------------------------------------------
// Only at the final close of the last process fd

static int gf_bridge_release(struct inode *inode, struct file *file)
{
	struct FEE_adapter *adapter = file->private_data;
	struct bridge_buffers *buffers = adapter->outgoing;
	int nr_users, f_count;

	spin_lock(&file->f_lock);
	nr_users = atomic_read(&adapter->nr_users);
	f_count = atomic_long_read(&file->f_count);
	spin_unlock(&file->f_lock);
	PR_V1("release: %d users, file count = %d\n", nr_users, f_count);
	BUG_ON(nr_users);
	kfree(buffers->wbuf);
	kfree(buffers);
	adapter->outgoing = NULL;
	return 0;
}

//-------------------------------------------------------------------------
// Prepend the sender id as a field separated by a colon, realized by two
// calls to copy_to_user and avoiding a temporary buffer here. copy_to_user
// can sleep and returns the number of bytes that could NOT be copied or
// -ERRNO.  Require both copies to work all the way.  

static ssize_t gf_bridge_read(struct file *file, char __user *buf,
				 size_t buflen, loff_t *ppos)
{
	struct FEE_adapter *adapter = file->private_data;
	struct FEE_mailslot *sender;
	int ret, n;
	// SID is 28 bits or 10 decimal digits; CID is 16 bits or 5 digits
	// so make the buffer big enough.
	char sidcidstr[32];

	// A successful return needs cleanup via FEE_release_incoming().
	sender = FEE_await_incoming(adapter, file->f_flags & O_NONBLOCK);
	if (IS_ERR(sender))
		return PTR_ERR(sender);
	PR_V2(GFBRSP "wait finished, %llu bytes to read\n", sender->buflen);

	// Two parts to the response: first is the sender "CID,SID:".
	// Omit  the [] brackets commonly seen in the spec, ala [CID,SID].
	n = snprintf(sidcidstr, sizeof(sidcidstr) - 1,
		"%llu,%llu:", sender->peer_CID, sender->peer_SID);

	if (n >= sizeof(sidcidstr) || buflen < sender->buflen + n - 1) {
		ret = -E2BIG;
		goto read_complete;
	}
	if ((ret = copy_to_user(buf, sidcidstr, n))) {
		if (ret > 0) ret= -EFAULT;	// partial transfer
		goto read_complete;
	}

	// The message body follows the colon of the previous snippet.
	ret = copy_to_user(buf + (uint64_t)n, sender->buf, sender->buflen);
	ret = !ret ? sender->buflen + n :
		(ret > 0 ? -EFAULT : ret);
	// Now it's either the length of the full responose or -ESOMETHING
	if (ret > 0)
		*ppos = 0;

read_complete:	// Whether I used it or not, let everything go
	FEE_release_incoming(adapter);
	return ret;
}

//-------------------------------------------------------------------------
// Use many idiot checks.  Performance is not the issue here.  The data
// might be binary (including unprintables and NULs), not just a C string.

static ssize_t gf_bridge_write(struct file *file, const char __user *buf,
				  size_t buflen, loff_t *ppos)
{
	struct FEE_adapter *adapter = file->private_data;
	struct bridge_buffers *buffers = adapter->outgoing;
	ssize_t successlen = buflen;
	char *bufbody;
	int ret, restarts, SID, CID;

	if (buflen >= adapter->max_buflen - 1) {	// Paranoia on term NUL
		PR_V1("buflen of %lu is too big\n", buflen);
		return -E2BIG;
	}
	mutex_lock(&buffers->wbuf_mutex);	// Multiuse of *file
	if ((ret = copy_from_user(buffers->wbuf, buf, buflen))) {
		if (ret > 0)
			ret = -EFAULT;
		goto unlock_return;
	}
	// Even if it's not a string, this puts a bound on the strchr(':')
	buffers->wbuf[buflen] = '\0';		

	// Split body into two pieces around the first colon: a proper string
	// and whatever the real payload is (string or binary).
	if (!(bufbody = strchr(buffers->wbuf, ':'))) {
		pr_err(GFBR "no colon in \"%s\"\n", buffers->wbuf);
		ret = -EBADMSG;
		goto unlock_return;
	}
	*bufbody = '\0';	// chomp ':', now two NUL-terminated sections
	bufbody++;
	buflen -= (uint64_t)bufbody - (uint64_t)buffers->wbuf;

	// SID and CID from varying input, including "expert use" of a peer id.

	SID = GENZ_FEE_SID_CID_IS_PEER_ID;
	if (STREQ(buffers->wbuf, "server") || STREQ(buffers->wbuf, "switch") ||
	    STREQ(buffers->wbuf, "link") || STREQ(buffers->wbuf, "interface"))
		CID = adapter->globals->server_id;
	else {
		char *comma = strchr(buffers->wbuf, ','); // Want CID,SID

		if (comma) {
			*comma = '\0';
			if ((ret = kstrtoint(buffers->wbuf, 0, &CID)))
				goto unlock_return;
			if ((ret = kstrtoint(comma + 1, 0, &SID)))
				goto unlock_return;
		} else {	// Direct use of an IVSHMSG peer id
			if ((ret = kstrtoint(buffers->wbuf, 0, &CID)))
				goto unlock_return;
		}
	}

	// Length or -ERRNO.  If length matched, then all is well, but
	// this final len is always shorter than the original length.  Some
	// code (ie, "echo") will resubmit the partial if the count is
	// short.  So lie about it to the caller.

	restarts = 0;
restart:
	ret = FEE_create_outgoing(CID, SID, bufbody, buflen, adapter);
	if (ret == -ERESTARTSYS) {	// spurious timeout
		if (restarts++ < 2)
			goto restart;
		ret = -ETIMEDOUT;
	} else if (ret == buflen)
		ret = successlen;
	else if (ret >= 0)
		ret = -EIO;	// partial transfer paranoia

unlock_return:
	mutex_unlock(&buffers->wbuf_mutex);
	return ret;
}

//-------------------------------------------------------------------------
// Returning 0 will cause the caller (epoll/poll/select) to sleep.

static uint gf_bridge_poll(struct file *file, struct poll_table_struct *wait)
{
	struct FEE_adapter *adapter = file->private_data;
	uint ret = 0;

	poll_wait(file, &bridge_reader_wait, wait);
		ret |= POLLIN | POLLRDNORM;
	// FIXME encapsulate this better, it's really the purview of sendstring
	if (!adapter->my_slot->buflen)
		ret |= POLLOUT | POLLWRNORM;
	return ret;
}

// Symbols show up in /proc/kallsyms so spell them out.
static const struct file_operations bridge_fops = {
	.owner =	THIS_MODULE,
	.open =		gf_bridge_open,
	.flush =	gf_bridge_flush,
	.release =	gf_bridge_release,
	.read =		gf_bridge_read,
	.write =	gf_bridge_write,
	.poll =		gf_bridge_poll,
};

//-------------------------------------------------------------------------
// Called from insmod.  Bind the driver set to all available FEE devices.

static int _nbindings = 0;

int __init gfbridge_init(void)
{
	int ret;

	pr_info("-------------------------------------------------------");
	pr_info(GFBR GFBRIDGE_VERSION "; parms:\n");
	pr_info(GFBRSP "verbose = %d\n", verbose);

	_nbindings = 0;
	if ((ret = FEE_register(GENZ_CCE_DISCRETE_BRIDGE, &bridge_fops)) < 0)
		return ret;
	_nbindings = ret;
	pr_info(GFBR "%d bindings made\n", _nbindings);
	return _nbindings ? 0 : -ENODEV;
}

module_init(gfbridge_init);

//-------------------------------------------------------------------------
// Called from rmmod.  Unbind this driver set from any registered bindings.

void gfbridge_exit(void)
{
	int ret = FEE_unregister(&bridge_fops);
	if (ret >= 0)
		pr_info(GFBR "%d/%d bindings released\n", ret, _nbindings);
	else
		pr_err(GFBR "module exit errno %d\n", -ret);
}

module_exit(gfbridge_exit);
