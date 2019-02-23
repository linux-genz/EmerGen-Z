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

// Global definitions.

#ifndef FEE_DOT_H
#define FEE_DOT_H

#include <linux/list.h>
#include <linux/pci.h>
#include <linux/semaphore.h>
#include <linux/wait.h>

#include "genz_baseline.h"

#define FEE_DEBUG			// See "Debug assistance" below

#define FEE_NAME	"FEE"
#define FEE_VERSION	FEE_NAME " v0.9.0: using Gen-Z subsystem"

#define FEE		"FEE: "		// pr_xxxx header
#define FEESP		"     "		// pr_xxxx header same length indent
#define DEFAULT_CCLASS	"FEEadapter"	// no spaces

struct ivshmem_registers {		// BAR 0
	uint32_t	Rev1Reserved1,	// Rev 0: Interrupt mask
			Rev1Reserved2,	// Rev 0: Interrupt status
			IVPosition,	// My peer id
			Doorbell;	// Upper and lower half
};

struct ivshmem_msi_x_table_pba {	// BAR 1: Not mapped, not used.  YET.
	uint32_t junk1, junk2;
};

// ivshmsg_server.py controls the mailbox slot size and number of slots
// (and therefore the total file size).  It gives these numbers to this driver.
// There are always a power-of-two number of mailbox slots, indexed by IVSHMSG
// client ID.  Slot 0 is reserved for global data cuz it's easy to find :-)
// Besides, ID 0 doesn't seem to work in the QEMU doorbell mechanism.  The
// last slot (with ID == nClients + 1) is for the Python server.  The remaining
// slots are for client IDs 1 through nClients.

struct FEE_globals {			// BAR 2: Start of IVSHMEM
	uint64_t slotsize, buf_offset, nClients, nEvents, server_id;
};

// Use only uint64_t and keep the buf[] on a 32-byte alignment for this:
// od -Ad -w32 -c -tx8 /dev/shm/ivshmsg_mailbox
struct __attribute__ ((packed)) FEE_mailslot {
	char nodename[32];		// off  0: of the owning client
	char cclass[32];		// off 32: of the owning client
	uint64_t buflen,		// off 64:
		 peer_id,		// off 72: Convenience; set by server
		 last_responder,	// off 80: To assist stale stompage
		 peer_SID,		// off 88: Calculated in MSI-X...
		 peer_CID,		// off 96: ...from last_responder
		 pad[3];		// off 104
	char buf[];			// off 128 == globals->buf_offset
};

// The primary configuration/context data.
struct FEE_adapter {
	struct list_head lister;
	atomic_t nr_users;				// User-space actors
	struct pci_dev *pdev;				// Paranoid reverse ptr
	int slot;					// pdev->devfn >> 3
	uint64_t max_buflen;
	uint16_t my_id;					// match ringer field
	struct ivshmem_registers __iomem *regs;		// BAR0
	struct FEE_globals __iomem *globals;		// BAR2
	struct FEE_mailslot *my_slot;			// indexed by my_id
	void *IRQ_private;				// arch-dependent?

	// Per-adapter handshaking between doorbell/mail delivery and a
	// driver read().  Doorbell comes in and sets the pointer then
	// issues a wakeup.  read() follows the pointer then sets it
	// to NULL for next one.  Since reading is more of a one-to-many
	// relationship this module can hold the one.

	struct FEE_mailslot *incoming_slot;
	struct wait_queue_head incoming_slot_wqh;
	spinlock_t incoming_slot_lock;

	// Writing is many to one, so support buffers etc are the
	// responsibility of that module, managed by open() & release().
	void *outgoing;

	struct genz_core_structure *core;		// Primary data structure
	struct genz_char_device *genz_chrdev;		// Convenience backpointers
	void *teardown;
};

//-------------------------------------------------------------------------
// fee_pci.c - insmod/rmmod handling with pci_register probe()/remove()

extern int verbose;				// insmod parameter
extern struct list_head FEE_adapter_list;
extern struct semaphore FEE_adapter_sema;

//-------------------------------------------------------------------------
// fee_adapter.c - create/populate and destroy an adapter structure

// Linked in to genzfee.ko, used by various other source modules
struct FEE_adapter *FEE_adapter_create(struct pci_dev *);
void FEE_adapter_destroy(struct FEE_adapter *);
struct FEE_mailslot __iomem *calculate_mailslot(struct FEE_adapter *, unsigned);

// Nothing EXPORTed

//.........................................................................
// fee_IVSHMSG.c - the actual messaging IO.

#define GENZ_FEE_SID_DEFAULT		27	// see twisted_server.py
#define GENZ_FEE_SID_CID_IS_PEER_ID	-42	// interpret cid as peer_id

// EXPORTed
extern struct FEE_mailslot *FEE_await_incoming(struct FEE_adapter *, int);
extern void FEE_release_incoming(struct FEE_adapter *);
extern int FEE_create_outgoing(int, int, char *, size_t, struct FEE_adapter *);

//.........................................................................
// FEE_???.c - handle interrupts from other FEE peers (input). By arch:
// x86_64:	FEE_MSI-X.c
// ARM64:	FEE_MSI-X.c with assist from QEMU vfio modules
// RISCV:	not written yet

irqreturn_t FEE_link_request(struct FEE_mailslot __iomem *, struct FEE_adapter *);

// EXPORTed
int FEE_ISR_setup(struct pci_dev *);
void FEE_ISR_teardown(struct pci_dev *);

//.........................................................................
// fee_register.c - accept end-driver requests to use FEE.

// EXPORTed
extern int FEE_register(const struct genz_core_structure *,
			const struct file_operations *);
extern int FEE_unregister(const struct file_operations *);

//-------------------------------------------------------------------------
// Legibility assistance

// Send a command for the switch interpreter
#define UPDATE_SWITCH(AdApTeR) FEE_create_outgoing( \
			AdApTeR->globals->server_id, \
			GENZ_FEE_SID_CID_IS_PEER_ID, \
			"dump", 4, AdApTeR);

// linux/pci.h missed one
#ifndef pci_resource_name
#define pci_resource_name(dev, bar) (char *)((dev)->resource[(bar)].name)
#endif

#define CARDLOC(ptr) (pci_resource_name(ptr, 1))

#define STREQ(s1, s2) (!strcmp(s1, s2))
#define STREQ_N(s1, s2, lll) (!strncmp(s1, s2, lll))
#define STARTS(s1, s2) (!strncmp(s1, s2, strlen(s2)))

//-------------------------------------------------------------------------
// Debug assistance

#ifdef FEE_DEBUG
#define PR_V1(a...)	{ if (verbose) pr_info(FEE a); }
#define PR_V2(a...)	{ if (verbose > 1) pr_info(FEE a); }
#define PR_V3(a...)	{ if (verbose > 2) pr_info(FEE a); }
#else
#define PR_V1(a...)
#define PR_V2(a...)
#define PR_V3(a...)
#endif

#define _F_		__FUNCTION__
#define PR_ENTER(a...)	{ if (verbose) { \
				pr_info(FEE "enter %s: ", _F_); pr_cont(a); }}
#define PR_EXIT(a...)	{ if (verbose) { \
				pr_info(FEE "exit %s: ", _F_); pr_cont(a); }}

#define PR_SLEEPMS(_txt, _ms) { pr_info(FEE " " _txt); msleep(_ms); }

#endif
