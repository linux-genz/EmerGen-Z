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

// Initial discovery and setup of IVSHMEM/IVSHMSG devices
// HP(E) lineage: res2hot from MMS PoC "mimosa" mms_base.c, flavored by zhpe.

#include <linux/module.h>

#include "fee.h"

MODULE_LICENSE("GPL");
MODULE_VERSION(FEE_VERSION);
MODULE_AUTHOR("Rocky Craig <rocky.craig@hpe.com>");
MODULE_DESCRIPTION("PCI shim for EmerGen-Z project on F.E.E.");

// Find the one macro that does the right thing.  Notice there is no "device"
// for QEMU in the PCI ID database, just the sub* things.

static struct pci_device_id FEE_PCI_ID_table[] = {
    { PCI_DEVICE_SUB(	// vend, dev, subvend, subdev
    	PCI_VENDOR_ID_REDHAT_QUMRANET,
    	PCI_ANY_ID,
    	PCI_SUBVENDOR_ID_REDHAT_QUMRANET,
	PCI_SUBDEVICE_ID_QEMU)
    },
    { 0 },
};

MODULE_DEVICE_TABLE(pci, FEE_PCI_ID_table);	// depmod, hotplug, modinfo

// module parameters are global

int verbose = 0;
module_param(verbose, uint, 0644);
MODULE_PARM_DESC(verbose, "increase amount of printk info (0)");

// Multiple bridge "devices" accepted by FEE_init_one().  PCI core might
// do everything I need but I can't shake the feeling I want this for
// something else...right now it just tracks insmod/rmmod.

LIST_HEAD(FEE_adapter_list);
DEFINE_SEMAPHORE(FEE_adapter_sema);

//-------------------------------------------------------------------------
// Called at insmod time and also at hotplug events (shouldn't be any).
// Only take IVSHMEM (filtered by PCI core) with a BAR 1 and 64 vectors.

static char get_peer_attributes[] = "Link CTL Peer-Attribute";

static int FEE_init_one(
	struct pci_dev *pdev, const struct pci_device_id *pdev_id)
{
	struct FEE_adapter *adapter = NULL, *cur = NULL;
	int ret = -ENOTTY;

	PR_V1("%s(%s)\n", __FUNCTION__, CARDLOC(pdev));

	if (pci_get_drvdata(pdev)) {	// Is this possible?
		pr_err(FEESP "This device is already configured\n");
		return -EALREADY;
	}

	// Enable it to discriminate values and create a configuration for
	// this instance.

	if ((ret = pci_enable_device(pdev)) < 0) {
		pr_err(FEESP "pci_enable_device failed: %d\n", ret);
		return ret;
	}
	if (pdev->revision != 1 ||
	    !pdev->msix_cap ||
	    !pci_resource_start(pdev, 1)) {
		PR_V1("IVSHMEM @ %s is missing IVSHMSG features\n", CARDLOC(pdev));
		ret = -ENODEV;
		goto err_pci_disable_device;
	}
	pr_info(FEE "IVSHMEM @ %s has IVSHMSG features\n", CARDLOC(pdev));

	if (IS_ERR_VALUE((adapter = FEE_adapter_create(pdev)))) {
		ret = PTR_ERR(adapter);
		adapter = NULL;
		goto err_pci_disable_device;
	}

	if ((ret = FEE_ISR_setup(pdev)))
		goto err_pci_disable_device;

	// It's a keeper...unless it's already there.  Unlikely, but it's
	// not paranoia when in the kernel.
	ret = down_interruptible(&FEE_adapter_sema);	// FIXME: deal with ret
	ret = 0;
	list_for_each_entry(cur, &FEE_adapter_list, lister) {
		if (STREQ(CARDLOC(pdev), pci_resource_name(cur->pdev, 1))) {
			ret = -EALREADY;
			break;
		}
	}
	if (!ret) {
		if (pdev->slot) {	// See lscpi -v
			char newname[32];

			// Originally slot number %d
			// pr_info("Slot name = %s\n", pci_slot_name(pdev->slot));
			sprintf(newname, "%s.%02x",
				FEE_NAME, (unsigned)(pdev->slot->number));
			ret = kobject_rename(&pdev->slot->kobj, newname);
			ret = 0;	// __must_check, but __dont_care
		}
		list_add_tail(&adapter->lister, &FEE_adapter_list);
	}
	up(&FEE_adapter_sema);
	if (ret) {
		pr_err(FEESP "This device is already in active list\n");
		goto err_MSIX_teardown;
	}


	// Get peer-attributes from ivshmsg_server; response processed inline
	ret = FEE_create_outgoing(
		adapter->globals->server_id,
		GENZ_FEE_SID_CID_IS_PEER_ID,
		get_peer_attributes, strlen(get_peer_attributes), adapter);
	if (ret > 0)
		ret = ret == strlen(get_peer_attributes) ? 0 : -EIO;
	if (!ret) {
		UPDATE_SWITCH(adapter);
		return ret;	// else fall through
	}

err_MSIX_teardown:
	PR_V1("tearing down MSI-X %s\n", CARDLOC(pdev));
	FEE_ISR_teardown(pdev);

err_pci_disable_device:
	PR_V1("disabling device %s\n", CARDLOC(pdev));
	pci_disable_device(pdev);

// err_destroy_adapter:
	FEE_adapter_destroy(adapter);
	return ret;
}

//-------------------------------------------------------------------------

static void FEE_remove_one(struct pci_dev *pdev)
{
	struct FEE_adapter *cur, *next, *adapter = pci_get_drvdata(pdev);
	char oldname[8];
	int ret;

	pr_info(FEE "%s(%s): ", __FUNCTION__, CARDLOC(pdev));
	if (!adapter) {
		pr_cont("still not my circus\n");
		return;
	}
	pr_cont("disabling/removing/freeing resources\n");

	// Fix lspci -v
	sprintf(oldname, "%u", (unsigned)(pdev->slot->number));
	ret = kobject_rename(&pdev->slot->kobj, oldname);
	ret = 0;	// __must_check, but __dont_care

	strcpy(adapter->my_slot->cclass, "Driverless QEMU");
	UPDATE_SWITCH(adapter);

	FEE_ISR_teardown(pdev);

	pci_disable_device(pdev);

	if (atomic_read(&adapter->nr_users))
		pr_err(FEESP "# users is non-zero, very interesting\n");
	
	ret = down_interruptible(&FEE_adapter_sema);	// FIXME: deal with ret
	list_for_each_entry_safe(cur, next, &FEE_adapter_list, lister) {
		if (STREQ(CARDLOC(cur->pdev), CARDLOC(pdev)))
			list_del(&(cur->lister));
	}
	up(&FEE_adapter_sema);

	FEE_adapter_destroy(adapter);
}

//-------------------------------------------------------------------------

static struct pci_driver FEE_driver = {
	.name =		FEE_NAME,
	.id_table =	FEE_PCI_ID_table,
	.probe =	FEE_init_one,
	.remove =	FEE_remove_one
};

int __init FEE_init(void)
{
	int ret;

	pr_info("-------------------------------------------------------");
	pr_info(FEE FEE_VERSION "; parms:\n");
	pr_info(FEESP "verbose = %d\n", verbose);

	if ((ret = pci_register_driver(&FEE_driver)))
		pr_err(FEE "pci_register_driver() = %d\n", ret);

	return ret;
}

module_init(FEE_init);

//-------------------------------------------------------------------------
// Called from rmmod.

void FEE_exit(void)
{
	pci_unregister_driver(&FEE_driver);
}

module_exit(FEE_exit);
