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

// Only the beginning

#ifndef GENZ_ROUTING_FABRIC_DOT_H
#define GENZ_ROUTING_FABRIC_DOT_H

#include <linux/list.h>
#include <linux/mutex.h>

// Definitions below ending in "_structure" are merely pertinent fields.
// Those ending in "_format" are the packed binary layout.

// Gen-Z 1.0 "8.29 Component Destination Table Structure"
struct genz_component_destination_table_structure {
	int HiMom;
};

// Gen-Z 1.0 "8.29 Single-Subnet Destination Table Structure"
struct genz_single_subnet_destination_table_structure {
	int HiMom;
};


#endif
