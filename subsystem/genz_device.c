/*
 * (C) Copyright 2018 Hewlett Packard Enterprise Development LP.
 * All rights reserved.
 *
 * This source code file is part of the FAME-Z project.
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

#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "genz_baseline.h"
#include "genz_class.h"
#include "genz_device.h"
#include "genz_routing_fabric.h"

#define __UNUSED__ __attribute__ ((unused))

/*
 * MINORBITS is 20, which is 1M components, which is cool, but it's 16k longs
 * in the bitmap, or 128k, which seems like uncool overkill.
 */

#define GENZ_MINORBITS	14			/* 16k components per class */
#define MAXMINORS	(1 << GENZ_MINORBITS)	/* 2k space per bitmap */

const char * const genz_component_class_str[] = {
	"BAD HACKER. BAD!",
	"MemoryP2PCore",
	"MemoryExplicitOpClass",
	"IntegratedSwitch",
	"EnclosureExpansionSwitch",
	"FabricSwitch",
	"Processor",
	"Processor_NB",
	"Accelerator_NB_NC",
	"Accelerator_NB",
	"Accelerator_NC",
	"Accelerator",
	"IO_NB_NC",
	"IO_NB",
	"IO_NC",
	"IO",
	"BlockStorage",
	"BlockStorage_NB",
	"TransparentRouter",
	"MultiClass",
	"DiscreteBridge",
	"IntegratedBridge",
	NULL
};

static DEFINE_MUTEX(bridge_mutex);
static DECLARE_BITMAP(bridge_minor_bitmap, MAXMINORS) = { 0 };
static LIST_HEAD(bridge_list);

static uint64_t bridge_major = 0;		/* Until first allocation */

/**
 * genz_core_structure_create - allocate and populate a Gen-Z Core Structure
 * @alloc: a bitfield directing which sub-structures to allocate.
 * 
 * Create a semantically-complete Core Structure (not binary field-precise).
 */

struct genz_core_structure *genz_core_structure_create(unsigned CCE)
{
	uint64_t alloc = 0;
	struct genz_core_structure *core;

	switch (CCE) {
	case GENZ_CCE_DISCRETE_BRIDGE:
	case GENZ_CCE_INTEGRATED_BRIDGE:
		alloc = GENZ_CORE_STRUCTURE_ALLOC_COMP_DEST_TABLE;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}
	if (!(core = kzalloc(sizeof(*core), GFP_KERNEL)))
		return ERR_PTR(-ENOMEM);
	core->CCE = CCE;

	if ((alloc & GENZ_CORE_STRUCTURE_ALLOC_COMP_DEST_TABLE) &&
	    !(core->comp_dest_table =
	      kzalloc(sizeof(*core->comp_dest_table), GFP_KERNEL))) {
		genz_core_structure_destroy(core);
		return ERR_PTR(-ENOMEM);
	}
	return core;
}
EXPORT_SYMBOL(genz_core_structure_create);

void genz_core_structure_destroy(struct genz_core_structure *core)
{
	if (!core)
		return;
	if (core->comp_dest_table) {
		kfree(core->comp_dest_table);
		core->comp_dest_table = NULL;
	}
	kfree(core);
}
EXPORT_SYMBOL(genz_core_structure_destroy);

static ssize_t chrdev_bin_read(
	struct file *file, struct kobject *kobj, struct bin_attribute *bin_attr,
	char *buf, loff_t offset, size_t size)
{
	pr_info("%s(%s::%s, %lu bytes @ %lld)\n",
		__FUNCTION__, kobj->name, bin_attr->attr.name, size, offset);
	memset(buf, 0, size);
	snprintf(buf, size - 1, "You're in a maze of twisty little passages, all alike.\n");
	return size;
}

static ssize_t chrdev_bin_write(
	struct file *file, struct kobject *kobj, struct bin_attribute *bin_attr,
	char *buf, loff_t offset, size_t size)
{
	pr_info("%s(%s::%s, %lu bytes @ %lld)\n",
		__FUNCTION__, kobj->name, bin_attr->attr.name, size, offset);
	buf[size - 1] = '\0';
	if (size < 128)
		pr_cont(" = %s", buf);
	pr_cont("\n");
	return size;
}

/**
 * genz_register_char_device - add a new character device and driver
 * @core: Core structure with CCE set appropriately
 * @fops: driver set for the device
 * @file_private_data: to be attached as file->private_data in all fops
 * @bin_attr: optional private routines/data for setting up sysfs binary files
 * @instance: an integer whose semantic value differentiates multiple slots
 * Based on misc_register().  Returns pointer to new structure on success
 * or ERR_PTR(-ESOMETHING).
 */

