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

#ifndef GENZ_BUS_DOT_H
#define GENZ_BUS_DOT_H

#include <linux/device.h>
#include <linux/list.h>

#include "genz_device.h"

struct genz_device_ops {
	int (*init)(struct genz_device *genz_dev);
	void (*uninit)(struct genz_device *genz_dev);
};

//-------------------------------------------------------------------------
// genz_bus.c

struct device *genz_find_bus_by_PCIslotnum(int);

#endif
