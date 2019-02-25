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

#ifndef GENZ_DEVICE_DOT_H
#define GENZ_DEVICE_DOT_H

#include <linux/cdev.h>
#include <linux/fs.h>

#include "genz_baseline.h"

// Composition pattern to realize all data needed to represent a device.
// "misc" class devices get it all clearly spelled out in struct miscdevice.
// and it's all populated by misc_register() in the core.  cdev is kept
// as a full structure; it can be be pulled from the filp->f_inode->i_cdev
// and used as anchor in to_xxxx lookups.

struct genz_char_device {
	struct genz_core_structure *core;
	int instance;			// Currently external control
	const char *cclass;		// genz_component_class_str[CCE]
	void *file_private_data;	// Extracted at first fops->open()
	struct class *genz_class;	// Multi-purpose struct
	struct cdev cdev;		// full structure, has
					// kobject
					// owner
					// ops (fops)
					// list_head
					// dev_t (base maj/min)
					// count (of minors)

	// Copied from miscdevice, in active use
	struct device *parent;		// set by caller, now to figure out WTF?
	struct device *this_device;	// created on the fly.

	// Copied from miscdevice, not used yet
	umode_t mode;
	const struct attribute_group **attr_groups;	// Null-term array
	const char *name;		// used in device_create[_with_groups]
	const char *nodename;		// used in misc_class->devnode()
					// callback to name...
	// NOT copied from miscdevice
	// minor, because cdev has a dev_t
	// list_head, because cdev has one

	// Additional items under /sys/devices/.../one_device
	struct bin_attribute sysCoreStructure;	// file
	struct kobject *sysInterfaces;		// directory
	struct bin_attribute *iface_attrs;	// file for each interface
};

static inline void *genz_char_drv_1stopen_private_data(struct file *file)
{
	struct genz_char_device *container = container_of(
		file->f_inode->i_cdev,		// member address
		struct genz_char_device,	// container type
		cdev);				// container member
	return container->file_private_data;
}

extern const char * const genz_component_class_str[];

// EXPORTed

extern struct genz_core_structure *genz_core_structure_create(unsigned);
extern void genz_core_structure_destroy(struct genz_core_structure *);

extern struct genz_char_device *genz_register_char_device(
	const struct genz_core_structure *,
	const struct file_operations *,
	void *file_private_data,
	const struct bin_attribute *,
	int instance);

extern void genz_unregister_char_device(struct genz_char_device *);
#endif
