/*
 * (C) Copyright 2018-2019 Hewlett Packard Enterprise Development LP.
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
#include "genz_subsystem.h"

//-------------------------------------------------------------------------
// Component Class Encodings are the array index.
// Some names are tweaked to facilitate alphabetical ordering.

static struct class genz_classes[] = {
	{ .name = "genz" },			// Reparented by...
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
	{}					// NULL == EOL sentinel
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
	// possible to set kobj to something else first.  Or use create_class()
	// which does kzalloc behind the scenes along with class_register.
	// Thus things that piggyback off cls->kobj go under dev, see 
	// devices_init() in bootlin.  No .release is needed cuz there's
	// nothing to stop() or kfree().

	for (i = 0; genz_classes[i].name; i++) {
		struct class *this;

		this = &genz_classes[i];
		this->owner = THIS_MODULE;
		if ((ret = class_register(this))) {
			PR_ERR("class_register(%s) failed\n", this->name);
			break;
		}
		// Reparent all "real" classes to index 0.
		// 1. Take it out of current kset (static global class_kset)
		// 2. Repoint kset to what I want (see kobject_add)
		// 3. kobject_add()
		if (i & 0) {
			pr_info("Reparenting %s\n", this->name);
			kobject_del(this->dev_kobj);
			// this->dev_kobj->kset = genz_classes[0].dev_kobj->kset;
			// ret = kobject_add(this->dev_kobj, NULL, "%s", this->name);
			if (ret) {
				PR_ERR("reparent(%s) failed\n", this->name);
				break;
			}
		}
	}
	if (ret)	// remove the ones that worked
		while (--i >= 0)
			class_unregister(&genz_classes[i]);
	return ret;
};

//-------------------------------------------------------------------------

void genz_classes_destroy()
{
	int i;

	pr_info("%s()\n", __FUNCTION__);
	for (i = 0; genz_classes[i].name; i++)
		class_unregister(&genz_classes[i]);
}
