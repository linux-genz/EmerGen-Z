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

// Implement the mailbox/mailslot protocol of IVSHMSG.

#include <linux/delay.h>	// usleep_range, wait_event*
#include <linux/export.h>
#include <linux/jiffies.h>	// jiffies

#include "fee.h"

//-------------------------------------------------------------------------
// Return positive (bytecount) on success, negative on error, never 0.
// The synchronous rate seems to be determined mostly by the sleep 
// duration. I tried a 3x timeout whose success varied from 2 minutes to
// three hours before it popped. 4x was better, lasted until I did a
// compile, so...use a slightly adaptive timeout to reach the LOOP_MAX.
// CID,SID is the order used in the spec.

#define PRIOR_RESP_WAIT		(5 * HZ)	// 5x
#define DELAY_MS_LOOP_MAX	10		// or about 100 writes/second

static unsigned long longest = PRIOR_RESP_WAIT/2;

int FEE_create_outgoing(int CID, int SID, char *buf, size_t buflen,
			  struct FEE_adapter *adapter)
{
	uint32_t peer_id;
	unsigned long now = 0, this_delay,
		 hw_timeout = get_jiffies_64() + PRIOR_RESP_WAIT;

	// The IVSHMEM "vector" will map to an MSI-X "entry" value.  "vector"
	// is the lower 16 bits and the combo must be assigned atomically.
	union __attribute__ ((packed)) {
		struct { uint16_t vector, peer; };
		uint32_t Doorbell;
	} ringer;

	peer_id = SID == GENZ_FEE_SID_CID_IS_PEER_ID ? CID : CID / 100;

	// Might NOT be printable C string.
	PR_V1("%s(%lu bytes) to %d:%d -> %d\n",
		__FUNCTION__, buflen, SID, CID, peer_id);

	// FIXME: integrate with Link RFC results
	if (SID != 27 && SID != GENZ_FEE_SID_CID_IS_PEER_ID)
		return -ENETUNREACH;

	if (peer_id < 1 || peer_id > adapter->globals->server_id)
		return -EBADSLT;
	if (buflen >= adapter->max_buflen)
		return -E2BIG;
	if (!buflen)
		return -ENODATA; // FIXME: is there value to a "silent kick"?

	// Pseudo-"HW ready": wait until my_slot has pushed a previous write
	// through. In truth it's the previous responder clearing my buflen.
	// The macro makes many references to its parameters, so...
	this_delay = 1;
	while (adapter->my_slot->buflen && time_before(now, hw_timeout)) {
		if (in_interrupt())
			mdelay(this_delay); // (25k) leads to compiler error
		else
		 	msleep(this_delay);
		if (this_delay < DELAY_MS_LOOP_MAX)
			this_delay += 2;
		now = get_jiffies_64();
	}
	if ((hw_timeout -= now) > longest) {
		// pr_warn(FZ "%s() biggest TO goes from %lu to %lu\n",
			// __FUNCTION__, longest, hw_timeout);
		longest = hw_timeout;
	}

	// FIXME: add stompcounter tracker, return -EXXXX. To start with, just
	// emit an error on first occurrence and see what falls out.
	if (adapter->my_slot->buflen) {
		pr_err("%s() would stomp previous message to %llu\n",
			__FUNCTION__, adapter->my_slot->last_responder);
		return -ERESTARTSYS;
	}
	// Keep nodename and buf pointer; update buflen and buf contents.
	// buflen is the handshake out to the world that I'm busy.
	adapter->my_slot->buflen = buflen;
	adapter->my_slot->buf[buflen] = '\0';	// ASCII strings paranoia
	adapter->my_slot->last_responder = peer_id;
	memcpy(adapter->my_slot->buf, buf, buflen);

	// Choose the correct vector set from all sent to me via the peer.
	// Trigger the vector corresponding to me with the vector.
	ringer.peer = peer_id;
	ringer.vector = adapter->my_id;
	adapter->regs->Doorbell = ringer.Doorbell;
	return buflen;
}
EXPORT_SYMBOL(FEE_create_outgoing);

//-------------------------------------------------------------------------
// Return a pointer to the data structure or ERRPTR, rather than an integer
// ret, so the caller doesn't need to understand the adapter structure to
// look it up.  Intermix locking with that in msix_all().

struct FEE_mailslot *FEE_await_incoming(struct FEE_adapter *adapter,
					    int nonblocking)
{
	int ret = 0;

	if (adapter->incoming_slot)
		return adapter->incoming_slot;
	if (nonblocking)
		return ERR_PTR(-EAGAIN);
	PR_V2("%s() waiting...\n", __FUNCTION__);

	// wait_event_xxx checks the the condition BEFORE waiting but
	// does modify the run state.  Does that side effect matter?
	// FIXME: wait_event_interruptible_locked?
	if ((ret = wait_event_interruptible(adapter->incoming_slot_wqh, 
					    adapter->incoming_slot)))
		return ERR_PTR(ret);
	return adapter->incoming_slot;
}
EXPORT_SYMBOL(FEE_await_incoming);

//-------------------------------------------------------------------------

void FEE_release_incoming(struct FEE_adapter *adapter)
{
	spin_lock(&adapter->incoming_slot_lock);
	adapter->incoming_slot->buflen = 0;	// The slot of the sender.
	adapter->incoming_slot = NULL;		// The local MSI-X handler.
	spin_unlock(&adapter->incoming_slot_lock);
}
EXPORT_SYMBOL(FEE_release_incoming);
