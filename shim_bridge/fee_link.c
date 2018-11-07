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

// Link-level messages, mostly from the switch (IVSHMSG server).
// It may hijack and finish off the message.

#include "fee.h"

// See ivshmsg_requests.py:_Link_CTL(), etc for required formats.
// I'm skipping the tracker EZT for now.

#define LINK_CTL_PEER_ATTRIBUTE \
	"Link CTL Peer-Attribute"

#define LINK_CTL_ACK \
	"Link CTL ACK C-Class=%s,CID0=%d,SID0=%d"

#define CTL_WRITE_0_CID_SID \
	"CTL-Write Space=0,PFMCID=%d,PFMSID=%d,CID=%d,SID=%d,Tag=%d"

#define STANDALONE_ACKNOWLEDGMENT \
	"Standalone Acknowledgment Tag=%d,Reason=OK"

//-------------------------------------------------------------------------
// This is called in interrupt context with the incoming_slot->lock held.

irqreturn_t FEE_link_request(struct FEE_mailslot __iomem *incoming_slot,
			       struct FEE_adapter *adapter)
{
	uint32_t PFMSID, PFMCID, SID, CID, tag;
	char outbuf[128];

	// These are all fixed values now, but someday...
	incoming_slot->peer_SID = GENZ_FEE_SID_DEFAULT;
	incoming_slot->peer_CID = incoming_slot->peer_id * 100;

	// Simple proof-of-life, must be an exact match.
	if (incoming_slot->buflen == 4 &&
	    STREQ_N(incoming_slot->buf, "ping", 4)) {
		incoming_slot->buflen = 0;	// buf received
		spin_unlock(&(adapter->incoming_slot_lock));
		FEE_create_outgoing(
			incoming_slot->peer_id,
			GENZ_FEE_SID_CID_IS_PEER_ID,
			"pong", 4,
			adapter);
		return IRQ_HANDLED;
	}

	if (STREQ_N(incoming_slot->buf, LINK_CTL_PEER_ATTRIBUTE,
		strlen(LINK_CTL_PEER_ATTRIBUTE))) {
		incoming_slot->buflen = 0;	// buf received
		spin_unlock(&(adapter->incoming_slot_lock));
		sprintf(outbuf, LINK_CTL_ACK,
			adapter->core->Base_C_Class_str,
			adapter->core->CID0,
			adapter->core->SID0);
		FEE_create_outgoing(
			incoming_slot->peer_id,
			GENZ_FEE_SID_CID_IS_PEER_ID,
			outbuf, strlen(outbuf),
			adapter);
		return IRQ_HANDLED;
	}

	if (sscanf(incoming_slot->buf, CTL_WRITE_0_CID_SID,
		   &PFMCID, &PFMSID, &CID, &SID, &tag) == 5) {
		incoming_slot->buflen = 0;	// buf received
		spin_unlock(&(adapter->incoming_slot_lock));
		adapter->core->PFMCID = PFMCID;
		adapter->core->PFMSID = PFMSID;
		adapter->core->CID0 = CID;
		adapter->core->SID0 = SID;
		adapter->core->PMCID = -1;
		sprintf(outbuf, STANDALONE_ACKNOWLEDGMENT, tag);
		FEE_create_outgoing(
			incoming_slot->peer_id,
			GENZ_FEE_SID_CID_IS_PEER_ID,
			outbuf, strlen(outbuf),
			adapter);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}