struct genz_char_device *genz_register_char_device(
	const struct genz_core_structure *core,
	const struct file_operations *fops,
	void *file_private_data,
	const struct bin_attribute *attr_custom,
	int instance)
{
	struct bin_attribute attr_final;
	int i, ret = 0;
	char *ownername = NULL;
	struct genz_char_device *genz_chrdev = NULL;
	dev_t base_dev_t = 0;
	uint64_t minor;
	struct mutex *themutex;
	unsigned long *thebitmap;	// See types.h DECLARE_BITMAP
	uint64_t *themajor;

	switch (core->CCE) {
	case GENZ_CCE_DISCRETE_BRIDGE:
	case GENZ_CCE_INTEGRATED_BRIDGE:
		themutex = &bridge_mutex;
		thebitmap = bridge_minor_bitmap;	// It's an array
		themajor = &bridge_major;
		break;
	default:
		PR_ERR("unhandled Component Encoding %d\n", core->CCE);
		return ERR_PTR(-EDOM);
	}
	ownername = fops->owner->name;

	// Idiot checking. Some day check offset limits, etc.
	if (!core->MaxInterface || core->MaxInterface > 1024) {
		PR_ERR("core->MaxInterface=%d is out of range\n",
			core->MaxInterface);
		return ERR_PTR(-EINVAL);
	}
	if (core->MaxCTL < 8192) {
		PR_ERR("core->MaxCTL=%d is too small\n",
			core->MaxCTL);
		return ERR_PTR(-EINVAL);
	}

	// Memory allocation before the mutex lock (easier cleanup).

	if (!(genz_chrdev = kzalloc(sizeof(*genz_chrdev), GFP_KERNEL)))
		return ERR_PTR(-ENOMEM);

	if (!(genz_chrdev->iface_attrs = kzalloc(
			sizeof(struct bin_attribute) * core->MaxInterface,
			GFP_KERNEL)))
		return ERR_PTR(-ENOMEM);
	
	// Do this math once.
	sysfs_bin_attr_init(&attr_final);
	attr_final.private = attr_custom->private ?
		attr_custom->private : genz_chrdev;
	attr_final.read = attr_custom->read ?
		attr_custom->read : chrdev_bin_read;
	attr_final.write = attr_custom->write ?
		attr_custom->write : chrdev_bin_write;
	attr_final.mmap = attr_custom->mmap ? attr_custom->mmap : NULL;

	for (i = 0; i < core->MaxInterface; i++) {
		struct bin_attribute *this;
		char *name;

		this = &(genz_chrdev->iface_attrs[i]);
		sysfs_bin_attr_init(this);
		if (!(name = kzalloc(8, GFP_KERNEL))) {
			PR_ERR("Cannot allocate space for iface %d\n", i);
			while (--i >= 0)
				kfree(genz_chrdev->iface_attrs[i].attr.name);
			return ERR_PTR(-ENOMEM);
		}
		sprintf(name, "%04d", i);
		this->attr.name = name;
		this->attr.mode = S_IRUSR | S_IWUSR;
		this->size = 4096;
		this->private = attr_final.private;
		this->read = attr_final.read;
		this->write = attr_final.write;
		this->mmap = attr_final.mmap;
	}

	mutex_lock(themutex);
	minor = find_first_zero_bit(thebitmap, GENZ_MINORBITS);
	if (minor >= GENZ_MINORBITS) {
		PR_ERR("exhausted minor numbers for major %llu (%s)\n",
			*themajor, ownername);
		ret = -EDOM;
		goto up_and_out;
	}
	if (*themajor) {
		base_dev_t = MKDEV(*themajor, minor);
		ret = register_chrdev_region(base_dev_t, 1, ownername);
	} else {
		if (!(ret = alloc_chrdev_region(&base_dev_t, minor, 1, ownername)))
			*themajor = MAJOR(base_dev_t);
	}
	if (ret) {
		PR_ERR("can't allocate chrdev_region: %d\n", ret);
		goto up_and_out;
	}
	set_bit(minor, thebitmap);
	pr_info("%s(%s) dev_t = %llu:%llu\n", __FUNCTION__, ownername,
		*themajor, minor);

	if (!(genz_chrdev->parent = genz_find_bus_by_instance(instance))) {
		PR_ERR("genz_find_bus_by_instance() failed\n");
		ret = -ENODEV;
		goto up_and_out;
	}
	genz_chrdev->core = core;
	genz_chrdev->file_private_data = file_private_data;
	genz_chrdev->genz_class = genz_class_getter(core->CCE);
	genz_chrdev->cclass = genz_component_class_str[core->CCE];
	genz_chrdev->mode = 0666;
	genz_chrdev->instance = instance;	// FIXME: managed in here?

	// This sets .fops, .list, and .kobj == ktype_cdev_default.
	// Then add anything else.
	cdev_init(&genz_chrdev->cdev, fops);
	genz_chrdev->cdev.dev = MKDEV(*themajor, minor);
	genz_chrdev->cdev.count = 1;
	if ((ret = kobject_set_name(&genz_chrdev->cdev.kobj,
				    "%s_%02x", ownername, instance))) {
		PR_ERR("kobject_set_name(%s) failed\n", ownername);
		goto up_and_out;
	}
	if ((ret = cdev_add(&genz_chrdev->cdev,
			    genz_chrdev->cdev.dev,
			    genz_chrdev->cdev.count))) {
		PR_ERR("cdev_add() failed\n");
		goto up_and_out;
	}

	// Final work: there's also plain "device_create()".  Driver
	// becomes "live" on success so insure data is ready.
	genz_chrdev->this_device = device_create_with_groups(
		genz_chrdev->genz_class,
		genz_chrdev->parent,	// ugly croakage if this is NULL
		genz_chrdev->cdev.dev,
		genz_chrdev,		// drvdata: not sure where this goes
		genz_chrdev->attr_groups,
		"%s_%02x",
		ownername, instance);
	if (IS_ERR(genz_chrdev->this_device)) {
		ret = PTR_ERR(genz_chrdev->this_device);
		PR_ERR("device_create_with_groups() failed\n");
		goto up_and_out;
	}

	// Section 8.14
	sysfs_bin_attr_init(&(genz_chrdev->sysCoreStructure));
	genz_chrdev->sysCoreStructure.attr.name = "core";
	genz_chrdev->sysCoreStructure.attr.mode = S_IRUSR | S_IWUSR;
	genz_chrdev->sysCoreStructure.size = 4096;	// really 512
	genz_chrdev->sysCoreStructure.private = attr_final.private;
	genz_chrdev->sysCoreStructure.read = attr_final.read;
	genz_chrdev->sysCoreStructure.write = attr_final.write;
	genz_chrdev->sysCoreStructure.mmap = attr_final.mmap;

	if ((ret = device_create_bin_file(
			genz_chrdev->this_device, 
			&genz_chrdev->sysCoreStructure))) {
		PR_ERR("couldn't create sys core structure file: %d\n", ret);
		goto up_and_out;
	}

	if (!(genz_chrdev->sysInterfaces = kobject_create_and_add(
			"interfaces", &genz_chrdev->this_device->kobj))) {
		PR_ERR("couldn't create interfaces directory\n");
		ret = -EBADF;
		goto up_and_out;
	}

	for (i = 0; i < core->MaxInterface; i++)
		if ((ret = sysfs_create_bin_file(
				genz_chrdev->sysInterfaces,
				&(genz_chrdev->iface_attrs[i])))) {
			PR_ERR("couldn't create interface %d: %d\n", i, ret);
			goto up_and_out;
		}

up_and_out:
	mutex_unlock(themutex);
	if (ret) {
		genz_unregister_char_device(genz_chrdev);
		return ERR_PTR(ret);
	}
	return genz_chrdev;
}
EXPORT_SYMBOL(genz_register_char_device);

void genz_unregister_char_device(struct genz_char_device *genz_chrdev)
{
	// FIXME: review for memory leaks
	if (!genz_chrdev)
		return;
	if (genz_chrdev->sysInterfaces) {
		int i;

		for (i = 0; i < genz_chrdev->core->MaxInterface; i++) {
			struct bin_attribute *this;

			this = &(genz_chrdev->iface_attrs[i]);
			sysfs_remove_bin_file(genz_chrdev->sysInterfaces, this);
			kfree(this->attr.name);
		}
		kfree(genz_chrdev->iface_attrs);
		genz_chrdev->iface_attrs = NULL;
		kobject_del(genz_chrdev->sysInterfaces);
		genz_chrdev->sysInterfaces = NULL;
	}
	device_remove_bin_file(
		genz_chrdev->this_device,
		&genz_chrdev->sysCoreStructure);
	memset(&genz_chrdev->sysCoreStructure, 0, sizeof(struct bin_attribute));
	device_destroy(genz_chrdev->genz_class, genz_chrdev->cdev.dev);
	kfree(genz_chrdev);
}
EXPORT_SYMBOL(genz_unregister_char_device);
