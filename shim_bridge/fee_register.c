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

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/module.h>	// fops->owner

#include "fee.h"
#include "genz_device.h"

//-------------------------------------------------------------------------

int FEE_register(const struct genz_core_structure *core,
		 const struct file_operations *fops)
{
	struct FEE_adapter *adapter;
	char *ownername;
	int ret, nbindings;

	if ((ret = down_interruptible(&FEE_adapter_sema)))
		return ret;
	ownername = fops->owner->name;	
	nbindings = 0;
	list_for_each_entry(adapter, &FEE_adapter_list, lister) {
		struct pci_dev *pdev;

		pdev = adapter->pdev;
		// Device file name is meant to be reminiscent of lspci output.
		pr_info(FEE "binding %s to %s: ",
			ownername, pci_resource_name(pdev, 1));

		adapter->genz_chrdev = genz_register_char_device(
			core, fops, adapter, adapter->slot);
		if (IS_ERR(adapter->genz_chrdev)) {
			ret = PTR_ERR(adapter->genz_chrdev);
			goto up_and_out;
		}
		adapter->core = core;

		// Now that all allocs have worked, change adapter.  Yes it's
		// slightly after the "live" activation, get over it.
		strncpy(adapter->core->Base_C_Class_str,
			adapter->genz_chrdev->cclass,
			sizeof(adapter->core->Base_C_Class_str) - 1);
		strncpy(adapter->my_slot->cclass,
			adapter->genz_chrdev->cclass,
			sizeof(adapter->my_slot->cclass) - 1);

		UPDATE_SWITCH(adapter)

		pr_cont("success\n");
		nbindings++;
	}
	ret = nbindings;

up_and_out:
	up(&FEE_adapter_sema);
	return ret;
}
EXPORT_SYMBOL(FEE_register);

//-------------------------------------------------------------------------
// Return the count of un-bindings or -ERRNO.

int FEE_unregister(const struct file_operations *fops)
{
	struct FEE_adapter *adapter;
	int ret;

	if ((ret = down_interruptible(&FEE_adapter_sema)))
		return ret;

	ret = 0;
	list_for_each_entry(adapter, &FEE_adapter_list, lister) {

		pr_info(FEE "UNbind %s from %s: ",
			fops->owner->name, pci_resource_name(adapter->pdev, 0));

		if (adapter->genz_chrdev &&
		    adapter->genz_chrdev->cdev.ops == fops) {
		    	genz_unregister_char_device(adapter->genz_chrdev);
			adapter->genz_chrdev = NULL;
			strncpy(adapter->my_slot->cclass,
				DEFAULT_CCLASS,
				sizeof(adapter->my_slot->cclass) - 1);
			strncpy(adapter->core->Base_C_Class_str,
				DEFAULT_CCLASS,
				sizeof(adapter->core->Base_C_Class_str) - 1);
			UPDATE_SWITCH(adapter)
			ret++;
			pr_cont("success\n");
		} else {
			pr_cont("not actually bound\n");
			pr_info("Lookup == 0x%p\n", adapter->genz_chrdev);
		}
	}
	up(&FEE_adapter_sema);
	return ret;
}
EXPORT_SYMBOL(FEE_unregister);
