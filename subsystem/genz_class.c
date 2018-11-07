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

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "genz_class.h"

//-------------------------------------------------------------------------
// Component Class Encodings are the array index.
// Some names are tweaked to facilitate alphabetical ordering.

static struct class genz_classes[] = {
	{ .name = "RESERVED" },			// 0x0, provides index 1
	{ .name = "genz_memory_p2p" },		// 0x1
	{ .name = "genz_memory_explicit" },
	{ .name = "genz_switch_integrated" },
	{ .name = "genz_switch_enclosure" },
	{ .name = "genz_switch_fabric" },	// 0x5
	{ .name = "genz_processor" },
	{ .name = "genz_processor_nb" },
	{ .name = "genz_accelerator_nb_nc" },
	{ .name = "genz_accelerator_nb" },
	{ .name = "genz_accelerator_nc" },	// 0xA
	{ .name = "genz_accelerator" },
	{ .name = "genz_io_nb_nc" },
	{ .name = "genz_io_nb" },
	{ .name = "genz_io_nc" },
	{ .name = "genz_io" },			// 0xF
	{ .name = "genz_block" },		// 0x10
	{ .name = "genz_block_nb" },
	{ .name = "genz_tr" },
	{ .name = "genz_multiclass" },
	{ .name = "genz_bridge_discrete" },
	{ .name = "genz_bridge_integrated" },	// 0x15
	{}
};

//-------------------------------------------------------------------------
// It's actually all pointers so the math is good.

static unsigned maxindex = (sizeof(genz_classes)/sizeof(genz_classes[0])) - 2;

struct class *genz_class_getter(unsigned index)
{
	return (index && index <= maxindex) ? &genz_classes[index] : NULL;
}
EXPORT_SYMBOL(genz_class_getter);

//-------------------------------------------------------------------------
// 0 or error

int genz_classes_init()
{
	int i, ret = 0;

	pr_info("%s() max index = 0x%x\n", __FUNCTION__, maxindex);

	// class_register() defaults to a kobj of "sysfs_dev_char_kobj".  It's
	// possible set kobj to something else first.  Or use create_class()
	// which does kzalloc behind the scenes along with class_register.
	// Thus things that piggyback off cls->kobj go under dev, see 
	// devices_init() in bootlin.  No .release is needed cuz there's
	// nothing to stop() or kfree().

	for (i = 1; genz_classes[i].name; i++) {	// Skip RESERVED
		genz_classes[i].owner = THIS_MODULE;
		if ((ret = class_register(&genz_classes[i]))) {
			pr_err("class_register(%s) failed\n",
				genz_classes[i].name);
			// Unregister the ones that succeeded.
			while (--i > 0)
				class_unregister(&genz_classes[i]);
			return ret;
		}
	}	
	return 0;
};

//-------------------------------------------------------------------------

void genz_classes_destroy()
{
	int i;

	pr_info("%s()\n", __FUNCTION__);
	for (i = 1; genz_classes[i].name; i++)
		class_unregister(&genz_classes[i]);
}
