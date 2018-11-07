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

// Arch-specific ISR handler for x86_64: configure and handle MSI-X interrupts
// from IVSHMEM device.

#include <linux/interrupt.h>	// irq_enable, etc

#include "fee.h"

//-------------------------------------------------------------------------
// FIXME: can a spurious interrupt get me here "too fast" so that I'm
// overrunning the incoming slot during a tight loop client?

static irqreturn_t all_msix(int vector, void *data) {
	struct FEE_adapter *adapter = data;
	struct msix_entry *msix_entries = adapter->IRQ_private;
	int slotnum, stomped = 0;
	uint16_t incoming_id = 0;	// see pci.h for msix_entry
	struct FEE_mailslot __iomem *incoming_slot;

	spin_lock(&(adapter->incoming_slot_lock));

	// Match the IRQ vector to entry/vector pair which yields the sender.
	// Turns out i and msix_entries[i].entry are identical in famez.
	// FIXME: preload a lookup table if I ever care about speed.
	for (slotnum = 1; slotnum < adapter->globals->nEvents; slotnum++) {
		if (vector == msix_entries[slotnum].vector)
			break;
	}
	if (slotnum >= adapter->globals->nEvents) {
		spin_unlock(&(adapter->incoming_slot_lock));
		pr_err(FEE "IRQ handler could not match vector %d\n", vector);
		return IRQ_NONE;
	}
	// All returns from here are IRQ_HANDLED
	
	incoming_id = msix_entries[slotnum].entry;
	if (!(incoming_slot = calculate_mailslot(adapter, incoming_id))) {
		spin_unlock(&(adapter->incoming_slot_lock));
		pr_err(FEE "Could not match peer %u\n", incoming_id);
		return IRQ_HANDLED;
	}

	// This may do weird things with the spinlock held.
	PR_V2("IRQ %d == sender %u -> \"%s\"\n",
		vector, incoming_id, incoming_slot->buf);

	// Link layer management can be fully processed here, otherwise 
	// deal with a "normal" message.
	if (FEE_link_request(incoming_slot, adapter) == IRQ_HANDLED)
		return IRQ_HANDLED;

	if (adapter->incoming_slot)	// print outside the spinlock
		stomped = adapter->incoming_slot->peer_id;
	adapter->incoming_slot = incoming_slot;
	spin_unlock(&(adapter->incoming_slot_lock));

	wake_up(&(adapter->incoming_slot_wqh));
	if (stomped)
		pr_warn(FEE "%s() stomped incoming slot for reader %d\n",
			__FUNCTION__, adapter->my_id);
	return IRQ_HANDLED;
}

//-------------------------------------------------------------------------
// As there are only nClients actual clients (because mailslot 0 is globals
// and server @ nslots-1) I SHOULDN'T actually activate those two IRQs.

int FEE_ISR_setup(struct pci_dev *pdev)
{
	struct FEE_adapter *adapter = pci_get_drvdata(pdev);
	int ret, i, nvectors = 0, last_irq_index;
	struct msix_entry *msix_entries;	// pci.h, will be an array

	// How many vectors are provided versus neeed?  Slot 0 doesn't need
	// one but all others do.
	if ((nvectors = pci_msix_vec_count(pdev)) < 0) {
		pr_err(FEE "Error retrieving MSI-X vector count\n");
		return nvectors;
	}
	pr_info(FEESP "%2d MSI-X vectors available (%sabled)\n",
		nvectors, pdev->msix_enabled ? "en" : "dis");
	if (nvectors < 16) {	// Convention in FAME emulation_configure.sh
		pr_err(FEE "QEMU must provide >= 16 MSI-X vectors; only %d\n", nvectors);
		return -EINVAL;
	}
	if (adapter->globals->nEvents > nvectors) {
		pr_err(FEE "need %llu MSI-X vectors, only %d available\n",
			adapter->globals->nEvents, nvectors);
		return -ENOSPC;
	}
	nvectors = adapter->globals->nEvents;		// legibility below

	ret = -ENOMEM;
	if (!(msix_entries = kzalloc(
			nvectors * sizeof(struct msix_entry), GFP_KERNEL))) {
		pr_err(FEE "Can't allocate MSI-X entries table\n");
		goto err_kfree_msix_entries;
	}
	adapter->IRQ_private = msix_entries;

	// .vector was zeroed by kzalloc
	for (i = 0; i < nvectors; i++)
		msix_entries[i].entry = i;

	// There used to be a direct call for "exact match".  Re-create it.
	if ((ret = pci_alloc_irq_vectors(
		pdev, nvectors, nvectors, PCI_IRQ_MSIX)) < 0) {
			pr_err(FEE "Can't allocate MSI-X IRQ vectors\n");
			goto err_kfree_msix_entries;
		}
	pr_info(FEESP "%2d MSI-X vectors used      (%sabled)\n",
		ret, pdev->msix_enabled ? "en" : "dis");
	if (ret < nvectors) {
		pr_err(FEE "%d vectors are not enough\n", ret);
		ret = -ENOSPC;		// Akin to pci_alloc_irq_vectors
		goto err_pci_free_irq_vectors;
	}

	// Attach each IRQ to the same handler.  pci_irq_vector() walks a
	// list and returns info on a match.  Success is merely a lookup,
	// not an allocation, so there's nothing to clean up from this step.
	// Reuse the table from the old pci_msix_xxx calls.  Note that
	// requested vectors are still option base 0.
	for (i = 0; i < nvectors; i++) {
		if ((ret = pci_irq_vector(pdev, i)) < 0) {
			pr_err("pci_irq_vector(%d) failed: %d\n", i, ret);
			goto err_pci_free_irq_vectors;
		}
		msix_entries[i].vector = ret;
	}

	// Now that they're all batched, assign them.  Each successful request
	// must be matched by a free_irq() someday.  No, the return value
	// is not stored anywhere.
	for (last_irq_index = 0;
	     last_irq_index < nvectors;
	     last_irq_index++) {
		if ((ret = request_irq(
			msix_entries[last_irq_index].vector,
			all_msix,
			0,
			FEE_NAME,
			adapter))) {
				pr_err(FEE "request_irq(%d) failed: %d\n",
					last_irq_index, ret);
				goto err_free_completed_irqs;
		}
		PR_V1(FEESP "%d = %d\n",
		      last_irq_index,
		      msix_entries[last_irq_index].vector);
	}
	return 0;

err_free_completed_irqs:
	for (i = 0; i < last_irq_index; i++)
		free_irq(msix_entries[i].vector, adapter);

err_pci_free_irq_vectors:
	pci_free_irq_vectors(pdev);

err_kfree_msix_entries:
	kfree(msix_entries);
	adapter->IRQ_private = NULL;	// sentinel for teardown
	return ret;
}

//-------------------------------------------------------------------------
// There is no disable control on this "device", hope one doesn't fire...
// Can be called from setup() above so account for partial completion.

void FEE_ISR_teardown(struct pci_dev *pdev)
{
	struct FEE_adapter *adapter = pci_get_drvdata(pdev);
	struct msix_entry *msix_entries = adapter->IRQ_private;
	int i;

	if (!msix_entries)	// Been there, done that
		return;

	for (i = 0; i < adapter->globals->nClients + 2; i++)
		free_irq(msix_entries[i].vector, adapter);
	pci_free_irq_vectors(pdev);
	kfree(msix_entries);
	adapter->IRQ_private = NULL;
}
