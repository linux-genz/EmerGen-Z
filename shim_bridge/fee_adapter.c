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

#include <linux/utsname.h>

#include "fee.h"
#include "genz_device.h"	// core structure APIs FIXME move them

//-------------------------------------------------------------------------
// Slot 0 is the globals data, so disallow its use.  Server id currently
// follows last client but in general could be discontiguous.

struct FEE_mailslot __iomem *calculate_mailslot(
	struct FEE_adapter *adapter,
	unsigned slotnum)
{
	struct FEE_mailslot __iomem *slot;

	if ((slotnum < 1 || slotnum > adapter->globals->nClients) &&
	     slotnum != adapter->globals->server_id) {
		pr_err(FEE "mailslot %u is out of range\n", slotnum);
		return NULL;
	}
	slot = (void *)(
		(uint64_t)adapter->globals + slotnum * adapter->globals->slotsize);
	return slot;
}

//-------------------------------------------------------------------------

static void unmapBARs(struct pci_dev *pdev)
{
	struct FEE_adapter *adapter = pci_get_drvdata(pdev);

	if (adapter->regs) pci_iounmap(pdev, adapter->regs);	// else whine
	adapter->regs = NULL;
	if (adapter->globals) pci_iounmap(pdev, adapter->globals);
	adapter->globals = NULL;
	pci_release_regions(pdev);
}

//-------------------------------------------------------------------------
// Map the regions and overlay data structures.  Since it's QEMU, ioremap
// (uncached) for BAR0/1 and ioremap_cached(BAR2) would be fine.  However,
// the proscribed calls do the start/end/length math so use them.

static int mapBARs(struct pci_dev *pdev)
{
	struct FEE_adapter *adapter = pci_get_drvdata(pdev);
	int ret;

	// "cat /proc/iomem" seems to be very finicky about spaces and
	// punctuation even if there are other things in there with it.
	if ((ret = pci_request_regions(pdev, FEE_NAME)) < 0) {
		pr_err(FEESP "pci_request_regions failed: %d\n", ret);
		return ret;
	}

	PR_V1(FEESP "Mapping BAR0 regs (%llu bytes)\n",
		pci_resource_len(pdev, 0));
	if (!(adapter->regs = pci_iomap(pdev, 0, 0)))
		goto err_unmap;

	PR_V1(FEESP "Mapping BAR2 globals/mailslots (%llu bytes)\n",
		pci_resource_len(pdev, 2));
	if (!(adapter->globals = pci_iomap(pdev, 2, 0)))
		goto err_unmap;
	
	return 0;

err_unmap:
	unmapBARs(pdev);
	return -ENOMEM;
}

//-------------------------------------------------------------------------

void FEE_adapter_destroy(struct FEE_adapter *adapter)
{
	struct pci_dev *pdev;

	if (!adapter) return;	// probably not worth whining
	if (!(pdev = adapter->pdev)) {
		pr_err(FEE "destroy_adapter() has NULL pdev\n");
		return;
	}

	unmapBARs(pdev);	// May have be done, doesn't hurt

	dev_set_drvdata(&pdev->dev, NULL);
	pci_set_drvdata(pdev, NULL);
	adapter->pdev = NULL;

	if (adapter->IRQ_private) kfree(adapter->IRQ_private);
	adapter->IRQ_private = NULL;
	// Probably other memory leakage if this ever executes.
	if (adapter->outgoing)
		kfree(adapter->outgoing);
	adapter->outgoing = NULL;

	genz_core_structure_destroy(adapter->core);
	kfree(adapter);
}

//-------------------------------------------------------------------------
// Set up more globals and mailbox references to realize dynamic padding.

struct FEE_adapter *FEE_adapter_create(struct pci_dev *pdev)
{
	struct FEE_adapter *adapter = NULL;
	int ret;

	if (!(adapter = kzalloc(sizeof(*adapter), GFP_KERNEL))) {
		pr_err(FEESP "Cannot kzalloc(adapter)\n");
		return ERR_PTR(-ENOMEM);
	}

	// Do it before interrupts.
	if (IS_ERR_OR_NULL((adapter->core = genz_core_structure_create(GENZ_CORE_STRUCTURE_ALLOC_ALL)))) {
		kfree(adapter);
		return ERR_PTR(-ENOMEM);
	}

	// Lots of backpointers.
	pci_set_drvdata(pdev, adapter);		// Just pass around pdev.
	dev_set_drvdata(&pdev->dev, adapter);	// Never hurts to go deep.
	adapter->pdev = pdev;			// Reverse pointers never hurt.
	adapter->slot = pdev->devfn >> 3;	// Needed in a few places

	// Simple fields.
	init_waitqueue_head(&(adapter->incoming_slot_wqh));
	spin_lock_init(&(adapter->incoming_slot_lock));

	// Real work.
	if ((ret = mapBARs(pdev))) 
		goto err_kfree;

	// Now that there's access to globals and registers...Docs for 
	// pci_iomap() say to use io[read|write]32.  Since this is QEMU,
	// direct memory references should work.  The offset passed in
	// globals is handcrafted in Python, make sure it's all kosher.
	// If these fail, go back and add tests to Python, not here.
	ret = -EINVAL;
	if (offsetof(struct FEE_mailslot, buf) != adapter->globals->buf_offset) {
		pr_err(FEE "MSG_OFFSET global != C offset in here\n");
		goto err_kfree;
	}
	if (adapter->globals->slotsize <= adapter->globals->buf_offset) {
		pr_err(FEE "MSG_OFFSET global is > SLOTSIZE global\n");
		goto err_kfree;
	}
	adapter->max_buflen = adapter->globals->slotsize -
			     adapter->globals->buf_offset;
	adapter->my_id = adapter->regs->IVPosition;

	// All the needed parameters are set to finish this off.
	if (!(adapter->my_slot = calculate_mailslot(adapter, adapter->my_id)))
		goto err_kfree;
	
	// Zap the slot but recover the peer_id set by server.
	if (adapter->my_id != adapter->my_slot->peer_id) {
		pr_err("Server-defined peer ID %llu is wrong\n", adapter->my_slot->peer_id);
		goto err_kfree;
	}
	memset(adapter->my_slot, 0, adapter->globals->slotsize);
	adapter->my_slot->peer_id = adapter->my_id;

	// Leave room for the NUL in strings.
	snprintf(adapter->my_slot->nodename,
		 sizeof(adapter->my_slot->nodename) - 1,
		 "%s.%02x", utsname()->nodename, adapter->pdev->devfn >> 3);
	strncpy(adapter->my_slot->cclass, DEFAULT_CCLASS,
		sizeof(adapter->my_slot->cclass) - 1);
	strncpy(adapter->core->Base_C_Class_str, DEFAULT_CCLASS,
		sizeof(adapter->core->Base_C_Class_str) - 1);

	PR_V1(FEESP "mailslot size=%llu, buf offset=%llu, server=%llu\n",
		adapter->globals->slotsize,
		adapter->globals->buf_offset,
		adapter->globals->server_id);

	return adapter;

err_kfree:
	FEE_adapter_destroy(adapter);
	return ERR_PTR(ret);
}
